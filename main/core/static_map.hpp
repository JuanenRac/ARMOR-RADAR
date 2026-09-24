// ARMOR-RADAR - suppression of static reflectors learned during an explicit calibration.
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D). GPL-3.0-or-later.
//
// A fence post or a metal door reflects the same echo every frame. Suppressing
// "anything that does not move" would hide a person standing still, so the map
// is learned only during a calibration the operator starts, from points that
// stayed put for `min_frames` frames while the area was known to be empty, and
// is applied only to tracks that are both inside a learned radius and stationary.
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include "../radar_tracks.hpp"

namespace armor {

constexpr std::size_t kMaxStaticPoints = 16;

class StaticMap {
 public:
  StaticMap(std::int32_t radius_mm = 250, std::int32_t max_speed_mm_s = 30, std::uint32_t min_frames = 20)
      : radius_(radius_mm), max_speed_(max_speed_mm_s), min_frames_(min_frames) {}

  // Calibration: call once per frame with the tracks seen while the area is empty.
  void observe(const Track* tracks, std::size_t count) {
    for (std::size_t i = 0; i < count; ++i) {
      if (!stationary(tracks[i])) continue;
      const int slot = nearest(tracks[i].x_mm, tracks[i].y_mm);
      if (slot >= 0) {
        if (points_[slot].frames < UINT32_MAX) ++points_[slot].frames;
      } else if (used_ < kMaxStaticPoints) {
        points_[used_++] = Point{tracks[i].x_mm, tracks[i].y_mm, 1};
      }
    }
  }

  // The number of points that persisted long enough to be treated as static.
  std::size_t learned() const {
    std::size_t n = 0;
    for (std::size_t i = 0; i < used_; ++i) n += points_[i].frames >= min_frames_ ? 1 : 0;
    return n;
  }

  // True when the track is a stationary echo at a learned position.
  bool suppresses(const Track& track) const {
    if (!stationary(track)) return false;
    const int slot = nearest(track.x_mm, track.y_mm);
    return slot >= 0 && points_[slot].frames >= min_frames_;
  }

  // Copy the tracks that are not suppressed into `out`; returns how many were kept.
  std::size_t filter(const Track* tracks, std::size_t count, Track* out, std::size_t capacity) const {
    std::size_t kept = 0;
    for (std::size_t i = 0; i < count && kept < capacity; ++i) {
      if (!suppresses(tracks[i])) out[kept++] = tracks[i];
    }
    return kept;
  }

  void clear() { used_ = 0; }

 private:
  struct Point {
    std::int32_t x;
    std::int32_t y;
    std::uint32_t frames;
  };
  bool stationary(const Track& t) const { return std::abs(static_cast<std::int32_t>(t.speed_mm_s)) <= max_speed_; }
  int nearest(std::int32_t x, std::int32_t y) const {
    for (std::size_t i = 0; i < used_; ++i) {
      const std::int64_t dx = x - points_[i].x, dy = y - points_[i].y;
      if (dx * dx + dy * dy <= static_cast<std::int64_t>(radius_) * radius_) return static_cast<int>(i);
    }
    return -1;
  }

  std::int32_t radius_;
  std::int32_t max_speed_;
  std::uint32_t min_frames_;
  std::array<Point, kMaxStaticPoints> points_{};
  std::size_t used_ = 0;
};

}  // namespace armor
