// ARMOR-RADAR - host tests for the hardware-independent core.
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D). GPL-3.0-or-later.
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include "../main/core/climate.hpp"
#include "../main/core/frame_framer.hpp"
#include "../main/core/ld2450.hpp"
#include "../main/core/node_id.hpp"
#include "../main/core/radar_calibration.hpp"
#include "../main/core/radar_health.hpp"
#include "../main/core/semver.hpp"
#include "../main/core/static_map.hpp"
#include "../main/core/telemetry_json.hpp"
#include "../main/core/veml7700.hpp"

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


// ---- HLK-LD2450 (Hi-Link manual V1.00, section 6) --------------------------------------------------------------
// The worked example of the manual, byte for byte.
static const std::uint8_t kManualExample[30] = {0xAA, 0xFF, 0x03, 0x00, 0x0E, 0x03, 0xB1, 0x86, 0x10, 0x00, 0x40, 0x01, 0x00, 0x00, 0x00,
                                                0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x55, 0xCC};

// The inverse of the manual's encoding, used to build frames in the tests: bit 15 set = positive.
static void put(std::vector<std::uint8_t>& frame, int value) {
  const unsigned raw = static_cast<unsigned>(value < 0 ? -value : value) | (value >= 0 ? 0x8000u : 0u);
  frame.push_back(static_cast<std::uint8_t>(raw & 0xFF));
  frame.push_back(static_cast<std::uint8_t>(raw >> 8));
}

static std::vector<std::uint8_t> make_frame(const int (*targets)[4], std::size_t count) {
  std::vector<std::uint8_t> frame = {0xAA, 0xFF, 0x03, 0x00};
  for (std::size_t slot = 0; slot < 3; ++slot) {
    if (slot < count) {
      put(frame, targets[slot][0]);
      put(frame, targets[slot][1]);
      put(frame, targets[slot][2]);
      frame.push_back(static_cast<std::uint8_t>(targets[slot][3] & 0xFF));
      frame.push_back(static_cast<std::uint8_t>(targets[slot][3] >> 8));
    } else {
      for (int i = 0; i < 8; ++i) frame.push_back(0);
    }
  }
  frame.push_back(0x55);
  frame.push_back(0xCC);
  return frame;
}

