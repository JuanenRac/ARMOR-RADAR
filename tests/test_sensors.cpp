// ARMOR-RADAR - host tests for the other sensors: the LD2461 tracker and the LD2410, LD2412, LD2410S and MR24HPC1 presence sensors.
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D). GPL-3.0-or-later.
// The frames marked "manual" are the worked examples of the manufacturers' documents; the others are built from the documented layout.
#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>
#include "../main/core/ld2461.hpp"
#include "../main/core/presence.hpp"
#include "../main/core/sensor_model.hpp"
#include "../main/core/track_stabiliser.hpp"
#include "../main/core/var_framer.hpp"

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
using Bytes = std::vector<std::uint8_t>;

static std::vector<Bytes> run(VarFramer& framer, const Bytes& bytes, std::size_t chunk) {
  std::vector<Bytes> frames;
  for (std::size_t i = 0; i < bytes.size(); i += chunk) {
    const std::size_t n = std::min(chunk, bytes.size() - i);
    framer.feed(bytes.data() + i, n, [&](const std::uint8_t* frame, std::size_t length) { frames.emplace_back(frame, frame + length); });
  }
  return frames;
}

static Bytes cat(std::initializer_list<Bytes> parts) { Bytes out; for (const Bytes& p : parts) out.insert(out.end(), p.begin(), p.end()); return out; }

// ---- the models --------------------------------------------------------------------------------------------------------------------

static void test_models() {
  using sensors::Model;
  for (const sensors::ModelInfo& entry : sensors::kModels) {
    Model parsed;
    CHECK(sensors::from_text(entry.id, parsed) && parsed == entry.model && std::string(sensors::to_text(entry.model)) == entry.id);
    CHECK(entry.default_baud >= 9600 && entry.default_baud <= 460800);
    CHECK(entry.tracker == (entry.max_targets > 0));
  }
  Model none;
  CHECK(!sensors::from_text("ld9999", none) && !sensors::from_text("", none));
  CHECK(sensors::info(Model::kLd2450).default_baud == 256000 && sensors::info(Model::kLd2461).default_baud == 9600 && sensors::info(Model::kLd2412).default_baud == 115200);
  CHECK(sensors::info(Model::kLd2461).max_targets == 5 && sensors::info(Model::kLd2461).half_angle_deg == 45 && sensors::info(Model::kLd2450).half_angle_deg == 60);
  CHECK(sensors::info(Model::kMr24hpc1).default_baud == 9600 && !sensors::info(Model::kMr24hpc1).tracker && !sensors::info(Model::kMr24hpc1).command_family);
}

// ---- the variable-length framer ----------------------------------------------------------------------------------------------------

static void test_var_framer() {
  const Bytes basic = {0xF4, 0xF3, 0xF2, 0xF1, 0x0D, 0x00, 0x02, 0xAA, 0x02, 0x51, 0x00, 0x00, 0x00, 0x00, 0x3B, 0x00, 0x00, 0x55, 0x00, 0xF8, 0xF7, 0xF6, 0xF5};  // manual (LD2410C)
  VarFramer framer(presence::report_protocol());
  for (std::size_t chunk : {1u, 2u, 3u, 7u, 100u}) {
    VarFramer f(presence::report_protocol());
    const auto frames = run(f, cat({basic, basic}), chunk);
    CHECK(frames.size() == 2 && frames[0] == basic && frames[1] == basic && f.stats().frames == 2 && f.stats().dropped_bytes == 0);
  }
  // noise before, between, and inside a lost frame
  const Bytes noisy = cat({{0x00, 0xF4, 0xF3, 0x11}, basic, {0xF4, 0xF3, 0xF2, 0xF1, 0x0D, 0x00, 0x02}, basic});
  const auto frames = run(framer, noisy, 5);
  CHECK(frames.size() >= 2 && frames.front() == basic && frames.back() == basic);
  // a wrong end marker costs the frame, not the stream
  Bytes bad = basic;
  bad[bad.size() - 1] = 0x00;
  VarFramer f2(presence::report_protocol());
  const auto after = run(f2, cat({bad, basic}), 4);
  CHECK(after.size() == 1 && after[0] == basic && f2.stats().bad_frames >= 1);
  // an absurd length is refused without waiting for it
  VarFramer f3(presence::report_protocol());
  const auto huge = run(f3, cat({{0xF4, 0xF3, 0xF2, 0xF1, 0xFF, 0xFF}, basic}), 3);
  CHECK(huge.size() == 1 && huge[0] == basic);
  CHECK(!VarFrameSpec{}.configured() && presence::report_protocol().configured() && presence::mr24_protocol().configured() && ld2461::protocol().configured());
}

