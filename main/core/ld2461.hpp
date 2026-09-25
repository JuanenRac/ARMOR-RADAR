// ARMOR-RADAR - the HLK-LD2461 (2T4R tracking radar): its report frames and its command frames.
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D). GPL-3.0-or-later.
//
// Source: Hi-Link "HLK-LD2461 serial communication protocol" V1.1 and the module's specification V1.1 (ARMOR's sensors folder); the worked examples
// of the protocol document are tests. Nothing here has been checked against a module.
//
//   Big-endian. Frame: FF EE DD | length (2) | command (1) | value (N) | checksum (1) | DD EE FF
//   length = 1 + N; checksum = the low byte of the sum of the command and the value bytes.
//   Reports (the module speaks first): command 0x07 = the coordinates of every target, an int8 x and an int8 y each, in units of 0.1 m
//                                      (the value is 2 bytes per target); command 0x08 = one byte per zone, 1 occupied, 0 free (3 zones).
//   Which report comes is a setting of the module (command 0x02: 1 coordinates, 2 zones, 3 both; the factory default is 2, zones only), so a node
//   that wants tracks must make sure the format is 1 or 3 (see radar_manager.cpp).
//   Serial 8N1, 9600 baud by default (the specification and the baud table say 9600; one sentence of the protocol says 256000: the node lets the
//   operator set the speed of each radar).
//   Field of view: 90 degrees horizontally (+-45) and 50 vertically, five tracks, moving people up to 8 m.
// What the documents do not say: the orientation of x and y beyond "x sideways, y forward" in the zone example, whether a target that is gone is
// reported as (0, 0) or left out, whether a track keeps its slot, and the speed of a target (there is none).
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>

#include "../radar_tracks.hpp"
#include "var_framer.hpp"