static void test_ld2450() {
  namespace radar = armor::ld2450;
  CHECK(radar::kFrameLength == 30);
  CHECK(radar::kBaudRate == 256000);
  CHECK(radar::protocol().configured());

  // The manual's own example: one target, x = -782 mm, y = 1713 mm, speed = -16 cm/s, gate 320 mm.
  radar::Frame frame;
  CHECK(radar::decode_frame(kManualExample, sizeof kManualExample, frame));
  CHECK(frame.count() == 1);
  CHECK(frame.targets[0].present && !frame.targets[1].present && !frame.targets[2].present);
  CHECK(frame.targets[0].x_mm == -782);
  CHECK(frame.targets[0].y_mm == 1713);
  CHECK(frame.targets[0].speed_cm_s == -16);
  CHECK(frame.targets[0].resolution_mm == 320);

  // In the units of the message contract: slot 1 is track 1 and the speed is in mm/s.
  Track tracks[3];
  CHECK(radar::to_tracks(2, frame, tracks) == 1);
  CHECK(tracks[0].sensor_id == 2 && tracks[0].track_id == 1 && tracks[0].x_mm == -782 && tracks[0].y_mm == 1713 && tracks[0].speed_mm_s == -160);

  // The sign rule on its own: bit 15 set is positive, clear is negative, and the extremes hold.
  CHECK(radar::decode_signed(0x8000) == 0 && radar::decode_signed(0x0000) == 0);
  CHECK(radar::decode_signed(0x8001) == 1 && radar::decode_signed(0x0001) == -1);
  CHECK(radar::decode_signed(0xFFFF) == 32767 && radar::decode_signed(0x7FFF) == -32767);

  // Three targets in both directions, built with the inverse encoding and read back.
  const int three[3][4] = {{1500, 4200, 30, 360}, {-2600, 800, -120, 360}, {40, 5900, 0, 480}};
  const std::vector<std::uint8_t> full = make_frame(three, 3);
  CHECK(full.size() == 30);
  CHECK(radar::decode_frame(full.data(), full.size(), frame) && frame.count() == 3);
  CHECK(frame.targets[0].x_mm == 1500 && frame.targets[0].y_mm == 4200 && frame.targets[0].speed_cm_s == 30);
  CHECK(frame.targets[1].x_mm == -2600 && frame.targets[1].y_mm == 800 && frame.targets[1].speed_cm_s == -120);
  CHECK(frame.targets[2].y_mm == 5900 && frame.targets[2].resolution_mm == 480);
  CHECK(radar::to_tracks(3, frame, tracks) == 3 && tracks[1].track_id == 2 && tracks[1].speed_mm_s == -1200);

  // A slot in the middle can be empty: the other slots keep their numbers.
  std::vector<std::uint8_t> gap = full;
  for (int i = 0; i < 8; ++i) gap[4 + 8 + i] = 0;
  CHECK(radar::decode_frame(gap.data(), gap.size(), frame) && frame.count() == 2 && !frame.targets[1].present);
  CHECK(radar::to_tracks(1, frame, tracks) == 2 && tracks[0].track_id == 1 && tracks[1].track_id == 3);

  // An empty frame is valid and has no targets; anything malformed is refused and leaves nothing behind.
  std::vector<std::uint8_t> empty = make_frame(nullptr, 0);
  CHECK(radar::decode_frame(empty.data(), empty.size(), frame) && frame.count() == 0);
  std::vector<std::uint8_t> bad = full;
  bad[0] = 0xAB;
  CHECK(!radar::decode_frame(bad.data(), bad.size(), frame) && frame.count() == 0);
  bad = full;
  bad[29] = 0x00;
  CHECK(!radar::decode_frame(bad.data(), bad.size(), frame));
  CHECK(!radar::decode_frame(full.data(), 29, frame) && !radar::decode_frame(full.data(), 31, frame) && !radar::decode_frame(nullptr, 30, frame));

  // The speed of the contract is clamped, never wrapped: 32767 cm/s cannot occur, but must not become a small number.
  const int fast[1][4] = {{100, 100, 32767, 320}};
  const std::vector<std::uint8_t> rocket = make_frame(fast, 1);
  CHECK(radar::decode_frame(rocket.data(), rocket.size(), frame));
  CHECK(radar::to_tracks(1, frame, tracks) == 1 && tracks[0].speed_mm_s == 32767);
  const int back[1][4] = {{100, 100, -32767, 320}};
  const std::vector<std::uint8_t> reverse = make_frame(back, 1);
  CHECK(radar::decode_frame(reverse.data(), reverse.size(), frame) && radar::to_tracks(1, frame, tracks) == 1 && tracks[0].speed_mm_s == -32768);
}

static void test_ld2450_stream() {
  namespace radar = armor::ld2450;
  // A UART starts mid-frame and picks up noise: the framer must still find every real frame.
  const int one[1][4] = {{-500, 2000, 10, 320}};
  const std::vector<std::uint8_t> a = make_frame(one, 1);
  std::vector<std::uint8_t> stream = {0x12, 0x55, 0xCC, 0xAA, 0xFF, 0x00};  // junk, including a false header start
  stream.insert(stream.end(), kManualExample, kManualExample + 30);
  stream.insert(stream.end(), {0x00, 0xAA});
  stream.insert(stream.end(), a.begin(), a.end());
  stream.insert(stream.end(), kManualExample, kManualExample + 30);
  for (std::size_t chunk : {1u, 3u, 7u, 30u, 64u}) {
    FrameFramer framer(radar::protocol());
    std::size_t decoded = 0, targets = 0;
    for (std::size_t i = 0; i < stream.size(); i += chunk) {
      const std::size_t n = std::min<std::size_t>(chunk, stream.size() - i);
      framer.feed(stream.data() + i, n, [&](const std::uint8_t* frame_bytes, std::size_t length) {
        radar::Frame frame;
        if (radar::decode_frame(frame_bytes, length, frame)) { ++decoded; targets += frame.count(); }
      });
    }
    CHECK(decoded == 3 && targets == 3);
    CHECK(framer.stats().frames == 3);
  }
}

