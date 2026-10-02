// ARMOR-RADAR - the node's network: Ethernet, the Wi-Fi access point (bridged to the wire or on its own) and the Wi-Fi station.
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D). GPL-3.0-or-later.
//
// The Ethernet parts exist only in the s3-eth image (the s3-wifi board has no port, and its image has no W5500 driver).
// Nothing here has run on a board. The bridged layout follows ESP-IDF's own "bridge" example (network/bridge): the Ethernet port and the
// access point are two ports of one lwIP bridge, and the bridge carries the node's address.
#include "network.hpp"

#include <cstdio>
#include <algorithm>
#include <cstring>
#include <mutex>
extern "C" {
#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#if !defined(ARMOR_BOARD_S3_WIFI)
#include "esp_netif_br_glue.h"
#endif
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "sdkconfig.h"
}
#include "ping/ping_sock.h"
#if !defined(ARMOR_BOARD_S3_WIFI)
#include "board_ethernet.hpp"
#endif
#include "core/net_text.hpp"

namespace armor::network {
namespace {
constexpr char kTag[] = "armor-net";

std::mutex g_lock;
Status g_status;
netplan::Plan g_plan;
config::Settings g_settings;
esp_netif_t* g_ip_netif = nullptr;   // the interface that holds the node's address
#if !defined(ARMOR_BOARD_S3_WIFI)
esp_eth_handle_t g_eth = nullptr;
#endif
esp_timer_handle_t g_reconnect_timer = nullptr;
bool g_wifi_running = false;   // the driver was started by start() (an access point, a station, or both)
std::mutex g_scan_lock;

std::string text_of(const esp_ip4_addr_t& address) {
  char text[16];
  std::snprintf(text, sizeof text, IPSTR, IP2STR(&address));
  return text;
}

std::string mac_text(const std::uint8_t* mac) {
  char text[18];
  std::snprintf(text, sizeof text, "%02x:%02x:%02x:%02x:%02x:%02x", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  return text;
}

void refresh_ip(esp_netif_t* netif) {
  esp_netif_ip_info_t info{};
  if (netif == nullptr || esp_netif_get_ip_info(netif, &info) != ESP_OK) return;
  std::lock_guard<std::mutex> guard(g_lock);
  g_status.ip = text_of(info.ip);
  g_status.netmask = text_of(info.netmask);
  g_status.gateway = text_of(info.gw);
  esp_netif_dns_info_t dns{};
  if (esp_netif_get_dns_info(netif, ESP_NETIF_DNS_MAIN, &dns) == ESP_OK) g_status.dns = text_of(dns.ip.u_addr.ip4);
  g_status.has_ip = info.ip.addr != 0;
}

void on_ip_event(void*, esp_event_base_t, int32_t event_id, void* data) {
  const auto* event = static_cast<ip_event_got_ip_t*>(data);
  {
    std::lock_guard<std::mutex> guard(g_lock);
    g_status.ip = text_of(event->ip_info.ip);
    g_status.netmask = text_of(event->ip_info.netmask);
    g_status.gateway = text_of(event->ip_info.gw);
    g_status.has_ip = true;
  }
  refresh_ip(event->esp_netif);
  ESP_LOGI(kTag, "address %s, gateway " IPSTR ", netmask " IPSTR " (%s)", g_status.ip.c_str(), IP2STR(&event->ip_info.gw), IP2STR(&event->ip_info.netmask), event_id == IP_EVENT_STA_GOT_IP ? "Wi-Fi" : "wire");
}

void on_ip_lost(void*, esp_event_base_t, int32_t, void*) {
  std::lock_guard<std::mutex> guard(g_lock);
  g_status.has_ip = false;
  g_status.ip.clear();
}

#if !defined(ARMOR_BOARD_S3_WIFI)
void on_eth_event(void*, esp_event_base_t, int32_t event_id, void*) {
  switch (event_id) {
    case ETHERNET_EVENT_CONNECTED: {
      {
        std::lock_guard<std::mutex> guard(g_lock);
        g_status.link_up = true;
      }
      ESP_LOGI(kTag, "link up");
      // A fixed address gets no DHCP event: it is usable as soon as the link is.
      if (!g_settings.ip.dhcp && g_settings.uplink == config::Uplink::kEthernet) refresh_ip(g_ip_netif);
      break;
    }
    case ETHERNET_EVENT_DISCONNECTED: {
      std::lock_guard<std::mutex> guard(g_lock);
      g_status.link_up = false;
      g_status.has_ip = false;
      ESP_LOGW(kTag, "link down (cable, switch or PoE injector)");
      break;
    }
    case ETHERNET_EVENT_START: ESP_LOGI(kTag, "Ethernet driver started, waiting for the link"); break;
    default: break;
  }
}
#endif

void reconnect_station(void*) { esp_wifi_connect(); }

// The station's own link: found for real on a bench, twice in a row (two different routers, nothing shared with it but the same ESP32-S3 station): the
// radio stays "connected" (no WIFI_EVENT_STA_DISCONNECTED, the signal is good, nothing in the log says anything went wrong) but the node stops being
// reachable - the gateway itself stops answering it - until it is reset by hand. This task notices that from the inside and forces a fresh association,
// since nothing else here would ever find out on its own.
constexpr int kLinkCheckEverySeconds = 20;
constexpr int kLinkBadRoundsBeforeReconnect = 3;   // three rounds with not even one reply: about a minute of the gateway not answering

struct PingRound { SemaphoreHandle_t done; uint32_t replies; };

void on_link_ping_end(esp_ping_handle_t hdl, void* args) {
  auto* round = static_cast<PingRound*>(args);
  esp_ping_get_profile(hdl, ESP_PING_PROF_REPLY, &round->replies, sizeof(round->replies));
  xSemaphoreGive(round->done);
}

// True when at least one of a few pings to the gateway got an answer; false only when none did (a lost packet is not an outage).
bool gateway_answers(const esp_ip4_addr_t& gateway) {
  PingRound round{xSemaphoreCreateBinary(), 0};
  if (round.done == nullptr) return true;   // could not even try: do not act on a guess
  esp_ping_config_t config = ESP_PING_DEFAULT_CONFIG();
  config.target_addr.type = IPADDR_TYPE_V4;
  config.target_addr.u_addr.ip4.addr = gateway.addr;   // esp_ip4_addr_t and lwIP's own ip4_addr_t are not the same type, only the same layout
  config.count = 3;
  config.interval_ms = 400;
  config.timeout_ms = 1200;
  const esp_ping_callbacks_t callbacks = {.cb_args = &round, .on_ping_success = nullptr, .on_ping_timeout = nullptr, .on_ping_end = &on_link_ping_end};
  esp_ping_handle_t session = nullptr;
  bool answered = true;
  if (esp_ping_new_session(&config, &callbacks, &session) == ESP_OK) {
    esp_ping_start(session);
    if (xSemaphoreTake(round.done, pdMS_TO_TICKS(8000)) == pdTRUE) answered = round.replies > 0;
    esp_ping_delete_session(session);
  }
  vSemaphoreDelete(round.done);
  return answered;
}

void link_watchdog_task(void*) {
  int bad_rounds = 0;
  for (;;) {
    vTaskDelay(pdMS_TO_TICKS(kLinkCheckEverySeconds * 1000));
    bool has_ip;
    { std::lock_guard<std::mutex> guard(g_lock); has_ip = g_status.has_ip; }
    if (!has_ip || g_ip_netif == nullptr) { bad_rounds = 0; continue; }
    esp_netif_ip_info_t info{};
    if (esp_netif_get_ip_info(g_ip_netif, &info) != ESP_OK || info.gw.addr == 0) { bad_rounds = 0; continue; }
    if (gateway_answers(info.gw)) { bad_rounds = 0; continue; }
    bad_rounds += 1;
    ESP_LOGW(kTag, "the gateway has not answered a single ping in %d round(s) of %d s", bad_rounds, kLinkCheckEverySeconds);
    if (bad_rounds >= kLinkBadRoundsBeforeReconnect) {
      ESP_LOGW(kTag, "the station looks connected but the gateway never answers: forcing a fresh association");
      bad_rounds = 0;
      esp_wifi_disconnect();   // the disconnect handler already schedules a reconnect a few seconds later
    }
  }
}


void on_wifi_event(void*, esp_event_base_t, int32_t event_id, void* data) {
  switch (event_id) {
    case WIFI_EVENT_AP_START: ESP_LOGI(kTag, "access point \"%s\" started on channel %d", g_plan.ap.ssid.c_str(), g_plan.ap.channel); break;
    case WIFI_EVENT_AP_STACONNECTED: {
      const auto* event = static_cast<wifi_event_ap_staconnected_t*>(data);
      ESP_LOGI(kTag, "a client joined the access point: %s", mac_text(event->mac).c_str());
      break;
    }
    case WIFI_EVENT_AP_STADISCONNECTED: {
      const auto* event = static_cast<wifi_event_ap_stadisconnected_t*>(data);
      ESP_LOGI(kTag, "a client left the access point: %s", mac_text(event->mac).c_str());
      break;
    }
    case WIFI_EVENT_STA_START: esp_wifi_connect(); break;
    case WIFI_EVENT_STA_CONNECTED: {
      {
      std::lock_guard<std::mutex> guard(g_lock);
      g_status.link_up = true;
      g_status.sta_connected = true;
      g_status.sta_ssid = g_settings.sta.ssid;
      g_status.sta_error.clear();
      ESP_LOGI(kTag, "joined the Wi-Fi network \"%s\"", g_settings.sta.ssid.c_str());
      }
      if (!g_settings.ip.dhcp && g_ip_netif != nullptr) refresh_ip(g_ip_netif);   // a fixed address gets no DHCP event (called outside the lock: it takes it)
      break;
    }
    case WIFI_EVENT_STA_DISCONNECTED: {
      const auto* event = static_cast<wifi_event_sta_disconnected_t*>(data);
      const char* error = "failed";
      switch (event->reason) {
        case WIFI_REASON_NO_AP_FOUND: error = "network_not_found"; break;
        case WIFI_REASON_AUTH_FAIL: case WIFI_REASON_AUTH_EXPIRE: case WIFI_REASON_ASSOC_FAIL: case WIFI_REASON_HANDSHAKE_TIMEOUT: case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT: error = "wrong_password"; break;
        default: break;
      }
      {
        std::lock_guard<std::mutex> guard(g_lock);
        g_status.link_up = false;
        g_status.sta_connected = false;
        g_status.has_ip = false;
        g_status.sta_error = error;
      }
      ESP_LOGW(kTag, "the Wi-Fi network \"%s\" is not reachable (%s, reason %u): trying again in 3 s", g_settings.sta.ssid.c_str(), error, static_cast<unsigned>(event->reason));
      if (g_reconnect_timer != nullptr) esp_timer_start_once(g_reconnect_timer, 3 * 1000 * 1000);
      break;
    }
    default: break;
  }
}

// The name, and either DHCP or the fixed address, mask, gateway and DNS of the settings.
bool apply_ip(esp_netif_t* netif, const config::Settings& s, const std::string& hostname) {
  esp_netif_set_hostname(netif, hostname.c_str());
  if (s.ip.dhcp) return true;
  esp_netif_ip_info_t info{};
  std::uint32_t address = 0, mask = 0, gateway = 0;
  if (!net::parse_ipv4(s.ip.address, address) || !net::parse_ipv4(s.ip.netmask, mask) || !net::parse_ipv4(s.ip.gateway, gateway)) {
    ESP_LOGE(kTag, "the fixed address, mask or gateway in the settings is not valid: asking for an address by DHCP instead");
    return true;
  }
  info.ip.addr = esp_netif_htonl(address);
  info.netmask.addr = esp_netif_htonl(mask);
  info.gw.addr = esp_netif_htonl(gateway);
  ESP_ERROR_CHECK(esp_netif_dhcpc_stop(netif));
  ESP_ERROR_CHECK(esp_netif_set_ip_info(netif, &info));
  const std::string servers[2] = {s.ip.dns1.empty() ? s.ip.gateway : s.ip.dns1, s.ip.dns2};
  const esp_netif_dns_type_t kinds[2] = {ESP_NETIF_DNS_MAIN, ESP_NETIF_DNS_BACKUP};
  for (int i = 0; i < 2; ++i) {
    std::uint32_t value = 0;
    if (servers[i].empty() || !net::parse_ipv4(servers[i], value)) continue;
    esp_netif_dns_info_t dns{};
    dns.ip.type = ESP_IPADDR_TYPE_V4;
    dns.ip.u_addr.ip4.addr = esp_netif_htonl(value);
    esp_netif_set_dns_info(netif, kinds[i], &dns);
  }
  return true;
}

wifi_auth_mode_t auth_mode(config::WifiSecurity security) {
  switch (security) {
    case config::WifiSecurity::kOpen: return WIFI_AUTH_OPEN;
    case config::WifiSecurity::kWpa2: return WIFI_AUTH_WPA2_PSK;
    case config::WifiSecurity::kWpa3: return WIFI_AUTH_WPA3_PSK;
    case config::WifiSecurity::kWpa2Wpa3: return WIFI_AUTH_WPA2_WPA3_PSK;
  }
  return WIFI_AUTH_WPA2_PSK;
}

bool set_access_point(const netplan::Plan& plan) {
  wifi_config_t ap{};
  std::strncpy(reinterpret_cast<char*>(ap.ap.ssid), plan.ap.ssid.c_str(), sizeof ap.ap.ssid - 1);
  ap.ap.ssid_len = static_cast<std::uint8_t>(plan.ap.ssid.size());
  if (plan.ap.security != config::WifiSecurity::kOpen) std::strncpy(reinterpret_cast<char*>(ap.ap.password), plan.ap.password.c_str(), sizeof ap.ap.password - 1);
  ap.ap.channel = static_cast<std::uint8_t>(plan.ap.channel);
  ap.ap.authmode = auth_mode(plan.ap.security);
  ap.ap.ssid_hidden = plan.ap.hidden ? 1 : 0;
  ap.ap.max_connection = static_cast<std::uint8_t>(plan.ap.max_clients);
  ap.ap.beacon_interval = 100;
  if (plan.ap.security == config::WifiSecurity::kWpa3) ap.ap.pmf_cfg.required = true;
  if (plan.ap.security == config::WifiSecurity::kWpa2Wpa3) ap.ap.pmf_cfg.capable = true;
  if (esp_wifi_set_config(WIFI_IF_AP, &ap) != ESP_OK) { ESP_LOGE(kTag, "the access point settings were refused"); return false; }
  return true;
}

bool wifi_setup(const config::Settings& s, const netplan::Plan& plan, bool station) {
  wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
  if (esp_wifi_init(&init) != ESP_OK) { ESP_LOGE(kTag, "the Wi-Fi driver could not be initialised"); return false; }
  esp_wifi_set_storage(WIFI_STORAGE_RAM);  // the settings live in this node's own flash store, not in the driver's
  const std::string& country = plan.ap.enabled ? plan.ap.country : s.ap.country;
  if (esp_wifi_set_country_code(country.c_str(), true) != ESP_OK) ESP_LOGW(kTag, "the country code %s was not accepted: the default channels apply", country.c_str());

  const bool access_point = plan.ap.enabled;
  esp_wifi_set_mode(station && access_point ? WIFI_MODE_APSTA : (station ? WIFI_MODE_STA : WIFI_MODE_AP));
  if (access_point && !set_access_point(plan)) return false;
  if (station) {
    wifi_config_t sta{};
    std::strncpy(reinterpret_cast<char*>(sta.sta.ssid), s.sta.ssid.c_str(), sizeof sta.sta.ssid - 1);
    std::strncpy(reinterpret_cast<char*>(sta.sta.password), s.sta.password.c_str(), sizeof sta.sta.password - 1);
    sta.sta.threshold.authmode = s.sta.password.empty() ? WIFI_AUTH_OPEN : WIFI_AUTH_WPA_PSK;
    sta.sta.pmf_cfg.capable = true;
    sta.sta.pmf_cfg.required = false;
    if (esp_wifi_set_config(WIFI_IF_STA, &sta) != ESP_OK) { ESP_LOGE(kTag, "the station settings were refused"); return false; }
  }
  esp_wifi_set_ps(WIFI_PS_NONE);  // an access point that sleeps answers late
  return true;
}

void wifi_tune(const netplan::Plan& plan) {
  if (!plan.ap.enabled) return;
  esp_wifi_set_bandwidth(WIFI_IF_AP, plan.ap.bandwidth_mhz == 40 ? WIFI_BW_HT40 : WIFI_BW_HT20);
  esp_wifi_set_max_tx_power(static_cast<std::int8_t>(plan.ap.tx_power_dbm * 4));  // the driver counts in quarter dBm
}

netplan::Plan g_rescue;
int g_rescue_after_s = 0;

// A node with no address (no cable, no Wi-Fi network of its own that it can join) and no access point of its own cannot be reached by anyone, and a person who
// has just set it up would think it vanished. After a while without an address it opens the set-up access point again, protected with the set-up code.
void open_rescue_access_point() {
  const netplan::Plan& plan = g_rescue;
  ESP_LOGW(kTag, "no address after %d s and no access point: opening \"%s\" (password: the set-up code) so the node can be reached", g_rescue_after_s, plan.ap.ssid.c_str());
  esp_netif_create_default_wifi_ap();
  if (!g_wifi_running) {
    if (!wifi_setup(g_settings, plan, false)) return;
    if (esp_wifi_start() != ESP_OK) { ESP_LOGE(kTag, "the rescue access point could not be started"); return; }
    g_wifi_running = true;
  } else {
    esp_wifi_set_mode(WIFI_MODE_APSTA);
    if (!set_access_point(plan)) return;
  }
  wifi_tune(plan);
  std::lock_guard<std::mutex> guard(g_lock);
  g_status.ap_active = true;
  g_status.ap_setup = true;
  g_status.ap_ssid = plan.ap.ssid;
  g_status.ap_channel = plan.ap.channel;
}

void rescue_task(void*) {
  vTaskDelay(pdMS_TO_TICKS(g_rescue_after_s * 1000));
  bool reachable;
  {
    std::lock_guard<std::mutex> guard(g_lock);
    reachable = g_status.has_ip || g_status.ap_active;
  }
  if (!reachable) open_rescue_access_point();
  vTaskDelete(nullptr);
}
}  // namespace

void arm_rescue(const netplan::Plan& rescue_plan, int after_seconds) {
  if (!rescue_plan.ap.enabled || after_seconds <= 0) return;
  g_rescue = rescue_plan;
  g_rescue_after_s = after_seconds;
  xTaskCreate(rescue_task, "net-rescue", 6144, nullptr, 2, nullptr);
}

bool start(const config::Settings& s, const netplan::Plan& plan) {
  g_plan = plan;
  g_settings = s;
  g_status.layout = netplan::to_text(plan.layout);
  g_status.ap_active = plan.ap.enabled;
  g_status.ap_setup = plan.ap.setup;
  g_status.ap_bridged = plan.ap.bridged;
  g_status.ethernet_available = board::kHasEthernet;
  g_status.board = board::kId;
  g_status.ap_ssid = plan.ap.ssid;
  g_status.ap_channel = plan.ap.enabled ? plan.ap.channel : 0;
  std::uint8_t mac[6]{};
  esp_read_mac(mac, board::kHasEthernet ? ESP_MAC_ETH : ESP_MAC_WIFI_STA);
  g_status.mac = mac_text(mac);

  ESP_ERROR_CHECK(esp_netif_init());
  ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &on_wifi_event, nullptr));
  ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &on_ip_event, nullptr));
  ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_LOST_IP, &on_ip_lost, nullptr));
