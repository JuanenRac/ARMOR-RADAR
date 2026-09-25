// ARMOR-RADAR - the ambient-light reading of the node.
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D). GPL-3.0-or-later.
#pragma once
#include "core/node_config.hpp"

namespace armor {

// Starts the task that reads the VEML7700 (when the settings say one is connected, on their I2C pins) and keeps the newest value. Without
// the sensor nothing starts and the value stays unknown unless the settings hold a bench value (lux_fallback). Returns false when the sensor was expected and
// could not be initialised (the node then keeps running, and says so).
bool light_sensor_start(const config::Sensors& sensors);

// Ambient light in lux, or a negative number while unknown: the message contract needs a real value, so the node withholds
// telemetry instead of inventing one.
float light_lux();

}  // namespace armor
