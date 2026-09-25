// ARMOR-RADAR - decides how the node's network is built from its settings: which interfaces, which are bridged, and the access point.
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D). GPL-3.0-or-later.
//
// The five layouts:
//   kEthernet             wired only; the node's address is the Ethernet one (DHCP or fixed)
//   kEthernetBridgedAp    wired, and an access point joined to the wired network: the phones and sensors that connect get addresses from
//                         the same DHCP server as everything else, and keep them when they move to another node with the same SSID
//   kEthernetWithAp       wired, and an access point with a network of its own (192.168.4.x, the node hands out the addresses)
//   kWifiStation          Wi-Fi only: the node joins an existing network as a station
//   kWifiStationWithAp    a station and an access point at once (the access point follows the station's channel)
// A node that has no user yet is in setup: it opens an access point of its own, "ARMOR-SETUP-xxxxxx", protected with the setup code, so a
// node with no Ethernet can still be reached to be configured. That access point is never bridged.
#pragma once
#include <string>
#include <string_view>

#include "node_config.hpp"

namespace armor::netplan {

enum class Layout { kEthernet, kEthernetBridgedAp, kEthernetWithAp, kWifiStation, kWifiStationWithAp };

struct AccessPointPlan {
  bool enabled = false;
  bool setup = false;   // the access point of a node that is waiting to be set up
  bool bridged = false;
  std::string ssid, password, country;
  config::WifiSecurity security = config::WifiSecurity::kWpa2;
  int channel = 1;
  bool hidden = false;
  int max_clients = 8;
  int tx_power_dbm = 15;
  int bandwidth_mhz = 20;
};

struct Plan {
  Layout layout = Layout::kEthernet;
  AccessPointPlan ap;
  std::string hostname;
};

// A host name from the settings, or "armor-" + the node id (underscores become hyphens, at most 32 characters, never ending in a hyphen).
inline std::string hostname_for(const config::Settings& s) {
  if (!s.ip.hostname.empty()) return s.ip.hostname;
  std::string name = s.node_id.compare(0, 6, "armor-") == 0 ? "" : "armor-";   // a node still named after its MAC is already armor-xxxxxx
  for (const char c : s.node_id) name += c == '_' ? '-' : c;
  if (name.size() > 32) name.resize(32);
  while (!name.empty() && name.back() == '-') name.pop_back();
  return name;
}

inline std::string upper(std::string_view text) {
  std::string out(text);
  for (char& c : out) if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
  return out;
}

inline Plan plan_network(const config::Settings& s, bool setup_mode, std::string_view setup_code, std::string_view mac_tail, unsigned mac_sum) {
  Plan plan;
  plan.hostname = hostname_for(s);
  const bool wired = s.uplink == config::Uplink::kEthernet;
  AccessPointPlan& ap = plan.ap;
  if (setup_mode) {
    ap.enabled = true;
    ap.setup = true;
    ap.bridged = false;
    ap.ssid = "ARMOR-SETUP-" + upper(mac_tail);
    ap.password = std::string(setup_code);
    ap.security = config::WifiSecurity::kWpa2;
    ap.max_clients = 4;
  } else if (s.ap.enabled) {
    ap.enabled = true;
    ap.bridged = wired && s.ap.bridge;
    ap.ssid = s.ap.ssid;
    ap.password = s.ap.password;
    ap.security = s.ap.security;
    ap.hidden = s.ap.hidden;
    ap.max_clients = s.ap.max_clients;
  }
  if (ap.enabled) {
    ap.channel = config::effective_channel(s.ap, mac_sum);
    ap.country = s.ap.country;
    ap.tx_power_dbm = s.ap.tx_power_dbm;
    ap.bandwidth_mhz = s.ap.bandwidth_mhz;
  }
  if (wired) plan.layout = !ap.enabled ? Layout::kEthernet : (ap.bridged ? Layout::kEthernetBridgedAp : Layout::kEthernetWithAp);
  else plan.layout = ap.enabled ? Layout::kWifiStationWithAp : Layout::kWifiStation;
  return plan;
}

inline const char* to_text(Layout layout) {
  switch (layout) {
    case Layout::kEthernet: return "ethernet";
    case Layout::kEthernetBridgedAp: return "ethernet+ap-bridged";
    case Layout::kEthernetWithAp: return "ethernet+ap";
    case Layout::kWifiStation: return "wifi-station";
    case Layout::kWifiStationWithAp: return "wifi-station+ap";
  }
  return "ethernet";
}

}  // namespace armor::netplan
