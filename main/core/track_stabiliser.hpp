// ARMOR-RADAR - stable track identities for a radar whose slots are not identities.
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D). GPL-3.0-or-later.
//
// A radar reports "target in slot 1, slot 2, slot 3", and nothing says that slot 1 is the same person from one frame to the next: when a target
// is lost for a frame the others may change slot, and the slot number the node used to send as the track id then jumped from one person to another.
// The server counts and times targets by track id (dwell before an alarm, the target counted once), so an identity that jumps costs alarms.
//
// This keeps its own tracks. Each frame, every detection is matched to the nearest existing track within a gate (greedy, nearest pair first);
// a matched track keeps its id and its position is smoothed a little; a detection nobody claims starts a new track with the next id; a track that
// is not matched is kept for a few frames (and still reported for the first ones, so one lost frame does not make a target flicker) and then dropped.
// A silence longer than `reset_after_ms` forgets everything, as a radar that was off knows nothing about who was where.
// Ids run 1 to 255 and wrap, skipping 0 (the contract wants a positive id). Nothing here decides who is a person: it only keeps a name attached.
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>

#include "../radar_tracks.hpp"

namespace armor {

struct StabiliserConfig {
  int gate_mm = 600;                // how far a target may move between two frames and still be the same one (10 frames per second: 6 m/s)
  int gate_growth_mm = 200;         // the gate grows by this for every frame a track was missed, up to twice the gate
  int smoothing_percent = 60;       // how much of the new position is taken: 100 = raw, lower = calmer
  unsigned max_missed = 5;          // frames a track survives without a detection
  unsigned coast_reported = 2;      // of those, the first frames it is still reported at its last position
  std::uint32_t reset_after_ms = 1000;
};

class TrackStabiliser {
 public:
  explicit TrackStabiliser(const StabiliserConfig& config = StabiliserConfig{}) : config_(config) {}

  // The detections of one frame in, the stable tracks out (`out` must hold kTracksPerRadar entries). Returns how many were written.
  std::size_t update(const Track* detections, std::size_t count, std::uint64_t now_ms, Track* out) {
    if (seen_ && now_ms - last_ms_ > config_.reset_after_ms) reset();
    seen_ = true;
    last_ms_ = now_ms;
    if (count > kTracksPerRadar) count = kTracksPerRadar;

    // 1. every (track, detection) pair inside the track's gate, nearest first
    struct Pair { std::size_t track, detection; long distance2; };
    Pair pairs[kTracksPerRadar * kTracksPerRadar];
    std::size_t pair_count = 0;
    for (std::size_t t = 0; t < kTracksPerRadar; ++t) {
      if (!tracks_[t].used) continue;
      const long gate = gate_of(tracks_[t]);
      for (std::size_t d = 0; d < count; ++d) {
        const long dx = detections[d].x_mm - tracks_[t].x, dy = detections[d].y_mm - tracks_[t].y;
        const long distance2 = dx * dx + dy * dy;
        if (distance2 <= gate * gate) pairs[pair_count++] = Pair{t, d, distance2};
      }
    }
    for (std::size_t i = 1; i < pair_count; ++i) {   // insertion sort: at most 25 pairs
      const Pair key = pairs[i];
      std::size_t j = i;
      while (j > 0 && pairs[j - 1].distance2 > key.distance2) { pairs[j] = pairs[j - 1]; --j; }
      pairs[j] = key;
    }
    // 2. the nearest pair first, each track and each detection used once
    bool track_taken[kTracksPerRadar] = {};
    bool detection_taken[kTracksPerRadar] = {};
    bool fresh[kTracksPerRadar] = {};
    for (std::size_t i = 0; i < pair_count; ++i) {
      const Pair& p = pairs[i];
      if (track_taken[p.track] || detection_taken[p.detection]) continue;
      track_taken[p.track] = detection_taken[p.detection] = true;
      Slot& slot = tracks_[p.track];
      slot.x += (detections[p.detection].x_mm - slot.x) * config_.smoothing_percent / 100;
      slot.y += (detections[p.detection].y_mm - slot.y) * config_.smoothing_percent / 100;
      slot.speed = detections[p.detection].speed_mm_s;
      slot.missed = 0;
      fresh[p.track] = true;
    }
    // 3. a track that found nothing ages; the ones too old go
    for (std::size_t t = 0; t < kTracksPerRadar; ++t) {
      if (!tracks_[t].used || track_taken[t]) continue;
      if (++tracks_[t].missed > config_.max_missed) tracks_[t].used = false;
    }
    // 4. a detection nobody claimed is a new track (a free slot, or the one that has been missing longest)
    for (std::size_t d = 0; d < count; ++d) {
      if (detection_taken[d]) continue;
      std::size_t chosen = kTracksPerRadar;
      for (std::size_t t = 0; t < kTracksPerRadar; ++t) if (!tracks_[t].used) { chosen = t; break; }
      if (chosen == kTracksPerRadar) {
        unsigned worst = 0;
        for (std::size_t t = 0; t < kTracksPerRadar; ++t) if (!fresh[t] && tracks_[t].missed >= worst) { worst = tracks_[t].missed; chosen = t; }
      }
      if (chosen == kTracksPerRadar) continue;   // every slot was matched this frame: nothing to give up
      Slot& slot = tracks_[chosen];
      slot = Slot{};
      slot.used = true;
      slot.id = next_id_;
      next_id_ = next_id_ == 255 ? 1 : static_cast<std::uint8_t>(next_id_ + 1);
      slot.x = detections[d].x_mm;
      slot.y = detections[d].y_mm;
      slot.speed = detections[d].speed_mm_s;
      fresh[chosen] = true;
      sensor_ = detections[d].sensor_id;
    }
    // 5. what is reported: the tracks seen now, and the ones only just lost
    std::size_t written = 0;
    for (std::size_t t = 0; t < kTracksPerRadar; ++t) {
      const Slot& slot = tracks_[t];
      if (!slot.used || (!fresh[t] && slot.missed > config_.coast_reported)) continue;
      out[written++] = Track{sensor_, slot.id, static_cast<std::int16_t>(slot.x), static_cast<std::int16_t>(slot.y), fresh[t] ? slot.speed : static_cast<std::int16_t>(0)};
    }
    return written;
  }

  // The radar id that goes on the tracks (a stabiliser serves one radar).
  void set_sensor(std::uint8_t sensor_id) { sensor_ = sensor_id; }

  void reset() {
    for (Slot& slot : tracks_) slot = Slot{};
    seen_ = false;
  }

 private:
  struct Slot {
    bool used = false;
    std::uint8_t id = 0;
    long x = 0, y = 0;
    std::int16_t speed = 0;
    unsigned missed = 0;
  };
  long gate_of(const Slot& slot) const {
    long gate = config_.gate_mm + static_cast<long>(slot.missed) * config_.gate_growth_mm;
    return gate > 2L * config_.gate_mm ? 2L * config_.gate_mm : gate;
  }

  StabiliserConfig config_;
  std::array<Slot, kTracksPerRadar> tracks_{};
  std::uint8_t next_id_ = 1;
  std::uint8_t sensor_ = 1;
  std::uint64_t last_ms_ = 0;
  bool seen_ = false;
};

}  // namespace armor
