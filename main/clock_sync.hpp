// ARMOR-RADAR - the node's clock: the time zone, the time server (SNTP) and, when the time server is off, setting the time by hand.
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D). GPL-3.0-or-later.
//
// This used to live inside the broker link, which meant a node with no broker had no time at all. It is its own piece now: the zone applies
// at start, the time server starts as soon as the network has an address, and the broker link only waits for the result.
#pragma once
#include <cstdint>
#include <string>

#include "core/node_config.hpp"

namespace armor::clocksync {

// Applies the zone and, when the settings ask for a time server, starts SNTP once the network has an address.
void start(const config::Settings& settings);

bool is_set();   // the clock holds a real date (not the 1970 of a board that has just powered on)

// Sets the clock to `seconds` since 1970, for a node with no time server. False for a date before the firmware existed or absurdly far.
bool set_unix(std::int64_t seconds);

struct Info {
  bool set = false;
  bool synced = false;          // a time server has answered since the start
  std::int64_t epoch = 0;       // seconds since 1970 (UTC)
  int utc_offset_min = 0;       // the zone's offset right now, summer time included
  std::string local;            // "2000-01-01 10:31:07"
  std::string zone;             // the zone rule in use
};
Info info();

}  // namespace armor::clocksync
