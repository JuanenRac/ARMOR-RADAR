// ARMOR-RADAR - host tests for the hardware-independent core.
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D). GPL-3.0-or-later.
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include "../main/core/climate.hpp"
#include "../main/core/frame_framer.hpp"
#include "../main/core/node_id.hpp"
#include "../main/core/static_map.hpp"
#include "../main/core/telemetry_json.hpp"

static int failures = 0;
static int checks = 0;
#define CHECK(condition)                                                              \
  do {                                                                                \
    ++checks;                                                                         \
    if (!(condition)) {                                                               \
      ++failures;                                                                     \
      std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #condition);                \
    }                                                                                 \
  } while (0)

using namespace armor;

// A made-up protocol, used only to test the framer. It says nothing about any real radar.
static ProtocolSpec synthetic_spec() {
  ProtocolSpec spec;
  spec.header = {0xA1, 0xB2};
  spec.header_length = 2;
  spec.tail = {0xC3};
  spec.tail_length = 1;
  spec.frame_length = 6;  // 2 header + 3 payload + 1 tail
  return spec;
}

static std::vector<std::vector<std::uint8_t>> run_framer(FrameFramer& framer, const std::vector<std::uint8_t>& bytes, std::size_t chunk) {
  std::vector<std::vector<std::uint8_t>> frames;
  for (std::size_t i = 0; i < bytes.size(); i += chunk) {
    const std::size_t n = std::min(chunk, bytes.size() - i);
    framer.feed(bytes.data() + i, n, [&](const std::uint8_t* frame, std::size_t length) { frames.emplace_back(frame, frame + length); });
  }
  return frames;
}

static void test_node_id() {
  CHECK(node_id_is_valid("north-1"));
  CHECK(node_id_is_valid("a"));
  CHECK(node_id_is_valid("node_2-b"));
  CHECK(!node_id_is_valid(""));
  CHECK(!node_id_is_valid("North"));
  CHECK(!node_id_is_valid("-north"));
  CHECK(!node_id_is_valid("a b"));
  CHECK(!node_id_is_valid("a/b"));
  CHECK(node_id_is_valid(std::string(64, 'a')));
  CHECK(!node_id_is_valid(std::string(65, 'a')));
}

static void test_framer() {
  const ProtocolSpec spec = synthetic_spec();
  CHECK(spec.configured());
  CHECK(!ProtocolSpec::unconfigured().configured());

  const std::vector<std::uint8_t> frame = {0xA1, 0xB2, 1, 2, 3, 0xC3};
  {
    FrameFramer framer(spec);
    auto frames = run_framer(framer, frame, 6);
    CHECK(frames.size() == 1 && frames[0] == frame);
  }
  {  // any chunk size, including one byte at a time
    for (std::size_t chunk : {1u, 2u, 3u, 5u, 7u}) {
      FrameFramer framer(spec);
      std::vector<std::uint8_t> stream = frame;
      stream.insert(stream.end(), frame.begin(), frame.end());
      CHECK(run_framer(framer, stream, chunk).size() == 2);
    }
  }
  {  // it starts in the middle of a frame and picks up noise
    FrameFramer framer(spec);
    std::vector<std::uint8_t> stream = {9, 9, 0xC3, 4, 0xA1};                     // tail of a cut frame, noise, half a header
    stream.insert(stream.end(), frame.begin(), frame.end());
    auto frames = run_framer(framer, stream, 3);
    CHECK(frames.size() == 1);
    CHECK(framer.stats().dropped_bytes > 0);
  }
  {  // a false header with the wrong tail costs one frame, not the stream
    FrameFramer framer(spec);
    std::vector<std::uint8_t> stream = {0xA1, 0xB2, 1, 2, 3, 0x00};  // wrong tail
    stream.insert(stream.end(), frame.begin(), frame.end());
    auto frames = run_framer(framer, stream, 4);
    CHECK(frames.size() == 1 && frames[0] == frame);
    CHECK(framer.stats().bad_frames >= 1);
  }
  {  // a header that appears inside a payload does not derail the next frame
    FrameFramer framer(spec);
    std::vector<std::uint8_t> stream = {0xA1, 0xB2, 0xA1, 0xB2, 3, 0xC3};
    stream.insert(stream.end(), frame.begin(), frame.end());
    CHECK(run_framer(framer, stream, 1).size() >= 1);
  }
  {  // the real radar protocol is not configured, so nothing is ever emitted
    FrameFramer framer(ProtocolSpec::unconfigured());
    CHECK(!framer.configured());
    CHECK(run_framer(framer, frame, 1).empty());
  }
}

