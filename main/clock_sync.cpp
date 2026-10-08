// ARMOR-RADAR - see clock_sync.hpp.
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D). GPL-3.0-or-later.
#include "clock_sync.hpp"

#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <sys/time.h>
extern "C" {
#include "esp_log.h"
#include "esp_sntp.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
}
#include "core/civil_time.hpp"
#include "network.hpp"

namespace armor::clocksync {
namespace {
constexpr const char* kTag = "armor-clock";
constexpr std::time_t kFirstRealDate = 1700000000;   // Nov 2023: anything earlier is a clock nobody has set
constexpr std::time_t kLastSaneDate = 4102444800;    // 2100

std::string g_zone = "UTC0";
std::string g_server;
bool g_ntp = false;

// SNTP keeps the name it is given (it does not copy it), so it lives here.
void sync_task(void*) {
  while (!network::has_ip()) vTaskDelay(pdMS_TO_TICKS(500));
  esp_sntp_setoperatingmode(ESP_SNTP_OPMODE_POLL);
  esp_sntp_setservername(0, g_server.c_str());
  esp_sntp_init();
  ESP_LOGI(kTag, "asking %s for the time", g_server.c_str());
  vTaskDelete(nullptr);
}
}  // namespace

void start(const config::Settings& settings) {
  g_zone = settings.time.zone;
  g_server = settings.time.ntp;
  g_ntp = settings.time.ntp_enabled;
  setenv("TZ", g_zone.c_str(), 1);
  tzset();
  if (g_ntp) xTaskCreate(sync_task, "clock-sync", 3072, nullptr, 2, nullptr);
  else ESP_LOGW(kTag, "no time server: the time has to be set from the panel after each restart");
}

bool is_set() { return std::time(nullptr) > kFirstRealDate; }

bool set_unix(std::int64_t seconds) {
  if (seconds < static_cast<std::int64_t>(kFirstRealDate) || seconds > static_cast<std::int64_t>(kLastSaneDate)) return false;
  timeval tv{};
  tv.tv_sec = static_cast<std::time_t>(seconds);
  return settimeofday(&tv, nullptr) == 0;
}

Info info() {
  Info out;
  out.zone = g_zone;
  const std::time_t now = std::time(nullptr);
  out.set = now > kFirstRealDate;
  out.synced = g_ntp && esp_sntp_get_sync_status() == SNTP_SYNC_STATUS_COMPLETED;
  out.epoch = static_cast<std::int64_t>(now);
  if (out.set) {
    std::tm local{};
    localtime_r(&now, &local);
    char text[24];
    std::strftime(text, sizeof text, "%Y-%m-%d %H:%M:%S", &local);
    out.local = text;
    out.utc_offset_min = civil::utc_offset_minutes(static_cast<std::int64_t>(now), local.tm_year + 1900, local.tm_mon + 1, local.tm_mday, local.tm_hour, local.tm_min, local.tm_sec);
  }
  return out;
}

}  // namespace armor::clocksync
