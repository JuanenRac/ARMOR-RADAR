// ARMOR-RADAR - the three LD2450 radars: the UARTs, the report frames, their health, and the command channel.
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D). GPL-3.0-or-later.
#include "radar_manager.hpp"

#include <array>
#include <cstdio>
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
#include "core/ld2450.hpp"

namespace armor::radar {
namespace {
constexpr char kTag[] = "armor-radar";
constexpr std::array<uart_port_t, 3> kUarts{UART_NUM_0, UART_NUM_1, UART_NUM_2};
constexpr int kAnswerTimeoutMs = 600;

struct Line {
  bool enabled = false;
  int rx = -1, tx = -1;
  armor::FrameFramer framer{armor::ld2450::protocol()};
  ld2450cmd::AckParser acks;
  QueueHandle_t answers = nullptr;   // Ack structures from the reader task to whoever is running a command
  std::mutex command_lock;           // one command at a time on a radar
  std::string firmware;
  int tracking_mode = 0;
  // frames per second over a sliding window
  std::uint32_t frames_at_window_start = 0;
  std::uint64_t window_start_ms = 0;
  double fps = 0.0;
};

std::array<Line, 3> g_lines;
std::mutex g_data_lock;          // the track set, the counters and the fps windows
armor::TrackSet g_tracks;
armor::RadarHealth g_health;

std::uint64_t monotonic_ms() { return static_cast<std::uint64_t>(esp_timer_get_time()) / 1000ULL; }

void configure_uart(std::size_t index, int rx_pin, int tx_pin) {
  const uart_port_t uart = kUarts[index];
  uart_config_t config{};
  config.baud_rate = static_cast<int>(ld2450::kBaudRate);
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
    if (state == armor::RadarState::kReporting) ESP_LOGI(kTag, "radar %u: %s, %.1f frames/s, %u frames, %u bad, %u bytes", radar, armor::RadarHealth::describe(state), g_lines[i].fps,
                                                        static_cast<unsigned>(counters.frames), static_cast<unsigned>(counters.bad_frames), static_cast<unsigned>(counters.bytes));
    else ESP_LOGW(kTag, "radar %u: %s (%u bytes, %u frames, %u bad)", radar, armor::RadarHealth::describe(state), static_cast<unsigned>(counters.bytes), static_cast<unsigned>(counters.frames), static_cast<unsigned>(counters.bad_frames));
  }
}

// Drains the UARTs through the report framers and the acknowledgement parsers.
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
      ld2450cmd::Ack ack;
      for (int b = 0; b < read; ++b) if (line.acks.feed(buffer[b], ack)) xQueueSend(line.answers, &ack, 0);
      line.framer.feed(buffer, static_cast<std::size_t>(read), [&](const std::uint8_t* bytes, std::size_t length) {
        armor::ld2450::Frame frame;
        std::lock_guard<std::mutex> guard(g_data_lock);
        if (!armor::ld2450::decode_frame(bytes, length, frame)) { g_health.frame_bad(sensor_id); return; }
        g_health.frame_ok(sensor_id, monotonic_ms());
#if CONFIG_ARMOR_RADAR_HEX_DUMP
        dump_frame(sensor_id, bytes, length);
#endif
        armor::Track tracks[armor::ld2450::kTargetsPerFrame];
        const std::size_t count = armor::ld2450::to_tracks(sensor_id, frame, tracks);
        g_tracks.update(sensor_id, tracks, count, monotonic_ms());
      });
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
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

std::string hex_of(const std::uint8_t* bytes, std::size_t length) {
  std::string out;
  char two[4];
  for (std::size_t i = 0; i < length; ++i) { std::snprintf(two, sizeof two, "%02x", bytes[i]); out += two; }
  return out;
}

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
}  // namespace

void start(const config::Settings& settings) {
  for (std::size_t i = 0; i < 3; ++i) {
    Line& line = g_lines[i];
    const config::RadarLine& wanted = settings.radars[i];
    line.enabled = wanted.enabled && wanted.rx >= 0;
    line.rx = wanted.rx;
    line.tx = wanted.tx;
    if (!line.enabled) continue;
    line.answers = xQueueCreate(6, sizeof(ld2450cmd::Ack));
    configure_uart(i, wanted.rx, wanted.tx);
    ESP_LOGI(kTag, "radar %u on UART%u: RX GPIO %d, TX GPIO %d", static_cast<unsigned>(i + 1), static_cast<unsigned>(i), wanted.rx, wanted.tx);
  }
  xTaskCreate(reader_task, "radar", 6144, nullptr, 5, nullptr);
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
  if (!line.enabled) { s.state_text = "disabled"; return s; }
  std::lock_guard<std::mutex> guard(g_data_lock);
  const std::uint8_t radar = static_cast<std::uint8_t>(index + 1);
  const armor::RadarState state = g_health.state(radar, monotonic_ms());
  const armor::RadarCounters counters = g_health.counters(radar);
  s.state = state == armor::RadarState::kReporting ? "reporting" : state == armor::RadarState::kNoData ? "no_data" : state == armor::RadarState::kGarbled ? "garbled" : "silent";
  s.state_text = armor::RadarHealth::describe(state);
  s.bytes = counters.bytes;
  s.frames = counters.frames;
  s.bad_frames = counters.bad_frames;
  s.frames_per_second = line.fps;
  s.firmware = line.firmware;
  s.tracking_mode = line.tracking_mode;
  return s;
}

CommandResult run(std::size_t index, Op op, bool flag, const ld2450cmd::ZoneFilter* zones) {
  CommandResult result;
  if (index >= 3 || !g_lines[index].enabled) { result.error = "disabled"; return result; }
  Line& line = g_lines[index];
  if (line.tx < 0) { result.error = "no_tx"; return result; }
  if (op == Op::kSetZones && (zones == nullptr || zones->type > 2)) { result.error = "bad_argument"; return result; }
  std::unique_lock<std::mutex> command(line.command_lock, std::try_to_lock);
  if (!command.owns_lock()) { result.error = "busy"; return result; }

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
  switch (op) {
    case Op::kReadInfo: {
      if (simple(ld2450cmd::kReadFirmware, ld2450cmd::kReadFirmware)) { result.firmware = ld2450cmd::firmware_text(ack); line.firmware = result.firmware; }
      else ok = false;
      if (ok && simple(ld2450cmd::kQueryTrackingMode, ld2450cmd::kQueryTrackingMode)) { result.tracking_mode = ld2450cmd::tracking_mode(ack); line.tracking_mode = result.tracking_mode; }
      if (ok && simple(ld2450cmd::kQueryZones, ld2450cmd::kQueryZones)) result.has_zones = ld2450cmd::zones_from_ack(ack, result.zones);
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

}  // namespace armor::radar
