// ARMOR-RADAR - the pins the operator mapped in the panel.
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D). GPL-3.0-or-later.
#include "gpio_manager.hpp"

#include <algorithm>
#include <memory>
#include <mutex>
extern "C" {
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
}
#include "core/gpio_logic.hpp"
#include "entropy.hpp"

namespace armor::pins {
namespace {
constexpr char kTag[] = "armor-pins";
constexpr unsigned kPwmBits = 10;
constexpr int kDefaultPulseMs = 500;

struct Pin {
  config::MappedPin cfg;
  bool on = false;                 // logical state
  int percent = 0;                 // PWM level
  int last_nonzero_percent = 100;
  bool fell_back = false;
  double value = 0.0;
  bool has_value = false;
  gpio::Debouncer debounce;
  std::uint64_t last_adc_ms = 0;
  ledc_channel_t channel = LEDC_CHANNEL_0;
  adc_channel_t adc_channel = ADC_CHANNEL_0;
  esp_timer_handle_t pulse = nullptr;
};

std::mutex g_lock;
std::vector<std::unique_ptr<Pin>> g_pins;
std::string g_node_id;
Publisher g_publish;
LinkProbe g_link;
gpio::LinkWatch g_watch;
adc_oneshot_unit_handle_t g_adc = nullptr;
adc_cali_handle_t g_cali = nullptr;

std::uint64_t now_ms() { return static_cast<std::uint64_t>(esp_timer_get_time()) / 1000ULL; }

std::string payload_of(const Pin& pin) {
  const std::string field = config::report_of(pin.cfg);
  switch (pin.cfg.mode) {
    case config::PinMode::kInput: case config::PinMode::kOutput: return gpio::report_boolean(field, pin.on);
    case config::PinMode::kPwm: return gpio::report_number(field, pin.percent);
    case config::PinMode::kAdc: return pin.has_value ? gpio::report_number(field, pin.value) : "";
    case config::PinMode::kDisabled: break;
  }
  return "";
}

void publish(const Pin& pin) {
  if (!g_publish) return;
  const std::string payload = payload_of(pin), topic = gpio::topic(g_node_id, pin.cfg.name, "state");
  if (!payload.empty() && !topic.empty()) g_publish(topic, payload);
}

// ---- outputs ---------------------------------------------------------------------------------------------------------------------

void write_output(Pin& pin, bool on) {
  pin.on = on;
  if (pin.cfg.mode == config::PinMode::kPwm) {
    pin.percent = on ? pin.last_nonzero_percent : 0;
    const std::uint32_t duty = gpio::pwm_duty(pin.percent, kPwmBits);
    ledc_set_duty(LEDC_LOW_SPEED_MODE, pin.channel, duty);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, pin.channel);
  } else {
    gpio_set_level(static_cast<gpio_num_t>(pin.cfg.gpio), gpio::level_for(on, pin.cfg.invert) ? 1 : 0);
  }
}

void write_level(Pin& pin, int percent) {
  pin.percent = percent;
  pin.on = percent > 0;
  if (percent > 0) pin.last_nonzero_percent = percent;
  ledc_set_duty(LEDC_LOW_SPEED_MODE, pin.channel, gpio::pwm_duty(percent, kPwmBits));
  ledc_update_duty(LEDC_LOW_SPEED_MODE, pin.channel);
}

void pulse_ended(void* argument) {
  std::lock_guard<std::mutex> guard(g_lock);
  Pin* pin = static_cast<Pin*>(argument);
  write_output(*pin, false);
  publish(*pin);
}

Pin* find(const std::string& name) {
  for (auto& pin : g_pins) if (pin->cfg.name == name) return pin.get();
  return nullptr;
}

// ---- set-up ------------------------------------------------------------------------------------------------------------------------

void setup_adc() {
  adc_oneshot_unit_init_cfg_t unit{};
  unit.unit_id = ADC_UNIT_1;
  if (adc_oneshot_new_unit(&unit, &g_adc) != ESP_OK) { ESP_LOGE(kTag, "the analogue converter could not be started: analogue pins will not report"); g_adc = nullptr; return; }
  adc_cali_curve_fitting_config_t cali{};
  cali.unit_id = ADC_UNIT_1;
  cali.atten = ADC_ATTEN_DB_12;
  cali.bitwidth = ADC_BITWIDTH_DEFAULT;
  if (adc_cali_create_scheme_curve_fitting(&cali, &g_cali) != ESP_OK) { ESP_LOGW(kTag, "the converter has no calibration on this chip: readings are approximate"); g_cali = nullptr; }
}

void setup_pin(Pin& pin, const std::vector<int>& frequencies) {
  const config::MappedPin& cfg = pin.cfg;
  const gpio_num_t number = static_cast<gpio_num_t>(cfg.gpio);
  switch (cfg.mode) {
    case config::PinMode::kInput: {
      gpio_config_t io{};
      io.pin_bit_mask = 1ULL << cfg.gpio;
      io.mode = GPIO_MODE_INPUT;
      io.pull_up_en = cfg.pull == config::Pull::kUp ? GPIO_PULLUP_ENABLE : GPIO_PULLUP_DISABLE;
      io.pull_down_en = cfg.pull == config::Pull::kDown ? GPIO_PULLDOWN_ENABLE : GPIO_PULLDOWN_DISABLE;
      gpio_config(&io);
      pin.on = gpio::logical_state(gpio_get_level(number) != 0, cfg.invert);
      pin.debounce.reset(pin.on, now_ms());
      break;
    }
    case config::PinMode::kOutput: {
      // The level is written before the pin becomes an output, so it never glitches through the wrong state.
      gpio_reset_pin(number);
      gpio_set_level(number, gpio::level_for(cfg.initial_on, cfg.invert) ? 1 : 0);
      gpio_set_direction(number, GPIO_MODE_INPUT_OUTPUT);
      pin.on = cfg.initial_on;
      esp_timer_create_args_t timer{};
      timer.callback = &pulse_ended;
      timer.arg = &pin;
      timer.name = "pin-pulse";
      esp_timer_create(&timer, &pin.pulse);
      break;
    }
    case config::PinMode::kPwm: {
      const std::size_t slot = static_cast<std::size_t>(std::find(frequencies.begin(), frequencies.end(), cfg.freq_hz) - frequencies.begin());
      ledc_timer_config_t timer{};
      timer.speed_mode = LEDC_LOW_SPEED_MODE;
      timer.duty_resolution = static_cast<ledc_timer_bit_t>(kPwmBits);
      timer.timer_num = static_cast<ledc_timer_t>(slot);
      timer.freq_hz = static_cast<std::uint32_t>(cfg.freq_hz);
      timer.clk_cfg = LEDC_AUTO_CLK;
      ledc_timer_config(&timer);
      ledc_channel_config_t channel{};
      channel.gpio_num = cfg.gpio;
      channel.speed_mode = LEDC_LOW_SPEED_MODE;
      channel.channel = pin.channel;
      channel.timer_sel = static_cast<ledc_timer_t>(slot);
      channel.duty = gpio::pwm_duty(cfg.initial_on ? 100 : 0, kPwmBits);
      channel.flags.output_invert = cfg.invert ? 1 : 0;
      ledc_channel_config(&channel);
      pin.on = cfg.initial_on;
      pin.percent = cfg.initial_on ? 100 : 0;
      break;
    }
    case config::PinMode::kAdc: {
      if (g_adc == nullptr) break;
      adc_unit_t unit;
      if (adc_oneshot_io_to_channel(cfg.gpio, &unit, &pin.adc_channel) != ESP_OK || unit != ADC_UNIT_1) { ESP_LOGE(kTag, "GPIO %d is not an ADC1 input", cfg.gpio); break; }
      adc_oneshot_chan_cfg_t channel{};
      channel.atten = ADC_ATTEN_DB_12;
      channel.bitwidth = ADC_BITWIDTH_DEFAULT;
      adc_oneshot_config_channel(g_adc, pin.adc_channel, &channel);
      break;
    }
    case config::PinMode::kDisabled: break;
  }
}

bool read_millivolts(Pin& pin, int& mv) {
  if (g_adc == nullptr) return false;
  std::lock_guard<std::mutex> adc(adc_lock());  // the random generator borrows the converter's noise, so they never overlap
  int raw = 0;
  if (adc_oneshot_read(g_adc, pin.adc_channel, &raw) != ESP_OK) return false;
  if (g_cali != nullptr && adc_cali_raw_to_voltage(g_cali, raw, &mv) == ESP_OK) return true;
  mv = raw * 3100 / 4095;  // roughly 0 to 3.1 V at 12 dB without calibration
  return true;
}

// ---- the task ----------------------------------------------------------------------------------------------------------------------

void poll_task(void*) {
  std::uint64_t last_link_check = 0;
  for (;;) {
    const std::uint64_t now = now_ms();
    {
      std::lock_guard<std::mutex> guard(g_lock);
      for (auto& owned : g_pins) {
        Pin& pin = *owned;
        if (pin.cfg.mode == config::PinMode::kInput) {
          const bool raw = gpio::logical_state(gpio_get_level(static_cast<gpio_num_t>(pin.cfg.gpio)) != 0, pin.cfg.invert);
          if (pin.debounce.update(raw, now, static_cast<std::uint32_t>(pin.cfg.debounce_ms))) { pin.on = pin.debounce.level(); publish(pin); }
        } else if (pin.cfg.mode == config::PinMode::kAdc && now - pin.last_adc_ms >= static_cast<std::uint64_t>(pin.cfg.period_s) * 1000ULL) {
          pin.last_adc_ms = now;
          int mv = 0;
          if (read_millivolts(pin, mv)) { pin.value = gpio::analogue_value(mv, pin.cfg); pin.has_value = true; publish(pin); }
        }
      }
      if (now - last_link_check >= 1000) {
        last_link_check = now;
        g_watch.update(g_link ? g_link() : true, now);
        for (auto& owned : g_pins) {
          Pin& pin = *owned;
          if (pin.cfg.mode != config::PinMode::kOutput) continue;
          if (!g_watch.lost()) { pin.fell_back = false; continue; }
          if (!pin.fell_back && gpio::must_fall_back(pin.cfg, g_watch.lost_for_ms(now))) {
            pin.fell_back = true;
            write_output(pin, pin.cfg.safe == config::SafeState::kOn);
            ESP_LOGW(kTag, "the broker has been unreachable for %d s: \"%s\" falls back to %s", pin.cfg.link_timeout_s, pin.cfg.name.c_str(), config::to_text(pin.cfg.safe));
          }
        }
      }
    }
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}
}  // namespace

void start(const config::Settings& settings, Publisher publisher, LinkProbe link) {
  g_node_id = settings.node_id;
  g_publish = std::move(publisher);
  g_link = std::move(link);
  std::vector<int> frequencies;
  bool needs_adc = false;
  for (const config::MappedPin& pin : settings.pins) {
    if (pin.mode == config::PinMode::kPwm && std::find(frequencies.begin(), frequencies.end(), pin.freq_hz) == frequencies.end()) frequencies.push_back(pin.freq_hz);
    if (pin.mode == config::PinMode::kAdc) needs_adc = true;
  }
  if (needs_adc) setup_adc();
  int next_channel = 0;
  for (const config::MappedPin& mapped : settings.pins) {
    if (mapped.mode == config::PinMode::kDisabled) continue;
    auto pin = std::make_unique<Pin>();
    pin->cfg = mapped;
    if (mapped.mode == config::PinMode::kPwm) pin->channel = static_cast<ledc_channel_t>(next_channel++);
    setup_pin(*pin, frequencies);
    ESP_LOGI(kTag, "pin \"%s\": GPIO %d as %s", mapped.name.c_str(), mapped.gpio, config::to_text(mapped.mode));
    g_pins.push_back(std::move(pin));
  }
  if (!g_pins.empty()) xTaskCreate(poll_task, "pins", 4096, nullptr, 4, nullptr);
}

Outcome command(const std::string& pin_name, std::string_view payload) {
  std::lock_guard<std::mutex> guard(g_lock);
  Pin* pin = find(pin_name);
  if (pin == nullptr) return Outcome::kUnknownPin;
  const gpio::Command parsed = gpio::parse_command(payload, pin->cfg.mode);
  if (parsed.action == gpio::Action::kNone) return Outcome::kRefused;
  if (pin->pulse != nullptr) esp_timer_stop(pin->pulse);  // any new command cancels a pulse in progress
  pin->fell_back = false;
  switch (parsed.action) {
    case gpio::Action::kOn: write_output(*pin, true); break;
    case gpio::Action::kOff: write_output(*pin, false); break;
    case gpio::Action::kToggle: write_output(*pin, !pin->on); break;
    case gpio::Action::kLevel: write_level(*pin, parsed.percent); break;
    case gpio::Action::kPulse: {
      write_output(*pin, true);
      const int ms = parsed.milliseconds > 0 ? parsed.milliseconds : (pin->cfg.pulse_ms > 0 ? pin->cfg.pulse_ms : kDefaultPulseMs);
      if (pin->pulse != nullptr) esp_timer_start_once(pin->pulse, static_cast<std::uint64_t>(ms) * 1000ULL);
      break;
    }
    case gpio::Action::kNone: break;
  }
  publish(*pin);
  return Outcome::kOk;
}

void publish_all() {
  std::lock_guard<std::mutex> guard(g_lock);
  for (auto& pin : g_pins) publish(*pin);
}

std::vector<Live> snapshot(const std::string& node_id) {
  std::lock_guard<std::mutex> guard(g_lock);
  std::vector<Live> out;
  for (auto& owned : g_pins) {
    const Pin& pin = *owned;
    Live live;
    live.name = pin.cfg.name;
    live.gpio = pin.cfg.gpio;
    live.mode = config::to_text(pin.cfg.mode);
    live.report = config::report_of(pin.cfg);
    live.on = pin.on;
    live.percent = pin.percent;
    live.value = pin.value;
    live.has_value = pin.has_value;
    live.fell_back = pin.fell_back;
    live.topic_state = gpio::topic(node_id, pin.cfg.name, "state");
    live.topic_set = pin.cfg.mode == config::PinMode::kOutput || pin.cfg.mode == config::PinMode::kPwm ? gpio::topic(node_id, pin.cfg.name, "set") : "";
    out.push_back(std::move(live));
  }
  return out;
}

}  // namespace armor::pins
