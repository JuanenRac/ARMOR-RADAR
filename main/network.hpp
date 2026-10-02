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
  std::string sta_error;              // why the station is not connected: "network_not_found", "wrong_password" or "failed" (empty when it is, or has not tried)
  int sta_rssi = 0;
  bool ethernet_ok = true;            // false: the W5500 did not answer
  bool ethernet_available = false;    // this board has an Ethernet port (the s3-eth profile)
  std::string board;                  // "s3-eth" or "s3-wifi"
};

// Builds the network from the settings and starts it. Returns false only when nothing at all could be started; the node then keeps its
// radars and its pins running and says why on the console.
bool start(const config::Settings& settings, const netplan::Plan& plan);

// If, `after_seconds` after the start, the node has no address and no access point of its own, it opens `rescue_plan`'s access point (the set-up one) so
// that it can still be reached. Does nothing when the node already has an access point.
void arm_rescue(const netplan::Plan& rescue_plan, int after_seconds);

bool has_ip();
Status status();

// Tells the access point the station is leaving before a restart, instead of just vanishing off the air: some access points
// get stuck for a station that never sent a real disconnect, and then need restarting themselves. Does nothing on an
// Ethernet-only node (Wi-Fi was never started) or one that never joined a network.
void disconnect_before_restart();

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
