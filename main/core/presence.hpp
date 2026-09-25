// ARMOR-RADAR - the presence sensors: the HLK-LD2410 (B and C), LD2412, LD2410S and the Seeed MR24HPC1.
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D). GPL-3.0-or-later.
//
// Sources (ARMOR's sensors folder): "HLK-LD2410C serial communication protocol" V1.00 and the LD2410B's; "HLK-LD2412 serial communication protocol"
// V1.05; "HLK-LD2410S user manual" V1.2, section 5; "MR24HPC1 user manual" V1.5, section 7. The worked examples of the documents are tests
// (tests/test_sensors.cpp). None of these sensors has been connected.
//
// LD2410 / LD2412 report frame (small-endian):  F4 F3 F2 F1 | length (2) | data | F8 F7 F6 F5, and the data is
//     type (1: 1 engineering, 2 basic) | 0xAA | state (1) | moving distance cm (2) | moving energy (1) | stationary distance cm (2) |
//     stationary energy (1) | [LD2410 only: detection distance cm (2)] | ... | 0x55 | 0x00
//   State: 0 nobody, 1 moving, 2 stationary, 3 both; on the LD2412 4 to 6 are the results of the background-noise calibration.
//   In engineering mode the same fields come first and the energy of each distance gate follows; the gates are not decoded here.
//   The LD2412's basic frame has no detection distance (its length is 11, the LD2410's 13): a variant flag tells them apart.
// LD2410S report frame: F4 F3 F2 F1 | length (2) | state (1: 0/1 nobody, 2/3 somebody) | distance cm (2) | 34 reserved bytes | F8 F7 F6 F5.
// MR24HPC1 frame (big-endian length): 53 59 | control (1) | command (1) | length (2) | data | checksum (1) | 54 43, the checksum being the low byte of
//     the sum of everything before it. Reports: control 0x80 with command 0x01 occupied (0/1), 0x02 motion (0 none, 1 motionless, 2 active),
//     0x03 body-movement parameter (0 to 100), 0x0B proximity (0 none, 1 approaching, 2 moving away).
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>

#include "var_framer.hpp"