static Track track(std::uint8_t sensor, std::uint8_t id, std::int16_t x, std::int16_t y, std::int16_t speed) { return Track{sensor, id, x, y, speed}; }

static void test_telemetry_json() {
  char out[512];
  std::size_t length = 0;
  const Track tracks[] = {track(1, 1, 1200, -300, 0), track(3, 2, -5000, 4000, -785)};
  CHECK(build_telemetry("north-1", 1000, 250.5f, tracks, 2, out, sizeof out, length) == JsonResult::kOk);
  CHECK(std::string(out, length) ==
        "{\"node_id\":\"north-1\",\"timestamp_ms\":1000,\"lux\":250.5,\"targets\":[{\"sensor_id\":1,\"track_id\":1,\"x_mm\":1200,\"y_mm\":-300,\"speed_mm_s\":0},"
        "{\"sensor_id\":3,\"track_id\":2,\"x_mm\":-5000,\"y_mm\":4000,\"speed_mm_s\":-785}]}");
  CHECK(build_telemetry("north-1", 0, 0.0f, nullptr, 0, out, sizeof out, length) == JsonResult::kOk);
  CHECK(std::string(out, length) == "{\"node_id\":\"north-1\",\"timestamp_ms\":0,\"lux\":0.0,\"targets\":[]}");

  CHECK(build_telemetry("Bad Id", 0, 1.0f, nullptr, 0, out, sizeof out, length) == JsonResult::kInvalidNodeId && length == 0);
  CHECK(build_telemetry("n", 0, -0.1f, nullptr, 0, out, sizeof out, length) == JsonResult::kInvalidLux);
  CHECK(build_telemetry("n", 0, 200001.0f, nullptr, 0, out, sizeof out, length) == JsonResult::kInvalidLux);
  CHECK(build_telemetry("n", 0, std::nanf(""), nullptr, 0, out, sizeof out, length) == JsonResult::kInvalidLux);
  CHECK(build_telemetry("n", 0, 1.0f, tracks, 2, out, 20, length) == JsonResult::kBufferTooSmall);

  const Track bad_sensor[] = {track(4, 1, 0, 0, 0)};
  const Track bad_id[] = {track(1, 0, 0, 0, 0)};
  CHECK(build_telemetry("n", 0, 1.0f, bad_sensor, 1, out, sizeof out, length) == JsonResult::kInvalidTrack);
  CHECK(build_telemetry("n", 0, 1.0f, bad_id, 1, out, sizeof out, length) == JsonResult::kInvalidTrack);

  std::vector<Track> many;  // six tracks on one sensor is more than one radar reports
  for (int i = 1; i <= 6; ++i) many.push_back(track(1, static_cast<std::uint8_t>(i), 0, 1000, 0));
  CHECK(build_telemetry("n", 0, 1.0f, many.data(), many.size(), out, sizeof out, length) == JsonResult::kTooManyTracks);
  std::vector<Track> sixteen(16, track(1, 1, 0, 0, 0));
  CHECK(build_telemetry("n", 0, 1.0f, sixteen.data(), sixteen.size(), out, sizeof out, length) == JsonResult::kTooManyTracks);

  std::vector<Track> fifteen;  // 3 sensors x 5 tracks: exactly the limit
  for (std::uint8_t s = 1; s <= 3; ++s)
    for (std::uint8_t i = 1; i <= 5; ++i) fifteen.push_back(track(s, i, 100, 100, 0));
  char big[2048];
  CHECK(build_telemetry("n", 0, 1.0f, fifteen.data(), fifteen.size(), big, sizeof big, length) == JsonResult::kOk);
}