// ---- LD2461 --------------------------------------------------------------------------------------------------------------------------

static void test_ld2461() {
  using namespace ld2461;
  // the manual: coordinates of two targets, (-1.5, 1.5) and (1.5, 1.5), checksum 0x25
  const Bytes report = {0xFF, 0xEE, 0xDD, 0x00, 0x05, 0x07, 0xF1, 0x0F, 0x0F, 0x0F, 0x25, 0xDD, 0xEE, 0xFF};
  VarFramer framer(protocol());
  const auto frames = run(framer, cat({{0x12, 0x34}, report}), 3);
  CHECK(frames.size() == 1 && frames[0] == report);
  Frame parsed;
  CHECK(parse(report.data(), report.size(), parsed) && parsed.command == kReportCoordinates && parsed.value_length == 4);
  Track tracks[kMaxTargets];
  CHECK(to_tracks(2, parsed, tracks) == 2);
  CHECK(tracks[0].sensor_id == 2 && tracks[0].track_id == 1 && tracks[0].x_mm == -1500 && tracks[0].y_mm == 1500 && tracks[0].speed_mm_s == 0);
  CHECK(tracks[1].track_id == 2 && tracks[1].x_mm == 1500 && tracks[1].y_mm == 1500);
  CHECK(track_is_valid(tracks[0]) && track_is_valid(tracks[1]));

  // the manual: zones report, zone 1 occupied, zone 2 free, zone 3 occupied, checksum 0x0A
  const Bytes zones = {0xFF, 0xEE, 0xDD, 0x00, 0x04, 0x08, 0x01, 0x00, 0x01, 0x0A, 0xDD, 0xEE, 0xFF};
  CHECK(parse(zones.data(), zones.size(), parsed) && parsed.command == kReportZones);
  bool occupied[3];
  CHECK(zone_occupancy(parsed, occupied) && occupied[0] && !occupied[1] && occupied[2]);
  CHECK(to_tracks(1, parsed, tracks) == 0);   // a zones report has no positions

  // a slot of zeros is no target, and five tracks are the most
  Frame five;
  five.command = kReportCoordinates;
  five.value_length = 12;
  const std::uint8_t values[12] = {10, 20, 0, 0, 0xF6, 0x1E, 1, 1, 2, 2, 3, 3};
  for (std::size_t i = 0; i < 12; ++i) five.value[i] = values[i];
  CHECK(to_tracks(1, five, tracks) == 4 && tracks[0].track_id == 1 && tracks[1].track_id == 3 && tracks[1].x_mm == -1000 && tracks[1].y_mm == 3000);

  // damaged frames are refused: checksum, length, footer
  Bytes wrong = report;
  wrong[10] = 0x26;
  CHECK(!parse(wrong.data(), wrong.size(), parsed));
  Bytes short_length = report;
  short_length[4] = 0x04;
  CHECK(!parse(short_length.data(), short_length.size(), parsed));
  Bytes footer = report;
  footer[13] = 0x00;
  CHECK(!parse(footer.data(), footer.size(), parsed));

  // commands, byte for byte as the manual shows them
  std::uint8_t out[64];
  std::size_t n = build_baud(115200, out, sizeof out);   // "set the baud rate to 115200"
  CHECK(Bytes(out, out + n) == (Bytes{0xFF, 0xEE, 0xDD, 0x00, 0x04, 0x01, 0x01, 0xC2, 0x00, 0xC4, 0xDD, 0xEE, 0xFF}));
  CHECK(build_baud(12345, out, sizeof out) == 0);
  n = build_query(kVersion, out, sizeof out);
  CHECK(Bytes(out, out + n) == (Bytes{0xFF, 0xEE, 0xDD, 0x00, 0x02, 0x09, 0x01, 0x0A, 0xDD, 0xEE, 0xFF}));
  n = build_query(kReadZones, out, sizeof out);
  CHECK(Bytes(out, out + n) == (Bytes{0xFF, 0xEE, 0xDD, 0x00, 0x02, 0x06, 0x01, 0x07, 0xDD, 0xEE, 0xFF}));
  n = build_query(kReadReportFormat, out, sizeof out);
  CHECK(Bytes(out, out + n) == (Bytes{0xFF, 0xEE, 0xDD, 0x00, 0x02, 0x03, 0x01, 0x04, 0xDD, 0xEE, 0xFF}));
  n = build_query(kFactoryReset, out, sizeof out);
  CHECK(Bytes(out, out + n) == (Bytes{0xFF, 0xEE, 0xDD, 0x00, 0x02, 0x0A, 0x01, 0x0B, 0xDD, 0xEE, 0xFF}));
  n = build_set_format(3, out, sizeof out);   // "setting the value of the displayed trace coordinates"
  CHECK(Bytes(out, out + n) == (Bytes{0xFF, 0xEE, 0xDD, 0x00, 0x02, 0x02, 0x03, 0x05, 0xDD, 0xEE, 0xFF}));
  CHECK(build_set_format(0, out, sizeof out) == 0 && build_set_format(4, out, sizeof out) == 0);
  n = build_cancel_zone(1, out, sizeof out);   // "cancellation of filter setting for area 1"
  CHECK(Bytes(out, out + n) == (Bytes{0xFF, 0xEE, 0xDD, 0x00, 0x02, 0x05, 0x01, 0x06, 0xDD, 0xEE, 0xFF}));
  CHECK(build_cancel_zone(0, out, sizeof out) == 0 && build_cancel_zone(4, out, sizeof out) == 0);
  // the manual: zone 1 with vertices (-0.5, 2), (-0.5, 1), (0.5, 1), (0.5, 2), detect only what is inside
  n = build_set_zone(1, 0, -500, 1000, 500, 2000, out, sizeof out);
  CHECK(Bytes(out, out + n) == (Bytes{0xFF, 0xEE, 0xDD, 0x00, 0x0B, 0x04, 0x01, 0xFB, 0x14, 0xFB, 0x0A, 0x05, 0x0A, 0x05, 0x14, 0x00, 0x41, 0xDD, 0xEE, 0xFF}));
  n = build_set_zone(1, 0, 500, 2000, -500, 1000, out, sizeof out);   // the corners in any order give the same frame
  CHECK(Bytes(out, out + n).size() == 20 && out[7] == 0xFB && out[8] == 0x14);
  CHECK(build_set_zone(4, 0, 0, 0, 1000, 1000, out, sizeof out) == 0 && build_set_zone(1, 2, 0, 0, 1000, 1000, out, sizeof out) == 0 && build_set_zone(1, 0, 0, 0, 20000, 1000, out, sizeof out) == 0);

  // the answers of the manual
  const Bytes version = {0xFF, 0xEE, 0xDD, 0x00, 0x09, 0x09, 0x3B, 0x01, 0x00, 0x01, 0x5C, 0x5A, 0xD5, 0x56, 0x27, 0xDD, 0xEE, 0xFF};
  CHECK(parse(version.data(), version.size(), parsed));
  const Version v = version_from(parsed);
  CHECK(v.ok && v.major == 0 && v.minor == 1 && v.identifier == 0x5C5AD556u);
  const Bytes read_zones = {0xFF, 0xEE, 0xDD, 0x00, 0x1F, 0x06, 0x01, 0x00, 0xEC, 0x14, 0xEC, 0x0A, 0xF6, 0x0A, 0xF6, 0x14, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                            0x03, 0x01, 0x0A, 0x14, 0x0A, 0x0A, 0x14, 0x0A, 0x14, 0x14, 0x85, 0xDD, 0xEE, 0xFF};
  CHECK(parse(read_zones.data(), read_zones.size(), parsed));
  Zone read[3];
  CHECK(zones_from(parsed, read));
  CHECK(read[0].number == 1 && read[0].type == 0 && read[0].set && read[0].x1_mm == -2000 && read[0].x2_mm == -1000 && read[0].y1_mm == 1000 && read[0].y2_mm == 2000);
  CHECK(read[1].number == 2 && !read[1].set);
  CHECK(read[2].number == 3 && read[2].type == 1 && read[2].x1_mm == 1000 && read[2].x2_mm == 2000 && read[2].y1_mm == 1000 && read[2].y2_mm == 2000);
  const Bytes ack = {0xFF, 0xEE, 0xDD, 0x00, 0x02, 0x01, 0x01, 0x02, 0xDD, 0xEE, 0xFF};   // baud rate: success
  CHECK(parse(ack.data(), ack.size(), parsed) && parsed.command == kBaudRate && parsed.value_length == 1 && parsed.value[0] == 1);
}