static void test_track_set() {
  TrackSet set(500);
  Track out[kMaximumTracks];
  CHECK(set.collect(out, 1000) == 0 && !set.any_fresh(1000));
  const Track a[] = {Track{1, 1, 100, 1000, 0}, Track{1, 2, 200, 2000, 10}};
  const Track b[] = {Track{2, 1, -300, 3000, -20}};
  set.update(1, a, 2, 1000);
  set.update(2, b, 1, 1100);
  CHECK(set.collect(out, 1200) == 3 && set.any_fresh(1200));
  CHECK(out[0].sensor_id == 1 && out[2].sensor_id == 2);
  // A radar that stops reporting drops out after the age limit, the other one stays.
  CHECK(set.collect(out, 1550) == 1 && out[0].sensor_id == 2);
  CHECK(set.collect(out, 1700) == 0 && !set.any_fresh(1700));
  // An empty frame from a live radar clears its targets and still proves the radar is alive.
  set.update(1, nullptr, 0, 2000);
  CHECK(set.collect(out, 2100) == 0 && set.any_fresh(2100));
  // Unknown sensors are ignored; too many tracks are capped at the contract limit per radar.
  set.update(0, a, 2, 2000);
  set.update(4, a, 2, 2000);
  Track many[8];
  for (std::size_t i = 0; i < 8; ++i) many[i] = Track{3, static_cast<std::uint8_t>(i + 1), 0, 0, 0};
  set.update(3, many, 8, 2000);
  CHECK(set.collect(out, 2100) == kTracksPerRadar);
}

static bool near(float a, float b, float tolerance) { return std::fabs(a - b) <= tolerance; }

static void test_veml7700() {
  using namespace armor::veml7700;
  // The datasheet's figures: 0.0036 lx per count at gain x2 and 800 ms, and 0.0576 at gain x1 and 100 ms.
  CHECK(near(resolution_lux_per_count({Gain::kX2, Integration::kMs800}), 0.0036f, 1e-6f));
  CHECK(near(resolution_lux_per_count({Gain::kX1, Integration::kMs100}), 0.0576f, 1e-5f));
  CHECK(near(resolution_lux_per_count({Gain::kX1_8, Integration::kMs25}), 0.0036f * 32 * 16, 1e-4f));  // the coarsest setting
  // Registers: the configuration word puts the gain in bits 12:11 and the integration time in bits 9:6.
  CHECK(config_word({Gain::kX1, Integration::kMs100}) == 0x0000);
  CHECK(config_word({Gain::kX2, Integration::kMs800}) == ((1u << 11) | (0b0011u << 6)));
  CHECK(config_word({Gain::kX1_8, Integration::kMs25}) == ((2u << 11) | (0b1100u << 6)));
  CHECK((config_word({Gain::kX1_4, Integration::kMs50}) & 1u) == 0);  // never asks for shutdown
  // Conversion: low light is nearly linear, high light gets the correction.
  CHECK(near(lux_from_counts(1000, {Gain::kX1, Integration::kMs100}), 58.0f, 0.1f));  // 57.6 lx raw, +0.7 % from the polynomial
  CHECK(lux_from_counts(0, {Gain::kX1, Integration::kMs100}) == 0.0f);
  const float high_raw = 30000 * resolution_lux_per_count({Gain::kX1_8, Integration::kMs25});
  CHECK(lux_from_counts(30000, {Gain::kX1_8, Integration::kMs25}) > high_raw);  // the polynomial lifts the compressed top end
  // Range selection walks the ladder in the right direction and stops at both ends.
  CHECK(next_step(kStartStep, 30000) == kStartStep);
  CHECK(next_step(kStartStep, 65000) == kStartStep - 1);
  CHECK(next_step(kStartStep, 10) == kStartStep + 1);
  CHECK(next_step(0, 65535) == 0);
  CHECK(next_step(kLadderSteps - 1, 0) == kLadderSteps - 1);
  CHECK(next_step(999, 0) == kStartStep);
  CHECK(reading_is_trustworthy(kStartStep, 5000) && !reading_is_trustworthy(kStartStep, 5));
  // The ladder really goes from the coarsest to the finest resolution, and each step needs longer to settle than a reading takes.
  for (std::size_t i = 1; i < kLadderSteps; ++i) CHECK(resolution_lux_per_count(kLadder[i]) <= resolution_lux_per_count(kLadder[i - 1]));
  CHECK(settle_ms({Gain::kX2, Integration::kMs800}) >= 1600);
  // A full dark room resolves to well under a lux at the top of the ladder; direct sun still fits at the bottom.
  CHECK(lux_from_counts(1, kLadder[kLadderSteps - 1]) < 0.01f);
  CHECK(lux_from_counts(65535, kLadder[0]) > 100000.0f);
}

