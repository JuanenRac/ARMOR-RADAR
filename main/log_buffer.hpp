// ARMOR-RADAR - the last few kilobytes of the node's log, so the panel can show what the console would.
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D). GPL-3.0-or-later.
#pragma once
#include <cstdint>
#include <string>

namespace armor::logbuf {

// Hooks the ESP-IDF log output (the console keeps working) and keeps the newest text in a ring.
void start();

// The text written since position `from`, and the position to ask from next time. A position older than the ring starts at its oldest
// byte. `max_bytes` bounds one answer.
std::string read_from(std::uint32_t from, std::uint32_t& next, std::size_t max_bytes = 6000);

}  // namespace armor::logbuf
