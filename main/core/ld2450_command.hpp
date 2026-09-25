// ARMOR-RADAR - the command channel of the HLK-LD2450: builds the configuration commands and reads their acknowledgements.
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D). GPL-3.0-or-later.
//
// SOURCE AND STATUS. The report frame (ld2450.hpp) is written from Hi-Link's Instruction manual V1.00. The command channel is NOT in
// that manual: it is Hi-Link's separate serial-communication document for the module, which is not among the documents available
// here, so this file follows that protocol as it is widely documented and as community drivers implement it. It has NOT been checked
// against the document nor against a real module. The panel says so, offers only commands that cannot leave the module unreachable
// (there is no baud-rate change in the panel), and always shows the module's raw answer so a mismatch is seen at once. The first check
// on the bench is "read the firmware version": an answer with a version such as 1.02.22062416 confirms the framing.
//
//   command frame  FD FC FB FA | length (2, LE: command word + value) | command word (2, LE) | value | 04 03 02 01
//   acknowledgement same frame, command word = command | 0x0100, value = status (2, LE, 0 = done) + the answer
//   Commands only work between "enable configuration" (0x00FF) and "end configuration" (0x00FE); in between the module stops reporting
//   targets, so the caller must always finish with end_configuration.
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>

namespace armor::ld2450cmd {

constexpr std::array<std::uint8_t, 4> kHeader{0xFD, 0xFC, 0xFB, 0xFA};
constexpr std::array<std::uint8_t, 4> kFooter{0x04, 0x03, 0x02, 0x01};
constexpr std::size_t kMaxValue = 30;
constexpr std::size_t kMaxFrame = 4 + 2 + 2 + kMaxValue + 4;  // 42

enum Command : std::uint16_t {
  kEnableConfiguration = 0x00FF,
  kEndConfiguration = 0x00FE,
  kSingleTarget = 0x0080,
  kMultiTarget = 0x0090,
  kQueryTrackingMode = 0x0091,
  kReadFirmware = 0x00A0,
  kSetBaudRate = 0x00A1,  // built and tested, but the panel does not offer it: a wrong index would leave the module unreachable
  kRestoreFactory = 0x00A2,
  kRestart = 0x00A3,
  kBluetooth = 0x00A4,
  kReadMac = 0x00A5,
  kQueryZones = 0x00C1,
  kSetZones = 0x00C2,
};

// Writes a command frame; returns its length, or 0 when the value is too long for the buffer.
inline std::size_t build(std::uint16_t command, const std::uint8_t* value, std::size_t value_length, std::uint8_t* out, std::size_t capacity) {
  if (value_length > kMaxValue || capacity < 4 + 2 + 2 + value_length + 4) return 0;
  std::size_t at = 0;
  for (std::uint8_t byte : kHeader) out[at++] = byte;
  const std::uint16_t inner = static_cast<std::uint16_t>(2 + value_length);
  out[at++] = static_cast<std::uint8_t>(inner & 0xFF);
  out[at++] = static_cast<std::uint8_t>(inner >> 8);
  out[at++] = static_cast<std::uint8_t>(command & 0xFF);
  out[at++] = static_cast<std::uint8_t>(command >> 8);
  for (std::size_t i = 0; i < value_length; ++i) out[at++] = value[i];
  for (std::uint8_t byte : kFooter) out[at++] = byte;
  return at;
}

inline std::size_t build_enable_configuration(std::uint8_t* out, std::size_t capacity) {
  const std::uint8_t value[2] = {0x01, 0x00};
  return build(kEnableConfiguration, value, 2, out, capacity);
}
inline std::size_t build_simple(std::uint16_t command, std::uint8_t* out, std::size_t capacity) { return build(command, nullptr, 0, out, capacity); }
inline std::size_t build_bluetooth(bool on, std::uint8_t* out, std::size_t capacity) {
  const std::uint8_t value[2] = {static_cast<std::uint8_t>(on ? 0x01 : 0x00), 0x00};
  return build(kBluetooth, value, 2, out, capacity);
}
// The index of the serial speed: 1 = 9600, 2 = 19200, 3 = 38400, 4 = 57600, 5 = 115200, 6 = 230400, 7 = 256000 (the factory value), 8 = 460800.
inline std::size_t build_baud_rate(std::uint16_t index, std::uint8_t* out, std::size_t capacity) {
  if (index < 1 || index > 8) return 0;
  const std::uint8_t value[2] = {static_cast<std::uint8_t>(index & 0xFF), static_cast<std::uint8_t>(index >> 8)};
  return build(kSetBaudRate, value, 2, out, capacity);
}

// A rectangle of the radar's own plane, in millimetres, between two opposite corners.
struct Zone {
  std::int16_t x1 = 0, y1 = 0, x2 = 0, y2 = 0;
};
// 0: no filtering; 1: report only what is inside the zones; 2: report nothing that is inside the zones.
struct ZoneFilter {
  std::uint16_t type = 0;
  std::array<Zone, 3> zones{};
};

inline void put16(std::uint8_t* out, std::size_t& at, std::int16_t value) {
  const std::uint16_t raw = static_cast<std::uint16_t>(value);
  out[at++] = static_cast<std::uint8_t>(raw & 0xFF);
  out[at++] = static_cast<std::uint8_t>(raw >> 8);
}
inline std::int16_t get16(const std::uint8_t* in) { return static_cast<std::int16_t>(static_cast<std::uint16_t>(in[0] | (in[1] << 8))); }

inline std::size_t build_set_zones(const ZoneFilter& filter, std::uint8_t* out, std::size_t capacity) {
  if (filter.type > 2) return 0;
  std::uint8_t value[26];
  std::size_t at = 0;
  put16(value, at, static_cast<std::int16_t>(filter.type));
  for (const Zone& zone : filter.zones) { put16(value, at, zone.x1); put16(value, at, zone.y1); put16(value, at, zone.x2); put16(value, at, zone.y2); }
  return build(kSetZones, value, at, out, capacity);
}

// An acknowledgement: the command it answers, whether the module says it is done, and the answer that follows the status.
struct Ack {
  std::uint16_t command = 0;  // the command word with the 0x0100 answer bit removed
  bool ok = false;
  std::uint8_t data[kMaxValue]{};
  std::size_t data_length = 0;
};

// Finds acknowledgement frames in the byte stream of a radar. The module also sends its 30-byte reports in the same stream: those start
// with a different header and are ignored (the report framer takes them).
class AckParser {
 public:
  // Feeds a byte; true when it completed an acknowledgement, which is then in `ack`.
  bool feed(std::uint8_t byte, Ack& ack) {
    switch (state_) {
      case State::kHeader:
        if (byte == kHeader[matched_]) { if (++matched_ == kHeader.size()) { state_ = State::kLength; length_bytes_ = 0; matched_ = 0; } }
        else matched_ = (byte == kHeader[0]) ? 1 : 0;
        return false;
      case State::kLength:
        length_ = length_bytes_ == 0 ? byte : static_cast<std::uint16_t>(length_ | (byte << 8));
        if (++length_bytes_ == 2) {
          if (length_ < 4 || length_ > 2 + kMaxValue + 2) { state_ = State::kHeader; matched_ = 0; return false; }  // room for command, status and up to kMaxValue answer bytes
          state_ = State::kBody;
          body_length_ = 0;
        }
        return false;
      case State::kBody:
        body_[body_length_++] = byte;
        if (body_length_ == static_cast<std::size_t>(length_) + kFooter.size()) {
          state_ = State::kHeader;
          matched_ = 0;
          for (std::size_t i = 0; i < kFooter.size(); ++i) if (body_[length_ + i] != kFooter[i]) return false;
          const std::uint16_t word = static_cast<std::uint16_t>(body_[0] | (body_[1] << 8));
          if ((word & 0x0100) == 0) return false;  // not an answer
          ack.command = static_cast<std::uint16_t>(word & ~0x0100);
          ack.ok = body_[2] == 0 && body_[3] == 0;
          ack.data_length = static_cast<std::size_t>(length_) - 4;
          for (std::size_t i = 0; i < ack.data_length && i < kMaxValue; ++i) ack.data[i] = body_[4 + i];
          return true;
        }
        return false;
    }
    return false;
  }
  void reset() { state_ = State::kHeader; matched_ = 0; }

