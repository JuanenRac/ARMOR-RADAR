// ARMOR-RADAR - prints the payloads the firmware would publish, one per line, so the
// message contract (ARMOR-COMMON) can validate the firmware's real serialiser.
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D). GPL-3.0-or-later.
#include <cstdio>
#include <vector>
#include "../main/core/telemetry_json.hpp"

using namespace armor;

int main() {
  char out[2048];
  std::size_t length = 0;
  auto emit = [&](const char* kind, JsonResult result) {
    if (result != JsonResult::kOk) {
      std::printf("ERROR %s\n", kind);
      return;
    }
    std::printf("%s %.*s\n", kind, static_cast<int>(length), out);
  };
  emit("health", build_health("north-1", 0, true, out, sizeof out, length));
  emit("health", build_health("north-1", 61000, false, out, sizeof out, length));
  emit("telemetry", build_telemetry("north-1", 1000, 0.0f, nullptr, 0, out, sizeof out, length));
  const Track one[] = {Track{1, 1, 1200, -300, 0}};
  emit("telemetry", build_telemetry("north-1", 2000, 250.5f, one, 1, out, sizeof out, length));
  std::vector<Track> fifteen;
  for (std::uint8_t sensor = 1; sensor <= 3; ++sensor)
    for (std::uint8_t id = 1; id <= 5; ++id) fifteen.push_back(Track{sensor, id, static_cast<std::int16_t>(-1500 + id * 700), static_cast<std::int16_t>(sensor * 900), static_cast<std::int16_t>(id * -100)});
  emit("telemetry", build_telemetry("node_2-b", 3000, 200000.0f, fifteen.data(), fifteen.size(), out, sizeof out, length));
  const Track extreme[] = {Track{3, 255, 32767, -32768, -32768}};
  emit("telemetry", build_telemetry("a", 4294967296ULL, 12345.6f, extreme, 1, out, sizeof out, length));
  return 0;
}
