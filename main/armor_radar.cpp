// ARMOR-RADAR — field-node configuration guard.
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D). GPL-3.0-or-later.
#include "radar_tracks.hpp"

namespace armor {
static_assert(kMaximumTracks == 15, "The telemetry contract permits 3 x 5 tracks");
}