 private:
  enum class State { kHeader, kLength, kBody };
  State state_ = State::kHeader;
  std::size_t matched_ = 0;
  std::size_t length_bytes_ = 0;
  std::uint16_t length_ = 0;
  std::size_t body_length_ = 0;
  std::uint8_t body_[2 + kMaxValue + 2 + 4]{};
};

// The version in the answer to kReadFirmware, written the way the vendor's tools show it (for example "1.02.22062416"): the answer is a
// type word, a two-byte major (the high byte first in the text) and a four-byte build code printed high byte first.
inline std::string firmware_text(const Ack& ack) {
  if (ack.command != kReadFirmware || !ack.ok || ack.data_length < 8) return "";
  char text[40];
  std::snprintf(text, sizeof text, "%u.%02X.%02X%02X%02X%02X", static_cast<unsigned>(ack.data[3]), static_cast<unsigned>(ack.data[2]), static_cast<unsigned>(ack.data[7]),
                static_cast<unsigned>(ack.data[6]), static_cast<unsigned>(ack.data[5]), static_cast<unsigned>(ack.data[4]));
  return text;
}

// The tracking mode in the answer to kQueryTrackingMode: 1 = one target, 2 = up to three; 0 when the answer is not understood.
inline int tracking_mode(const Ack& ack) {
  if (ack.command != kQueryTrackingMode || !ack.ok || ack.data_length < 2) return 0;
  const int mode = ack.data[0] | (ack.data[1] << 8);
  return mode == 1 || mode == 2 ? mode : 0;
}

// The zones in the answer to kQueryZones. False when the answer is not understood.
inline bool zones_from_ack(const Ack& ack, ZoneFilter& filter) {
  if (ack.command != kQueryZones || !ack.ok || ack.data_length < 2 + 3 * 8) return false;
  filter.type = static_cast<std::uint16_t>(ack.data[0] | (ack.data[1] << 8));
  if (filter.type > 2) return false;
  for (std::size_t i = 0; i < 3; ++i) {
    const std::uint8_t* at = ack.data + 2 + i * 8;
    filter.zones[i] = {get16(at), get16(at + 2), get16(at + 4), get16(at + 6)};
  }
  return true;
}

}  // namespace armor::ld2450cmd
