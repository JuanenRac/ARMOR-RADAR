// ARMOR-RADAR - which file of a GitHub release is the firmware of THIS board, and which is its checksum. Pure: tested on a computer.
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D). GPL-3.0-or-later.
//
// A release attaches one standalone application image per board, `armor_radar-<board>.bin` (for example `armor_radar-s3-wifi.bin`), and next to each its SHA-256, `<image>.sha256`.
// A release that predates that naming has only `armor_radar.bin`: the image of the project's DEFAULT board (s3-eth, or lcd7box for the touch panel). A node finds the image built for
// its own board; the plain name is accepted only on the default board, and a node of any other board never installs an image that is not its own.
#pragma once
#include <string>

#include "json.hpp"

namespace armor::release {

constexpr const char* kAssetPrefix = "armor_radar";

inline std::string asset_name(const char* board_id) { return std::string(kAssetPrefix) + "-" + (board_id != nullptr ? board_id : "") + ".bin"; }

// The plain name, for the board whose image a release without the board's name carries; "" for every other board.
inline std::string legacy_asset_name(const char* board_id) {
  const std::string board = board_id != nullptr ? board_id : "";
  return board == "s3-eth" || board == "lcd7box" ? std::string(kAssetPrefix) + ".bin" : "";
}

struct Picked {
  std::string image_url;      // empty: the release has no image for this board
  std::string checksum_url;   // empty: it has the image but not the checksum file next to it
  bool legacy = false;        // the image is the one with the plain name
};

// `assets` is the "assets" array of the release's JSON (each with "name" and "browser_download_url"). The image built for the board wins; the plain name is the fallback.
inline Picked pick(const json::Value* assets, const char* board_id) {
  Picked picked;
  if (assets == nullptr || !assets->is_array()) return picked;
  const std::string wanted = asset_name(board_id), legacy = legacy_asset_name(board_id);
  std::string wanted_url, wanted_sum, legacy_url, legacy_sum;
  for (const json::Value& asset : assets->items) {
    const std::string name = asset.string_or("name", ""), url = asset.string_or("browser_download_url", "");
    if (name == wanted) wanted_url = url;
    else if (name == wanted + ".sha256") wanted_sum = url;
    else if (!legacy.empty() && name == legacy) legacy_url = url;
    else if (!legacy.empty() && name == legacy + ".sha256") legacy_sum = url;
  }
  if (!wanted_url.empty()) { picked.image_url = wanted_url; picked.checksum_url = wanted_sum; }
  else if (!legacy_url.empty()) { picked.image_url = legacy_url; picked.checksum_url = legacy_sum; picked.legacy = true; }
  return picked;
}

}  // namespace armor::release
