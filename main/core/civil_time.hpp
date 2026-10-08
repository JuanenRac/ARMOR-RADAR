// ARMOR-RADAR - calendar arithmetic with no library behind it, so the clock's offset from UTC can be tested on a computer.
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D). GPL-3.0-or-later.
#pragma once
#include <cstdint>

namespace armor::civil {

// Days from 1970-01-01 to a calendar date (proleptic Gregorian) - Howard Hinnant's algorithm. month 1..12, day 1..31.
inline std::int64_t days_from_civil(int year, int month, int day) {
  year -= month <= 2;
  const std::int64_t era = (year >= 0 ? year : year - 399) / 400;
  const unsigned year_of_era = static_cast<unsigned>(year - era * 400);
  const unsigned day_of_year = (153 * static_cast<unsigned>(month + (month > 2 ? -3 : 9)) + 2) / 5 + static_cast<unsigned>(day) - 1;
  const unsigned day_of_era = year_of_era * 365 + year_of_era / 4 - year_of_era / 100 + day_of_year;
  return era * 146097 + static_cast<std::int64_t>(day_of_era) - 719468;
}

// Seconds since 1970 of a wall-clock reading taken as if it were UTC.
inline std::int64_t seconds_as_utc(int year, int month, int day, int hour, int minute, int second) {
  return days_from_civil(year, month, day) * 86400LL + hour * 3600LL + minute * 60LL + second;
}

// The zone's offset from UTC, in minutes: the local wall-clock reading read as UTC, minus the true UTC time. Summer time shows up in it.
inline int utc_offset_minutes(std::int64_t utc_epoch, int year, int month, int day, int hour, int minute, int second) {
  return static_cast<int>((seconds_as_utc(year, month, day, hour, minute, second) - utc_epoch) / 60);
}

}  // namespace armor::civil
