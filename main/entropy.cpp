// ARMOR-RADAR - random bytes for session tokens, salts and the setup code.
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D). GPL-3.0-or-later.
#include "entropy.hpp"

extern "C" {
#include "bootloader_random.h"
#include "esp_random.h"
#include "esp_timer.h"
}

namespace armor {

std::mutex& adc_lock() {
  static std::mutex lock;
  return lock;
}

void random_bytes(std::uint8_t* out, std::size_t length) {
  std::lock_guard<std::mutex> guard(adc_lock());
  bootloader_random_enable();
  esp_fill_random(out, length);
  bootloader_random_disable();
  // Mix in the time of the call as well: it costs nothing and never makes the bytes worse.
  const std::uint64_t now = static_cast<std::uint64_t>(esp_timer_get_time());
  for (std::size_t i = 0; i < length; ++i) out[i] = static_cast<std::uint8_t>(out[i] ^ static_cast<std::uint8_t>(now >> ((i % 8) * 8)) ^ static_cast<std::uint8_t>(esp_random() >> 24));
}

}  // namespace armor