// ---- LD2410, LD2412, LD2410S -------------------------------------------------------------------------------------------------------

static void test_ld2410_family() {
  using namespace presence;
  Reading r;
  // the LD2410C manual, normal mode: stationary target, moving 81 cm energy 0, stationary 0 cm energy 59, detection 0 cm
  const Bytes c_basic = {0xF4, 0xF3, 0xF2, 0xF1, 0x0D, 0x00, 0x02, 0xAA, 0x02, 0x51, 0x00, 0x00, 0x00, 0x00, 0x3B, 0x00, 0x00, 0x55, 0x00, 0xF8, 0xF7, 0xF6, 0xF5};
  CHECK(decode_report(Family::kLd2410, c_basic.data(), c_basic.size(), r));
  CHECK(r.valid && r.state == 2 && r.present && !r.moving && r.stationary && !r.engineering && r.moving_cm == 81 && r.moving_energy == 0 && r.static_cm == 0 && r.static_energy == 59 && r.detection_cm == 0);
  // the LD2410C manual, engineering mode: moving and stationary, moving 30 cm energy 60, stationary 0 cm energy 57
  const Bytes c_engineering = {0xF4, 0xF3, 0xF2, 0xF1, 0x23, 0x00, 0x01, 0xAA, 0x03, 0x1E, 0x00, 0x3C, 0x00, 0x00, 0x39, 0x00, 0x00, 0x08, 0x08, 0x3C, 0x22, 0x05, 0x03, 0x03, 0x04, 0x03, 0x06, 0x05,
                               0x00, 0x00, 0x39, 0x10, 0x13, 0x06, 0x06, 0x08, 0x04, 0x03, 0x05, 0x55, 0x00, 0xF8, 0xF7, 0xF6, 0xF5};
  CHECK(decode_report(Family::kLd2410, c_engineering.data(), c_engineering.size(), r));
  CHECK(r.valid && r.engineering && r.state == 3 && r.present && r.moving && r.stationary && r.moving_cm == 30 && r.moving_energy == 60 && r.static_energy == 57 && r.distance_cm == 0);
  VarFramer framer(report_protocol());
  const auto framed = run(framer, cat({c_basic, c_engineering}), 6);
  CHECK(framed.size() == 2 && framed[1] == c_engineering);

  // the LD2412 manual, normal mode (no detection distance): stationary, moving 81 cm energy 0, stationary 0 cm energy 59
  const Bytes l_basic = {0xF4, 0xF3, 0xF2, 0xF1, 0x0B, 0x00, 0x02, 0xAA, 0x02, 0x51, 0x00, 0x00, 0x00, 0x00, 0x3B, 0x55, 0x00, 0xF8, 0xF7, 0xF6, 0xF5};
  CHECK(decode_report(Family::kLd2412, l_basic.data(), l_basic.size(), r));
  CHECK(r.valid && r.state == 2 && r.stationary && r.moving_cm == 81 && r.static_energy == 59 && r.detection_cm == -1);
  CHECK(!decode_report(Family::kLd2410, l_basic.data(), l_basic.size(), r));   // read as an LD2410 the frame is too short: the variant matters
  // the LD2412 manual, engineering mode: the same fields first. (The manual's example says the data are 0x2B = 43 bytes long but prints 45, so its length
  // is corrected here to 0x2D; the fields are the ones it prints.)
  const Bytes l_eng = {0xF4, 0xF3, 0xF2, 0xF1, 0x2D, 0x00, 0x01, 0xAA, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0D, 0x0D, 0x00, 0x03, 0x02, 0x01, 0x00, 0x00, 0x00, 0x00, 0x02, 0x02, 0x02,
                       0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x7E, 0x61, 0x0D, 0x0F, 0x05, 0x05, 0x04, 0x02, 0x01, 0x02, 0x01, 0x01, 0xE3, 0x00, 0x55, 0x00, 0xF8, 0xF7, 0xF6, 0xF5};
  CHECK(decode_report(Family::kLd2412, l_eng.data(), l_eng.size(), r) && r.valid && r.engineering && r.state == 2 && r.moving_cm == 0 && r.static_cm == 0);
  // the LD2412's calibration states: no one is reported while it runs
  Bytes calibration = l_basic;
  calibration[8] = 0x04;
  CHECK(decode_report(Family::kLd2412, calibration.data(), calibration.size(), r) && r.valid && r.calibrating && !r.present);
  calibration[8] = 0x07;
  CHECK(!decode_report(Family::kLd2412, calibration.data(), calibration.size(), r));
  Bytes ld2410_calibration = c_basic;
  ld2410_calibration[8] = 0x04;
  CHECK(!decode_report(Family::kLd2410, ld2410_calibration.data(), ld2410_calibration.size(), r));   // states 4 to 6 exist only on the LD2412

  // distances: moving and stationary both, the nearer one is the distance; moving only, its own
  Bytes both = c_basic;
  both[8] = 0x03; both[9] = 0x64; both[10] = 0x00; both[12] = 0x28; both[13] = 0x00; both[14] = 0x00;   // moving 100 cm, stationary 40 cm
  CHECK(decode_report(Family::kLd2410, both.data(), both.size(), r) && r.moving && r.stationary && r.distance_cm == 40);
  Bytes moving_only = both;
  moving_only[8] = 0x01;
  CHECK(decode_report(Family::kLd2410, moving_only.data(), moving_only.size(), r) && r.moving && !r.stationary && r.distance_cm == 100);
  Bytes nobody = both;
  nobody[8] = 0x00;
  CHECK(decode_report(Family::kLd2410, nobody.data(), nobody.size(), r) && !r.present && r.distance_cm == -1);
  // damaged frames
  Bytes no_head = c_basic;
  no_head[7] = 0x00;
  CHECK(!decode_report(Family::kLd2410, no_head.data(), no_head.size(), r));
  Bytes no_tail = c_basic;
  no_tail[17] = 0x00;
  CHECK(!decode_report(Family::kLd2410, no_tail.data(), no_tail.size(), r));
  Bytes wrong_length = c_basic;
  wrong_length[4] = 0x0C;
  CHECK(!decode_report(Family::kLd2410, wrong_length.data(), wrong_length.size(), r));
  Bytes wrong_type = c_basic;
  wrong_type[6] = 0x07;
  CHECK(!decode_report(Family::kLd2410, wrong_type.data(), wrong_type.size(), r));

  // the LD2410S (built from the manual's layout: state, distance in cm, 34 reserved bytes)
  Bytes s = {0xF4, 0xF3, 0xF2, 0xF1, 0x25, 0x00, 0x02, 0x2C, 0x01};
  s.insert(s.end(), 34, 0x00);
  s.insert(s.end(), {0xF8, 0xF7, 0xF6, 0xF5});
  CHECK(decode_report(Family::kLd2410s, s.data(), s.size(), r) && r.valid && r.present && r.state == 2 && r.distance_cm == 300);
  s[6] = 0x01;
  CHECK(decode_report(Family::kLd2410s, s.data(), s.size(), r) && !r.present);
  s[6] = 0x09;
  CHECK(!decode_report(Family::kLd2410s, s.data(), s.size(), r));
}

