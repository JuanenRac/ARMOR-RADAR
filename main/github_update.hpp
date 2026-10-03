// ARMOR-RADAR - checking GitHub's own releases for a newer firmware and installing it, next to the existing manual upload (web_server.cpp's
// /api/v1/ota): both end up in the same OTA slot through the same esp_ota_* calls, this one just gets its bytes from the Internet instead
// of the panel's own upload. Nothing here replaces the manual path - a board with no Internet route, or a fleet an operator wants to hold
// back, still updates exactly as it always has.
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D). GPL-3.0-or-later.
#pragma once
#include <cstddef>
#include <string>

namespace armor::github_update {

// The repository releases are read from, and the exact name of the standalone app image attached to each one (never the "-complete" image,
// which has the bootloader and partition table too - installing that into an OTA slot would not boot).
constexpr const char* kRepo = "JuanenRac/ARMOR-RADAR";
constexpr const char* kAssetName = "armor_radar.bin";

struct CheckResult {
  bool ok = false;             // false: `error` says why (network, no release, no matching asset...)
  std::string error;
  std::string latest_version;  // the release's own tag, without a leading "v"
  std::string asset_url;       // empty when the latest release has no armor_radar.bin attached
  bool update_available = false;
};

// Asks GitHub's API for the newest release and compares it to the running firmware. Blocks for the duration of the HTTPS request.
CheckResult check();

struct InstallResult {
  bool ok = false;
  std::string error;
  std::string version;
  std::size_t bytes = 0;
};

// Downloads `asset_url` (check()'s own, so only ever what GitHub itself published) straight into the next OTA slot - the same checks as
// an upload (minimum size, the image's own magic byte, the project name) - and sets it to boot. Does not restart; the caller decides when.
InstallResult install(const std::string& asset_url);

}  // namespace armor::github_update
