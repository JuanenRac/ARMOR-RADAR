// ARMOR-RADAR - what each radar has reported, to tell a live radar from a silent or a garbled one.
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D). GPL-3.0-or-later.
//
// On the bench the first question is always "is this radar wired right?": bytes arriving without frames mean the wrong baud rate or
// a noisy line, frames arriving means the wiring is right, and nothing at all means RX is not connected. The node logs these counters
// every few seconds so that answer is on the serial console.
#pragma once
#include <cstddef>
#include <cstdint>
#include "../radar_tracks.hpp"

namespace armor {

enum class RadarState : std::uint8_t {
  kNoData,     // not one byte since start: RX not connected, no power, or the wrong pin
  kGarbled,    // bytes arrive but no valid frame: the wrong baud rate, a bad ground or a module in another mode
  kSilent,     // it was reporting and stopped
  kReporting,  // valid frames, recently
};

struct RadarCounters {
  std::uint32_t bytes = 0;
  std::uint32_t frames = 0;
  std::uint32_t bad_frames = 0;
  std::uint64_t last_frame_ms = 0;
};

class RadarHealth {
 public:
  explicit RadarHealth(std::uint32_t silent_after_ms = 3000) : silent_after_ms_(silent_after_ms) {}

  void bytes_received(std::uint8_t radar, std::size_t count) { if (valid(radar)) counters_[radar - 1].bytes += static_cast<std::uint32_t>(count); }
  void frame_ok(std::uint8_t radar, std::uint64_t now_ms) {
    if (!valid(radar)) return;
    RadarCounters& counters = counters_[radar - 1];
    ++counters.frames;
    counters.last_frame_ms = now_ms;
  }
  void frame_bad(std::uint8_t radar) { if (valid(radar)) ++counters_[radar - 1].bad_frames; }

  const RadarCounters& counters(std::uint8_t radar) const { return counters_[valid(radar) ? radar - 1 : 0]; }

  RadarState state(std::uint8_t radar, std::uint64_t now_ms) const {
    if (!valid(radar)) return RadarState::kNoData;
    const RadarCounters& counters = counters_[radar - 1];
    if (counters.frames > 0) return now_ms - counters.last_frame_ms > silent_after_ms_ ? RadarState::kSilent : RadarState::kReporting;
    return counters.bytes > 0 ? RadarState::kGarbled : RadarState::kNoData;
  }

  static const char* describe(RadarState state) {
    switch (state) {
      case RadarState::kNoData: return "no data (RX not connected, no power or the wrong pin)";
      case RadarState::kGarbled: return "bytes but no valid frame (wrong baud rate, bad ground or another mode)";
      case RadarState::kSilent: return "was reporting and stopped";
      case RadarState::kReporting: return "reporting";
    }
    return "?";
  }

 private:
  static bool valid(std::uint8_t radar) { return radar >= 1 && radar <= kRadarCount; }
  RadarCounters counters_[kRadarCount]{};
  std::uint32_t silent_after_ms_;
};

}  // namespace armor
