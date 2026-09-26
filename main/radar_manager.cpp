// ARMOR-RADAR - the sensors on the three serial ports: the UARTs, the report frames, their health, and the command channel of each model.
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D). GPL-3.0-or-later.
#include "radar_manager.hpp"

#include <array>
#include <cstdio>
#include <memory>
#include <mutex>
extern "C" {
#include "driver/uart.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "sdkconfig.h"
}
#include "core/frame_framer.hpp"
#include "core/gpio_logic.hpp"
#include "core/json.hpp"
#include "core/ld2450.hpp"
#include "core/ld2461.hpp"
#include "core/presence.hpp"
#include "core/sensor_model.hpp"
#include "core/track_stabiliser.hpp"
#include "core/var_framer.hpp"

namespace armor::radar {
namespace {
constexpr char kTag[] = "armor-radar";
constexpr std::array<uart_port_t, 3> kUarts{UART_NUM_0, UART_NUM_1, UART_NUM_2};
constexpr int kAnswerTimeoutMs = 600;
constexpr std::uint64_t kPresenceRepeatMs = 30000;

struct Line {
  bool enabled = false;
  int rx = -1, tx = -1;
  sensors::Model model = sensors::Model::kLd2450;
  std::string name;
  // one of these framers is used, according to the model
  armor::FrameFramer framer{armor::ld2450::protocol()};   // LD2450
  armor::TrackStabiliser stabiliser;                       // the tracks of a tracker keep their identity from frame to frame
  std::unique_ptr<armor::VarFramer> var;                   // every other model
  ld2450cmd::AckParser acks;                               // the command family (LD2450, LD2410, LD2412) answers in FD FC FB FA frames
  QueueHandle_t answers = nullptr;                         // Ack structures from the reader task to whoever is running a command
  QueueHandle_t frame_answers = nullptr;                   // LD2461: the frames that are not reports
  std::mutex command_lock;                                 // one command at a time on a sensor
  std::string firmware;
  int tracking_mode = 0;
  // frames per second over a sliding window
  std::uint32_t frames_at_window_start = 0;
  double fps = 0.0;
  // what the latest frames said
  std::uint32_t coordinate_frames = 0;                     // LD2461: reports of coordinates (a module left on zones-only sends none)
  bool zones_known = false;
  bool zone_occupied[3] = {false, false, false};           // LD2461
  presence::Reading reading;                               // LD2410 family
  presence::Mr24State mr24;
  int published_present = -1;
  int published_distance = -1;
  std::uint64_t published_at_ms = 0;
};

std::array<Line, 3> g_lines;
std::mutex g_data_lock;          // the track set, the counters, the readings and the fps windows
armor::TrackSet g_tracks;
armor::RadarHealth g_health;
Publisher g_publish;
std::string g_node_id;
bool g_republish = false;        // set when the broker (re)connects: every presence sensor says its state again

std::uint64_t monotonic_ms() { return static_cast<std::uint64_t>(esp_timer_get_time()) / 1000ULL; }

void configure_uart(std::size_t index, int rx_pin, int tx_pin, int baud) {
  const uart_port_t uart = kUarts[index];
  uart_config_t config{};
  config.baud_rate = baud;
  config.data_bits = UART_DATA_8_BITS;
  config.parity = UART_PARITY_DISABLE;
  config.stop_bits = UART_STOP_BITS_1;
  config.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
  config.source_clk = UART_SCLK_DEFAULT;
  ESP_ERROR_CHECK(uart_param_config(uart, &config));
  ESP_ERROR_CHECK(uart_set_pin(uart, tx_pin >= 0 ? tx_pin : UART_PIN_NO_CHANGE, rx_pin, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
  ESP_ERROR_CHECK(uart_driver_install(uart, 2048, 0, 0, nullptr, 0));
}

#if CONFIG_ARMOR_RADAR_HEX_DUMP
void dump_frame(std::uint8_t radar, const std::uint8_t* bytes, std::size_t length) {
  static int dumped[3] = {0, 0, 0};
  if (dumped[radar - 1] >= CONFIG_ARMOR_RADAR_HEX_DUMP_FRAMES) return;
  ++dumped[radar - 1];
  char text[3 * armor::ld2450::kFrameLength + 1];
  std::size_t at = 0;
  for (std::size_t i = 0; i < length && at + 3 < sizeof text; ++i) at += static_cast<std::size_t>(std::snprintf(text + at, sizeof text - at, "%02x", bytes[i]));
  ESP_LOGI(kTag, "FRAME r%u %s", static_cast<unsigned>(radar), text);
}
#endif

void log_statistics(std::uint64_t now) {
  for (std::size_t i = 0; i < 3; ++i) {
    if (!g_lines[i].enabled) continue;
    const std::uint8_t radar = static_cast<std::uint8_t>(i + 1);
    const armor::RadarCounters counters = g_health.counters(radar);
    const armor::RadarState state = g_health.state(radar, now);
    const char* model = sensors::to_text(g_lines[i].model);
    if (state == armor::RadarState::kReporting) ESP_LOGI(kTag, "sensor %u (%s): %s, %.1f frames/s, %u frames, %u bad, %u bytes", radar, model, armor::RadarHealth::describe(state), g_lines[i].fps,
                                                        static_cast<unsigned>(counters.frames), static_cast<unsigned>(counters.bad_frames), static_cast<unsigned>(counters.bytes));
    else ESP_LOGW(kTag, "sensor %u (%s): %s (%u bytes, %u frames, %u bad)", radar, model, armor::RadarHealth::describe(state), static_cast<unsigned>(counters.bytes), static_cast<unsigned>(counters.frames), static_cast<unsigned>(counters.bad_frames));
  }
}

std::string hex_of(const std::uint8_t* bytes, std::size_t length) {
  std::string out;
  char two[4];
  for (std::size_t i = 0; i < length; ++i) { std::snprintf(two, sizeof two, "%02x", bytes[i]); out += two; }
  return out;
}

// ---- what each model's bytes become ------------------------------------------------------------------------------------------------

void feed_ld2450(Line& line, std::uint8_t sensor_id, const std::uint8_t* buffer, std::size_t read) {
  ld2450cmd::Ack ack;
  for (std::size_t b = 0; b < read; ++b) if (line.acks.feed(buffer[b], ack)) xQueueSend(line.answers, &ack, 0);
  line.framer.feed(buffer, read, [&](const std::uint8_t* bytes, std::size_t length) {
    armor::ld2450::Frame frame;
    std::lock_guard<std::mutex> guard(g_data_lock);
    if (!armor::ld2450::decode_frame(bytes, length, frame)) { g_health.frame_bad(sensor_id); return; }
    g_health.frame_ok(sensor_id, monotonic_ms());
#if CONFIG_ARMOR_RADAR_HEX_DUMP
    dump_frame(sensor_id, bytes, length);
#endif
    armor::Track tracks[armor::ld2450::kTargetsPerFrame];
    std::size_t count = armor::ld2450::to_tracks(sensor_id, frame, tracks);
#if CONFIG_ARMOR_TRACK_STABILISER
    armor::Track stable[armor::kTracksPerRadar];
    count = line.stabiliser.update(tracks, count, monotonic_ms(), stable);
    g_tracks.update(sensor_id, stable, count, monotonic_ms());
#else
    g_tracks.update(sensor_id, tracks, count, monotonic_ms());
#endif
  });
}

void feed_ld2461(Line& line, std::uint8_t sensor_id, const std::uint8_t* buffer, std::size_t read) {
  line.var->feed(buffer, read, [&](const std::uint8_t* bytes, std::size_t length) {
    ld2461::Frame frame;
    const bool valid = ld2461::parse(bytes, length, frame);
    if (valid && frame.command != ld2461::kReportCoordinates && frame.command != ld2461::kReportZones) { xQueueSend(line.frame_answers, &frame, 0); return; }   // an answer to a command
    std::lock_guard<std::mutex> guard(g_data_lock);
    if (!valid) { g_health.frame_bad(sensor_id); return; }
    g_health.frame_ok(sensor_id, monotonic_ms());
    if (frame.command == ld2461::kReportCoordinates) {
      armor::Track tracks[ld2461::kMaxTargets];
      std::size_t count = ld2461::to_tracks(sensor_id, frame, tracks);
#if CONFIG_ARMOR_TRACK_STABILISER
      armor::Track stable[armor::kTracksPerRadar];
      count = line.stabiliser.update(tracks, count, monotonic_ms(), stable);
      g_tracks.update(sensor_id, stable, count, monotonic_ms());
#else
      g_tracks.update(sensor_id, tracks, count, monotonic_ms());
#endif
      ++line.coordinate_frames;
    } else {
      line.zones_known = ld2461::zone_occupancy(frame, line.zone_occupied);
    }
  });
}

void feed_report_family(Line& line, std::uint8_t sensor_id, presence::Family family, const std::uint8_t* buffer, std::size_t read) {
  ld2450cmd::Ack ack;
  for (std::size_t b = 0; b < read; ++b) if (line.acks.feed(buffer[b], ack)) xQueueSend(line.answers, &ack, 0);
  line.var->feed(buffer, read, [&](const std::uint8_t* bytes, std::size_t length) {
    presence::Reading reading;
    std::lock_guard<std::mutex> guard(g_data_lock);
    if (!presence::decode_report(family, bytes, length, reading)) { g_health.frame_bad(sensor_id); return; }
    g_health.frame_ok(sensor_id, monotonic_ms());
    line.reading = reading;
  });
}

void feed_mr24(Line& line, std::uint8_t sensor_id, const std::uint8_t* buffer, std::size_t read) {
  line.var->feed(buffer, read, [&](const std::uint8_t* bytes, std::size_t length) {
    presence::Mr24Event event;
    std::lock_guard<std::mutex> guard(g_data_lock);
    if (!presence::decode_mr24(bytes, length, event)) { g_health.frame_bad(sensor_id); return; }
    g_health.frame_ok(sensor_id, monotonic_ms());
    line.mr24.apply(event);
  });
}

// ---- presence sensors as devices of the server -------------------------------------------------------------------------------------

// What a presence sensor knows now: 1 somebody, 0 nobody, -1 not yet, and the distance in centimetres or -1. Call with the data lock held.
void presence_now(const Line& line, int& present, int& distance) {
  present = -1;
  distance = -1;
  switch (line.model) {
    case sensors::Model::kLd2410: case sensors::Model::kLd2412: case sensors::Model::kLd2410s:
      if (line.reading.valid) { present = line.reading.present ? 1 : 0; distance = line.reading.present ? line.reading.distance_cm : -1; }
      break;
    case sensors::Model::kMr24hpc1:
      if (line.mr24.known) present = line.mr24.occupied ? 1 : 0;
      break;
    default: break;
  }
}

void publish_presence_devices(std::uint64_t now) {
  struct Message { std::string topic, payload; };
  Message messages[3];
  std::size_t count = 0;
  {
    std::lock_guard<std::mutex> guard(g_data_lock);
    for (Line& line : g_lines) {
      if (!line.enabled || sensors::info(line.model).tracker) continue;
      int present = -1, distance = -1;
      presence_now(line, present, distance);
      if (present < 0) continue;
      const bool changed = present != line.published_present;
      if (!changed && !g_republish && now - line.published_at_ms < kPresenceRepeatMs) continue;
      messages[count].topic = gpio::topic(g_node_id, line.name, "state");
      messages[count].payload = presence::device_payload(present == 1, distance);
      ++count;
      line.published_present = present;
      line.published_distance = distance;
      line.published_at_ms = now;
    }
    g_republish = false;
  }
  if (!g_publish) return;
  for (std::size_t i = 0; i < count; ++i) if (!messages[i].topic.empty()) g_publish(messages[i].topic, messages[i].payload);
}

// Drains the UARTs through the framers of each model.
void reader_task(void*) {
  std::uint8_t buffer[128];
  std::uint64_t last_stats = 0, last_window = 0;
  for (;;) {
    for (std::size_t i = 0; i < 3; ++i) {
      Line& line = g_lines[i];
      if (!line.enabled) continue;
      const int read = uart_read_bytes(kUarts[i], buffer, sizeof buffer, 0);
      if (read <= 0) continue;
      const std::uint8_t sensor_id = static_cast<std::uint8_t>(i + 1);
      {
        std::lock_guard<std::mutex> guard(g_data_lock);
        g_health.bytes_received(sensor_id, static_cast<std::size_t>(read));
      }
      const std::size_t bytes = static_cast<std::size_t>(read);
      switch (line.model) {
        case sensors::Model::kLd2450: feed_ld2450(line, sensor_id, buffer, bytes); break;
        case sensors::Model::kLd2461: feed_ld2461(line, sensor_id, buffer, bytes); break;
        case sensors::Model::kLd2410: feed_report_family(line, sensor_id, presence::Family::kLd2410, buffer, bytes); break;
        case sensors::Model::kLd2412: feed_report_family(line, sensor_id, presence::Family::kLd2412, buffer, bytes); break;
        case sensors::Model::kLd2410s: feed_report_family(line, sensor_id, presence::Family::kLd2410s, buffer, bytes); break;
        case sensors::Model::kMr24hpc1: feed_mr24(line, sensor_id, buffer, bytes); break;
      }
    }
    const std::uint64_t now = monotonic_ms();
    if (now - last_window >= 5000) {
      std::lock_guard<std::mutex> guard(g_data_lock);
      for (std::size_t i = 0; i < 3; ++i) {
        Line& line = g_lines[i];
        const std::uint32_t frames = g_health.counters(static_cast<std::uint8_t>(i + 1)).frames;
        line.fps = last_window == 0 ? 0.0 : (frames - line.frames_at_window_start) * 1000.0 / static_cast<double>(now - last_window);
        line.frames_at_window_start = frames;
      }
      last_window = now;
    }
    if (now - last_stats >= 10000) { last_stats = now; log_statistics(now); }
    publish_presence_devices(now);
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

// ---- commands ------------------------------------------------------------------------------------------------------------------

// Sends one frame and waits for the acknowledgement of `expected` (0: any). True when the module answered and the status said done.
bool exchange(Line& line, std::size_t index, const std::uint8_t* frame, std::size_t length, std::uint16_t expected, ld2450cmd::Ack& ack, CommandResult& result) {
  xQueueReset(line.answers);
  uart_write_bytes(kUarts[index], reinterpret_cast<const char*>(frame), length);
  const std::uint64_t deadline = monotonic_ms() + kAnswerTimeoutMs;
  for (;;) {
    const std::uint64_t now = monotonic_ms();
    if (now >= deadline) { result.error = "no_answer"; return false; }
    if (xQueueReceive(line.answers, &ack, pdMS_TO_TICKS(deadline - now)) != pdTRUE) { result.error = "no_answer"; return false; }
    result.last_answer_hex = hex_of(ack.data, ack.data_length);
    if (expected != 0 && ack.command != expected) continue;  // an answer to something older
    if (!ack.ok) { result.error = "refused"; return false; }
    return true;
  }
}

// The LD2461's exchange: its answers are ordinary frames, and the command they answer is the one they carry.
bool exchange_ld2461(Line& line, std::size_t index, const std::uint8_t* frame, std::size_t length, std::uint8_t expected, ld2461::Frame& answer, CommandResult& result) {
  xQueueReset(line.frame_answers);
  uart_write_bytes(kUarts[index], reinterpret_cast<const char*>(frame), length);
  const std::uint64_t deadline = monotonic_ms() + kAnswerTimeoutMs;
  for (;;) {
    const std::uint64_t now = monotonic_ms();
    if (now >= deadline) { result.error = "no_answer"; return false; }
    if (xQueueReceive(line.frame_answers, &answer, pdMS_TO_TICKS(deadline - now)) != pdTRUE) { result.error = "no_answer"; return false; }
    std::uint8_t raw[1 + ld2461::kMaxValue];
    raw[0] = answer.command;
    for (std::size_t i = 0; i < answer.value_length; ++i) raw[1 + i] = answer.value[i];
    result.last_answer_hex = hex_of(raw, 1 + answer.value_length);
    if (answer.command != expected) continue;
    return true;
  }
}

// The command family (LD2450, and the LD2410 and LD2412 which share its framing): enter configuration, the command, leave configuration.
CommandResult run_command_family(std::size_t index, Op op, bool flag, const ld2450cmd::ZoneFilter* zones) {
  CommandResult result;
  Line& line = g_lines[index];
  std::uint8_t frame[ld2450cmd::kMaxFrame];
  ld2450cmd::Ack ack;
  // 1. enter configuration mode (twice if the first try is not answered: the module may be in the middle of a report)
  std::size_t length = ld2450cmd::build_enable_configuration(frame, sizeof frame);
  bool entered = exchange(line, index, frame, length, ld2450cmd::kEnableConfiguration, ack, result);
  if (!entered) { vTaskDelay(pdMS_TO_TICKS(200)); entered = exchange(line, index, frame, length, ld2450cmd::kEnableConfiguration, ack, result); }
  if (!entered) return result;
  result.error.clear();

  const auto simple = [&](std::uint16_t command_word, std::uint16_t expected_answer) {
    length = ld2450cmd::build_simple(command_word, frame, sizeof frame);
    return exchange(line, index, frame, length, expected_answer, ack, result);
  };
  bool ok = true;
  bool leaves_configuration = true;  // false after a restart or a factory reset: the module restarts by itself
  const bool ld2450 = line.model == sensors::Model::kLd2450;
  switch (op) {
    case Op::kReadInfo: {
      if (simple(ld2450cmd::kReadFirmware, ld2450cmd::kReadFirmware)) { result.firmware = ld2450cmd::firmware_text(ack); line.firmware = result.firmware; }
      else ok = false;
      if (ld2450 && ok && simple(ld2450cmd::kQueryTrackingMode, ld2450cmd::kQueryTrackingMode)) { result.tracking_mode = ld2450cmd::tracking_mode(ack); line.tracking_mode = result.tracking_mode; }
      if (ld2450 && ok && simple(ld2450cmd::kQueryZones, ld2450cmd::kQueryZones)) result.has_zones = ld2450cmd::zones_from_ack(ack, result.zones);
      break;
    }
    case Op::kSingleTarget: ok = simple(ld2450cmd::kSingleTarget, ld2450cmd::kSingleTarget); if (ok) line.tracking_mode = 1; break;
    case Op::kMultiTarget: ok = simple(ld2450cmd::kMultiTarget, ld2450cmd::kMultiTarget); if (ok) line.tracking_mode = 2; break;
    case Op::kSetZones:
      length = ld2450cmd::build_set_zones(*zones, frame, sizeof frame);
      ok = exchange(line, index, frame, length, ld2450cmd::kSetZones, ack, result);
      break;
    case Op::kBluetooth:
      length = ld2450cmd::build_bluetooth(flag, frame, sizeof frame);
      ok = exchange(line, index, frame, length, ld2450cmd::kBluetooth, ack, result);
      break;
    case Op::kRestart: ok = simple(ld2450cmd::kRestart, ld2450cmd::kRestart); leaves_configuration = false; break;
    case Op::kFactoryReset: ok = simple(ld2450cmd::kRestoreFactory, ld2450cmd::kRestoreFactory); leaves_configuration = !ok; break;
  }
  // 2. always leave configuration mode, so the module reports again
  if (leaves_configuration) {
    const std::string error = result.error, answer = result.last_answer_hex;  // what went wrong first is what is reported
    simple(ld2450cmd::kEndConfiguration, ld2450cmd::kEndConfiguration);
    result.error = error;
    result.last_answer_hex = answer;
  }
  result.ok = ok;
  if (ok) result.error.clear();
  return result;
}

bool zone_is_empty(const ld2450cmd::Zone& zone) { return zone.x1 == 0 && zone.y1 == 0 && zone.x2 == 0 && zone.y2 == 0; }

// The LD2461 has no configuration mode: each command is a frame, and the module answers with a frame of the same command.
CommandResult run_ld2461(std::size_t index, Op op, const ld2450cmd::ZoneFilter* zones) {
  CommandResult result;
  Line& line = g_lines[index];
  std::uint8_t frame[64];
  ld2461::Frame answer;
  const auto ask = [&](std::size_t length, std::uint8_t expected) { return length != 0 && exchange_ld2461(line, index, frame, length, expected, answer, result); };
  bool ok = true;
  switch (op) {
    case Op::kReadInfo: {
      if (ask(ld2461::build_query(ld2461::kVersion, frame, sizeof frame), ld2461::kVersion)) {
        const ld2461::Version version = ld2461::version_from(answer);
        char text[48];
        std::snprintf(text, sizeof text, "%u.%u (%02u-%02u, id %08X)", version.major, version.minor, version.month, version.day, static_cast<unsigned>(version.identifier));
        result.firmware = version.ok ? text : "";
        line.firmware = result.firmware;
      } else ok = false;
      if (ok && ask(ld2461::build_query(ld2461::kReadReportFormat, frame, sizeof frame), ld2461::kReadReportFormat) && answer.value_length >= 1) {
        result.tracking_mode = answer.value[0];
        line.tracking_mode = result.tracking_mode;
      }
      if (ok && ask(ld2461::build_query(ld2461::kReadZones, frame, sizeof frame), ld2461::kReadZones)) {
        ld2461::Zone zone[3];
        if (ld2461::zones_from(answer, zone)) {
          result.has_zones = true;
          bool any = false, all_detect = true;
          for (std::size_t i = 0; i < 3; ++i) {
            result.zones.zones[i] = {static_cast<std::int16_t>(zone[i].x1_mm), static_cast<std::int16_t>(zone[i].y1_mm), static_cast<std::int16_t>(zone[i].x2_mm), static_cast<std::int16_t>(zone[i].y2_mm)};
            if (zone[i].set) { any = true; if (zone[i].type != 0) all_detect = false; }
          }
          result.zones.type = !any ? 0 : all_detect ? 1 : 2;   // the panel's filter types: 0 none, 1 only inside, 2 ignore inside
        }
      }
      break;
    }
    case Op::kSetZones: {
      // The panel's filter (0 none, 1 report only what is inside the zones, 2 ignore what is inside) in the module's terms: a zone of type 0 detects only
      // what is inside, a zone of type 1 ignores what is inside; "none" cancels every zone.
      for (std::uint8_t zone = 1; zone <= 3 && ok; ++zone) {
        const ld2450cmd::Zone& rect = zones->zones[zone - 1];
        if (zones->type == 0 || zone_is_empty(rect)) ok = ask(ld2461::build_cancel_zone(zone, frame, sizeof frame), ld2461::kCancelZone);
        else {
          const std::size_t length = ld2461::build_set_zone(zone, zones->type == 1 ? 0 : 1, rect.x1, rect.y1, rect.x2, rect.y2, frame, sizeof frame);
          if (length == 0) { result.error = "bad_argument"; return result; }
          ok = ask(length, ld2461::kSetZone);
        }
      }
      break;
    }
    case Op::kFactoryReset: ok = ask(ld2461::build_query(ld2461::kFactoryReset, frame, sizeof frame), ld2461::kFactoryReset); break;
    default: result.error = "unsupported"; return result;
  }
  result.ok = ok;
  if (ok) result.error.clear();
  return result;
}

// A node that wants tracks needs the LD2461 to send coordinates; the factory default is zones only. Read its format and, if it is not 1 or 3, set 3.
void ld2461_format_task(void* argument) {
  const std::size_t index = reinterpret_cast<std::size_t>(argument);
  vTaskDelay(pdMS_TO_TICKS(3000));   // the module needs a moment after power-up
  for (int attempt = 0; attempt < 5; ++attempt) {
    Line& line = g_lines[index];
    std::unique_lock<std::mutex> command(line.command_lock, std::try_to_lock);
    if (command.owns_lock()) {
      CommandResult result;
      std::uint8_t frame[32];
      ld2461::Frame answer;
      std::size_t length = ld2461::build_query(ld2461::kReadReportFormat, frame, sizeof frame);
      const bool read_ok = exchange_ld2461(line, index, frame, length, ld2461::kReadReportFormat, answer, result) && answer.value_length >= 1;
      const int format = read_ok ? answer.value[0] : 0;
      if (format == 1 || format == 3) { line.tracking_mode = format; ESP_LOGI(kTag, "sensor %u (ld2461): report format %d, coordinates on", static_cast<unsigned>(index + 1), format); break; }
      length = ld2461::build_set_format(3, frame, sizeof frame);
      if (exchange_ld2461(line, index, frame, length, ld2461::kSetReportFormat, answer, result)) { line.tracking_mode = 3; ESP_LOGI(kTag, "sensor %u (ld2461): report format set to coordinates and zones", static_cast<unsigned>(index + 1)); break; }
      ESP_LOGW(kTag, "sensor %u (ld2461): could not set the report format (%s); without coordinates it gives no tracks", static_cast<unsigned>(index + 1), result.error.c_str());
    }
    vTaskDelay(pdMS_TO_TICKS(5000));
  }
  vTaskDelete(nullptr);
}
}  // namespace

void start(const config::Settings& settings, Publisher publisher) {
  g_publish = std::move(publisher);
  g_node_id = settings.node_id;
  for (std::size_t i = 0; i < 3; ++i) {
    Line& line = g_lines[i];
    const config::RadarLine& wanted = settings.radars[i];
    line.enabled = wanted.enabled && wanted.rx >= 0;
    line.rx = wanted.rx;
    line.tx = wanted.tx;
    line.name = wanted.name;
    if (!sensors::from_text(wanted.model, line.model)) line.model = sensors::Model::kLd2450;
    if (!line.enabled) continue;
    const sensors::ModelInfo& model = sensors::info(line.model);
    const int baud = wanted.baud != 0 ? wanted.baud : static_cast<int>(model.default_baud);
    switch (line.model) {
      case sensors::Model::kLd2450: break;
      case sensors::Model::kLd2461: line.var = std::make_unique<armor::VarFramer>(ld2461::protocol()); break;
      case sensors::Model::kLd2410: case sensors::Model::kLd2412: case sensors::Model::kLd2410s: line.var = std::make_unique<armor::VarFramer>(presence::report_protocol()); break;
      case sensors::Model::kMr24hpc1: line.var = std::make_unique<armor::VarFramer>(presence::mr24_protocol()); break;
    }
    {
      armor::StabiliserConfig stabilising;
      if (line.model == sensors::Model::kLd2461) stabilising.gate_mm = 900;   // its positions come in steps of 0.1 m, and it may report less often
      line.stabiliser = armor::TrackStabiliser(stabilising);
      line.stabiliser.set_sensor(static_cast<std::uint8_t>(i + 1));
    }
    line.answers = xQueueCreate(6, sizeof(ld2450cmd::Ack));
    line.frame_answers = xQueueCreate(4, sizeof(ld2461::Frame));
    configure_uart(i, wanted.rx, wanted.tx, baud);
    ESP_LOGI(kTag, "sensor %u (%s) on UART%u at %d baud: RX GPIO %d, TX GPIO %d%s%s", static_cast<unsigned>(i + 1), model.label, static_cast<unsigned>(i), baud, wanted.rx, wanted.tx,
             model.tracker ? "" : ", device ", model.tracker ? "" : wanted.name.c_str());
  }
  xTaskCreate(reader_task, "radar", 6144, nullptr, 5, nullptr);
  for (std::size_t i = 0; i < 3; ++i) {
    if (g_lines[i].enabled && g_lines[i].model == sensors::Model::kLd2461) {
      if (g_lines[i].tx >= 0) xTaskCreate(ld2461_format_task, "ld2461-fmt", 4096, reinterpret_cast<void*>(i), 3, nullptr);
      else ESP_LOGW(kTag, "sensor %u (ld2461): TX is not wired, so the report format cannot be set: it must be coordinates already (or the sensor gives no tracks)", static_cast<unsigned>(i + 1));
    }
  }
}

void publish_presence_now() {
  std::lock_guard<std::mutex> guard(g_data_lock);
  g_republish = true;
}

bool any_fresh() {
  std::lock_guard<std::mutex> guard(g_data_lock);
  return g_tracks.any_fresh(monotonic_ms());
}

std::size_t collect_tracks(Track* out) {
  std::lock_guard<std::mutex> guard(g_data_lock);
  return g_tracks.collect(out, monotonic_ms());
}

Status status(std::size_t index) {
  Status s;
  if (index >= 3) return s;
  Line& line = g_lines[index];
  s.enabled = line.enabled;
  s.rx = line.rx;
  s.tx = line.tx;
  s.model = sensors::to_text(line.model);
  s.tracker = sensors::info(line.model).tracker;
  s.name = line.name;
  if (!line.enabled) { s.state_text = "disabled"; return s; }
  std::lock_guard<std::mutex> guard(g_data_lock);
  const std::uint8_t radar = static_cast<std::uint8_t>(index + 1);
  const armor::RadarState state = g_health.state(radar, monotonic_ms());
  const armor::RadarCounters counters = g_health.counters(radar);
  s.state = state == armor::RadarState::kReporting ? "reporting" : state == armor::RadarState::kNoData ? "no_data" : state == armor::RadarState::kGarbled ? "garbled" : "silent";
  s.state_text = armor::RadarHealth::describe(state);
  // the MR24 reports only when something changes: a quiet room is not a silent sensor
  if (line.model == sensors::Model::kMr24hpc1 && counters.frames > 0 && state == armor::RadarState::kSilent) { s.state = "reporting"; s.state_text = "reporting on change"; }
  if (line.model == sensors::Model::kLd2461 && state == armor::RadarState::kReporting && line.coordinate_frames == 0) s.state_text = "reporting zones only: no coordinates (the report format must be 1 or 3)";
  s.bytes = counters.bytes;
  s.frames = counters.frames;
  s.bad_frames = counters.bad_frames;
  s.frames_per_second = line.fps;
  s.firmware = line.firmware;
  s.tracking_mode = line.tracking_mode;
  int present = -1, distance = -1;
  presence_now(line, present, distance);
  s.present = present;
  s.distance_cm = distance;
  json::Writer w;
  switch (line.model) {
    case sensors::Model::kLd2461:
      w.begin_object().field("coordinates", line.coordinate_frames > 0);
      if (line.zones_known) {
        w.key("zones").begin_array();
        for (bool occupied : line.zone_occupied) w.boolean(occupied);
        w.end_array();
      }
      w.end_object();
      break;
    case sensors::Model::kLd2410: case sensors::Model::kLd2412:
      if (line.reading.valid) {
        w.begin_object().field("state", static_cast<int>(line.reading.state)).field("moving_cm", line.reading.moving_cm).field("moving_energy", line.reading.moving_energy)
            .field("static_cm", line.reading.static_cm).field("static_energy", line.reading.static_energy).field("calibrating", line.reading.calibrating);
        if (line.reading.detection_cm >= 0) w.field("detection_cm", line.reading.detection_cm);
        w.end_object();
      }
      break;
    case sensors::Model::kLd2410s:
      if (line.reading.valid) w.begin_object().field("state", static_cast<int>(line.reading.state)).end_object();
      break;
    case sensors::Model::kMr24hpc1:
      if (line.mr24.known) w.begin_object().field("occupied", line.mr24.occupied).field("motion", line.mr24.motion).field("body_movement", line.mr24.body_movement).field("proximity", line.mr24.proximity).end_object();
      break;
    default: break;
  }
  s.detail = w.str();
  return s;
}

CommandResult run(std::size_t index, Op op, bool flag, const ld2450cmd::ZoneFilter* zones) {
  CommandResult result;
  if (index >= 3 || !g_lines[index].enabled) { result.error = "disabled"; return result; }
  Line& line = g_lines[index];
  // what each model can be told: the LD2450 everything; the LD2461 its own set; the LD2410 and LD2412 read, restart, reset and bluetooth
  bool supported = false;
  switch (line.model) {
    case sensors::Model::kLd2450: supported = true; break;
    case sensors::Model::kLd2461: supported = op == Op::kReadInfo || op == Op::kSetZones || op == Op::kFactoryReset; break;
    case sensors::Model::kLd2410: case sensors::Model::kLd2412: supported = op == Op::kReadInfo || op == Op::kRestart || op == Op::kFactoryReset || op == Op::kBluetooth; break;
    default: supported = false; break;
  }
  if (!supported) { result.error = "unsupported"; return result; }
  if (line.tx < 0) { result.error = "no_tx"; return result; }
  if (op == Op::kSetZones && (zones == nullptr || zones->type > 2)) { result.error = "bad_argument"; return result; }
  std::unique_lock<std::mutex> command(line.command_lock, std::try_to_lock);
  if (!command.owns_lock()) { result.error = "busy"; return result; }
  return line.model == sensors::Model::kLd2461 ? run_ld2461(index, op, zones) : run_command_family(index, op, flag, zones);
}

}  // namespace armor::radar
