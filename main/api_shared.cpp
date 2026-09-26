// ARMOR-RADAR - what the panel (HTTP) and the Bluetooth channel both do: the status, the settings and the search for Wi-Fi networks.
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D). GPL-3.0-or-later.
#include "api_shared.hpp"

#include <cstring>
extern "C" {
#include "esp_app_desc.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "esp_timer.h"
}
#include "core/json.hpp"
#include "light_sensor.hpp"
#include "mqtt_link.hpp"
#include "network.hpp"
#include "node_store.hpp"
#include "radar_manager.hpp"
#include "web_server.hpp"

namespace armor::api {


std::string version_text() { return esp_app_get_description()->version; }

const char* reset_reason_text() {
  switch (esp_reset_reason()) {
    case ESP_RST_POWERON: return "power_on";
    case ESP_RST_SW: return "software";
    case ESP_RST_PANIC: return "crash";
    case ESP_RST_INT_WDT: case ESP_RST_TASK_WDT: case ESP_RST_WDT: return "watchdog";
    case ESP_RST_BROWNOUT: return "brownout";
    case ESP_RST_DEEPSLEEP: return "deep_sleep";
    default: return "other";
  }
}

void write_network(json::Writer& w) {
  const network::Status n = network::status();
  w.key("network").begin_object().field("layout", n.layout).field("link_up", n.link_up).field("has_ip", n.has_ip).field("ip", n.ip).field("netmask", n.netmask).field("gateway", n.gateway)
      .field("dns", n.dns).field("mac", n.mac).field("ethernet_ok", n.ethernet_ok).field("ap_active", n.ap_active).field("ap_setup", n.ap_setup).field("ap_bridged", n.ap_bridged)
      .field("ap_ssid", n.ap_ssid).field("ap_channel", n.ap_channel).field("ap_clients", n.ap_clients).field("sta_connected", n.sta_connected).field("sta_ssid", n.sta_ssid)
      .field("sta_rssi", n.sta_rssi).end_object();
}

void write_radar(json::Writer& w, std::size_t index) {
  const radar::Status s = radar::status(index);
  w.begin_object().field("radar", static_cast<int>(index + 1)).field("enabled", s.enabled).field("model", s.model).field("tracker", s.tracker).field("name", s.name).field("rx", s.rx).field("tx", s.tx)
      .field("state", s.state).field("state_text", s.state_text).field("bytes", static_cast<long long>(s.bytes)).field("frames", static_cast<long long>(s.frames))
      .field("bad_frames", static_cast<long long>(s.bad_frames)).key("fps").number(s.frames_per_second, 1).field("firmware", s.firmware).field("tracking_mode", s.tracking_mode)
      .field("present", s.present).field("distance_cm", s.distance_cm);
  if (!s.detail.empty()) w.key("detail").raw(s.detail);
  w.end_object();
}

std::string status_json() {
  const config::Settings s = store::settings();
  const mqtt_link::Status m = mqtt_link::status();
  const esp_partition_t* running = esp_ota_get_running_partition();
  json::Writer w;
  w.begin_object().field("node_id", s.node_id).field("name", s.node_name).field("version", version_text()).field("uptime_s", static_cast<long long>(esp_timer_get_time() / 1000000LL))
      .field("reset_reason", reset_reason_text()).field("heap_free", static_cast<long long>(esp_get_free_heap_size())).field("heap_min", static_cast<long long>(esp_get_minimum_free_heap_size()))
      .field("psram_free", static_cast<long long>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM))).field("partition", running != nullptr ? running->label : "?");
  write_network(w);
  w.key("mqtt").begin_object().field("enabled", m.enabled).field("connected", m.connected).field("clock_set", m.clock_set).field("published", static_cast<long long>(m.published)).field("withheld", m.withheld).end_object();
  const float lux = light_lux();
  const web::TlsStatus tls = web::tls_status();
  w.key("web").begin_object().field("mode", tls.mode).field("https", tls.running).field("cert_sha256", tls.fingerprint).end_object();
  w.key("lux");
  if (lux >= 0.0f) w.number(lux, 1); else w.null();
  w.key("radars").begin_array();
  for (std::size_t i = 0; i < 3; ++i) write_radar(w, i);
  w.end_array().end_object();
  return w.str();
}


std::string radars_json() {
  json::Writer w;
  w.begin_array();
  for (std::size_t i = 0; i < 3; ++i) write_radar(w, i);
  w.end_array();
  return w.str();
}

std::string config_get_json() {
  const config::Settings s = store::settings();
  json::Writer w;
  w.begin_object().key("config").raw(config::to_json(s, false)).field("channel_auto", config::effective_channel(s.ap, store::mac_sum())).field("firmware", version_text()).end_object();
  return w.str();
}

std::string problems_json(const config::Problems& problems) {
  json::Writer w;
  w.begin_array();
  for (const config::Problem& problem : problems) w.begin_object().field("path", problem.path).field("code", problem.code).end_object();
  w.end_array();
  return w.str();
}

namespace {
// What changed in a way that needs a restart: everything but the node's display name and the panel language.
bool needs_restart(config::Settings before, config::Settings after) {
  before.node_name = after.node_name = "";
  before.language = after.language = "en";
  return config::to_json(before, true) != config::to_json(after, true);
}
}  // namespace

PutResult put_config(std::string_view document, config::Problems& problems, bool& restart_required) {
  const config::Settings before = store::settings();
  config::Settings after;
  problems.clear();
  if (!config::load(document, before, after, problems)) return PutResult::kInvalid;
  if (!store::save_settings(after)) return PutResult::kStorage;
  restart_required = needs_restart(before, after);
  return PutResult::kSaved;
}

bool wifi_scan_json(std::string& data, std::string& error) {
  std::vector<network::ScanEntry> found;
  if (!network::scan(found, error)) return false;
  json::Writer w;
  w.begin_object().key("networks").begin_array();
  for (const network::ScanEntry& entry : found) w.begin_object().field("ssid", entry.ssid).field("rssi", entry.rssi).field("channel", entry.channel).field("security", entry.security).end_object();
  w.end_array().end_object();
  data = w.str();
  return true;
}

}  // namespace armor::api
