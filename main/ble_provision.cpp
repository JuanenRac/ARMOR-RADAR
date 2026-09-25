// ARMOR-RADAR - configuration over Bluetooth Low Energy (NimBLE), for an app on a phone.
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D). GPL-3.0-or-later.
//
// One GATT service with two characteristics: RX (the app writes framed JSON requests) and TX (the node notifies framed JSON answers); see
// core/ble_frame.hpp for the framing and core/ble_dispatch.hpp for who may ask what. Writing needs an encrypted link (LE Secure Connections,
// "just works": it stops a passive listener, not an attacker who is present while the phone pairs), and every operation but hello needs the set-up
// code or a login. Requests are answered by a task of their own because checking a password takes a third of a second and must not stall the radio.
#include "ble_provision.hpp"

#include <cstring>
#include <memory>
#include <string>
extern "C" {
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
}
#include "api_shared.hpp"
#include "core/ble_dispatch.hpp"
#include "network.hpp"
#include "node_store.hpp"
#include "web_server.hpp"

namespace armor::ble_provision {
namespace {
constexpr char kTag[] = "armor-ble";
constexpr std::size_t kQueueLength = 4;

// a5c0de00-5261-4d4f-8000-41524d4f5200 (service), ...01 (RX, write), ...02 (TX, notify): stored little-endian as NimBLE wants.
const ble_uuid128_t kServiceUuid = BLE_UUID128_INIT(0x00, 0x52, 0x4f, 0x4d, 0x52, 0x41, 0x00, 0x80, 0x4f, 0x4d, 0x61, 0x52, 0x00, 0xde, 0xc0, 0xa5);
const ble_uuid128_t kRxUuid = BLE_UUID128_INIT(0x00, 0x52, 0x4f, 0x4d, 0x52, 0x41, 0x00, 0x80, 0x4f, 0x4d, 0x61, 0x52, 0x01, 0xde, 0xc0, 0xa5);
const ble_uuid128_t kTxUuid = BLE_UUID128_INIT(0x00, 0x52, 0x4f, 0x4d, 0x52, 0x41, 0x00, 0x80, 0x4f, 0x4d, 0x61, 0x52, 0x02, 0xde, 0xc0, 0xa5);

std::string g_name;
std::uint8_t g_own_address_type = 0;
std::uint16_t g_connection = BLE_HS_CONN_HANDLE_NONE;
std::uint16_t g_tx_handle = 0;
bool g_running = false;
ble::Assembler g_assembler;
ble::Session g_session;
auth::LoginThrottle g_throttle;
QueueHandle_t g_queue = nullptr;   // pointers to std::string: whole requests for the worker

class NodeBackend : public ble::Backend {
 public:
  bool has_users() override { return !store::users_empty(); }
  std::string hello_json() override {
    const config::Settings s = store::settings();
    const network::Status n = network::status();
    json::Writer w;
    w.begin_object().field("node_id", s.node_id).field("name", s.node_name).field("mac", n.mac).field("firmware", api::version_text()).field("setup", store::users_empty())
        .field("layout", n.layout).field("has_ip", n.has_ip).field("ip", n.ip).field("sta_connected", n.sta_connected).field("sta_ssid", n.sta_ssid).field("ap_active", n.ap_active).end_object();
    return w.str();
  }
  bool setup_code_ok(std::string_view code) override { return !store::setup_code().empty() && auth::same_text(code, store::setup_code()); }
  auth::Result add_administrator(std::string_view user, std::string_view password) override {
    const auth::Result result = store::user_add(user, password, auth::Role::kAdmin);
    if (result == auth::Result::kOk && !store::settings_are_stored()) store::save_settings(store::settings());   // keep the first settings from now on
    if (result == auth::Result::kOk) ESP_LOGI(kTag, "set-up over Bluetooth: the administrator \"%.*s\" exists", static_cast<int>(user.size()), user.data());
    return result;
  }
  bool verify(std::string_view user, std::string_view password, auth::Role& role) override { return store::user_verify(user, password, role); }
  std::string config_json() override { return api::config_get_json(); }
  int config_put(std::string_view document, std::string& data) override {
    config::Problems problems;
    bool restart_required = false;
    switch (api::put_config(document, problems, restart_required)) {
      case api::PutResult::kSaved: data = restart_required ? "{\"restart_required\":true}" : "{\"restart_required\":false}"; return 0;
      case api::PutResult::kInvalid: data = "{\"problems\":" + api::problems_json(problems) + "}"; return 422;
      case api::PutResult::kStorage: break;
    }
    return 500;
  }
  std::string status_json() override { return api::status_json(); }
  bool wifi_scan(std::string& data, std::string& error) override { return api::wifi_scan_json(data, error); }
  void restart_soon() override { web::restart_after(3000); }
  std::uint64_t now_ms() override { return static_cast<std::uint64_t>(esp_timer_get_time()) / 1000ULL; }
};
NodeBackend g_backend;

void advertise();

void send_message(const std::string& json_text) {
  if (g_connection == BLE_HS_CONN_HANDLE_NONE) return;
  const std::string framed = ble::frame(json_text);
  if (framed.empty()) return;
  const std::size_t payload = ble_att_mtu(g_connection) > 3 ? ble_att_mtu(g_connection) - 3u : 20u;
  for (const std::string& piece : ble::chunks_of(framed, payload)) {
    for (int attempt = 0; attempt < 20; ++attempt) {
      if (g_connection == BLE_HS_CONN_HANDLE_NONE) return;
      struct os_mbuf* buffer = ble_hs_mbuf_from_flat(piece.data(), static_cast<std::uint16_t>(piece.size()));
      if (buffer == nullptr) { vTaskDelay(pdMS_TO_TICKS(10)); continue; }
      const int rc = ble_gatts_notify_custom(g_connection, g_tx_handle, buffer);   // takes the buffer
      if (rc == 0) break;
      vTaskDelay(pdMS_TO_TICKS(15));   // the stack's queue is full: wait for it to drain
    }
    vTaskDelay(pdMS_TO_TICKS(3));
  }
}

void worker_task(void*) {
  for (;;) {
    std::string* raw = nullptr;
    if (xQueueReceive(g_queue, &raw, portMAX_DELAY) != pdTRUE || raw == nullptr) continue;
    const std::unique_ptr<std::string> text(raw);
    ble::Request request;
    if (!ble::parse_request(*text, request)) { send_message(ble::error_response(0, "bad_request")); continue; }
    send_message(ble::handle(g_backend, g_session, g_throttle, request));
  }
}

int on_rx(std::uint16_t, std::uint16_t, struct ble_gatt_access_ctxt* context, void*) {
  if (context->op != BLE_GATT_ACCESS_OP_WRITE_CHR) return BLE_ATT_ERR_UNLIKELY;
  std::uint8_t buffer[512];
  std::uint16_t length = 0;
  if (OS_MBUF_PKTLEN(context->om) > sizeof buffer) return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
  if (ble_hs_mbuf_to_flat(context->om, buffer, sizeof buffer, &length) != 0) return BLE_ATT_ERR_UNLIKELY;
  std::string message;
  bool have = g_assembler.feed(buffer, length, message);
  while (have) {
    std::string* copy = new std::string(message);
    if (xQueueSend(g_queue, &copy, 0) != pdTRUE) { delete copy; ESP_LOGW(kTag, "a request was dropped: the node is busy"); }
    have = g_assembler.take(message);
  }
  return 0;
}

int on_tx(std::uint16_t, std::uint16_t, struct ble_gatt_access_ctxt*, void*) { return BLE_ATT_ERR_READ_NOT_PERMITTED; }   // notifications only

const struct ble_gatt_chr_def kCharacteristics[] = {
    {.uuid = &kRxUuid.u, .access_cb = on_rx, .arg = nullptr, .descriptors = nullptr, .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP | BLE_GATT_CHR_F_WRITE_ENC,
     .min_key_size = 0, .val_handle = nullptr, .cpfd = nullptr},
    {.uuid = &kTxUuid.u, .access_cb = on_tx, .arg = nullptr, .descriptors = nullptr, .flags = BLE_GATT_CHR_F_NOTIFY, .min_key_size = 0, .val_handle = &g_tx_handle, .cpfd = nullptr},
    {},   // the terminator
};
const struct ble_gatt_svc_def kServices[] = {
    {.type = BLE_GATT_SVC_TYPE_PRIMARY, .uuid = &kServiceUuid.u, .includes = nullptr, .characteristics = kCharacteristics},
    {},   // the terminator
};

int on_gap(struct ble_gap_event* event, void*) {
  switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
      if (event->connect.status == 0) {
        g_connection = event->connect.conn_handle;
        g_assembler.reset();
        g_session.clear();
        ESP_LOGI(kTag, "a phone connected");
        ble_gap_security_initiate(g_connection);   // the phone pairs now, so the link is encrypted before the first request
      } else {
        advertise();
      }
      break;
    case BLE_GAP_EVENT_DISCONNECT:
      g_connection = BLE_HS_CONN_HANDLE_NONE;
      g_assembler.reset();
      g_session.clear();
      ESP_LOGI(kTag, "the phone disconnected");
      advertise();
      break;
    case BLE_GAP_EVENT_ADV_COMPLETE: advertise(); break;
    case BLE_GAP_EVENT_ENC_CHANGE: ESP_LOGI(kTag, "the link is %s", event->enc_change.status == 0 ? "encrypted" : "NOT encrypted"); break;
    case BLE_GAP_EVENT_MTU: ESP_LOGI(kTag, "MTU %u", static_cast<unsigned>(event->mtu.value)); break;
    case BLE_GAP_EVENT_REPEAT_PAIRING: {
      // The phone remembers a pairing this node does not (bonds are not kept): forget the old one and pair again.
      struct ble_gap_conn_desc description;
      if (ble_gap_conn_find(event->repeat_pairing.conn_handle, &description) == 0) ble_store_util_delete_peer(&description.peer_id_addr);
      return BLE_GAP_REPEAT_PAIRING_RETRY;
    }
    default: break;
  }
  return 0;
}

void advertise() {
  struct ble_hs_adv_fields fields;
  std::memset(&fields, 0, sizeof fields);
  fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
  fields.uuids128 = const_cast<ble_uuid128_t*>(&kServiceUuid);
  fields.num_uuids128 = 1;
  fields.uuids128_is_complete = 1;
  if (ble_gap_adv_set_fields(&fields) != 0) { ESP_LOGE(kTag, "the advertising data was refused"); return; }
  struct ble_hs_adv_fields response;
  std::memset(&response, 0, sizeof response);
  response.name = reinterpret_cast<const std::uint8_t*>(g_name.c_str());
  response.name_len = static_cast<std::uint8_t>(g_name.size());
  response.name_is_complete = 1;
  ble_gap_adv_rsp_set_fields(&response);
  struct ble_gap_adv_params parameters;
  std::memset(&parameters, 0, sizeof parameters);
  parameters.conn_mode = BLE_GAP_CONN_MODE_UND;
  parameters.disc_mode = BLE_GAP_DISC_MODE_GEN;
  const int rc = ble_gap_adv_start(g_own_address_type, nullptr, BLE_HS_FOREVER, &parameters, on_gap, nullptr);
  if (rc != 0 && rc != BLE_HS_EALREADY) ESP_LOGE(kTag, "advertising did not start (%d)", rc);
}

void on_sync() {
  if (ble_hs_util_ensure_addr(0) != 0 || ble_hs_id_infer_auto(0, &g_own_address_type) != 0) { ESP_LOGE(kTag, "the Bluetooth address could not be set"); return; }
  ESP_LOGI(kTag, "advertising as \"%s\"", g_name.c_str());
  advertise();
}

void on_reset(int reason) { ESP_LOGW(kTag, "the Bluetooth stack reset (%d)", reason); }

void host_task(void*) {
  nimble_port_run();   // returns only when the stack is stopped
  nimble_port_freertos_deinit();
}
}  // namespace