// ---- MR24HPC1 ------------------------------------------------------------------------------------------------------------------------

static Bytes mr24_frame(std::uint8_t control, std::uint8_t command, const Bytes& data) {
  Bytes frame = {0x53, 0x59, control, command, static_cast<std::uint8_t>(data.size() >> 8), static_cast<std::uint8_t>(data.size() & 0xFF)};
  frame.insert(frame.end(), data.begin(), data.end());
  frame.push_back(presence::mr24_checksum(frame.data(), frame.size()));
  frame.push_back(0x54);
  frame.push_back(0x43);
  return frame;
}

static void test_mr24() {
  using namespace presence;
  Mr24Event e;
  const Bytes occupied = mr24_frame(0x80, 0x01, {0x01});
  CHECK(occupied[occupied.size() - 3] == 0x2F);   // 53 59 80 01 00 01 01 sums to 0x12F
  CHECK(decode_mr24(occupied.data(), occupied.size(), e) && e.kind == Mr24Kind::kOccupied && e.value == 1);
  VarFramer framer(mr24_protocol());
  const Bytes stream = cat({{0x00, 0x53}, occupied, mr24_frame(0x80, 0x02, {0x02}), mr24_frame(0x80, 0x03, {0x37}), mr24_frame(0x80, 0x0B, {0x01}), mr24_frame(0x01, 0x01, {0x0F})});
  const auto frames = run(framer, stream, 4);
  CHECK(frames.size() == 5);
  Mr24State state;
  int changes = 0;
  for (const Bytes& frame : frames) {
    CHECK(decode_mr24(frame.data(), frame.size(), e));
    if (state.apply(e)) ++changes;
  }
  CHECK(state.known && state.occupied && state.motion == 2 && state.body_movement == 55 && state.proximity == 1 && changes == 1);
  const Bytes empty = mr24_frame(0x80, 0x01, {0x00});
  CHECK(decode_mr24(empty.data(), empty.size(), e) && state.apply(e) && !state.occupied);   // the flag changed: a moment to publish
  CHECK(decode_mr24(empty.data(), empty.size(), e) && !state.apply(e));                     // the same again: nothing new
  // a heartbeat is valid and says nothing; a wrong checksum is refused; values outside their range say nothing
  const Bytes heartbeat = mr24_frame(0x01, 0x01, {0x0F});
  CHECK(decode_mr24(heartbeat.data(), heartbeat.size(), e) && e.kind == Mr24Kind::kNone);
  Bytes wrong = occupied;
  wrong[wrong.size() - 3] ^= 0x01;
  CHECK(!decode_mr24(wrong.data(), wrong.size(), e));
  const Bytes silly = mr24_frame(0x80, 0x01, {0x07});
  CHECK(decode_mr24(silly.data(), silly.size(), e) && e.kind == Mr24Kind::kNone);
  const Bytes too_high = mr24_frame(0x80, 0x03, {0x65});
  CHECK(decode_mr24(too_high.data(), too_high.size(), e) && e.kind == Mr24Kind::kNone);
  Bytes cut(occupied.begin(), occupied.end() - 1);
  CHECK(!decode_mr24(cut.data(), cut.size(), e));
}

