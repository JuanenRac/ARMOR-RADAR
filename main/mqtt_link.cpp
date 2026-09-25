// ARMOR-RADAR - the node's link to the broker: the clock, the health message, the radar telemetry and the mapped pins' topics.
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D). GPL-3.0-or-later.
//
// What it does, as before: it publishes an "online" health message every heartbeat, with an MQTT last will that says "offline", and
// telemetry at most five times a second once the network, the clock, an ambient-light value and at least one radar that is really
// reporting are all there. It never publishes an empty "all clear" from radars that are silent.
#include "mqtt_link.hpp"

#include <atomic>
#include <ctime>
extern "C" {
#include <sys/time.h>
#include "esp_log.h"
#include "esp_sntp.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mqtt_client.h"
}
#include "core/gpio_logic.hpp"
#include "node_store.hpp"
#include "esp_app_desc.h"
#include "core/telemetry_json.hpp"
#include "gpio_manager.hpp"
#include "light_sensor.hpp"
#include "network.hpp"
#include "radar_manager.hpp"

namespace armor::mqtt_link {
namespace {
constexpr char kTag[] = "armor-mqtt";

config::Settings g_settings;
esp_mqtt_client_handle_t g_client = nullptr;
std::atomic<bool> g_connected{false};
std::atomic<bool> g_enabled{false};
std::atomic<std::uint32_t> g_published{0};
std::atomic<int> g_withheld{0};   // 0 nothing, 1 light, 2 radars

void on_mqtt(void*, esp_event_base_t, int32_t event_id, void* data) {
  auto* event = static_cast<esp_mqtt_event_handle_t>(data);
  switch (event_id) {
    case MQTT_EVENT_CONNECTED: {
      g_connected = true;
      ESP_LOGI(kTag, "MQTT connected to %s", g_settings.mqtt.uri.c_str());
      const std::string filter = gpio::command_filter(g_settings.node_id);
      if (!filter.empty()) esp_mqtt_client_subscribe(g_client, filter.c_str(), 1);
      pins::publish_all();
      publish_info();
      break;
    }
    case MQTT_EVENT_DISCONNECTED:
      g_connected = false;
      ESP_LOGW(kTag, "MQTT disconnected; the broker will publish the last will");
      break;
    case MQTT_EVENT_ERROR: ESP_LOGW(kTag, "MQTT error (the broker refused the identity, or is not reachable)"); break;
    case MQTT_EVENT_DATA: {
      if (event->data_len != event->total_data_len || event->current_data_offset != 0) break;  // a command is a few bytes: never a fragment
      const std::string name = gpio::pin_of_command_topic(g_settings.node_id, std::string_view(event->topic, static_cast<std::size_t>(event->topic_len)));
      if (name.empty()) break;
      const pins::Outcome outcome = pins::command(name, std::string_view(event->data, static_cast<std::size_t>(event->data_len)));
      if (outcome != pins::Outcome::kOk) ESP_LOGW(kTag, "command for \"%s\" ignored (%s)", name.c_str(), outcome == pins::Outcome::kUnknownPin ? "no such pin" : "not understood");
      break;
    }
    default: break;
  }
}

void publish_health(bool online) {
  if (!g_connected || !clock_is_set()) return;  // never publish a timestamp from an unset clock
  char topic[96];
  char payload[128];
  std::size_t length = 0;
  if (!armor::build_topic(g_settings.node_id, "health", topic, sizeof topic)) return;
  if (armor::build_health(g_settings.node_id, wall_clock_ms(), online, payload, sizeof payload, length) != armor::JsonResult::kOk) return;
  esp_mqtt_client_publish(g_client, topic, payload, static_cast<int>(length), 1, 0);
}

// Tells the server where this node's own web panel is, so Studio can link to it. Sent when the node connects and every minute.
void publish_info() {
  if (!g_connected || !clock_is_set()) return;
  const network::Status net = network::status();
  if (!net.has_ip) return;
  char topic[96];
  char payload[256];
  std::size_t length = 0;
  if (!armor::build_topic(g_settings.node_id, "info", topic, sizeof topic)) return;
  const config::Settings current = store::settings();   // the name may have been changed in the panel since the start
  if (armor::build_info(g_settings.node_id, wall_clock_ms(), current.node_name, esp_app_get_description()->version, net.ip, 80, payload, sizeof payload, length) != armor::JsonResult::kOk) return;
  esp_mqtt_client_publish(g_client, topic, payload, static_cast<int>(length), 1, 0);
}

void publish_telemetry() {
  static bool warned_about_light = false;
  static bool warned_about_radars = false;
  if (!g_connected || !clock_is_set()) return;
  const float lux = armor::light_lux();
  if (!(lux >= 0.0f)) {  // unknown light: withhold, and say so once
    g_withheld = 1;
    if (!warned_about_light) { ESP_LOGW(kTag, "telemetry withheld: no ambient-light reading (see the light-sensor log, or set a light fallback in the settings for a bench test)"); warned_about_light = true; }
    return;
  }
  warned_about_light = false;
  // Radars that are not reporting must not look like an empty, quiet perimeter: say nothing and let the server call the node silent.
  if (!radar::any_fresh()) {
    g_withheld = 2;
    if (!warned_about_radars) { ESP_LOGW(kTag, "telemetry withheld: no radar is reporting (see the radar statistics)"); warned_about_radars = true; }
    return;
  }
  warned_about_radars = false;
  g_withheld = 0;
  armor::Track tracks[armor::kMaximumTracks];
  const std::size_t count = radar::collect_tracks(tracks);
  char topic[96];
  char payload[1600];
  std::size_t length = 0;
  if (!armor::build_topic(g_settings.node_id, "telemetry", topic, sizeof topic)) return;
  if (armor::build_telemetry(g_settings.node_id, wall_clock_ms(), lux, tracks, count, payload, sizeof payload, length) != armor::JsonResult::kOk) return;
  if (esp_mqtt_client_publish(g_client, topic, payload, static_cast<int>(length), 0, 0) >= 0) ++g_published;
}

void heartbeat_task(void*) {
  std::uint64_t last_info_ms = 0;
  for (;;) {
    publish_health(true);
    const std::uint64_t now = static_cast<std::uint64_t>(esp_timer_get_time()) / 1000ULL;
    if (now - last_info_ms >= 60000) { last_info_ms = now; publish_info(); }
    vTaskDelay(pdMS_TO_TICKS(g_settings.mqtt.heartbeat_s * 1000));
  }
}

void telemetry_task(void*) {
  for (;;) {
    publish_telemetry();
    vTaskDelay(pdMS_TO_TICKS(g_settings.mqtt.telemetry_ms));
  }
}

// Waits for an address, then starts the clock and the broker connection once. The radars and the pins keep running meanwhile.
void link_task(void*) {
  while (!network::has_ip()) vTaskDelay(pdMS_TO_TICKS(500));
  esp_sntp_setoperatingmode(ESP_SNTP_OPMODE_POLL);
  esp_sntp_setservername(0, g_settings.mqtt.ntp.c_str());
  esp_sntp_init();

  static char health_topic[96];
  static char will[128];
  std::size_t will_length = 0;
  if (!armor::build_topic(g_settings.node_id, "health", health_topic, sizeof health_topic)) vTaskDelete(nullptr);
  // The last will carries the node's time at connection; the server always applies an offline message, whatever its timestamp.
  while (!clock_is_set()) vTaskDelay(pdMS_TO_TICKS(500));
  if (armor::build_health(g_settings.node_id, wall_clock_ms(), false, will, sizeof will, will_length) != armor::JsonResult::kOk) vTaskDelete(nullptr);

  esp_mqtt_client_config_t config{};
  config.broker.address.uri = g_settings.mqtt.uri.c_str();
  config.credentials.client_id = g_settings.node_id.c_str();
  config.credentials.username = g_settings.mqtt.username.c_str();
  config.credentials.authentication.password = g_settings.mqtt.password.c_str();
  config.session.keepalive = g_settings.mqtt.heartbeat_s * 2;
  config.session.last_will.topic = health_topic;
  config.session.last_will.msg = will;
  config.session.last_will.msg_len = static_cast<int>(will_length);
  config.session.last_will.qos = 1;
  config.session.last_will.retain = 0;
#ifdef ARMOR_MQTT_HAS_CA  // certs/ca.pem exists: main/CMakeLists.txt embeds it
  extern const char ca_pem_start[] asm("_binary_ca_pem_start");
  config.broker.verification.certificate = ca_pem_start;
#endif
  g_client = esp_mqtt_client_init(&config);
  ESP_ERROR_CHECK(esp_mqtt_client_register_event(g_client, MQTT_EVENT_ANY, on_mqtt, nullptr));
  ESP_ERROR_CHECK(esp_mqtt_client_start(g_client));
  xTaskCreate(heartbeat_task, "heartbeat", 4096, nullptr, 4, nullptr);
  xTaskCreate(telemetry_task, "telemetry", 5120, nullptr, 4, nullptr);
  vTaskDelete(nullptr);
}
}  // namespace

void start(const config::Settings& settings) {
  g_settings = settings;
  if (!settings.mqtt.enabled || settings.mqtt.uri.empty()) {
    ESP_LOGW(kTag, "no broker is set up: the node serves its panel and runs its radars and pins, and sends nothing");
    return;
  }
  g_enabled = true;
  xTaskCreate(link_task, "mqtt-link", 6144, nullptr, 4, nullptr);
}

bool connected() { return g_connected; }
bool clock_is_set() { return std::time(nullptr) > 1700000000; }

// The server ignores a message older than the last one it accepted from a node, so timestamps must be wall-clock time that survives a
// reboot, not uptime.
std::uint64_t wall_clock_ms() {
  timeval tv{};
  gettimeofday(&tv, nullptr);
  return static_cast<std::uint64_t>(tv.tv_sec) * 1000ULL + static_cast<std::uint64_t>(tv.tv_usec) / 1000ULL;
}

void publish(const std::string& topic, const std::string& payload) {
  if (!g_connected || g_client == nullptr) return;
  if (esp_mqtt_client_publish(g_client, topic.c_str(), payload.c_str(), static_cast<int>(payload.size()), 0, 0) >= 0) ++g_published;
}

Status status() {
  Status s;
  s.enabled = g_enabled;
  s.connected = g_connected;
  s.clock_set = clock_is_set();
  s.published = g_published;
  s.withheld = g_withheld == 1 ? "light" : g_withheld == 2 ? "radars" : "";
  return s;
}

}  // namespace armor::mqtt_link
