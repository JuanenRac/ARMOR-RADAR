// ARMOR-RADAR - conversion and range selection for the Vishay VEML7700 ambient-light sensor.
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D). GPL-3.0-or-later.
//
// Source: the Vishay VEML7700 datasheet and its application note "Designing the VEML7700 into an
// Application". Nothing here has been run against a real sensor yet: the register layout, the
// resolution table and the correction polynomial are checked by the host tests against the datasheet's own figures
// (0.0036 lx per count at gain x2 and 800 ms, and the 0.0576 lx per count of gain x1 at 100 ms), and the first job on the
// bench is to compare the value with a reference lux meter.
//
//   I2C address 0x10, registers are 16 bits, least significant byte first.
//   0x00 configuration: bits 12:11 gain (00 x1, 01 x2, 10 x1/8, 11 x1/4), bits 9:6 integration time
//        (1100 25 ms, 1000 50 ms, 0000 100 ms, 0001 200 ms, 0010 400 ms, 0011 800 ms), bit 0 shutdown.
//   0x04 ambient light counts, 0x05 white counts.
//
// The sensor has no automatic range: a reading of 0 in bright light or of 65535 in the dark is just a setting that does
// not fit. `next_step` walks a ladder of settings, from the least to the most sensitive, until the count is useful.
#pragma once
#include <cstddef>
#include <cstdint>

namespace armor {
namespace veml7700 {

constexpr std::uint8_t kAddress = 0x10;
constexpr std::uint8_t kRegisterConfig = 0x00;
constexpr std::uint8_t kRegisterAls = 0x04;

enum class Gain : std::uint8_t { kX1 = 0, kX2 = 1, kX1_8 = 2, kX1_4 = 3 };
enum class Integration : std::uint8_t { kMs25, kMs50, kMs100, kMs200, kMs400, kMs800 };

struct Setting {
  Gain gain;
  Integration integration;
};

// From the least to the most sensitive. Step 0 reaches about 120 000 lx, the last one resolves about 0.0036 lx.
constexpr Setting kLadder[] = {
    {Gain::kX1_8, Integration::kMs25},  {Gain::kX1_8, Integration::kMs100}, {Gain::kX1_4, Integration::kMs100}, {Gain::kX1, Integration::kMs100},
    {Gain::kX1, Integration::kMs200},   {Gain::kX2, Integration::kMs200},   {Gain::kX2, Integration::kMs400},   {Gain::kX2, Integration::kMs800},
};
constexpr std::size_t kLadderSteps = sizeof(kLadder) / sizeof(kLadder[0]);
constexpr std::size_t kStartStep = 3;  // gain x1, 100 ms: a sensible place to start indoors and out

constexpr std::uint16_t kTooFewCounts = 100;    // below this the reading is coarse: use a more sensitive setting
constexpr std::uint16_t kTooManyCounts = 60000; // above this it is near saturation: use a less sensitive one

inline std::uint16_t integration_bits(Integration integration) {
  switch (integration) {
    case Integration::kMs25: return 0b1100;
    case Integration::kMs50: return 0b1000;
    case Integration::kMs100: return 0b0000;
    case Integration::kMs200: return 0b0001;
    case Integration::kMs400: return 0b0010;
    case Integration::kMs800: return 0b0011;
  }
  return 0;
}

inline std::uint32_t integration_ms(Integration integration) {
  switch (integration) {
    case Integration::kMs25: return 25;
    case Integration::kMs50: return 50;
    case Integration::kMs100: return 100;
    case Integration::kMs200: return 200;
    case Integration::kMs400: return 400;
    case Integration::kMs800: return 800;
  }
  return 100;
}

// The 16-bit configuration register for a setting, with the sensor powered on (shutdown bit clear).
inline std::uint16_t config_word(Setting setting) {
  return static_cast<std::uint16_t>((static_cast<std::uint16_t>(setting.gain) << 11) | (integration_bits(setting.integration) << 6));
}

inline float gain_factor(Gain gain) {
  switch (gain) {
    case Gain::kX1: return 1.0f;
    case Gain::kX2: return 2.0f;
    case Gain::kX1_8: return 0.125f;
    case Gain::kX1_4: return 0.25f;
  }
  return 1.0f;
}

// Lux per count. The datasheet's finest resolution is 0.0036 at gain x2 and 800 ms; every halving of the gain or the integration
// time doubles it.
inline float resolution_lux_per_count(Setting setting) {
  return 0.0036f * (800.0f / static_cast<float>(integration_ms(setting.integration))) * (2.0f / gain_factor(setting.gain));
}

// The Vishay correction for the sensor's non-linearity at high light. It is close to the identity below a few hundred lux.
inline float correct_nonlinearity(float lux) {
  return (((6.0135e-13f * lux - 9.3924e-9f) * lux + 8.1488e-5f) * lux + 1.0023f) * lux;
}

inline float lux_from_counts(std::uint16_t counts, Setting setting) {
  const float raw = static_cast<float>(counts) * resolution_lux_per_count(setting);
  return correct_nonlinearity(raw);
}

// How long to wait after writing a new setting before a reading is valid: two integration periods, plus a margin.
inline std::uint32_t settle_ms(Setting setting) { return integration_ms(setting.integration) * 2 + 40; }

// The step to use after a reading: less sensitive when the count is near saturation, more sensitive when it is tiny.
// A reading is only trusted when the step stays the same.
inline std::size_t next_step(std::size_t step, std::uint16_t counts) {
  if (step >= kLadderSteps) return kStartStep;
  if (counts > kTooManyCounts && step > 0) return step - 1;
  if (counts < kTooFewCounts && step + 1 < kLadderSteps) return step + 1;
  return step;
}

inline bool reading_is_trustworthy(std::size_t step, std::uint16_t counts) { return next_step(step, counts) == step; }

}  // namespace veml7700
}  // namespace armor
