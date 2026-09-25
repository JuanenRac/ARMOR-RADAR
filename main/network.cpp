// ARMOR-RADAR - the node's network: Ethernet, the Wi-Fi access point (bridged to the wire or on its own) and the Wi-Fi station.
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D). GPL-3.0-or-later.
//
// Nothing here has run on a board. The bridged layout follows ESP-IDF's own "bridge" example (network/bridge): the Ethernet port and the
// access point are two ports of one lwIP bridge, and the bridge carries the node's address.
#include "network.hpp"

#include <cstdio>
#include <cstring>
#include <mutex>
extern "C" {
#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_netif_br_glue.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "sdkconfig.h"
}
#include "board_ethernet.hpp"
#include "core/net_text.hpp"

namespace armor::network {
namespace {
constexpr char kTag[] = "armor-net";

std::mutex g_lock;
Status g_status;
netplan::Plan g_plan;
config::Settings g_settings;
esp_netif_t* g_ip_netif = nullptr;   // the interface that holds the node's address
esp_eth_handle_t g_eth = nullptr;
esp_timer_handle_t g_reconnect_timer = nullptr;

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

void reconnect_station(void*) { esp_wifi_connect(); }

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
      std::lock_guard<std::mutex> guard(g_lock);
      g_status.link_up = true;
      g_status.sta_connected = true;
      g_status.sta_ssid = g_settings.sta.ssid;
      ESP_LOGI(kTag, "joined the Wi-Fi network \"%s\"", g_settings.sta.ssid.c_str());
      break;
    }
    case WIFI_EVENT_STA_DISCONNECTED: {
      {
        std::lock_guard<std::mutex> guard(g_lock);
        g_status.link_up = false;
        g_status.sta_connected = false;
        g_status.has_ip = false;
      }
      ESP_LOGW(kTag, "the Wi-Fi network is not reachable: trying again in 3 s");
      if (g_reconnect_timer != nullptr) esp_timer_start_once(g_reconnect_timer, 3 * 1000 * 1000);
      break;
    }
    default: break;
  }
}

// The name, and either DHCP or the fixed address, mask, gateway and DNS of the settings.
bool apply_ip(esp_netif_t* netif, const config::Settings& s, const std::string& hostname) {
  esp_netif_set_hostname(netif, hostname.c_str());
  if (s.ip.dhcp || s.uplink != config::Uplink::kEthernet) return true;
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

bool wifi_setup(const config::Settings& s, const netplan::Plan& plan, bool station) {
  wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
  if (esp_wifi_init(&init) != ESP_OK) { ESP_LOGE(kTag, "the Wi-Fi driver could not be initialised"); return false; }
  esp_wifi_set_storage(WIFI_STORAGE_RAM);  // the settings live in this node's own flash store, not in the driver's
  const std::string& country = plan.ap.enabled ? plan.ap.country : s.ap.country;
  if (esp_wifi_set_country_code(country.c_str(), true) != ESP_OK) ESP_LOGW(kTag, "the country code %s was not accepted: the default channels apply", country.c_str());

  const bool access_point = plan.ap.enabled;
  esp_wifi_set_mode(station && access_point ? WIFI_MODE_APSTA : (station ? WIFI_MODE_STA : WIFI_MODE_AP));
  if (access_point) {
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
  }
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
}  // namespace

bool start(const config::Settings& s, const netplan::Plan& plan) {
  g_plan = plan;
  g_settings = s;
  g_status.layout = netplan::to_text(plan.layout);
  g_status.ap_active = plan.ap.enabled;
  g_status.ap_setup = plan.ap.setup;
  g_status.ap_bridged = plan.ap.bridged;
  g_status.ap_ssid = plan.ap.ssid;
  g_status.ap_channel = plan.ap.enabled ? plan.ap.channel : 0;
  std::uint8_t mac[6]{};
  esp_read_mac(mac, ESP_MAC_ETH);
  g_status.mac = mac_text(mac);

  ESP_ERROR_CHECK(esp_netif_init());
  ESP_ERROR_CHECK(esp_event_handler_register(ETH_EVENT, ESP_EVENT_ANY_ID, &on_eth_event, nullptr));
  ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &on_wifi_event, nullptr));
  ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_ETH_GOT_IP, &on_ip_event, nullptr));
  ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &on_ip_event, nullptr));
  ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_ETH_LOST_IP, &on_ip_lost, nullptr));
  ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_LOST_IP, &on_ip_lost, nullptr));

  const bool wired = plan.layout == netplan::Layout::kEthernet || plan.layout == netplan::Layout::kEthernetBridgedAp || plan.layout == netplan::Layout::kEthernetWithAp;
  const bool station = !wired;

  if (wired) {
    g_eth = ethernet_driver_create();
    if (g_eth == nullptr) { g_status.ethernet_ok = false; if (!plan.ap.enabled) return false; }
  }

  if (station) {
    esp_timer_create_args_t timer{};
    timer.callback = &reconnect_station;
    timer.name = "wifi-reconnect";
    esp_timer_create(&timer, &g_reconnect_timer);
  }

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
    return true;
  }

  if (wired && g_eth != nullptr) {
    esp_netif_config_t eth_config = ESP_NETIF_DEFAULT_ETH();
    eth_netif = esp_netif_new(&eth_config);
    ESP_ERROR_CHECK(esp_netif_attach(eth_netif, esp_eth_new_netif_glue(g_eth)));
    g_ip_netif = eth_netif;
    apply_ip(eth_netif, s, plan.hostname);
  }
  if (plan.ap.enabled || station) {
    if (plan.ap.enabled) esp_netif_create_default_wifi_ap();
    if (station) {
      esp_netif_t* sta_netif = esp_netif_create_default_wifi_sta();
      g_ip_netif = sta_netif;
      apply_ip(sta_netif, s, plan.hostname);
    }
    if (!wifi_setup(s, plan, station)) return false;
  }
  if (g_eth != nullptr) ESP_ERROR_CHECK(esp_eth_start(g_eth));
  if (plan.ap.enabled || station) { ESP_ERROR_CHECK(esp_wifi_start()); wifi_tune(plan); }
  return true;
}

bool has_ip() {
  std::lock_guard<std::mutex> guard(g_lock);
  return g_status.has_ip;
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
