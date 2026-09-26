// ARMOR-RADAR - prints the payloads the firmware would publish, one per line, so the
// message contract (ARMOR-COMMON) can validate the firmware's real serialiser.
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D). GPL-3.0-or-later.
#include <cstdio>
#include <vector>
#include "../main/core/gpio_logic.hpp"
#include "../main/core/ld2450.hpp"
#include "../main/core/presence.hpp"
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
  emit("info", build_info("north-1", 7000, "North gate", "0.2.3", "192.168.0.181", 80, out, sizeof out, length));
  emit("info", build_info("node_2-b", 8000, "Per\xC3\xADmetro \"norte\" \\ 1", "0.10.20", "255.255.255.255", 65535, out, sizeof out, length));
  const Track one[] = {Track{1, 1, 1200, -300, 0}};
  emit("telemetry", build_telemetry("north-1", 2000, 250.5f, one, 1, out, sizeof out, length));
  std::vector<Track> fifteen;
  for (std::uint8_t sensor = 1; sensor <= 3; ++sensor)
    for (std::uint8_t id = 1; id <= 5; ++id) fifteen.push_back(Track{sensor, id, static_cast<std::int16_t>(-1500 + id * 700), static_cast<std::int16_t>(sensor * 900), static_cast<std::int16_t>(id * -100)});
  emit("telemetry", build_telemetry("node_2-b", 3000, 200000.0f, fifteen.data(), fifteen.size(), out, sizeof out, length));
  const Track extreme[] = {Track{3, 255, 32767, -32768, -32768}};
  emit("telemetry", build_telemetry("a", 4294967296ULL, 12345.6f, extreme, 1, out, sizeof out, length));
  // The manual's worked example, decoded and serialised: proof that real frames yield contract-valid messages.
  const std::uint8_t manual[30] = {0xAA, 0xFF, 0x03, 0x00, 0x0E, 0x03, 0xB1, 0x86, 0x10, 0x00, 0x40, 0x01, 0x00, 0x00, 0x00,
                                   0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x55, 0xCC};
  ld2450::Frame frame;
  Track decoded[ld2450::kTargetsPerFrame];
  if (ld2450::decode_frame(manual, sizeof manual, frame)) {
    const std::size_t count = ld2450::to_tracks(1, frame, decoded);
    emit("telemetry", build_telemetry("north-1", 5000, 12.0f, decoded, count, out, sizeof out, length));
  } else {
    std::printf("ERROR ld2450\n");
  }
  // What a node publishes for the devices of the server: presence sensors and mapped pins. These are not in ARMOR-COMMON's contract (a device state is
  // the server's own vocabulary); the server's tests read them from ARMOR-SERVER/tests/fixtures/firmware_device_states.txt.
  const auto device = [](const std::string& payload) { std::printf("device_state %s\n", payload.c_str()); };
  device(presence::device_payload(true, 240));    // an LD2410 with somebody at 2.4 m
  device(presence::device_payload(true, -1));     // an MR24HPC1: occupied, no distance
  device(presence::device_payload(false, -1));    // nobody
  device(gpio::report_boolean("triggered", true));
  device(gpio::report_boolean("open", false));
  device(gpio::report_boolean("on", true));
  device(gpio::report_number("brightness", 40));
  device(gpio::report_number("battery", 12.6));
  return 0;
}
