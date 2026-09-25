// ARMOR-RADAR - the ambient-light reading of the node (VEML7700 over I2C).
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D). GPL-3.0-or-later.
#include "light_sensor.hpp"

#include <atomic>
extern "C" {
#include "sdkconfig.h"
#include "driver/i2c_master.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
}
#include "core/veml7700.hpp"

namespace armor {
namespace {
constexpr char kTag[] = "armor-light";
constexpr float kUnknown = -1.0f;
std::atomic<float> g_lux{
#if CONFIG_ARMOR_LUX_FALLBACK >= 0
    static_cast<float>(CONFIG_ARMOR_LUX_FALLBACK)
#else
    kUnknown
#endif
};

#if CONFIG_ARMOR_LIGHT_VEML7700
i2c_master_dev_handle_t g_device = nullptr;

bool write_register(std::uint8_t reg, std::uint16_t value) {
  const std::uint8_t bytes[3] = {reg, static_cast<std::uint8_t>(value & 0xFF), static_cast<std::uint8_t>(value >> 8)};
  return i2c_master_transmit(g_device, bytes, sizeof bytes, 100) == ESP_OK;
}

bool read_register(std::uint8_t reg, std::uint16_t& value) {
  std::uint8_t raw[2]{};
  if (i2c_master_transmit_receive(g_device, &reg, 1, raw, sizeof raw, 100) != ESP_OK) return false;
  value = static_cast<std::uint16_t>(raw[0] | (raw[1] << 8));
  return true;
}

void light_task(void*) {
  using namespace veml7700;
  std::size_t step = kStartStep;
  bool need_settle = true;
  int failures = 0;
  for (;;) {
    if (need_settle) {
      if (!write_register(kRegisterConfig, config_word(kLadder[step]))) {
        if (++failures == 3) ESP_LOGE(kTag, "the VEML7700 does not answer (SDA %d, SCL %d, address 0x%02x)", CONFIG_ARMOR_I2C_SDA, CONFIG_ARMOR_I2C_SCL, kAddress);
        g_lux.store(
#if CONFIG_ARMOR_LUX_FALLBACK >= 0
            static_cast<float>(CONFIG_ARMOR_LUX_FALLBACK)
#else
            kUnknown
#endif
        );
        vTaskDelay(pdMS_TO_TICKS(2000));
        continue;
      }
      vTaskDelay(pdMS_TO_TICKS(settle_ms(kLadder[step])));
      need_settle = false;
    }
    std::uint16_t counts = 0;
    if (!read_register(kRegisterAls, counts)) { need_settle = true; vTaskDelay(pdMS_TO_TICKS(500)); continue; }
    failures = 0;
    const std::size_t next = next_step(step, counts);
    if (next != step) {  // the setting does not fit this light: change it and read again once it settled
      ESP_LOGD(kTag, "range %u -> %u (counts %u)", static_cast<unsigned>(step), static_cast<unsigned>(next), static_cast<unsigned>(counts));
      step = next;
      need_settle = true;
      continue;
    }
    float lux = lux_from_counts(counts, kLadder[step]);
    if (lux > 200000.0f) lux = 200000.0f;
    g_lux.store(lux);
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}
#endif
}  // namespace

bool light_sensor_start() {
#if CONFIG_ARMOR_LIGHT_VEML7700
  i2c_master_bus_config_t bus_config{};
  bus_config.i2c_port = I2C_NUM_0;
  bus_config.sda_io_num = static_cast<gpio_num_t>(CONFIG_ARMOR_I2C_SDA);
  bus_config.scl_io_num = static_cast<gpio_num_t>(CONFIG_ARMOR_I2C_SCL);
  bus_config.clk_source = I2C_CLK_SRC_DEFAULT;
  bus_config.glitch_ignore_cnt = 7;
  bus_config.flags.enable_internal_pullup = true;  // the breakout boards carry their own pull-ups; these only help
  i2c_master_bus_handle_t bus = nullptr;
  if (i2c_new_master_bus(&bus_config, &bus) != ESP_OK) { ESP_LOGE(kTag, "the I2C bus could not be initialised"); return false; }
  i2c_device_config_t device_config{};
  device_config.dev_addr_length = I2C_ADDR_BIT_LEN_7;
  device_config.device_address = veml7700::kAddress;
  device_config.scl_speed_hz = 100000;
  if (i2c_master_bus_add_device(bus, &device_config, &g_device) != ESP_OK) { ESP_LOGE(kTag, "the VEML7700 could not be added to the I2C bus"); return false; }
  xTaskCreate(light_task, "light", 3072, nullptr, 3, nullptr);
  return true;
#else
#if CONFIG_ARMOR_LUX_FALLBACK >= 0
  ESP_LOGW(kTag, "BENCH MODE: no light sensor, every message will carry the fixed value %d lx (CONFIG_ARMOR_LUX_FALLBACK)", CONFIG_ARMOR_LUX_FALLBACK);
#else
  ESP_LOGW(kTag, "no light sensor configured: telemetry is withheld (set CONFIG_ARMOR_LUX_FALLBACK for a bench test without the sensor)");
#endif
  return true;
#endif
}

float light_lux() { return g_lux.load(); }

}  // namespace armor
