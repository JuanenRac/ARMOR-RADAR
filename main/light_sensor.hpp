// ARMOR-RADAR - the ambient-light reading of the node.
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D). GPL-3.0-or-later.
#pragma once

namespace armor {

// Starts the task that reads the VEML7700 (CONFIG_ARMOR_LIGHT_VEML7700) and keeps the newest value. Without the sensor nothing starts
// and the value stays unknown unless CONFIG_ARMOR_LUX_FALLBACK sets a bench value. Returns false when the sensor was expected and
// could not be initialised (the node then keeps running, and says so).
bool light_sensor_start();

// Ambient light in lux, or a negative number while unknown: the message contract needs a real value, so the node withholds
// telemetry instead of inventing one.
float light_lux();

}  // namespace armor