static void test_radar_health() {
  RadarHealth health(3000);
  CHECK(health.state(1, 1000) == RadarState::kNoData);
  health.bytes_received(1, 40);
  CHECK(health.state(1, 1000) == RadarState::kGarbled);
  health.frame_bad(1);
  CHECK(health.counters(1).bad_frames == 1 && health.counters(1).bytes == 40);
  health.frame_ok(1, 2000);
  CHECK(health.state(1, 2500) == RadarState::kReporting);
  CHECK(health.state(1, 5001) == RadarState::kSilent);
  CHECK(health.state(1, 5000) == RadarState::kReporting);
  // The radars are independent, and a radar number outside 1..3 is ignored.
  CHECK(health.state(2, 2500) == RadarState::kNoData && health.state(3, 2500) == RadarState::kNoData);
  health.frame_ok(9, 100);
  health.bytes_received(0, 5);
  CHECK(health.counters(2).frames == 0);
  CHECK(health.state(9, 0) == RadarState::kNoData);
  for (RadarState state : {RadarState::kNoData, RadarState::kGarbled, RadarState::kSilent, RadarState::kReporting}) CHECK(RadarHealth::describe(state)[0] != '?');
}

// Three radars on one node, each looking 75 degrees from the next, cover 270 degrees: the geometry Studio's "270 degree node" uses.
static void test_270_degree_layout() {
  const double half_angle = 60.0, spacing = 75.0;
  const double covered = 2 * half_angle + 2 * spacing;  // the outer edges of the two outer radars
  CHECK(covered == 270.0);
  CHECK(spacing < 2 * half_angle);  // neighbouring radars overlap, so no gap opens between them
  CHECK(2 * half_angle - spacing == 45.0);  // and the overlap is 45 degrees
}

static void test_radar_calibration() {
  using namespace calibration;
  config::RadarLine zero;   // all defaults: 0 offset, 0 yaw, 0 pitch
  const Track raw{1, 1, 1000, 500, 200};
  const Track identity = apply(raw, zero);
  CHECK(identity.x_mm == raw.x_mm && identity.y_mm == raw.y_mm && identity.speed_mm_s == raw.speed_mm_s);

  // a 90 degree yaw turns (1000, 0) into (0, 1000) under this function's own rotation convention
  config::RadarLine yaw90; yaw90.yaw_deg = 90;
  const Track turned = apply(Track{1, 1, 1000, 0, 0}, yaw90);
  CHECK(turned.x_mm == 0 && turned.y_mm == 1000);

  // an offset alone is a plain translation
  config::RadarLine offset; offset.offset_x_mm = 500; offset.offset_y_mm = -200;
  const Track moved = apply(Track{1, 1, 100, 200, 0}, offset);
  CHECK(moved.x_mm == 600 && moved.y_mm == 0);

  // a 60 degree downward tilt foreshortens the forward distance by cos(60) = 0.5, before any rotation
  config::RadarLine tilted; tilted.pitch_deg = 60;
  const Track leveled = apply(Track{1, 1, 300, 1000, 0}, tilted);
  CHECK(leveled.x_mm == 300 && leveled.y_mm == 500);

  // two different radars within the threshold are the same person, averaged; the same radar never merges with itself
  Track pair[2] = {{1, 1, 1000, 1000, 0}, {2, 1, 1100, 1000, 100}};
  Track merged[2] = {pair[0], pair[1]};
  CHECK(merge_overlap(merged, 2, 300) == 1);
  CHECK(merged[0].x_mm == 1050 && merged[0].y_mm == 1000 && merged[0].speed_mm_s == 50);

  Track far[2] = {{1, 1, 0, 0, 0}, {2, 1, 5000, 5000, 0}};
  CHECK(merge_overlap(far, 2, 300) == 2);   // too far apart: kept separate

  Track same_sensor[2] = {{1, 1, 1000, 1000, 0}, {1, 2, 1010, 1000, 0}};
  CHECK(merge_overlap(same_sensor, 2, 300) == 2);   // one radar's own two targets are never "the same person"

  Track off[2] = {{1, 1, 1000, 1000, 0}, {2, 1, 1010, 1000, 0}};
  CHECK(merge_overlap(off, 2, 0) == 2);   // threshold 0: fusion is off, nothing merges
}

