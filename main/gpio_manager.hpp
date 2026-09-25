// ARMOR-RADAR - the pins the operator mapped in the panel: inputs, outputs, PWM and analogue readings that A.R.M.O.R. can read and switch.
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D). GPL-3.0-or-later.
#pragma once
#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include "core/node_config.hpp"

namespace armor::pins {

// Sends a message to the broker (the manager knows nothing about MQTT).
using Publisher = std::function<void(const std::string& topic, const std::string& payload)>;
// True while the node can reach its broker: an output falls back to its safe state after the link has been down long enough.
using LinkProbe = std::function<bool()>;

void start(const config::Settings& settings, Publisher publisher, LinkProbe link);

enum class Outcome { kOk, kUnknownPin, kRefused };
// A command for a mapped output or PWM pin, in the words the server uses (see core/gpio_logic.hpp parse_command).
Outcome command(const std::string& pin_name, std::string_view payload);

// Reports every pin's state now (after the broker connection came up).
void publish_all();

struct Live {
  std::string name, mode, report;
  int gpio = -1;
  bool on = false;          // logical state of an input or an output
  int percent = 0;          // PWM level
  double value = 0.0;       // analogue value, scaled
  bool has_value = false;
  bool fell_back = false;   // the output is in its safe state because the broker was unreachable
  std::string topic_state, topic_set;
};
std::vector<Live> snapshot(const std::string& node_id);

}  // namespace armor::pins
