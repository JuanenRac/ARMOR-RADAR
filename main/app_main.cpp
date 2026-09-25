/*
 * ARMOR-RADAR - ESP32-S3 field node.
 * Copyright (C) 2026 JuanenRac (Electro Hobby 3D). GPL-3.0-or-later.
 *
 * NOT COMPILED IN THIS REPOSITORY'S TEST ENVIRONMENT: the hardware-independent
 * core under main/core is built and tested on a computer (tests/), but this file
 * needs ESP-IDF 5.x and a board. Build it with `idf.py build` before use.
 *
 * What it does today:
 *   - refuses to start with an invalid or placeholder node identity;
 *   - configures three independent radar UARTs and refuses duplicate RX pins;
 *   - runs each radar's bytes through the resynchronising framer and decodes the
 *     HLK-LD2450 target frames (core/ld2450.hpp, from the Hi-Link manual V1.00);
 *   - publishes an "online" health message every CONFIG_ARMOR_HEARTBEAT_S, with an
 *     MQTT last will that announces "offline" if the node disappears;
 *   - publishes telemetry (at most five times a second) once the network, the clock
 *     and an ambient-light reading are all available.
 *
 * What it deliberately does not do yet: there is no light-sensor driver, so the lux
 * value is unknown and telemetry is withheld rather than invented; a module that is
 * an LD2461 is not decoded (the manual available is for the LD2450 only); and the
 * Ethernet PHY, TLS trust anchor and sensor drivers are board-specific.
 */