static Track det(int x, int y, int speed = 0) { return Track{1, 9, static_cast<std::int16_t>(x), static_cast<std::int16_t>(y), static_cast<std::int16_t>(speed)}; }

static void test_track_stabiliser() {
  TrackStabiliser stab;
  Track out[kTracksPerRadar];
  std::uint64_t now = 1000;
  // a target walking at 200 mm per frame keeps its id, whatever slot the radar puts it in
  Track a[1] = {det(1000, 2000)};
  CHECK(stab.update(a, 1, now, out) == 1);
  const std::uint8_t first_id = out[0].track_id;
  CHECK(first_id == 1 && out[0].sensor_id == 1);
  for (int step = 1; step <= 10; ++step) {
    now += 100;
    a[0] = det(1000 + 200 * step, 2000);
    CHECK(stab.update(a, 1, now, out) == 1 && out[0].track_id == first_id);
  }
  // the smoothing lags a little behind a moving target but never by more than the step
  CHECK(out[0].x_mm < 3000 && out[0].x_mm > 3000 - 400);
  // two targets: each keeps its own id when the radar swaps their order, and a new one far away gets a new id
  TrackStabiliser two;
  Track pair[2] = {det(-1500, 2000), det(1500, 3000)};
  CHECK(two.update(pair, 2, 5000, out) == 2);
  const std::uint8_t left = out[0].track_id, right = out[1].track_id;
  CHECK(left != right);
  Track swapped[2] = {det(1520, 3020), det(-1480, 2020)};
  CHECK(two.update(swapped, 2, 5100, out) == 2);
  CHECK((out[0].track_id == left && out[1].track_id == right) || (out[0].track_id == right && out[1].track_id == left));
  for (std::size_t i = 0; i < 2; ++i) CHECK(out[i].track_id == (out[i].x_mm < 0 ? left : right));
  Track three[3] = {det(-1480, 2020), det(1520, 3020), det(0, 5000)};
  CHECK(two.update(three, 3, 5200, out) == 3);
  int fresh = 0;
  for (std::size_t i = 0; i < 3; ++i) if (out[i].track_id != left && out[i].track_id != right) ++fresh;
  CHECK(fresh == 1);
  // one lost frame: the target is still reported where it was, with the same id, and the id is kept when it comes back
  TrackStabiliser lost;
  Track one[1] = {det(500, 1500, 300)};
  lost.update(one, 1, 9000, out);
  const std::uint8_t id = out[0].track_id;
  CHECK(lost.update(one, 0, 9100, out) == 1 && out[0].track_id == id && out[0].speed_mm_s == 0);
  CHECK(lost.update(one, 0, 9200, out) == 1);
  CHECK(lost.update(one, 0, 9300, out) == 0);          // no longer reported after the coasting frames
  one[0] = det(560, 1520);
  CHECK(lost.update(one, 1, 9400, out) == 1 && out[0].track_id == id);   // but still remembered
  // gone for more than the survival frames: the next detection is somebody new
  TrackStabiliser gone;
  gone.update(one, 1, 20000, out);
  const std::uint8_t old_id = out[0].track_id;
  for (int f = 1; f <= 7; ++f) gone.update(one, 0, 20000 + 100 * f, out);
  CHECK(gone.update(one, 1, 20800, out) == 1 && out[0].track_id != old_id);
  // a long silence forgets everyone
  TrackStabiliser silent;
  silent.update(one, 1, 30000, out);
  const std::uint8_t before = out[0].track_id;
  silent.update(one, 1, 33000, out);
  CHECK(out[0].track_id != before);
  // a target that jumps beyond the gate is a new target, and jitter inside it is calmed
  TrackStabiliser jump;
  jump.update(one, 1, 40000, out);
  const std::uint8_t start = out[0].track_id;
  Track far[1] = {det(560 + 1500, 1520)};
  const std::size_t after_jump = jump.update(far, 1, 40100, out);
  bool new_one = false;
  for (std::size_t i = 0; i < after_jump; ++i) new_one = new_one || (out[i].track_id != start && out[i].x_mm > 1500);
  CHECK(new_one);   // the one that moved away is still remembered for a moment, and the new one has its own id
  TrackStabiliser jitter;
  int lowest = 100000, highest = -100000;
  for (int f = 0; f < 40; ++f) {
    Track noisy[1] = {det(2000 + (f % 2 == 0 ? 120 : -120), 3000)};
    jitter.update(noisy, 1, 50000 + 100 * f, out);
    if (f >= 10) { lowest = std::min<int>(lowest, out[0].x_mm); highest = std::max<int>(highest, out[0].x_mm); }
  }
  CHECK(highest - lowest < 240 && highest - lowest > 0);
  // the ids wrap from 255 to 1 and never use 0, and a full house evicts the longest-missing track for a new one
  TrackStabiliser wrap;
  bool zero = false, wrapped = false;
  std::uint8_t previous = 0;
  for (int n = 0; n < 300; ++n) {
    Track lone[1] = {det(-3000 + (n % 2) * 6000, 4000)};
    const std::uint64_t at = 60000 + 3000 * n;   // each one after a long silence, so each is new
    wrap.update(lone, 1, at, out);
    zero = zero || out[0].track_id == 0;
    wrapped = wrapped || (previous == 255 && out[0].track_id == 1);
    previous = out[0].track_id;
  }
  CHECK(!zero && wrapped);
  TrackStabiliser crowd;
  Track five[5] = {det(-2000, 1000), det(-1000, 1000), det(0, 1000), det(1000, 1000), det(2000, 1000)};
  CHECK(crowd.update(five, 5, 70000, out) == 5);
  Track more[5] = {det(-2000, 1000), det(-1000, 1000), det(0, 1000), det(1000, 1000), det(2000, 1000)};
  CHECK(crowd.update(more, 5, 70100, out) == 5);
  Track sixth[1] = {det(-2900, 3900)};
  CHECK(crowd.update(sixth, 1, 70200, out) >= 1 && crowd.update(sixth, 1, 70300, out) >= 1);
}

int main() {
  test_track_stabiliser();
  test_models();
  test_var_framer();
  test_ld2461();
  test_ld2410_family();
  test_mr24();
  std::printf("%d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
