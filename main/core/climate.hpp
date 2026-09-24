// ARMOR-RADAR - dew point, the anti-fog PTC heater and the local day/night decision.
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D). GPL-3.0-or-later.
//
// Pure functions with no hardware in them, so the decisions can be tested on a
// computer. The heater is a *safety-relevant* actuator: it is only ever switched
// on by a fresh, plausible reading, and any bad reading switches it off.
#pragma once
#include <cmath>

namespace armor {

// Magnus approximation, accurate to about 0.4 C between -45 C and 60 C.
inline float dew_point_c(float temperature_c, float humidity_pct) {
  constexpr float a = 17.62f;
  constexpr float b = 243.12f;
  const float gamma = std::log(humidity_pct / 100.0f) + (a * temperature_c) / (b + temperature_c);
  return (b * gamma) / (a - gamma);
}

inline bool climate_reading_is_plausible(float temperature_c, float humidity_pct) {
  return temperature_c >= -50.0f && temperature_c <= 85.0f && humidity_pct > 0.0f && humidity_pct <= 100.0f;  // also rejects NaN
}

// Heats the window while it is within `on_margin_c` of the dew point (fog is about to form) and
// stops once it is more than `off_margin_c` above it, so it cannot chatter on the boundary.
// A reading that is not plausible turns the heater off.
class HeaterController {
 public:
  HeaterController(float on_margin_c = 3.0f, float off_margin_c = 5.0f) : on_(on_margin_c), off_(off_margin_c) {}

  bool update(float temperature_c, float humidity_pct) {
    if (!climate_reading_is_plausible(temperature_c, humidity_pct)) return heating_ = false;
    const float margin = temperature_c - dew_point_c(temperature_c, humidity_pct);
    if (!heating_ && margin < on_) heating_ = true;
    else if (heating_ && margin > off_) heating_ = false;
    return heating_;
  }
  bool heating() const { return heating_; }

 private:
  float on_;
  float off_;
  bool heating_ = false;
};

// Night below `low_lux`, day again above `high_lux`; in between the previous answer stands.
class DayNight {
 public:
  DayNight(float low_lux = 30.0f, float high_lux = 60.0f) : low_(low_lux), high_(high_lux) {}
  bool update(float lux) {
    if (!(lux >= 0.0f)) return night_;  // a bad reading changes nothing
    if (!night_ && lux < low_) night_ = true;
    else if (night_ && lux > high_) night_ = false;
    return night_;
  }
  bool is_night() const { return night_; }

 private:
  float low_;
  float high_;
  bool night_ = false;
};

}  // namespace armor