#include <array>
#include <cstdio>
#include <cstring>
#include <ctime>
extern "C" {
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
#include "core/frame_framer.hpp"
#include "core/ld2450.hpp"
#include "core/node_id.hpp"
#include "core/telemetry_json.hpp"

namespace {
constexpr char kTag[] = "armor-radar";
constexpr char kPlaceholderNodeId[] = "unconfigured-node";
constexpr std::array<uart_port_t, 3> kUarts{UART_NUM_0, UART_NUM_1, UART_NUM_2};
constexpr std::array<int, 3> kRxPins{CONFIG_ARMOR_RADAR_1_RX, CONFIG_ARMOR_RADAR_2_RX, CONFIG_ARMOR_RADAR_3_RX};
constexpr int kHeartbeatMs = CONFIG_ARMOR_HEARTBEAT_S * 1000;

esp_mqtt_client_handle_t g_mqtt = nullptr;
volatile bool g_mqtt_connected = false;
// One framer per radar, all speaking the HLK-LD2450 report protocol.
armor::FrameFramer g_framers[3] = {armor::FrameFramer(armor::ld2450::protocol()), armor::FrameFramer(armor::ld2450::protocol()),
                                   armor::FrameFramer(armor::ld2450::protocol())};
// The newest tracks of the three radars. Only radar_task touches it.
armor::TrackSet g_tracks;
// Ambient light in lux. There is no light-sensor driver yet, so it stays unknown (negative) and
// telemetry is withheld: a made-up value would mislead the day/night decisions of the server.
constexpr float kLuxUnknown = -1.0f;
float g_lux = kLuxUnknown;
constexpr std::uint64_t kTelemetryPeriodMs = 200;

bool unique_uart_pins() { return kRxPins[0] != kRxPins[1] && kRxPins[0] != kRxPins[2] && kRxPins[1] != kRxPins[2]; }

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
  const uart_config_t config{.baud_rate = 256000, .data_bits = UART_DATA_8_BITS, .parity = UART_PARITY_DISABLE, .stop_bits = UART_STOP_BITS_1,
                             .flow_ctrl = UART_HW_FLOWCTRL_DISABLE, .rx_flow_ctrl_thresh = 0, .source_clk = UART_SCLK_DEFAULT};
  ESP_ERROR_CHECK(uart_param_config(uart, &config));
  ESP_ERROR_CHECK(uart_set_pin(uart, UART_PIN_NO_CHANGE, rx_pin, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
  ESP_ERROR_CHECK(uart_driver_install(uart, 2048, 0, 0, nullptr, 0));
}

void on_mqtt(void*, esp_event_base_t, int32_t event_id, void*) {
  if (event_id == MQTT_EVENT_CONNECTED) {
    g_mqtt_connected = true;
    ESP_LOGI(kTag, "MQTT connected");
  } else if (event_id == MQTT_EVENT_DISCONNECTED) {
    g_mqtt_connected = false;
    ESP_LOGW(kTag, "MQTT disconnected; the broker will publish the last will");
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
  if (!g_mqtt_connected || !clock_is_set()) return;
  if (!(g_lux >= 0.0f)) {  // unknown light: withhold, and say so once
    if (!warned_about_light) { ESP_LOGW(kTag, "telemetry withheld: no ambient-light reading (the light-sensor driver is not implemented yet)"); warned_about_light = true; }
    return;
  }
  armor::Track tracks[armor::kMaximumTracks];
  const std::size_t count = g_tracks.collect(tracks, monotonic_ms());
  char topic[96];
  char payload[1600];
  std::size_t length = 0;
  if (!armor::build_topic(CONFIG_ARMOR_NODE_ID, "telemetry", topic, sizeof topic)) return;
  if (armor::build_telemetry(CONFIG_ARMOR_NODE_ID, now_ms(), g_lux, tracks, count, payload, sizeof payload, length) != armor::JsonResult::kOk) return;
  esp_mqtt_client_publish(g_mqtt, topic, payload, static_cast<int>(length), 0, 0);
}

// Drain the UARTs through the framers, decode each LD2450 frame into the track set and publish.
void radar_task(void*) {
  std::uint8_t buffer[128];
  std::uint64_t last_publish = 0;
  for (;;) {
    for (std::size_t i = 0; i < kUarts.size(); ++i) {
      const int read = uart_read_bytes(kUarts[i], buffer, sizeof buffer, 0);
      if (read <= 0) continue;
      const std::uint8_t sensor_id = static_cast<std::uint8_t>(i + 1);
      g_framers[i].feed(buffer, static_cast<std::size_t>(read), [sensor_id](const std::uint8_t* bytes, std::size_t length) {
        armor::ld2450::Frame frame;
        if (!armor::ld2450::decode_frame(bytes, length, frame)) return;
        armor::Track tracks[armor::ld2450::kTargetsPerFrame];
        const std::size_t count = armor::ld2450::to_tracks(sensor_id, frame, tracks);
        g_tracks.update(sensor_id, tracks, count, monotonic_ms());
      });
    }
    const std::uint64_t now = monotonic_ms();
    if (now - last_publish >= kTelemetryPeriodMs) { last_publish = now; publish_telemetry(); }
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

void heartbeat_task(void*) {
  for (;;) {
    publish_health(true);
    vTaskDelay(pdMS_TO_TICKS(kHeartbeatMs));
  }
}

// Board-specific: bring up the Ethernet PHY and wait for an address. Returns false until implemented.
bool network_start() { return false; }
}  // namespace

extern "C" void app_main() {
  if (!armor::node_id_is_valid(CONFIG_ARMOR_NODE_ID) || std::strcmp(CONFIG_ARMOR_NODE_ID, kPlaceholderNodeId) == 0) {
    ESP_LOGE(kTag, "refusing startup: set a unique CONFIG_ARMOR_NODE_ID (lowercase letters, digits, '-' or '_')");
    return;
  }
  if (!unique_uart_pins()) {
    ESP_LOGE(kTag, "refusing startup: duplicate radar UART RX GPIO configuration");
    return;
  }
  ESP_ERROR_CHECK(nvs_flash_init());
  ESP_ERROR_CHECK(esp_netif_init());
  ESP_ERROR_CHECK(esp_event_loop_create_default());
  for (std::size_t i = 0; i < kUarts.size(); ++i) configure_radar_uart(kUarts[i], kRxPins[i]);

  if (!network_start()) {
    ESP_LOGE(kTag, "no network driver for this board yet (Ethernet PHY pins and PoE hardware are pending); the node stays local and publishes nothing");
    xTaskCreate(radar_task, "radar", 4096, nullptr, 5, nullptr);
    return;
  }
  esp_sntp_setoperatingmode(ESP_SNTP_OPMODE_POLL);
  esp_sntp_setservername(0, CONFIG_ARMOR_NTP_SERVER);
  esp_sntp_init();

  static char health_topic[96];
  static char will[128];
  std::size_t will_length = 0;
  if (!armor::build_topic(CONFIG_ARMOR_NODE_ID, "health", health_topic, sizeof health_topic)) return;
  // The last will carries the node's time at connection; the server always applies an offline message, whatever its timestamp.
  if (armor::build_health(CONFIG_ARMOR_NODE_ID, clock_is_set() ? now_ms() : 0, false, will, sizeof will, will_length) != armor::JsonResult::kOk) return;

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
  g_mqtt = esp_mqtt_client_init(&config);
  ESP_ERROR_CHECK(esp_mqtt_client_register_event(g_mqtt, MQTT_EVENT_ANY, on_mqtt, nullptr));
  ESP_ERROR_CHECK(esp_mqtt_client_start(g_mqtt));

  xTaskCreate(radar_task, "radar", 4096, nullptr, 5, nullptr);
  xTaskCreate(heartbeat_task, "heartbeat", 4096, nullptr, 4, nullptr);
  ESP_LOGW(kTag, "the light-sensor driver and the TLS trust anchor still require board-specific completion before deployment");
}
