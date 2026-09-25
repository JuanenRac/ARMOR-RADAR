// ARMOR-RADAR - decoder for the HLK-LD2450 target report frame.
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D). GPL-3.0-or-later.
//
// Source: Hi-Link "HLK-LD2450 Motion target detection and tracking module, Instruction
// manual", version V1.00 (2023-05-10), section 6 "Communication protocols" and the
// electrical table in section 8. Nothing here is taken from memory or from another
// project; the worked example of the manual (a frame with one target) is a test.
//
//   UART 256000 baud, 8 data bits, 1 stop bit, no parity; 3.3 V logic; reported at 10 Hz.
//   Frame (30 bytes): header AA FF 03 00, three targets of 8 bytes each, tail 55 CC.
//   Target (all fields little-endian):
//     x           2 bytes  bit 15 = 1 positive, 0 negative; the other 15 bits are the magnitude, mm
//     y           2 bytes  same encoding, mm
//     speed       2 bytes  same encoding, cm/s
//     resolution  2 bytes  unsigned, the size of one distance gate, mm
//   A target that does not exist is reported with all eight bytes at 0x00.
//
// What the manual does NOT say, and this file therefore does not claim: whether a
// target keeps its slot (1 to 3) from frame to frame, the origin and orientation of the
// axes beyond the worked example, and anything about the LD2461. A slot number is used as
// the track id only because the manual gives no better identity.
//
// The configuration commands of the module are not part of this manual and are not implemented.
#pragma once
#include <cstddef>
#include <cstdint>
#include "frame_framer.hpp"
#include "../radar_tracks.hpp"

namespace armor {
namespace ld2450 {

constexpr std::uint32_t kBaudRate = 256000;
constexpr std::size_t kTargetsPerFrame = 3;
constexpr std::size_t kTargetBytes = 8;
constexpr std::size_t kHeaderLength = 4;
constexpr std::size_t kTailLength = 2;
constexpr std::size_t kFrameLength = kHeaderLength + kTargetsPerFrame * kTargetBytes + kTailLength;  // 30
constexpr std::uint8_t kHeader[kHeaderLength] = {0xAA, 0xFF, 0x03, 0x00};
constexpr std::uint8_t kTail[kTailLength] = {0x55, 0xCC};

// The framing rule for FrameFramer.
inline ProtocolSpec protocol() {
  ProtocolSpec spec;
  for (std::size_t i = 0; i < kHeaderLength; ++i) spec.header[i] = kHeader[i];
  spec.header_length = kHeaderLength;
  for (std::size_t i = 0; i < kTailLength; ++i) spec.tail[i] = kTail[i];
  spec.tail_length = kTailLength;
  spec.frame_length = kFrameLength;
  return spec;
}

struct Target {
  bool present = false;
  std::int16_t x_mm = 0;
  std::int16_t y_mm = 0;
  std::int16_t speed_cm_s = 0;
  std::uint16_t resolution_mm = 0;
};

struct Frame {
  Target targets[kTargetsPerFrame];
  std::size_t count() const {
    std::size_t n = 0;
    for (const Target& target : targets) n += target.present ? 1 : 0;
    return n;
  }
};

// Bit 15 set means positive, clear means negative; the other 15 bits are the magnitude.
inline std::int16_t decode_signed(std::uint16_t raw) {
  const int magnitude = raw & 0x7FFF;
  return static_cast<std::int16_t>((raw & 0x8000) ? magnitude : -magnitude);
}

inline std::uint16_t little_endian(const std::uint8_t* bytes) { return static_cast<std::uint16_t>(bytes[0] | (bytes[1] << 8)); }

// Decodes one frame as delivered by FrameFramer. Returns false, and leaves `out` empty, for
// anything that is not exactly a well-formed frame.
inline bool decode_frame(const std::uint8_t* data, std::size_t length, Frame& out) {
  out = Frame{};
  if (data == nullptr || length != kFrameLength) return false;
  for (std::size_t i = 0; i < kHeaderLength; ++i) if (data[i] != kHeader[i]) return false;
  for (std::size_t i = 0; i < kTailLength; ++i) if (data[kFrameLength - kTailLength + i] != kTail[i]) return false;
  for (std::size_t slot = 0; slot < kTargetsPerFrame; ++slot) {
    const std::uint8_t* raw = data + kHeaderLength + slot * kTargetBytes;
    bool all_zero = true;
    for (std::size_t i = 0; i < kTargetBytes; ++i) all_zero = all_zero && raw[i] == 0;
    if (all_zero) continue;  // an empty slot
    Target& target = out.targets[slot];
    target.present = true;
    target.x_mm = decode_signed(little_endian(raw));
    target.y_mm = decode_signed(little_endian(raw + 2));
    target.speed_cm_s = decode_signed(little_endian(raw + 4));
    target.resolution_mm = little_endian(raw + 6);
  }
  return true;
}

// The tracks of one frame, in the units of the message contract (speed in mm/s). The slot number
// (1 to 3) is the track id. A speed that would not fit the contract's range is clamped, which
// a real target can not reach. `out` must hold kTargetsPerFrame entries.
inline std::size_t to_tracks(std::uint8_t sensor_id, const Frame& frame, Track* out) {
  std::size_t count = 0;
  for (std::size_t slot = 0; slot < kTargetsPerFrame; ++slot) {
    const Target& target = frame.targets[slot];
    if (!target.present) continue;
    int speed = target.speed_cm_s * 10;
    if (speed > 32767) speed = 32767;
    if (speed < -32768) speed = -32768;
    out[count++] = Track{sensor_id, static_cast<std::uint8_t>(slot + 1), target.x_mm, target.y_mm, static_cast<std::int16_t>(speed)};
  }
  return count;
}

}  // namespace ld2450

// The latest tracks of each of the three radars. A radar that stops reporting must not leave ghost
// targets behind, so tracks older than `max_age_ms` are not reported.
class TrackSet {
 public:
  explicit TrackSet(std::uint32_t max_age_ms = 500) : max_age_ms_(max_age_ms) {}

  // Replace the tracks of one radar (1 to kRadarCount) with those of its newest frame.
  void update(std::uint8_t sensor_id, const Track* tracks, std::size_t count, std::uint64_t now_ms) {
    if (sensor_id < 1 || sensor_id > kRadarCount) return;
    Slot& slot = slots_[sensor_id - 1];
    slot.count = count > kTracksPerRadar ? kTracksPerRadar : count;
    for (std::size_t i = 0; i < slot.count; ++i) slot.tracks[i] = tracks[i];
    slot.updated_ms = now_ms;
    slot.seen = true;
  }

  // Every fresh track of every radar; returns how many were written (at most kMaximumTracks).
  std::size_t collect(Track* out, std::uint64_t now_ms) const {
    std::size_t total = 0;
    for (const Slot& slot : slots_) {
      if (!slot.seen || now_ms - slot.updated_ms > max_age_ms_) continue;
      for (std::size_t i = 0; i < slot.count; ++i) out[total++] = slot.tracks[i];
    }
    return total;
  }

  // True when at least one radar has reported recently (even an empty frame proves it is alive).
  bool any_fresh(std::uint64_t now_ms) const {
    for (const Slot& slot : slots_) if (slot.seen && now_ms - slot.updated_ms <= max_age_ms_) return true;
    return false;
  }

 private:
  struct Slot {
    Track tracks[kTracksPerRadar]{};
    std::size_t count = 0;
    std::uint64_t updated_ms = 0;
    bool seen = false;
  };
  Slot slots_[kRadarCount];
  std::uint32_t max_age_ms_;
};

}  // namespace armor
