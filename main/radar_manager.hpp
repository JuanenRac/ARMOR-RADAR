// ARMOR-RADAR - the three LD2450 radars: the UARTs, the report frames, their health, and the command channel.
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D). GPL-3.0-or-later.
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>

#include "core/ld2450_command.hpp"
#include "core/node_config.hpp"
#include "core/radar_health.hpp"
#include "radar_tracks.hpp"

namespace armor::radar {

// Opens the UART of every enabled radar on its pins and starts the task that reads them. Radars that are disabled stay untouched.
void start(const config::Settings& settings);

// True while at least one radar has reported a frame recently; the newest tracks of every radar.
bool any_fresh();
std::size_t collect_tracks(Track* out);

struct Status {
  bool enabled = false;
  int rx = -1, tx = -1;
  const char* state = "disabled";   // reporting, no data, garbled, silent, disabled
  std::string state_text;
  std::uint32_t bytes = 0, frames = 0, bad_frames = 0;
  double frames_per_second = 0.0;   // over the last few seconds
  std::string firmware;             // from the last read_info, "" when never read
  int tracking_mode = 0;            // 1 = one target, 2 = up to three, 0 = unknown
};
Status status(std::size_t index);

enum class Op { kReadInfo, kSingleTarget, kMultiTarget, kSetZones, kRestart, kFactoryReset, kBluetooth };

struct CommandResult {
  bool ok = false;
  std::string error;                // "no_tx", "disabled", "busy", "no_answer", "refused", "bad_argument"
  std::string firmware;
  int tracking_mode = 0;
  bool has_zones = false;
  ld2450cmd::ZoneFilter zones;
  std::string last_answer_hex;      // the module's last raw acknowledgement, shown in the panel so a mismatch is seen at once
};

// Runs one configuration command on a radar: enable configuration, the command, end configuration. Blocks for up to a couple of seconds.
// `flag` is the bluetooth on/off; `zones` is required for kSetZones.
CommandResult run(std::size_t index, Op op, bool flag = false, const ld2450cmd::ZoneFilter* zones = nullptr);

}  // namespace armor::radar