bool start(const config::Settings& settings, bool setup_mode) {
  const bool wanted = settings.ble == config::BleMode::kAlways || (settings.ble == config::BleMode::kSetup && setup_mode);
  if (!wanted) return false;
  g_name = "ARMOR-" + store::mac_tail();
  for (char& c : g_name) if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
  g_queue = xQueueCreate(kQueueLength, sizeof(std::string*));
  if (g_queue == nullptr) return false;
  if (nimble_port_init() != ESP_OK) { ESP_LOGE(kTag, "the Bluetooth stack could not start"); return false; }
  ble_hs_cfg.reset_cb = on_reset;
  ble_hs_cfg.sync_cb = on_sync;
  ble_hs_cfg.sm_io_cap = BLE_HS_IO_NO_INPUT_OUTPUT;   // "just works"
  ble_hs_cfg.sm_bonding = 0;
  ble_hs_cfg.sm_mitm = 0;
  ble_hs_cfg.sm_sc = 1;
  ble_svc_gap_init();
  ble_svc_gatt_init();
  if (ble_gatts_count_cfg(kServices) != 0 || ble_gatts_add_svcs(kServices) != 0) { ESP_LOGE(kTag, "the Bluetooth service could not be added"); return false; }
  ble_svc_gap_device_name_set(g_name.c_str());
  ble_att_set_preferred_mtu(247);
  xTaskCreate(worker_task, "ble-worker", 8192, nullptr, 4, nullptr);
  nimble_port_freertos_init(host_task);
  g_running = true;
  return true;
}

bool running() { return g_running; }

}  // namespace armor::ble_provision
