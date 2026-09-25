/*
 * ARMOR-RADAR - ESP32-S3 field node (Waveshare ESP32-S3-ETH, up to three HLK-LD2450 radars, wired Ethernet).
 * Copyright (C) 2026 JuanenRac (Electro Hobby 3D). GPL-3.0-or-later.
 *
 * The hardware-independent core under main/core is built and tested on a computer (tests/); this file and the two drivers
 * beside it (board_ethernet.cpp, light_sensor.cpp) are built with ESP-IDF 5.4 (tools/build_node.sh does it in a container).
 * None of it has run on a board yet: the bench checklist is in docs/BENCH_BRINGUP.md.
 *
 * What it does:
 *   - refuses to start with an invalid or placeholder node identity, duplicate pins or pins that clash with Ethernet or I2C;
 *   - reads up to three radars on the three UARTs, each through the resynchronising framer, and decodes the LD2450 frames;
 *   - counts, per radar, bytes, good frames and bad frames, and logs them every ten seconds, so on the bench the first look
 *     at the console says which radar is wired and which is not;
 *   - brings up the W5500 Ethernet, gets an address and the time (SNTP), and connects to the broker;
 *   - publishes an "online" health message every CONFIG_ARMOR_HEARTBEAT_S, with an MQTT last will that says "offline";
 *   - publishes telemetry at most five times a second once the network, the clock, an ambient-light value and at least one
 *     radar that is really reporting are all there. It never publishes an empty "all clear" from radars that are silent.
 *
 * What it deliberately does not do: configure the radar modules (their command set is not in the manual available), decode an
 * LD2461, or invent a light value (see CONFIG_ARMOR_LUX_FALLBACK for the explicit bench setting).
 */
#include <array>
#include <cstdio>
#include <cstring>
#include <ctime>
extern "C" {
#include "sdkconfig.h"
#include <sys/time.h>
#include "driver/uart.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_sntp.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mqtt_client.h"
#include "nvs_flash.h"
}
#include "board_ethernet.hpp"
#include "core/frame_framer.hpp"
#include "core/ld2450.hpp"
#include "core/node_id.hpp"
#include "core/radar_health.hpp"
#include "core/telemetry_json.hpp"
#include "light_sensor.hpp"

