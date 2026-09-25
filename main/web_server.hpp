// ARMOR-RADAR - the node's web panel and its JSON API.
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D). GPL-3.0-or-later.
#pragma once
#include "core/node_config.hpp"

namespace armor::web {

// Starts the HTTP server on port 80: the panel (embedded in the firmware) and /api/v1. Every route but the session and the first setup
// needs a login; a node with no user yet answers only the setup.
bool start(const config::Settings& settings);

// Restarts the node after `delay_ms` (the answer that caused it has time to leave).
void restart_after(unsigned delay_ms);

}  // namespace armor::web