#if !defined(ARMOR_BOARD_S3_WIFI)
  ESP_ERROR_CHECK(esp_event_handler_register(ETH_EVENT, ESP_EVENT_ANY_ID, &on_eth_event, nullptr));
  ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_ETH_GOT_IP, &on_ip_event, nullptr));
  ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_ETH_LOST_IP, &on_ip_lost, nullptr));
#endif

  const bool wired = plan.layout == netplan::Layout::kEthernet || plan.layout == netplan::Layout::kEthernetBridgedAp || plan.layout == netplan::Layout::kEthernetWithAp;
  const bool station = !wired;

#if !defined(ARMOR_BOARD_S3_WIFI)
  if (wired) {
    g_eth = ethernet_driver_create();
    if (g_eth == nullptr) { g_status.ethernet_ok = false; if (!plan.ap.enabled) return false; }
  }
#endif

  if (station) {
    esp_timer_create_args_t timer{};
    timer.callback = &reconnect_station;
    timer.name = "wifi-reconnect";
    esp_timer_create(&timer, &g_reconnect_timer);
  }

#if !defined(ARMOR_BOARD_S3_WIFI)
  esp_netif_t* eth_netif = nullptr;
  if (plan.layout == netplan::Layout::kEthernetBridgedAp) {
    // ---- one bridge: the wire and the access point are its ports, and the bridge carries the node's address ----
    if (!wifi_setup(s, plan, false)) return false;
    esp_netif_inherent_config_t eth_config = ESP_NETIF_INHERENT_DEFAULT_ETH();
    eth_config.flags = static_cast<esp_netif_flags_t>(0);  // the flags of a port that is to be bridged must be zero
    esp_netif_config_t eth_netif_config = {.base = &eth_config, .driver = nullptr, .stack = ESP_NETIF_NETSTACK_DEFAULT_ETH};
    if (g_eth != nullptr) {
      eth_netif = esp_netif_new(&eth_netif_config);
      ESP_ERROR_CHECK(esp_netif_attach(eth_netif, esp_eth_new_netif_glue(g_eth)));
    }
    esp_netif_inherent_config_t ap_config = ESP_NETIF_INHERENT_DEFAULT_WIFI_AP();
    ap_config.flags = ESP_NETIF_FLAG_AUTOUP;  // the one flag an access-point port keeps
    ap_config.ip_info = nullptr;              // the port has no address of its own
    esp_netif_t* ap_netif = esp_netif_create_wifi(WIFI_IF_AP, &ap_config);
    ESP_ERROR_CHECK(esp_wifi_set_default_wifi_ap_handlers());

    esp_netif_inherent_config_t bridge_config = ESP_NETIF_INHERENT_DEFAULT_BR();
    esp_netif_config_t bridge_netif_config = {.base = &bridge_config, .driver = nullptr, .stack = ESP_NETIF_NETSTACK_DEFAULT_BR};
    bridgeif_config_t bridge_settings = {.max_fdb_dyn_entries = 32, .max_fdb_sta_entries = 2, .max_ports = static_cast<std::uint8_t>(eth_netif != nullptr ? 2 : 1)};
    bridge_config.bridge_info = &bridge_settings;
    esp_read_mac(mac, ESP_MAC_ETH);
    std::memcpy(bridge_config.mac, mac, 6);
    g_ip_netif = esp_netif_new(&bridge_netif_config);
    esp_netif_br_glue_handle_t glue = esp_netif_br_glue_new();
    if (eth_netif != nullptr) ESP_ERROR_CHECK(esp_netif_br_glue_add_port(glue, eth_netif));
    ESP_ERROR_CHECK(esp_netif_br_glue_add_wifi_port(glue, ap_netif));
    ESP_ERROR_CHECK(esp_netif_attach(g_ip_netif, glue));
    apply_ip(g_ip_netif, s, plan.hostname);
    if (g_eth != nullptr) {
      bool promiscuous = true;  // the frames are forwarded by the bridge, so the MAC must pass them all
      esp_eth_ioctl(g_eth, ETH_CMD_S_PROMISCUOUS, &promiscuous);
      ESP_ERROR_CHECK(esp_eth_start(g_eth));
    }
    ESP_ERROR_CHECK(esp_wifi_start());
    wifi_tune(plan);
    g_wifi_running = true;
    return true;
  }

  if (wired && g_eth != nullptr) {
    esp_netif_config_t eth_config = ESP_NETIF_DEFAULT_ETH();
    eth_netif = esp_netif_new(&eth_config);
    ESP_ERROR_CHECK(esp_netif_attach(eth_netif, esp_eth_new_netif_glue(g_eth)));
    g_ip_netif = eth_netif;
    apply_ip(eth_netif, s, plan.hostname);
  }
