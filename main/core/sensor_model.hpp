// ARMOR-RADAR - the millimetre-wave sensors a node's serial ports can carry, and what each one is.
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D). GPL-3.0-or-later.
//
// Two kinds. A TRACKER reports where the targets are (x, y): the LD2450 and the LD2461. They feed the perimeter's tracks, so the server and
// Studio see them as radars. A PRESENCE sensor reports whether someone is there and how far (the LD2410, LD2412, LD2410S and MR24HPC1): it
// has no position, so it becomes a device of the server (armor/device/<node>/<name>/state, {"triggered":true}), like a pin does.
// The figures below come from each manufacturer's document (see docs/SENSORS.md); the sensors were not connected to anything.
#pragma once
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace armor::sensors {

enum class Model : std::uint8_t { kLd2450, kLd2461, kLd2410, kLd2412, kLd2410s, kMr24hpc1 };
constexpr std::size_t kModelCount = 6;

struct ModelInfo {
  Model model;
  const char* id;               // the value in the settings
  const char* label;
  bool tracker;                 // gives positions (a radar), otherwise presence (a device)
  std::uint32_t default_baud;
  std::uint16_t half_angle_deg; // trackers: half of the horizontal field of view
  std::uint16_t range_dm;       // longest distance the manual promises for a person, in decimetres
  std::uint8_t max_targets;     // trackers
  bool command_family;          // speaks the FD FC FB FA ... 04 03 02 01 command frames (LD2450, LD2410, LD2412, LD2410S)
};

constexpr ModelInfo kModels[kModelCount] = {
    {Model::kLd2450, "ld2450", "HLK-LD2450", true, 256000, 60, 60, 3, true},      // 3 targets, +-60 degrees, 6 m (manual V1.00)
    {Model::kLd2461, "ld2461", "HLK-LD2461", true, 9600, 45, 80, 5, false},       // 5 tracks, +-45 degrees, 8 m moving (specification V1.1)
    {Model::kLd2410, "ld2410", "HLK-LD2410B / LD2410C", false, 256000, 0, 60, 0, true},
    {Model::kLd2412, "ld2412", "HLK-LD2412", false, 115200, 0, 60, 0, true},
    {Model::kLd2410s, "ld2410s", "HLK-LD2410S", false, 115200, 0, 60, 0, true},
    {Model::kMr24hpc1, "mr24hpc1", "Seeed MR24HPC1", false, 9600, 0, 40, 0, false},
};

constexpr const ModelInfo& info(Model model) { return kModels[static_cast<std::size_t>(model)]; }
constexpr const char* to_text(Model model) { return info(model).id; }

// The model of a settings text, false when it is not one.
inline bool from_text(std::string_view text, Model& out) {
  for (const ModelInfo& entry : kModels) if (text == entry.id) { out = entry.model; return true; }
  return false;
}

}  // namespace armor::sensors
