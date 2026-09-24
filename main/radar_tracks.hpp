// ARMOR-RADAR — transport-neutral radar track limits.
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D). GPL-3.0-or-later.
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>

namespace armor {
constexpr std::size_t kRadarCount = 3;
constexpr std::size_t kTracksPerRadar = 5;
constexpr std::size_t kMaximumTracks = kRadarCount * kTracksPerRadar;
struct Track { std::uint8_t sensor_id; std::uint8_t track_id; std::int16_t x_mm; std::int16_t y_mm; std::int16_t speed_mm_s; };
inline bool track_is_valid(const Track& track) {
  return track.sensor_id >= 1 && track.sensor_id <= kRadarCount && track.track_id > 0;
}
}