#endif
  if (plan.ap.enabled || station) {
    if (plan.ap.enabled) esp_netif_create_default_wifi_ap();
    if (station) {
      esp_netif_t* sta_netif = esp_netif_create_default_wifi_sta();
      g_ip_netif = sta_netif;
      apply_ip(sta_netif, s, plan.hostname);
    }
    if (!wifi_setup(s, plan, station)) return false;
  }
#if !defined(ARMOR_BOARD_S3_WIFI)
  if (g_eth != nullptr) ESP_ERROR_CHECK(esp_eth_start(g_eth));
#endif
  if (plan.ap.enabled || station) { ESP_ERROR_CHECK(esp_wifi_start()); wifi_tune(plan); g_wifi_running = true; }
  if (station) xTaskCreate(link_watchdog_task, "net-link-wd", 4096, nullptr, 2, nullptr);
  return true;
}

namespace {
const char* security_text(wifi_auth_mode_t mode) {
  switch (mode) {
    case WIFI_AUTH_OPEN: return "open";
    case WIFI_AUTH_WEP: return "wep";
    case WIFI_AUTH_WPA_PSK: return "wpa";
    case WIFI_AUTH_WPA2_PSK: return "wpa2";
    case WIFI_AUTH_WPA_WPA2_PSK: return "wpa2";
    case WIFI_AUTH_WPA3_PSK: return "wpa3";
    case WIFI_AUTH_WPA2_WPA3_PSK: return "wpa2wpa3";
    case WIFI_AUTH_WPA2_ENTERPRISE: case WIFI_AUTH_WPA3_ENTERPRISE: case WIFI_AUTH_WPA2_WPA3_ENTERPRISE: return "enterprise";
    default: return "wpa2";
  }
}
}  // namespace

