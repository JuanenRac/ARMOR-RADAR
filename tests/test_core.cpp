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
  std::printf("%d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
