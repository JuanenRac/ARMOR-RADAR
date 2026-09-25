// ARMOR-RADAR - the node's network: Ethernet, the Wi-Fi access point (bridged to the wire or on its own) and the Wi-Fi station.
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D). GPL-3.0-or-later.
#pragma once
#include <string>
#include <vector>

#include "core/network_plan.hpp"
#include "core/node_config.hpp"

namespace armor::network {

struct Status {
  std::string layout;                 // see netplan::to_text
  bool link_up = false;               // the Ethernet cable (or the station's association)
  bool has_ip = false;
  std::string ip, netmask, gateway, dns, mac;
  bool ap_active = false;
  bool ap_setup = false;              // the setup access point of a node that has no user yet
  bool ap_bridged = false;
  std::string ap_ssid;
  int ap_channel = 0;
  int ap_clients = 0;
  bool sta_connected = false;
  std::string sta_ssid;
  int sta_rssi = 0;
  bool ethernet_ok = true;            // false: the W5500 did not answer
};

// Builds the network from the settings and starts it. Returns false only when nothing at all could be started; the node then keeps its
// radars and its pins running and says why on the console.
bool start(const config::Settings& settings, const netplan::Plan& plan);

bool has_ip();
Status status();

// One Wi-Fi network heard by a search.
struct ScanEntry {
  std::string ssid;
  int rssi = 0;
  int channel = 0;
  std::string security;   // open, wep, wpa, wpa2, wpa3, wpa2wpa3, enterprise
};
// Searches for Wi-Fi networks (up to 25, strongest first, one line per name). It works whatever the node's layout: if the radio is running only
// as an access point it is briefly used as a station too (the clients of the access point may notice), and if Wi-Fi is not running at all it is
// started for the search and stopped again. False, with a code in `error`, when the radio is busy or the search fails.
bool scan(std::vector<ScanEntry>& out, std::string& error);

}  // namespace armor::network