bool scan(std::vector<ScanEntry>& out, std::string& error) {
  out.clear();
  std::unique_lock<std::mutex> guard(g_scan_lock, std::try_to_lock);
  if (!guard.owns_lock()) { error = "wifi_busy"; return false; }
  bool started_here = false;   // the driver was not running: it is started for the search and stopped after
  wifi_mode_t previous = WIFI_MODE_NULL;
  if (!g_wifi_running) {
    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    if (esp_wifi_init(&init) != ESP_OK) { error = "wifi_unavailable"; return false; }
    esp_wifi_set_storage(WIFI_STORAGE_RAM);
    esp_wifi_set_mode(WIFI_MODE_STA);
    if (esp_wifi_start() != ESP_OK) { esp_wifi_deinit(); error = "wifi_unavailable"; return false; }
    started_here = true;
  } else {
    esp_wifi_get_mode(&previous);
    if (previous == WIFI_MODE_AP) esp_wifi_set_mode(WIFI_MODE_APSTA);   // an access point alone cannot search
  }
  wifi_scan_config_t config{};
  config.show_hidden = false;
  config.scan_type = WIFI_SCAN_TYPE_ACTIVE;
  config.scan_time.active.min = 80;
  config.scan_time.active.max = 200;
  const esp_err_t result = esp_wifi_scan_start(&config, true);   // blocks until the search is over
  bool ok = result == ESP_OK;
  if (ok) {
    std::uint16_t count = 0;
    esp_wifi_scan_get_ap_num(&count);
    if (count > 40) count = 40;
    std::vector<wifi_ap_record_t> records(count);
    if (count > 0) esp_wifi_scan_get_ap_records(&count, records.data());
    records.resize(count);
    std::sort(records.begin(), records.end(), [](const wifi_ap_record_t& a, const wifi_ap_record_t& b) { return a.rssi > b.rssi; });
    for (const wifi_ap_record_t& record : records) {
      const std::string name(reinterpret_cast<const char*>(record.ssid));
      if (name.empty() || out.size() >= 25) continue;
      if (std::any_of(out.begin(), out.end(), [&](const ScanEntry& e) { return e.ssid == name; })) continue;   // the strongest of a name is kept
      out.push_back({name, record.rssi, record.primary, security_text(record.authmode)});
    }
  } else {
    esp_wifi_clear_ap_list();
    error = result == ESP_ERR_WIFI_STATE ? "wifi_busy" : "scan_failed";
  }
  if (started_here) { esp_wifi_stop(); esp_wifi_deinit(); }
  else if (previous == WIFI_MODE_AP) esp_wifi_set_mode(WIFI_MODE_AP);
  return ok;
}

bool has_ip() {
  std::lock_guard<std::mutex> guard(g_lock);
  return g_status.has_ip;
}

void disconnect_before_restart() {
  wifi_mode_t mode;
  if (esp_wifi_get_mode(&mode) != ESP_OK) return;   // Wi-Fi was never started (an Ethernet-only node)
  if (mode != WIFI_MODE_STA && mode != WIFI_MODE_APSTA) return;
  esp_wifi_disconnect();
  vTaskDelay(pdMS_TO_TICKS(100));   // give the deauthentication frame a moment to actually go out before the radio powers down
}

Status status() {
  Status copy;
  {
    std::lock_guard<std::mutex> guard(g_lock);
    copy = g_status;
  }
  if (copy.ap_active) {
    wifi_sta_list_t list{};
    if (esp_wifi_ap_get_sta_list(&list) == ESP_OK) copy.ap_clients = list.num;
  }
  if (copy.sta_connected) {
    wifi_ap_record_t record{};
    if (esp_wifi_sta_get_ap_info(&record) == ESP_OK) copy.sta_rssi = record.rssi;
  }
  return copy;
}

}  // namespace armor::network
