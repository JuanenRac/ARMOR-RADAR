// ARMOR-RADAR - the node's link to the broker: the clock, the health message, the radar telemetry and the mapped pins' topics.
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D). GPL-3.0-or-later.
#pragma once
#include <cstdint>
#include <string>

#include "core/node_config.hpp"

namespace armor::mqtt_link {

// Starts the task that waits for an address, sets the clock (SNTP) and connects to the broker of the settings. Does nothing when the
// settings have no broker (mqtt disabled or no address yet): the node then only serves its panel.
void start(const config::Settings& settings);

bool connected();
bool clock_is_set();
std::uint64_t wall_clock_ms();

void publish(const std::string& topic, const std::string& payload);

struct Status {
  bool enabled = false;
  bool connected = false;
  bool clock_set = false;
  std::uint32_t published = 0;
  std::string withheld;   // "" or why telemetry is not being sent ("light", "radars")
};
Status status();

}  // namespace armor::mqtt_link
