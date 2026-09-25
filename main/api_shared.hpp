// ARMOR-RADAR - what the panel (HTTP) and the Bluetooth channel both do: the status, the settings and the search for Wi-Fi networks.
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D). GPL-3.0-or-later.
#pragma once
#include <string>
#include <string_view>

#include "core/node_config.hpp"

namespace armor::api {

std::string version_text();
std::string status_json();          // node, network, broker, light and radars, as the Overview page shows them
std::string radars_json();           // the state of the three radars
std::string config_get_json();      // {"config":{...},"channel_auto":n,"firmware":"x.y.z"}: no password ever leaves the node
std::string problems_json(const config::Problems& problems);   // [{"path":..,"code":..}]

enum class PutResult { kSaved, kInvalid, kStorage };
// Applies a (partial) settings document on top of the stored one: checked in full, and stored only when nothing is wrong.
PutResult put_config(std::string_view document, config::Problems& problems, bool& restart_required);

// The Wi-Fi networks in range: {"networks":[{"ssid","rssi","channel","security"}]}, strongest first. False, with a code, when the radio is busy.
bool wifi_scan_json(std::string& data, std::string& error);

}  // namespace armor::api