static void test_health_and_topics() {
  char out[128];
  std::size_t length = 0;
  CHECK(build_health("north-1", 5, true, out, sizeof out, length) == JsonResult::kOk);
  CHECK(std::string(out, length) == "{\"node_id\":\"north-1\",\"timestamp_ms\":5,\"online\":true}");
  CHECK(build_health("north-1", 5, false, out, sizeof out, length) == JsonResult::kOk);
  CHECK(std::string(out, length) == "{\"node_id\":\"north-1\",\"timestamp_ms\":5,\"online\":false}");
  CHECK(build_health("BAD", 5, true, out, sizeof out, length) == JsonResult::kInvalidNodeId);
  CHECK(build_health("north-1", 5, true, out, 10, length) == JsonResult::kBufferTooSmall);

  char topic[64];
  CHECK(build_topic("north-1", "health", topic, sizeof topic) && std::string(topic) == "armor/node/north-1/health");
  CHECK(!build_topic("BAD", "health", topic, sizeof topic));
  CHECK(!build_topic("north-1", "telemetry", topic, 10));
}

static void test_dew_point_and_heater() {
  CHECK(std::fabs(dew_point_c(20.0f, 50.0f) - 9.3f) < 0.3f);   // a standard reference point
  CHECK(std::fabs(dew_point_c(25.0f, 100.0f) - 25.0f) < 0.1f);  // saturated air: dew point equals temperature
  CHECK(dew_point_c(20.0f, 30.0f) < dew_point_c(20.0f, 60.0f));

  HeaterController heater(3.0f, 5.0f);
  CHECK(!heater.update(20.0f, 40.0f));                 // dry: margin is large
  CHECK(heater.update(10.0f, 95.0f));                  // nearly saturated: margin under 3 C
  CHECK(heater.update(10.0f, 88.0f));                  // still inside the hysteresis band: stays on
  CHECK(!heater.update(20.0f, 40.0f));                 // margin above 5 C: off
  CHECK(heater.update(10.0f, 95.0f));
  CHECK(!heater.update(std::nanf(""), 50.0f));         // a bad reading always switches it off
  CHECK(!heater.heating());
  CHECK(!heater.update(20.0f, 0.0f));
  CHECK(!heater.update(200.0f, 50.0f));
}

static void test_day_night() {
  DayNight dn(30.0f, 60.0f);
  CHECK(!dn.update(500.0f));
  CHECK(!dn.update(31.0f));
  CHECK(dn.update(29.0f));
  CHECK(dn.update(45.0f));    // inside the band: stays night
  CHECK(!dn.update(61.0f));
  CHECK(!dn.update(std::nanf("")));
  CHECK(dn.update(0.0f));
  CHECK(dn.update(-5.0f));    // a negative reading changes nothing
}

static void test_static_map() {
  StaticMap map(250, 30, 5);
  const Track post = track(1, 1, 1500, 3000, 0);
  for (int frame = 0; frame < 4; ++frame) map.observe(&post, 1);
  CHECK(map.learned() == 0 && !map.suppresses(post));   // not persistent enough yet
  map.observe(&post, 1);
  CHECK(map.learned() == 1);
  CHECK(map.suppresses(post));
  CHECK(map.suppresses(track(1, 9, 1600, 3050, 10)));    // near the learned point and stationary
  CHECK(!map.suppresses(track(1, 9, 1600, 3050, 900)));  // a moving target at the same place is never hidden
  CHECK(!map.suppresses(track(1, 9, 4000, 3000, 0)));    // a still person elsewhere is never hidden
  const Track mixed[] = {post, track(2, 2, -2000, 2500, 800), track(3, 3, 2500, 2500, 0)};
  Track kept[3];
  const std::size_t n = map.filter(mixed, 3, kept, 3);
  CHECK(n == 2 && kept[0].track_id == 2 && kept[1].track_id == 3);
  map.clear();
  CHECK(map.learned() == 0 && !map.suppresses(post));
  const Track moving = track(1, 1, 100, 100, 500);      // moving targets are never learned as static
  for (int i = 0; i < 30; ++i) map.observe(&moving, 1);
  CHECK(map.learned() == 0);
}

int main() {
  test_node_id();
  test_framer();
  test_telemetry_json();
  test_health_and_topics();
  test_dew_point_and_heater();
  test_day_night();
  test_static_map();
  std::printf("%d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