static void test_semver() {
  using namespace semver;
  CHECK(is_newer("v0.4.6", "0.4.5"));
  CHECK(is_newer("0.5.0", "0.4.9"));
  CHECK(is_newer("1.0.0", "0.4.5"));
  CHECK(!is_newer("0.4.5", "0.4.5"));
  CHECK(!is_newer("0.4.4", "0.4.5"));
  CHECK(!is_newer("0.4.6-rc1", "0.4.5"));   // a pre-release is never offered
  CHECK(!is_newer("not-a-version", "0.4.5"));
  CHECK(!is_newer("0.4.6", "also-not-a-version"));
  CHECK(!parse("").ok && !parse("1.2").ok && !parse("1.2.3.4").ok && !parse("1.2.x").ok);
  CHECK(parse("v2.10.3").ok && parse("v2.10.3").major == 2 && parse("v2.10.3").minor == 10 && parse("v2.10.3").patch == 3);
}

// Real frames captured from a node (tools/frames_to_fixture.py), when the file exists: the decoder's first check against a real module.
static void test_real_frames_fixture() {
  std::string path = __FILE__;
  path = path.substr(0, path.find_last_of("/\\") + 1) + "fixtures/ld2450_real.hex";
  std::FILE* file = std::fopen(path.c_str(), "r");
  if (file == nullptr) { std::printf("note: no tests/fixtures/ld2450_real.hex yet (capture real frames on the bench)\n"); return; }
  char line[256];
  int frames = 0;
  while (std::fgets(line, sizeof line, file) != nullptr) {
    if (line[0] == '#' || line[0] == '\n') continue;
    char radar = 0;
    char hex[128] = {0};
    if (std::sscanf(line, "r%c %127s", &radar, hex) != 2) { CHECK(false && "malformed fixture line"); continue; }
    std::uint8_t bytes[ld2450::kFrameLength] = {0};
    const std::size_t digits = std::strlen(hex);
    CHECK(digits == ld2450::kFrameLength * 2);
    if (digits != ld2450::kFrameLength * 2) continue;
    for (std::size_t i = 0; i < ld2450::kFrameLength; ++i) { unsigned value = 0; std::sscanf(hex + 2 * i, "%2x", &value); bytes[i] = static_cast<std::uint8_t>(value); }
    ld2450::Frame frame;
    CHECK(ld2450::decode_frame(bytes, ld2450::kFrameLength, frame));
    for (const ld2450::Target& target : frame.targets) if (target.present) CHECK(std::abs(target.x_mm) <= 32767 && target.y_mm >= -32767 && target.resolution_mm > 0);
    ++frames;
  }
  std::fclose(file);
  std::printf("real fixture: %d frames decoded\n", frames);
}

int main() {
  test_node_id();
  test_framer();
  test_telemetry_json();
  test_health_and_topics();
  test_dew_point_and_heater();
  test_day_night();
  test_static_map();
  test_ld2450();
  test_ld2450_stream();
  test_track_set();
  test_veml7700();
  test_radar_health();
  test_270_degree_layout();
  test_radar_calibration();
  test_semver();
  test_real_frames_fixture();
  std::printf("%d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
