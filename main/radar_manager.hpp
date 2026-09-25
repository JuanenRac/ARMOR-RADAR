// ARMOR-RADAR - the sensors on the three serial ports: trackers (LD2450, LD2461) and presence sensors (LD2410, LD2412, LD2410S, MR24HPC1).
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D). GPL-3.0-or-later.
#pragma once
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>

#include "core/ld2450_command.hpp"
#include "core/node_config.hpp"
#include "core/radar_health.hpp"
#include "radar_tracks.hpp"

namespace armor::radar {

// Sends a message to the broker (the manager knows nothing about MQTT): a presence sensor is published as a device of the server.
using Publisher = std::function<void(const std::string& topic, const std::string& payload)>;

// Opens the serial port of every enabled sensor on its pins, at the speed of its model (or the one set), and starts the task that reads them.
// A tracker feeds the tracks of the perimeter; a presence sensor is published through `publisher` as armor/device/<node>/<name>/state.
void start(const config::Settings& settings, Publisher publisher);

// The broker (re)connected: every presence sensor publishes its state again at once.
void publish_presence_now();

// True while at least one tracker has reported a frame recently; the newest tracks of every tracker.
bool any_fresh();
std::size_t collect_tracks(Track* out);

struct Status {
  bool enabled = false;
  int rx = -1, tx = -1;
  std::string model;                // ld2450, ld2461, ld2410, ...
  bool tracker = true;
  std::string name;                 // a presence sensor's device name
  const char* state = "disabled";   // reporting, no data, garbled, silent, disabled
  std::string state_text;
  std::uint32_t bytes = 0, frames = 0, bad_frames = 0;
  double frames_per_second = 0.0;   // over the last few seconds
  std::string firmware;             // from the last read_info, "" when never read
  int tracking_mode = 0;            // LD2450: 1 = one target, 2 = up to three, 0 = unknown; LD2461: its report format (1, 2 or 3)
  int present = -1;                 // a presence sensor: 1 somebody, 0 nobody, -1 not known yet
  int distance_cm = -1;             // a presence sensor: how far, when it says
  std::string detail;               // JSON of the latest reading (a presence sensor's numbers, an LD2461's zones), "" when there is none
};
Status status(std::size_t index);

enum class Op { kReadInfo, kSingleTarget, kMultiTarget, kSetZones, kRestart, kFactoryReset, kBluetooth };

struct CommandResult {
  bool ok = false;
  std::string error;                // "no_tx", "disabled", "busy", "no_answer", "refused", "bad_argument", "unsupported"
  std::string firmware;
  int tracking_mode = 0;
  bool has_zones = false;
  ld2450cmd::ZoneFilter zones;
  std::string last_answer_hex;      // the module's last raw acknowledgement, shown in the panel so a mismatch is seen at once
};

// Runs one configuration command on a sensor, the way its model does it (enter configuration, the command, leave configuration; the LD2461 has no
// configuration mode). Blocks for up to a few seconds. `flag` is the bluetooth on/off; `zones` is required for kSetZones. A command the model does not
// have is answered with the error "unsupported".
CommandResult run(std::size_t index, Op op, bool flag = false, const ld2450cmd::ZoneFilter* zones = nullptr);

}  // namespace armor::radar