namespace armor::presence {

// ---- framing rules ---------------------------------------------------------------------------------------------------------------

// LD2410, LD2412 and LD2410S reports: F4 F3 F2 F1, a 2-byte small-endian length of the data, then the F8 F7 F6 F5 end.
inline VarFrameSpec report_protocol() {
  VarFrameSpec spec;
  spec.header = {0xF4, 0xF3, 0xF2, 0xF1};
  spec.header_length = 4;
  spec.length_offset = 4;
  spec.length_bytes = 2;
  spec.length_big_endian = false;
  spec.length_extra = 4;
  spec.footer = {0xF8, 0xF7, 0xF6, 0xF5};
  spec.footer_length = 4;
  spec.max_frame = 4 + 2 + 80 + 4;
  return spec;
}

// MR24HPC1: 53 59, control, command, a 2-byte big-endian length of the data, the data, a checksum and 54 43.
inline VarFrameSpec mr24_protocol() {
  VarFrameSpec spec;
  spec.header = {0x53, 0x59, 0, 0};
  spec.header_length = 2;
  spec.length_offset = 4;
  spec.length_bytes = 2;
  spec.length_big_endian = true;
  spec.length_extra = 3;
  spec.footer = {0x54, 0x43, 0, 0};
  spec.footer_length = 2;
  spec.max_frame = 2 + 1 + 1 + 2 + 40 + 1 + 2;
  return spec;
}

// ---- readings --------------------------------------------------------------------------------------------------------------------

struct Reading {
  bool valid = false;
  std::uint8_t state = 0;          // 0 nobody, 1 moving, 2 stationary, 3 both (LD2410S: 0/1 nobody, 2/3 somebody)
  bool present = false;
  bool moving = false;
  bool stationary = false;
  bool calibrating = false;        // LD2412 background-noise calibration in progress (state 4)
  bool engineering = false;
  int moving_cm = -1, moving_energy = -1;
  int static_cm = -1, static_energy = -1;
  int detection_cm = -1;           // LD2410 only
  int distance_cm = -1;            // the distance to report: the nearer of the moving and the stationary target that exist, or the LD2410S's own
};

enum class Family { kLd2410, kLd2412, kLd2410s };

inline int le16(const std::uint8_t* p) { return p[0] | (p[1] << 8); }

// A report frame of the LD2410 family (the whole frame, header and end included, as the framer hands it over).
inline bool decode_report(Family family, const std::uint8_t* frame, std::size_t length, Reading& out) {
  if (length < 4 + 2 + 4 + 4) return false;
  const std::size_t counted = static_cast<std::size_t>(le16(frame + 4));
  if (4 + 2 + counted + 4 != length) return false;
  const std::uint8_t* data = frame + 6;
  out = Reading{};
  if (family == Family::kLd2410s) {
    if (counted < 3) return false;
    out.state = data[0];
    out.present = data[0] >= 2 && data[0] <= 3;
    out.distance_cm = le16(data + 1);
    out.valid = data[0] <= 3;
    return out.valid;
  }
  // type, 0xAA, state, moving distance (2), moving energy, stationary distance (2), stationary energy, [detection distance (2)], ..., 0x55, 0x00
  const std::size_t fixed = family == Family::kLd2410 ? 13 : 11;
  if (counted < fixed || data[0] < 1 || data[0] > 2 || data[1] != 0xAA) return false;
  if (data[counted - 2] != 0x55 || data[counted - 1] != 0x00) return false;
  out.engineering = data[0] == 1;
  out.state = data[2];
  out.moving_cm = le16(data + 3);
  out.moving_energy = data[5];
  out.static_cm = le16(data + 6);
  out.static_energy = data[8];
  if (family == Family::kLd2410) out.detection_cm = le16(data + 9);
  if (out.state > 3) {
    out.calibrating = family == Family::kLd2412 && out.state == 4;
    out.valid = family == Family::kLd2412 && out.state <= 6;
    return out.valid;   // 5 and 6 are the end of a calibration; nobody is reported
  }
  out.moving = out.state == 1 || out.state == 3;
  out.stationary = out.state == 2 || out.state == 3;
  out.present = out.state != 0;
  if (out.moving && out.stationary) out.distance_cm = out.moving_cm < out.static_cm ? out.moving_cm : out.static_cm;
  else if (out.moving) out.distance_cm = out.moving_cm;
  else if (out.stationary) out.distance_cm = out.static_cm;
  out.valid = true;
  return true;
}

// ---- MR24HPC1 --------------------------------------------------------------------------------------------------------------------

enum class Mr24Kind { kNone, kOccupied, kMotion, kBodyMovement, kProximity };
struct Mr24Event {
  Mr24Kind kind = Mr24Kind::kNone;
  int value = 0;
};

// The checksum of an MR24 frame: the low byte of the sum of every byte before it.
inline std::uint8_t mr24_checksum(const std::uint8_t* frame, std::size_t length_without_checksum_and_tail) {
  std::uint8_t sum = 0;
  for (std::size_t i = 0; i < length_without_checksum_and_tail; ++i) sum = static_cast<std::uint8_t>(sum + frame[i]);
  return sum;
}

// One MR24 frame as the framer hands it over. False when the checksum is wrong; an event of kind kNone when the frame is valid but is not one
// of the four reports (a heartbeat, product information...).
inline bool decode_mr24(const std::uint8_t* frame, std::size_t length, Mr24Event& out) {
  out = Mr24Event{};
  if (length < 2 + 1 + 1 + 2 + 1 + 2) return false;
  const std::size_t data_length = (static_cast<std::size_t>(frame[4]) << 8) | frame[5];
  if (2 + 1 + 1 + 2 + data_length + 1 + 2 != length) return false;
  if (mr24_checksum(frame, length - 3) != frame[length - 3]) return false;
  if (frame[2] != 0x80 || data_length != 1) return true;
  const int value = frame[6];
  switch (frame[3]) {
    case 0x01: if (value <= 1) out = {Mr24Kind::kOccupied, value}; break;
    case 0x02: if (value <= 2) out = {Mr24Kind::kMotion, value}; break;
    case 0x03: if (value <= 100) out = {Mr24Kind::kBodyMovement, value}; break;
    case 0x0B: if (value <= 2) out = {Mr24Kind::kProximity, value}; break;
    default: break;
  }
  return true;
}

// What the MR24 has said so far: it reports on change, so the state is the memory of the events.
struct Mr24State {
  bool known = false;             // an occupied report has been seen
  bool occupied = false;
  int motion = 0;                 // 0 none, 1 motionless, 2 active
  int body_movement = 0;
  int proximity = 0;
  // Applies an event; true when the occupied flag changed (the moment worth publishing at once).
  bool apply(const Mr24Event& event) {
    switch (event.kind) {
      case Mr24Kind::kOccupied: { const bool changed = !known || occupied != (event.value == 1); known = true; occupied = event.value == 1; return changed; }
      case Mr24Kind::kMotion: motion = event.value; return false;
      case Mr24Kind::kBodyMovement: body_movement = event.value; return false;
      case Mr24Kind::kProximity: proximity = event.value; return false;
      case Mr24Kind::kNone: break;
    }
    return false;
  }
};

}  // namespace armor::presence
