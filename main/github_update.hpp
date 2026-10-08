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
// Next to it every release carries the SHA-256 of that file (`armor_radar.bin.sha256`, the hash in hex, optionally followed by the file name, as sha256sum
// writes it). A node installs a release only when the image it downloaded has exactly that hash: a truncated, altered or mixed-up download is refused.
constexpr const char* kChecksumAssetName = "armor_radar.bin.sha256";

struct CheckResult {
  bool ok = false;             // false: `error` says why (network, no release, no matching asset...)
  std::string error;
  std::string latest_version;  // the release's own tag, without a leading "v"
  std::string asset_url;       // empty when the latest release has no armor_radar.bin attached
  std::string sha256;          // the hash the release publishes for it (64 lowercase hex digits); empty when it publishes none
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

// The install runs in a task of its own so the panel can show how far it is: start() returns at once, progress() says where it stands. `state` is
// "idle", "downloading", "verifying", "done" or "failed" (with `error`); a "done" install has already set the new image to boot and the caller restarts.
struct Progress {
  std::string state = "idle";
  std::size_t got = 0;
  std::size_t total = 0;
  std::string error;
  std::string version;
};
bool start(const std::string& asset_url, const std::string& expected_sha256);   // false when an install is already running
Progress progress();

// Downloads `asset_url` (check()'s own, so only ever what GitHub itself published) straight into the next OTA slot - the same checks as
// an upload (minimum size, the image's own magic byte, the project name) plus the hash: the image must hash to `expected_sha256` or it is thrown away - and sets it to boot. Does not restart; the caller decides when.
InstallResult install(const std::string& asset_url, const std::string& expected_sha256);

}  // namespace armor::github_update
