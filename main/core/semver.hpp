// ARMOR-RADAR - comparing "x.y.z" firmware versions (an optional leading "v" is ignored, as GitHub's tags have one).
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D). GPL-3.0-or-later.
#pragma once
#include <string_view>

namespace armor::semver {

struct Version {
  int major = 0, minor = 0, patch = 0;
  bool ok = false;   // false for anything that is not exactly (v)?digits.digits.digits
};

inline Version parse(std::string_view text) {
  Version v;
  if (!text.empty() && text[0] == 'v') text.remove_prefix(1);
  int part = 0;
  long value = 0;
  bool any_digit = false;
  for (const char c : text) {
    if (c >= '0' && c <= '9') { value = value * 10 + (c - '0'); any_digit = true; }
    else if (c == '.' && part < 2) {
      if (!any_digit) return v;
      if (part == 0) v.major = static_cast<int>(value); else v.minor = static_cast<int>(value);
      ++part; value = 0; any_digit = false;
    } else return v;   // a pre-release or build suffix ("-rc1", "+build5"): treated as not a plain release, never offered as an update
  }
  if (!any_digit || part != 2) return v;
  v.patch = static_cast<int>(value);
  v.ok = true;
  return v;
}

inline bool less(const Version& a, const Version& b) {
  if (a.major != b.major) return a.major < b.major;
  if (a.minor != b.minor) return a.minor < b.minor;
  return a.patch < b.patch;
}

// True only when both parse as plain releases and `candidate` is strictly newer than `current`.
inline bool is_newer(std::string_view candidate, std::string_view current) {
  const Version c = parse(candidate), r = parse(current);
  return c.ok && r.ok && less(r, c);
}

}  // namespace armor::semver
