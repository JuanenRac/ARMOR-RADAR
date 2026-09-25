// ARMOR-RADAR - the logic of the mapped pins: commands from the server, debouncing, the fall-back to a safe state, topics and reports.
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D). GPL-3.0-or-later.
//
// A pin the operator maps in the panel becomes a device of A.R.M.O.R.'s device layer (ARMOR-SERVER, armor/device/...): it reports
// its state as a small JSON object of canonical fields ({"on":true}, {"triggered":false}, {"battery":12.6}) and, if it is an output,
// listens for a command word. Nothing here touches a pin; gpio_manager.cpp does, and everything decided here is tested on a computer.
#pragma once
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <string_view>

#include "json.hpp"
#include "node_config.hpp"
#include "node_id.hpp"

namespace armor::gpio {

// ---- commands ------------------------------------------------------------------------------------------------------------------

enum class Action { kNone, kOn, kOff, kToggle, kPulse, kLevel };
struct Command {
  Action action = Action::kNone;
  int milliseconds = 0;  // kPulse: 0 means the pin's own pulse length
  int percent = 0;       // kLevel: 0..100
};

inline std::string lower_trimmed(std::string_view text) {
  std::size_t first = 0, last = text.size();
  while (first < last && std::isspace(static_cast<unsigned char>(text[first]))) ++first;
  while (last > first && std::isspace(static_cast<unsigned char>(text[last - 1]))) --last;
  std::string out(text.substr(first, last - first));
  for (char& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return out;
}

namespace detail {
// A whole number of digits only (no sign, no spaces), at most `longest` digits.
inline bool digits_to_int(std::string_view text, std::size_t longest, int& out) {
  if (text.empty() || text.size() > longest) return false;
  long value = 0;
  for (const char c : text) { if (c < '0' || c > '9') return false; value = value * 10 + (c - '0'); }
  out = static_cast<int>(value);
  return true;
}
inline Command word_command(const std::string& word, config::PinMode mode) {
  Command command;
  if (word == "on" || word == "true" || (mode == config::PinMode::kOutput && word == "1")) command.action = Action::kOn;
  else if (word == "off" || word == "false" || (mode == config::PinMode::kOutput && word == "0")) command.action = Action::kOff;
  else if (word == "toggle") command.action = Action::kToggle;
  else if (mode == config::PinMode::kOutput && word == "pulse") command.action = Action::kPulse;
  else if (mode == config::PinMode::kOutput && word.compare(0, 6, "pulse:") == 0) {
    int ms = 0;
    if (digits_to_int(std::string_view(word).substr(6), 7, ms) && ms >= 1 && ms <= 3600000) { command.action = Action::kPulse; command.milliseconds = ms; }
  } else if (mode == config::PinMode::kPwm) {
    int percent = 0;
    if (digits_to_int(word, 3, percent) && percent <= 100) { command.action = Action::kLevel; command.percent = percent; }
  }
  return command;
}
}  // namespace detail

// What arrives on a pin's command topic. Words (on, off, toggle, true, false; 1 and 0 for an output), "pulse" or "pulse:1500" (milliseconds)
// for an output, a bare number 0..100 for a PWM pin, or JSON such as {"on":true}, {"level":40} or {"pulse":1500}. Anything else is refused
// (Action::kNone), so a stray message never switches anything.
inline Command parse_command(std::string_view payload, config::PinMode mode) {
  if (mode != config::PinMode::kOutput && mode != config::PinMode::kPwm) return {};
  const std::string word = lower_trimmed(payload);
  if (!word.empty() && word.front() == '{') {
    json::Value document;
    if (!json::parse(word, document) || !document.is_object()) return {};
    if (const json::Value* on = document.get("on"); on != nullptr && on->is_bool()) return {on->boolean ? Action::kOn : Action::kOff, 0, 0};
    if (const json::Value* level = document.get("level"); level != nullptr && level->is_number() && mode == config::PinMode::kPwm && level->number >= 0 && level->number <= 100)
      return {Action::kLevel, 0, static_cast<int>(std::lround(level->number))};
    if (const json::Value* pulse = document.get("pulse"); pulse != nullptr && pulse->is_number() && mode == config::PinMode::kOutput && pulse->number >= 1 && pulse->number <= 3600000)
      return {Action::kPulse, static_cast<int>(pulse->number), 0};
    return {};
  }
  return detail::word_command(word, mode);
}

// ---- levels --------------------------------------------------------------------------------------------------------------------

// The electrical level for a logical "on": an inverted pin (a relay board that switches on a low level) flips it.
constexpr bool level_for(bool on, bool invert) { return invert ? !on : on; }
constexpr bool logical_state(bool level, bool invert) { return invert ? !level : level; }

// The duty of a PWM channel with `resolution_bits` bits for a percentage 0..100.
constexpr std::uint32_t pwm_duty(int percent, unsigned resolution_bits) {
  const std::uint32_t maximum = (1u << resolution_bits) - 1u;
  const int clamped = percent < 0 ? 0 : (percent > 100 ? 100 : percent);
  return static_cast<std::uint32_t>((static_cast<std::uint64_t>(maximum) * static_cast<std::uint32_t>(clamped) + 50u) / 100u);
}

// ---- debouncing ----------------------------------------------------------------------------------------------------------------

// An input changes its reported state only when the raw level has stayed different for `debounce_ms`.
class Debouncer {
 public:
  void reset(bool level, std::uint64_t now_ms) { stable_ = level; candidate_ = level; since_ms_ = now_ms; }
  bool level() const { return stable_; }
  // Feed the raw level; true when the stable level just changed.
  bool update(bool raw, std::uint64_t now_ms, std::uint32_t debounce_ms) {
    if (raw == stable_) { candidate_ = raw; since_ms_ = now_ms; return false; }
    if (raw != candidate_) { candidate_ = raw; since_ms_ = now_ms; }
    if (now_ms - since_ms_ >= debounce_ms) { stable_ = raw; return true; }
    return false;
  }

 private:
  bool stable_ = false;
  bool candidate_ = false;
  std::uint64_t since_ms_ = 0;
};

// ---- the link to the server ----------------------------------------------------------------------------------------------------

// How long the node has been unable to reach its broker; an output whose safe state is set falls back to it after link_timeout_s.
class LinkWatch {
 public:
  void update(bool connected, std::uint64_t now_ms) {
    if (connected) { lost_since_ms_ = 0; lost_ = false; }
    else if (!lost_) { lost_ = true; lost_since_ms_ = now_ms; }
  }
  std::uint64_t lost_for_ms(std::uint64_t now_ms) const { return lost_ ? now_ms - lost_since_ms_ : 0; }
  bool lost() const { return lost_; }

 private:
  bool lost_ = false;
  std::uint64_t lost_since_ms_ = 0;
};

// True when this output must fall back to its safe state now: a timeout is set, a safe state other than "keep" is chosen, and the link has been down that long.
inline bool must_fall_back(const config::MappedPin& pin, std::uint64_t lost_for_ms) {
  return pin.mode == config::PinMode::kOutput && pin.link_timeout_s > 0 && pin.safe != config::SafeState::kKeep && lost_for_ms >= static_cast<std::uint64_t>(pin.link_timeout_s) * 1000ULL;
}

// ---- topics and reports --------------------------------------------------------------------------------------------------------

// armor/device/<node>/<pin>/<suffix>, or "" when the node id or the pin name is not valid.
inline std::string topic(std::string_view node_id, std::string_view pin_name, std::string_view suffix) {
  if (!node_id_is_valid(node_id) || !config::valid_slug(pin_name) || (suffix != "state" && suffix != "set")) return "";
  return "armor/device/" + std::string(node_id) + "/" + std::string(pin_name) + "/" + std::string(suffix);
}
// The single subscription that covers every command topic of the node.
inline std::string command_filter(std::string_view node_id) { return node_id_is_valid(node_id) ? "armor/device/" + std::string(node_id) + "/+/set" : ""; }
// The pin name in a command topic of this node, or "" when the topic is not one.
inline std::string pin_of_command_topic(std::string_view node_id, std::string_view topic_text) {
  const std::string prefix = "armor/device/" + std::string(node_id) + "/";
  if (topic_text.substr(0, prefix.size()) != prefix) return "";
  const std::string_view rest = topic_text.substr(prefix.size());
  if (rest.size() < 5 || rest.substr(rest.size() - 4) != "/set") return "";
  const std::string_view name = rest.substr(0, rest.size() - 4);
  return config::valid_slug(name) ? std::string(name) : "";
}

inline std::string report_boolean(std::string_view field, bool value) {
  json::Writer w;
  w.begin_object().field(field, value).end_object();
  return w.str();
}
inline std::string report_number(std::string_view field, double value) {
  json::Writer w;
  w.begin_object().key(field).number(value, 3).end_object();
  return w.str();
}

// The number an analogue pin reports: millivolts at the pin, scaled and offset by the operator's own factors.
inline double analogue_value(int millivolts, const config::MappedPin& pin) { return static_cast<double>(millivolts) * pin.scale + pin.offset; }

}  // namespace armor::gpio
