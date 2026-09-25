// ARMOR-RADAR - serialisation of the message contract (telemetry and health).
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D). GPL-3.0-or-later.
//
// Produces exactly the payloads ARMOR-COMMON publishes as JSON Schemas:
//   telemetry: {"node_id","timestamp_ms","lux","targets":[{"sensor_id","track_id","x_mm","y_mm","speed_mm_s"}]}
//   health:    {"node_id","timestamp_ms","online"}
// Nothing is written unless the values already satisfy the contract, so a
// malformed message can not leave the node.
#pragma once
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string_view>
#include <cstring>
#include <string>
#include "json.hpp"
#include "net_text.hpp"
#include "node_id.hpp"
#include "../radar_tracks.hpp"

namespace armor {

constexpr float kMaxLux = 200000.0f;

enum class JsonResult { kOk, kInvalidNodeId, kInvalidLux, kTooManyTracks, kInvalidTrack, kBufferTooSmall, kInvalidInfo };

namespace detail {
// Append with bounds checking; the position moves past the buffer on overflow so the caller can detect it.
template <typename... Args>
void append(char* out, std::size_t capacity, std::size_t& position, const char* format, Args... args) {
  if (position >= capacity) return;
  const int written = std::snprintf(out + position, capacity - position, format, args...);
  position = written < 0 ? capacity : position + static_cast<std::size_t>(written);
}
}  // namespace detail

// Builds the telemetry payload. `length` receives the number of characters written (without a terminator).
// At most kTracksPerRadar tracks per sensor and kMaximumTracks in total are allowed.
inline JsonResult build_telemetry(std::string_view node_id, std::uint64_t timestamp_ms, float lux, const Track* tracks,
                                  std::size_t track_count, char* out, std::size_t capacity, std::size_t& length) {
  length = 0;
  if (!node_id_is_valid(node_id)) return JsonResult::kInvalidNodeId;
  if (!(lux >= 0.0f && lux <= kMaxLux)) return JsonResult::kInvalidLux;  // also rejects NaN
  if (track_count > kMaximumTracks) return JsonResult::kTooManyTracks;
  std::size_t per_sensor[kRadarCount + 1] = {0, 0, 0, 0};
  for (std::size_t i = 0; i < track_count; ++i) {
    if (!track_is_valid(tracks[i])) return JsonResult::kInvalidTrack;
    if (++per_sensor[tracks[i].sensor_id] > kTracksPerRadar) return JsonResult::kTooManyTracks;
  }
  std::size_t position = 0;
  detail::append(out, capacity, position, "{\"node_id\":\"%.*s\",\"timestamp_ms\":%llu,\"lux\":%.1f,\"targets\":[", static_cast<int>(node_id.size()),
                 node_id.data(), static_cast<unsigned long long>(timestamp_ms), static_cast<double>(lux));
  for (std::size_t i = 0; i < track_count; ++i) {
    detail::append(out, capacity, position, "%s{\"sensor_id\":%u,\"track_id\":%u,\"x_mm\":%d,\"y_mm\":%d,\"speed_mm_s\":%d}", i ? "," : "",
                   static_cast<unsigned>(tracks[i].sensor_id), static_cast<unsigned>(tracks[i].track_id), static_cast<int>(tracks[i].x_mm),
                   static_cast<int>(tracks[i].y_mm), static_cast<int>(tracks[i].speed_mm_s));
  }
  detail::append(out, capacity, position, "%s", "]}");
  if (position >= capacity) return JsonResult::kBufferTooSmall;
  length = position;
  return JsonResult::kOk;
}

inline JsonResult build_health(std::string_view node_id, std::uint64_t timestamp_ms, bool online, char* out, std::size_t capacity,
                               std::size_t& length) {
  length = 0;
  if (!node_id_is_valid(node_id)) return JsonResult::kInvalidNodeId;
  std::size_t position = 0;
  detail::append(out, capacity, position, "{\"node_id\":\"%.*s\",\"timestamp_ms\":%llu,\"online\":%s}", static_cast<int>(node_id.size()),
                 node_id.data(), static_cast<unsigned long long>(timestamp_ms), online ? "true" : "false");
  if (position >= capacity) return JsonResult::kBufferTooSmall;
  length = position;
  return JsonResult::kOk;
}

// The information message: armor/node/{node_id}/info  {"node_id","timestamp_ms","name","firmware","ip","port"}. It lets a console offer a link to the
// node's own web panel. Nothing is written unless every field satisfies the contract (name 1 to 48 characters of valid UTF-8, firmware x.y.z,
// a dotted-quad IPv4 address without leading zeros, port 1 to 65535).
inline JsonResult build_info(std::string_view node_id, std::uint64_t timestamp_ms, std::string_view name, std::string_view firmware, std::string_view ip, unsigned port,
                             char* out, std::size_t capacity, std::size_t& length) {
  length = 0;
  if (!node_id_is_valid(node_id)) return JsonResult::kInvalidNodeId;
  std::size_t characters = 0;
  if (!net::valid_utf8(name, &characters) || characters < 1 || characters > 48) return JsonResult::kInvalidInfo;
  std::uint32_t address = 0;
  if (!net::parse_ipv4(ip, address) || port < 1 || port > 65535) return JsonResult::kInvalidInfo;
  int dots = 0;
  bool digit_before = false, ok = !firmware.empty();
  for (const char c : firmware) {
    if (c >= '0' && c <= '9') digit_before = true;
    else if (c == '.' && digit_before) { ++dots; digit_before = false; }
    else ok = false;
  }
  if (!ok || dots != 2 || !digit_before) return JsonResult::kInvalidInfo;
  json::Writer w;
  w.begin_object().field("node_id", node_id).key("timestamp_ms").integer(static_cast<long long>(timestamp_ms)).field("name", name).field("firmware", firmware).field("ip", ip)
      .key("port").integer(port).end_object();
  if (w.str().size() + 1 > capacity) return JsonResult::kBufferTooSmall;
  std::memcpy(out, w.str().data(), w.str().size());
  length = w.str().size();
  return JsonResult::kOk;
}

// Topic for a message kind: armor/node/{node_id}/{kind}. Returns false if the id is invalid or the buffer is too small.
inline bool build_topic(std::string_view node_id, std::string_view kind, char* out, std::size_t capacity) {
  if (!node_id_is_valid(node_id)) return false;
  const int written = std::snprintf(out, capacity, "armor/node/%.*s/%.*s", static_cast<int>(node_id.size()), node_id.data(),
                                    static_cast<int>(kind.size()), kind.data());
  return written > 0 && static_cast<std::size_t>(written) < capacity;
}

}  // namespace armor