namespace {
constexpr char kTag[] = "armor-radar";
constexpr char kPlaceholderNodeId[] = "unconfigured-node";
constexpr std::size_t kRadars = CONFIG_ARMOR_RADAR_COUNT;
constexpr std::array<uart_port_t, 3> kUarts{UART_NUM_0, UART_NUM_1, UART_NUM_2};
constexpr std::array<int, 3> kRxPins{CONFIG_ARMOR_RADAR_1_RX, CONFIG_ARMOR_RADAR_2_RX, CONFIG_ARMOR_RADAR_3_RX};
constexpr int kHeartbeatMs = CONFIG_ARMOR_HEARTBEAT_S * 1000;
constexpr std::uint64_t kTelemetryPeriodMs = CONFIG_ARMOR_TELEMETRY_PERIOD_MS;
constexpr std::uint64_t kStatsPeriodMs = 10000;

esp_mqtt_client_handle_t g_mqtt = nullptr;
volatile bool g_mqtt_connected = false;
// One framer per radar, all speaking the HLK-LD2450 report protocol.
armor::FrameFramer g_framers[3] = {armor::FrameFramer(armor::ld2450::protocol()), armor::FrameFramer(armor::ld2450::protocol()),
                                   armor::FrameFramer(armor::ld2450::protocol())};
// The newest tracks of the radars and what each one has reported. Only radar_task touches them, apart from the read-only
// statistics line, which is written by the same task.
armor::TrackSet g_tracks;
armor::RadarHealth g_health;

bool pins_are_valid() {
  const int others[] = {CONFIG_ARMOR_ETH_MOSI, CONFIG_ARMOR_ETH_MISO, CONFIG_ARMOR_ETH_SCLK, CONFIG_ARMOR_ETH_CS, CONFIG_ARMOR_ETH_INT,
#if CONFIG_ARMOR_ETH_RST >= 0
                        CONFIG_ARMOR_ETH_RST,
#endif
#if CONFIG_ARMOR_LIGHT_VEML7700
                        CONFIG_ARMOR_I2C_SDA, CONFIG_ARMOR_I2C_SCL,
#endif
  };
  // Every radar pin must differ from every other radar pin and from the Ethernet and I2C pins, and those may not repeat either.
  for (std::size_t i = 0; i < kRadars; ++i) {
    for (std::size_t j = i + 1; j < kRadars; ++j) if (kRxPins[i] == kRxPins[j]) return false;
    for (int pin : others) if (kRxPins[i] == pin) return false;
  }
  constexpr std::size_t n = sizeof(others) / sizeof(others[0]);
  for (std::size_t i = 0; i < n; ++i) for (std::size_t j = i + 1; j < n; ++j) if (others[i] == others[j]) return false;
  return true;
}

// The server ignores a message older than the last one it accepted from a node,
// so timestamps must be wall-clock time that survives a reboot, not uptime.
bool clock_is_set() { return std::time(nullptr) > 1700000000; }
std::uint64_t now_ms() {
  timeval tv{};
  gettimeofday(&tv, nullptr);
  return static_cast<std::uint64_t>(tv.tv_sec) * 1000ULL + static_cast<std::uint64_t>(tv.tv_usec) / 1000ULL;
}
std::uint64_t monotonic_ms() { return static_cast<std::uint64_t>(esp_timer_get_time()) / 1000ULL; }

void configure_radar_uart(uart_port_t uart, int rx_pin) {
  uart_config_t config{};
  config.baud_rate = 256000;
  config.data_bits = UART_DATA_8_BITS;
  config.parity = UART_PARITY_DISABLE;
  config.stop_bits = UART_STOP_BITS_1;
  config.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
  config.source_clk = UART_SCLK_DEFAULT;
  ESP_ERROR_CHECK(uart_param_config(uart, &config));
  ESP_ERROR_CHECK(uart_set_pin(uart, UART_PIN_NO_CHANGE, rx_pin, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
  ESP_ERROR_CHECK(uart_driver_install(uart, 2048, 0, 0, nullptr, 0));
}

void on_mqtt(void*, esp_event_base_t, int32_t event_id, void*) {
  if (event_id == MQTT_EVENT_CONNECTED) {
    g_mqtt_connected = true;
    ESP_LOGI(kTag, "MQTT connected to %s", CONFIG_ARMOR_MQTT_URI);
  } else if (event_id == MQTT_EVENT_DISCONNECTED) {
    g_mqtt_connected = false;
    ESP_LOGW(kTag, "MQTT disconnected; the broker will publish the last will");
  } else if (event_id == MQTT_EVENT_ERROR) {
    ESP_LOGW(kTag, "MQTT error (the broker refused the identity, or is not reachable)");
  }
}

void publish_health(bool online) {
  if (!g_mqtt_connected || !clock_is_set()) return;  // never publish a timestamp from an unset clock
  char topic[96];
  char payload[128];
  std::size_t length = 0;
  if (!armor::build_topic(CONFIG_ARMOR_NODE_ID, "health", topic, sizeof topic)) return;
  if (armor::build_health(CONFIG_ARMOR_NODE_ID, now_ms(), online, payload, sizeof payload, length) != armor::JsonResult::kOk) return;
  esp_mqtt_client_publish(g_mqtt, topic, payload, static_cast<int>(length), 1, 0);
}

void publish_telemetry() {
  static bool warned_about_light = false;
  static bool warned_about_radars = false;
  if (!g_mqtt_connected || !clock_is_set()) return;
  const float lux = armor::light_lux();
  if (!(lux >= 0.0f)) {  // unknown light: withhold, and say so once
    if (!warned_about_light) { ESP_LOGW(kTag, "telemetry withheld: no ambient-light reading (see the light-sensor log, or CONFIG_ARMOR_LUX_FALLBACK for a bench test)"); warned_about_light = true; }
    return;
  }
  warned_about_light = false;
  // Radars that are not reporting must not look like an empty, quiet perimeter: say nothing and let the server call the node silent.
  if (!g_tracks.any_fresh(monotonic_ms())) {
    if (!warned_about_radars) { ESP_LOGW(kTag, "telemetry withheld: no radar is reporting (see the radar statistics)"); warned_about_radars = true; }
    return;
  }
  warned_about_radars = false;
  armor::Track tracks[armor::kMaximumTracks];
  const std::size_t count = g_tracks.collect(tracks, monotonic_ms());
  char topic[96];
  char payload[1600];
  std::size_t length = 0;
  if (!armor::build_topic(CONFIG_ARMOR_NODE_ID, "telemetry", topic, sizeof topic)) return;
  if (armor::build_telemetry(CONFIG_ARMOR_NODE_ID, now_ms(), lux, tracks, count, payload, sizeof payload, length) != armor::JsonResult::kOk) return;
  esp_mqtt_client_publish(g_mqtt, topic, payload, static_cast<int>(length), 0, 0);
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
  static std::uint32_t previous_frames[3] = {0, 0, 0};
  for (std::size_t i = 0; i < kRadars; ++i) {
    const std::uint8_t radar = static_cast<std::uint8_t>(i + 1);
    const armor::RadarCounters& counters = g_health.counters(radar);
    const armor::RadarState state = g_health.state(radar, now);
    const std::uint32_t recent = counters.frames - previous_frames[i];
    previous_frames[i] = counters.frames;
    if (state == armor::RadarState::kReporting) ESP_LOGI(kTag, "radar %u: %s, %.1f frames/s, %u frames, %u bad, %u bytes", radar, armor::RadarHealth::describe(state), recent * 1000.0 / kStatsPeriodMs,
                                                        static_cast<unsigned>(counters.frames), static_cast<unsigned>(counters.bad_frames), static_cast<unsigned>(counters.bytes));
    else ESP_LOGW(kTag, "radar %u: %s (%u bytes, %u frames, %u bad)", radar, armor::RadarHealth::describe(state), static_cast<unsigned>(counters.bytes), static_cast<unsigned>(counters.frames), static_cast<unsigned>(counters.bad_frames));
  }
  const float lux = armor::light_lux();
  ESP_LOGI(kTag, "node %s: ethernet %s%s, mqtt %s, clock %s, light %s", CONFIG_ARMOR_NODE_ID, armor::ethernet_has_ip() ? "up " : "no address", armor::ethernet_ip_text(),
           g_mqtt_connected ? "connected" : "not connected", clock_is_set() ? "set" : "not set", lux >= 0.0f ? "known" : "unknown");
  if (lux >= 0.0f) ESP_LOGI(kTag, "ambient light %.1f lx", static_cast<double>(lux));
}

// Drain the UARTs through the framers, decode each LD2450 frame into the track set and publish.
void radar_task(void*) {
  std::uint8_t buffer[128];
  std::uint64_t last_publish = 0, last_stats = 0;
  for (;;) {
    for (std::size_t i = 0; i < kRadars; ++i) {
      const int read = uart_read_bytes(kUarts[i], buffer, sizeof buffer, 0);
      if (read <= 0) continue;
      const std::uint8_t sensor_id = static_cast<std::uint8_t>(i + 1);
      g_health.bytes_received(sensor_id, static_cast<std::size_t>(read));
      g_framers[i].feed(buffer, static_cast<std::size_t>(read), [sensor_id](const std::uint8_t* bytes, std::size_t length) {
        armor::ld2450::Frame frame;
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
    if (now - last_publish >= kTelemetryPeriodMs) { last_publish = now; publish_telemetry(); }
    if (now - last_stats >= kStatsPeriodMs) { last_stats = now; log_statistics(now); }
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

void heartbeat_task(void*) {
  for (;;) {
    publish_health(true);
    vTaskDelay(pdMS_TO_TICKS(kHeartbeatMs));
  }
}

// Waits for an address, then starts the clock and the broker connection once. The radars keep running meanwhile.
void network_task(void*) {
  while (!armor::ethernet_has_ip()) vTaskDelay(pdMS_TO_TICKS(500));
  esp_sntp_setoperatingmode(ESP_SNTP_OPMODE_POLL);
  esp_sntp_setservername(0, CONFIG_ARMOR_NTP_SERVER);
  esp_sntp_init();

  static char health_topic[96];
  static char will[128];
  std::size_t will_length = 0;
  if (!armor::build_topic(CONFIG_ARMOR_NODE_ID, "health", health_topic, sizeof health_topic)) vTaskDelete(nullptr);
  // The last will carries the node's time at connection; the server always applies an offline message, whatever its timestamp.
  while (!clock_is_set()) vTaskDelay(pdMS_TO_TICKS(500));
  if (armor::build_health(CONFIG_ARMOR_NODE_ID, now_ms(), false, will, sizeof will, will_length) != armor::JsonResult::kOk) vTaskDelete(nullptr);

  esp_mqtt_client_config_t config{};
  config.broker.address.uri = CONFIG_ARMOR_MQTT_URI;
  config.credentials.client_id = CONFIG_ARMOR_NODE_ID;
  config.credentials.username = CONFIG_ARMOR_MQTT_USERNAME;
  config.credentials.authentication.password = CONFIG_ARMOR_MQTT_PASSWORD;
  config.session.keepalive = CONFIG_ARMOR_HEARTBEAT_S * 2;
  config.session.last_will.topic = health_topic;
  config.session.last_will.msg = will;
  config.session.last_will.msg_len = static_cast<int>(will_length);
  config.session.last_will.qos = 1;
  config.session.last_will.retain = 0;
#ifdef ARMOR_MQTT_HAS_CA  // certs/ca.pem exists: main/CMakeLists.txt embeds it
  extern const char ca_pem_start[] asm("_binary_ca_pem_start");
  config.broker.verification.certificate = ca_pem_start;
#endif
  g_mqtt = esp_mqtt_client_init(&config);
  ESP_ERROR_CHECK(esp_mqtt_client_register_event(g_mqtt, MQTT_EVENT_ANY, on_mqtt, nullptr));
  ESP_ERROR_CHECK(esp_mqtt_client_start(g_mqtt));
  xTaskCreate(heartbeat_task, "heartbeat", 4096, nullptr, 4, nullptr);
  vTaskDelete(nullptr);
}
}  // namespace

extern "C" void app_main() {
  if (!armor::node_id_is_valid(CONFIG_ARMOR_NODE_ID) || std::strcmp(CONFIG_ARMOR_NODE_ID, kPlaceholderNodeId) == 0) {
    ESP_LOGE(kTag, "refusing startup: set a unique CONFIG_ARMOR_NODE_ID (lowercase letters, digits, '-' or '_')");
    return;
  }
  if (!pins_are_valid()) {
    ESP_LOGE(kTag, "refusing startup: a radar RX pin repeats, or clashes with the Ethernet or I2C pins");
    return;
  }
  ESP_LOGI(kTag, "node %s starting: %u radar(s) on RX GPIO %d/%d/%d", CONFIG_ARMOR_NODE_ID, static_cast<unsigned>(kRadars), kRxPins[0], kRxPins[1], kRxPins[2]);
  ESP_ERROR_CHECK(nvs_flash_init());
  ESP_ERROR_CHECK(esp_netif_init());
  ESP_ERROR_CHECK(esp_event_loop_create_default());
  for (std::size_t i = 0; i < kRadars; ++i) configure_radar_uart(kUarts[i], kRxPins[i]);
  // The radars run first: on the bench their statistics are the first thing to look at, with or without a network.
  xTaskCreate(radar_task, "radar", 6144, nullptr, 5, nullptr);

  if (!armor::light_sensor_start()) ESP_LOGE(kTag, "the light sensor could not be started: telemetry stays withheld unless CONFIG_ARMOR_LUX_FALLBACK is set");
  if (!armor::ethernet_start()) {
    ESP_LOGE(kTag, "Ethernet could not be started: the node stays local and publishes nothing");
    return;
  }
  xTaskCreate(network_task, "network", 6144, nullptr, 4, nullptr);
}