namespace armor::ld2461 {

constexpr std::array<std::uint8_t, 3> kHeader{0xFF, 0xEE, 0xDD};
constexpr std::array<std::uint8_t, 3> kFooter{0xDD, 0xEE, 0xFF};
constexpr std::size_t kMaxTargets = 5;
constexpr std::size_t kMaxValue = 40;

enum Command : std::uint8_t {
  kBaudRate = 0x01,
  kSetReportFormat = 0x02,
  kReadReportFormat = 0x03,
  kSetZone = 0x04,
  kCancelZone = 0x05,
  kReadZones = 0x06,
  kReportCoordinates = 0x07,
  kReportZones = 0x08,
  kVersion = 0x09,
  kFactoryReset = 0x0A,
};

// The framing rule for VarFramer: header, a 2-byte big-endian length that counts the command and the value, then the checksum and the footer.
inline VarFrameSpec protocol() {
  VarFrameSpec spec;
  spec.header = {0xFF, 0xEE, 0xDD, 0};
  spec.header_length = 3;
  spec.length_offset = 3;
  spec.length_bytes = 2;
  spec.length_big_endian = true;
  spec.length_extra = 4;   // the checksum and the three footer bytes
  spec.footer = {0xDD, 0xEE, 0xFF, 0};
  spec.footer_length = 3;
  spec.max_frame = 3 + 2 + 1 + kMaxValue + 4;
  return spec;
}

struct Frame {
  std::uint8_t command = 0;
  std::array<std::uint8_t, kMaxValue> value{};
  std::size_t value_length = 0;
};

// Checks a frame the framer found (header, length, checksum, footer) and splits it. False when it is not a valid frame.
inline bool parse(const std::uint8_t* frame, std::size_t length, Frame& out) {
  if (length < 3 + 2 + 1 + 1 + 3) return false;
  for (std::size_t i = 0; i < 3; ++i) if (frame[i] != kHeader[i] || frame[length - 3 + i] != kFooter[i]) return false;
  const std::size_t counted = (static_cast<std::size_t>(frame[3]) << 8) | frame[4];
  if (counted < 1 || 3 + 2 + counted + 1 + 3 != length) return false;
  const std::size_t value_length = counted - 1;
  if (value_length > kMaxValue) return false;
  std::uint8_t sum = 0;
  for (std::size_t i = 0; i < counted; ++i) sum = static_cast<std::uint8_t>(sum + frame[5 + i]);
  if (sum != frame[5 + counted]) return false;
  out.command = frame[5];
  out.value_length = value_length;
  for (std::size_t i = 0; i < value_length; ++i) out.value[i] = frame[6 + i];
  return true;
}

// The tracks of a coordinates report (command 0x07): the target in slot k becomes track k + 1, x and y from 0.1 m to millimetres, no speed.
// A pair of zeros is a slot without a target (a person cannot stand on the radar). Returns how many tracks were written.
inline std::size_t to_tracks(std::uint8_t sensor_id, const Frame& frame, Track* out) {
  if (frame.command != kReportCoordinates) return 0;
  std::size_t count = 0;
  const std::size_t pairs = frame.value_length / 2;
  for (std::size_t slot = 0; slot < pairs && slot < kMaxTargets; ++slot) {
    const std::int8_t x = static_cast<std::int8_t>(frame.value[slot * 2]);
    const std::int8_t y = static_cast<std::int8_t>(frame.value[slot * 2 + 1]);
    if (x == 0 && y == 0) continue;
    out[count++] = Track{sensor_id, static_cast<std::uint8_t>(slot + 1), static_cast<std::int16_t>(x * 100), static_cast<std::int16_t>(y * 100), 0};
  }
  return count;
}

// The zones report (command 0x08): occupied[0..2]; false when the frame is not one.
inline bool zone_occupancy(const Frame& frame, bool occupied[3]) {
  if (frame.command != kReportZones || frame.value_length < 3) return false;
  for (std::size_t i = 0; i < 3; ++i) occupied[i] = frame.value[i] != 0;
  return true;
}

// ---- commands ------------------------------------------------------------------------------------------------------------------

// A command frame: header, length, command, value, checksum, footer. Returns its length, or 0 when the value is too long for the buffer.
inline std::size_t build(std::uint8_t command, const std::uint8_t* value, std::size_t value_length, std::uint8_t* out, std::size_t capacity) {
  if (value_length > kMaxValue || capacity < 3 + 2 + 1 + value_length + 1 + 3) return 0;
  std::size_t at = 0;
  for (std::uint8_t byte : kHeader) out[at++] = byte;
  const std::size_t counted = 1 + value_length;
  out[at++] = static_cast<std::uint8_t>(counted >> 8);
  out[at++] = static_cast<std::uint8_t>(counted & 0xFF);
  std::uint8_t sum = command;
  out[at++] = command;
  for (std::size_t i = 0; i < value_length; ++i) { out[at++] = value[i]; sum = static_cast<std::uint8_t>(sum + value[i]); }
  out[at++] = sum;
  for (std::uint8_t byte : kFooter) out[at++] = byte;
  return at;
}

inline std::size_t build_query(std::uint8_t command, std::uint8_t* out, std::size_t capacity) {   // the commands with the value 0x01: version, read format, read zones, factory reset
  const std::uint8_t value = 0x01;
  return build(command, &value, 1, out, capacity);
}
inline std::size_t build_set_format(std::uint8_t format, std::uint8_t* out, std::size_t capacity) {   // 1 coordinates, 2 zones, 3 both
  if (format < 1 || format > 3) return 0;
  return build(kSetReportFormat, &format, 1, out, capacity);
}
inline std::size_t build_cancel_zone(std::uint8_t zone, std::uint8_t* out, std::size_t capacity) {
  if (zone < 1 || zone > 3) return 0;
  return build(kCancelZone, &zone, 1, out, capacity);
}
// The serial speed, as three bytes big-endian of the value in the protocol's table: 9600 = 0x002580 ... 256000 = 0x03E800. Not offered by the panel.
inline std::size_t build_baud(std::uint32_t baud, std::uint8_t* out, std::size_t capacity) {
  std::uint32_t code = 0;
  switch (baud) { case 9600: code = 0x002580; break; case 19200: code = 0x004B00; break; case 38400: code = 0x009600; break; case 57600: code = 0x00E100; break;
                  case 115200: code = 0x01C200; break; case 256000: code = 0x03E800; break; default: return 0; }
  const std::uint8_t value[3] = {static_cast<std::uint8_t>(code >> 16), static_cast<std::uint8_t>(code >> 8), static_cast<std::uint8_t>(code)};
  return build(kBaudRate, value, 3, out, capacity);
}

// A zone is a rectangle between two opposite corners, in millimetres of the radar's plane, sent as its four vertices in the order of the protocol's
// figure (the example: (-0.5, 2), (-0.5, 1), (0.5, 1), (0.5, 2), that is far-left, near-left, near-right, far-right), each coordinate an int8 in 0.1 m.
// `type`: 0 detect only what is inside, 1 ignore what is inside. Returns the frame length, or 0 for a rectangle beyond the int8 range (+-12.7 m).
inline std::size_t build_set_zone(std::uint8_t zone, std::uint8_t type, int x1_mm, int y1_mm, int x2_mm, int y2_mm, std::uint8_t* out, std::size_t capacity) {
  if (zone < 1 || zone > 3 || type > 1) return 0;
  const auto decimetres = [](int mm) { return mm >= 0 ? (mm + 50) / 100 : -((-mm + 50) / 100); };
  const int xa = decimetres(x1_mm < x2_mm ? x1_mm : x2_mm), xb = decimetres(x1_mm < x2_mm ? x2_mm : x1_mm);
  const int ya = decimetres(y1_mm < y2_mm ? y1_mm : y2_mm), yb = decimetres(y1_mm < y2_mm ? y2_mm : y1_mm);
  for (int v : {xa, xb, ya, yb}) if (v < -127 || v > 127) return 0;
  const std::uint8_t value[10] = {zone, static_cast<std::uint8_t>(xa), static_cast<std::uint8_t>(yb), static_cast<std::uint8_t>(xa), static_cast<std::uint8_t>(ya),
                                  static_cast<std::uint8_t>(xb), static_cast<std::uint8_t>(ya), static_cast<std::uint8_t>(xb), static_cast<std::uint8_t>(yb), type};
  return build(kSetZone, value, sizeof value, out, capacity);
}

struct Zone {
  std::uint8_t number = 0;
  std::uint8_t type = 0;              // 0 detect only, 1 ignore
  int x1_mm = 0, y1_mm = 0, x2_mm = 0, y2_mm = 0;
  bool set = false;                   // a zone that was never set reads back as zeros
};

// The three zones of a read-zones answer (command 0x06): 3 x (number, type, eight coordinates).
inline bool zones_from(const Frame& frame, Zone out[3]) {
  if (frame.command != kReadZones || frame.value_length < 30) return false;
  for (std::size_t i = 0; i < 3; ++i) {
    const std::uint8_t* at = frame.value.data() + i * 10;
    out[i].number = at[0];
    out[i].type = at[1];
    int xs[4], ys[4];
    bool any = false;
    for (std::size_t k = 0; k < 4; ++k) { xs[k] = static_cast<std::int8_t>(at[2 + k * 2]); ys[k] = static_cast<std::int8_t>(at[3 + k * 2]); any = any || xs[k] != 0 || ys[k] != 0; }
    out[i].set = any;
    int min_x = xs[0], max_x = xs[0], min_y = ys[0], max_y = ys[0];
    for (std::size_t k = 1; k < 4; ++k) { if (xs[k] < min_x) min_x = xs[k]; if (xs[k] > max_x) max_x = xs[k]; if (ys[k] < min_y) min_y = ys[k]; if (ys[k] > max_y) max_y = ys[k]; }
    out[i].x1_mm = min_x * 100; out[i].x2_mm = max_x * 100; out[i].y1_mm = min_y * 100; out[i].y2_mm = max_y * 100;
  }
  return true;
}

// The version of the answer to kVersion: 4 bytes (month, day, major, minor) then 4 bytes of identifier. Text such as "0.1 (59/1)" is not what
// the manual shows, so this gives the parts: version "major.minor", date month and day, and the identifier as 8 hexadecimal digits.
struct Version {
  bool ok = false;
  unsigned month = 0, day = 0, major = 0, minor = 0;
  std::uint32_t identifier = 0;
};
inline Version version_from(const Frame& frame) {
  Version v;
  if (frame.command != kVersion || frame.value_length < 8) return v;
  v.ok = true;
  v.month = frame.value[0]; v.day = frame.value[1]; v.major = frame.value[2]; v.minor = frame.value[3];
  v.identifier = (static_cast<std::uint32_t>(frame.value[4]) << 24) | (static_cast<std::uint32_t>(frame.value[5]) << 16) | (static_cast<std::uint32_t>(frame.value[6]) << 8) | frame.value[7];
  return v;
}

}  // namespace armor::ld2461
