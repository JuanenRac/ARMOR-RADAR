// ARMOR-RADAR - node identifier rules shared with the message contract.
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D). GPL-3.0-or-later.
#pragma once
#include <cstddef>
#include <string_view>

namespace armor {

// ^[a-z0-9][a-z0-9_-]{0,63}$ - the same rule as ARMOR-COMMON's schemas.
constexpr std::size_t kMaxNodeIdLength = 64;

constexpr bool node_id_is_valid(std::string_view id) {
  if (id.empty() || id.size() > kMaxNodeIdLength) return false;
  const auto lower_alnum = [](char c) { return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'); };
  if (!lower_alnum(id.front())) return false;
  for (const char c : id) {
    if (!lower_alnum(c) && c != '_' && c != '-') return false;
  }
  return true;
}

}  // namespace armor
