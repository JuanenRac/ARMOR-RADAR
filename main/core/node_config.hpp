// ARMOR-RADAR - the settings of a node: what the web panel edits, what is stored in flash and what the firmware obeys.
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D). GPL-3.0-or-later.
//
// One document (JSON) holds everything that differs from node to node, so the same firmware image serves every node and nothing
// secret has to be compiled in. This file only reads, checks and writes that document; it touches no hardware, so all of it is tested
// on a computer. Passwords are never written back to the panel: a section sent without a password (or with an empty one) keeps the
// stored one, and "password_set" tells the panel that there is one.
#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <initializer_list>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "board_pins.hpp"
#include "json.hpp"
#include "net_text.hpp"
#include "node_id.hpp"
#include "sensor_model.hpp"

namespace armor::config {

constexpr int kVersion = 1;
constexpr std::size_t kRadarCount = 3;
constexpr std::size_t kMaxMappedPins = 16;
constexpr std::size_t kMaxPasswordText = 64;

enum class Uplink { kEthernet, kWifi };
enum class WifiSecurity { kOpen, kWpa2, kWpa3, kWpa2Wpa3 };
enum class PinMode { kDisabled, kInput, kOutput, kPwm, kAdc };
enum class Pull { kNone, kUp, kDown };
enum class SafeState { kOff, kOn, kKeep };
// When the node accepts configuration over Bluetooth: never, only while it has no user (set-up), or always.
enum class BleMode { kOff, kSetup, kAlways };
// The panel over plain HTTP only, over HTTP and HTTPS (a certificate the node made for itself), or over HTTPS only (port 80 sends the browser to HTTPS).
enum class WebMode { kHttp, kBoth, kHttps };

struct IpSettings {
  bool dhcp = true;
  std::string address, netmask = "255.255.255.0", gateway, dns1, dns2;
  std::string hostname;  // empty: "armor-" + the node id
};

struct AccessPoint {
  bool enabled = false;
  std::string ssid = "ARMOR";
  WifiSecurity security = WifiSecurity::kWpa2;
  std::string password;
  int channel = 0;  // 0: one of 1, 6 or 11 chosen from the node's MAC, so neighbouring nodes rarely share a channel
  bool hidden = false;
  int max_clients = 8;
  int tx_power_dbm = 15;
  int bandwidth_mhz = 20;
  std::string country = "ES";  // two capital letters: sets which channels and how much power the radio may use
  bool bridge = true;  // the access point joins the wired network (one network, one address range); false: its own network
};

struct Network { std::string ssid, password; };
constexpr std::size_t kMaxBackupNetworks = 3;

struct Station {
  bool enabled = false;
  std::string ssid, password;
  // Tried in order, after the network above, whenever the current one cannot be joined for a while (main/network.cpp); never while the
  // network above still works. The same Wi-Fi password rules apply to each.
  std::vector<Network> backup;
};

struct Broker { std::string uri, username, password; };
constexpr std::size_t kMaxBackupBrokers = 2;

struct Mqtt {
  bool enabled = false;  // a node that was never configured has no broker yet
  std::string uri, username, password;
  int heartbeat_s = 10;
  int telemetry_ms = 200;
  std::string ntp = "pool.ntp.org";
  // Tried in order, after the broker above, whenever it cannot be reached for a while (main/mqtt_link.cpp); never while it still works.
  std::vector<Broker> backup;
};

struct RadarLine {
  bool enabled = false;
  std::string model = "ld2450";   // which sensor is on this serial port (core/sensor_model.hpp)
  int baud = 0;                    // 0: the model's own speed
  std::string name;                // a presence sensor is a device of the server with this name (armor/device/<node>/<name>/state); trackers do not use it
  int rx = -1;  // the GPIO that receives the radar's TX line
  int tx = -1;  // the GPIO that sends to the radar's RX line; -1: not wired, the radar cannot be configured from the panel
};

struct Sensors {
  bool veml7700 = true;
  int sda = board::kDefaultI2cSda, scl = board::kDefaultI2cScl;
  int lux_fallback = -1;  // -1: withhold telemetry without a light reading
};

// One mapped pin: something the server can read (an input, an analogue value) or switch (an output, a PWM level).
struct MappedPin {
  int gpio = -1;
  std::string name;  // a-z, 0-9, '_': becomes a piece of the MQTT topic
  PinMode mode = PinMode::kDisabled;
  bool invert = false;     // input/output: "on" is a low level
  Pull pull = Pull::kNone;  // input only
  bool initial_on = false;  // output: the level at start
  SafeState safe = SafeState::kKeep;  // output: the state to fall back to when the server is silent for link_timeout_s
  int link_timeout_s = 0;   // 0: never fall back
  int pulse_ms = 0;         // output: a "pulse" command without a length lasts this long (0: 500)
  int debounce_ms = 30;     // input
  int period_s = 10;        // analogue: how often the value is published
  int freq_hz = 1000;       // pwm
  // The field of the device state this pin fills in when it reports to the server (see report_of); empty: the default of its mode.
  std::string report;
  double scale = 1.0, offset = 0.0;  // analogue: reported value = millivolts * scale + offset
};

// The report field of a pin and the ones its mode allows. The names are the canonical fields of A.R.M.O.R.'s device layer; "mv" is
// the raw millivolts at the pin, for an analogue value the server maps itself.
inline const char* default_report(PinMode mode) {
  switch (mode) { case PinMode::kInput: return "triggered"; case PinMode::kOutput: return "on"; case PinMode::kPwm: return "brightness"; case PinMode::kAdc: return "mv"; case PinMode::kDisabled: break; }
  return "";
}
inline bool report_allowed(PinMode mode, std::string_view field) {
  const auto in = [&](std::initializer_list<const char*> list) { for (const char* item : list) if (field == item) return true; return false; };
  switch (mode) {
    case PinMode::kInput: return in({"triggered", "open", "on", "tamper"});
    case PinMode::kOutput: return in({"on", "locked"});
    case PinMode::kPwm: return in({"brightness"});
    case PinMode::kAdc: return in({"mv", "battery", "brightness", "humidity", "temperature", "lux", "power_w", "co_ppm"});
    case PinMode::kDisabled: break;
  }
  return false;
}
inline std::string report_of(const MappedPin& pin) { return pin.report.empty() ? default_report(pin.mode) : pin.report; }

struct Settings {
  std::string node_id;
  std::string node_name;
  Uplink uplink = board::kHasEthernet ? Uplink::kEthernet : Uplink::kWifi;   // the board's own way in until the panel says otherwise
  IpSettings ip;
  AccessPoint ap;
  Station sta;
  Mqtt mqtt;
  std::array<RadarLine, kRadarCount> radars;
  Sensors sensors;
  std::vector<MappedPin> pins;
  BleMode ble = BleMode::kSetup;
  WebMode web = WebMode::kBoth;
  std::string language = "en";
  // A periodic, unconditional restart (disconnect_before_restart() then esp_restart()), independent of any fault: 0 means never. One of
  // {0, 1, 2, 3, 4, 6, 12, 24, 48} hours (auto_restart_hours_is_valid()).
  int auto_restart_hours = 0;
};

struct Problem {
  std::string path;  // "ap.ssid"
  std::string code;  // "required", "too_long", "range", "invalid", "conflict", "reserved" ...
};
using Problems = std::vector<Problem>;

inline const char* to_text(Uplink v) { return v == Uplink::kWifi ? "wifi" : "ethernet"; }
inline const char* to_text(WifiSecurity v) {
  switch (v) { case WifiSecurity::kOpen: return "open"; case WifiSecurity::kWpa2: return "wpa2"; case WifiSecurity::kWpa3: return "wpa3"; case WifiSecurity::kWpa2Wpa3: return "wpa2wpa3"; }
  return "wpa2";
}
inline const char* to_text(PinMode v) {
  switch (v) { case PinMode::kDisabled: return "disabled"; case PinMode::kInput: return "input"; case PinMode::kOutput: return "output"; case PinMode::kPwm: return "pwm"; case PinMode::kAdc: return "adc"; }
  return "disabled";
}
inline const char* to_text(Pull v) { return v == Pull::kUp ? "up" : v == Pull::kDown ? "down" : "none"; }
inline const char* to_text(WebMode v) { return v == WebMode::kHttps ? "https" : v == WebMode::kHttp ? "http" : "both"; }
inline const char* to_text(BleMode v) { return v == BleMode::kAlways ? "always" : v == BleMode::kOff ? "off" : "setup"; }
inline const char* to_text(SafeState v) { return v == SafeState::kOn ? "on" : v == SafeState::kOff ? "off" : "keep"; }

// ---- the defaults of a node that has never been configured ---------------------------------------------------------------------

// `mac_tail` is the last three bytes of the node's MAC as six lowercase hexadecimal digits: it makes the first identity unique.
inline Settings default_settings(std::string_view mac_tail) {
  Settings s;
  s.node_id = "armor-" + std::string(mac_tail);
  s.node_name = s.node_id;
  for (std::size_t i = 0; i < kRadarCount; ++i) {
    s.radars[i].enabled = true;
    s.radars[i].rx = board::kDefaultRadarRx[i];
    s.radars[i].tx = board::kDefaultRadarTx[i];
  }
  return s;
}

// The Wi-Fi channel a node uses: its own setting, or (auto) one of the three that do not overlap, chosen from the node's MAC so that
// the access points of a perimeter spread over 1, 6 and 11 without anyone having to plan it.
inline int effective_channel(const AccessPoint& ap, unsigned mac_sum) {
  if (ap.channel >= 1 && ap.channel <= 13) return ap.channel;
  constexpr int kNonOverlapping[3] = {1, 6, 11};
  return kNonOverlapping[mac_sum % 3];
}

// ---- reading -------------------------------------------------------------------------------------------------------------------

namespace detail {
template <typename E>
bool read_choice(const json::Value& parent, const char* name, std::initializer_list<std::pair<const char*, E>> options, E& target) {
  const json::Value* member = parent.get(name);
  if (member == nullptr) return true;
  if (!member->is_string()) return false;
  for (const auto& option : options) if (member->text == option.first) { target = option.second; return true; }
  return false;
}

inline void bad(Problems& problems, std::string path, const char* code) { problems.push_back({std::move(path), code}); }

inline void read_text(const json::Value& parent, const char* name, std::string& target, std::size_t longest, const std::string& path, Problems& problems) {
  const json::Value* member = parent.get(name);
  if (member == nullptr) return;
  if (!member->is_string()) { bad(problems, path, "invalid"); return; }
  if (member->text.size() > longest) { bad(problems, path, "too_long"); return; }
  target = member->text;
}

// A secret: absent or empty keeps the stored one; "<name>_clear": true erases it.
inline void read_secret(const json::Value& parent, const char* name, std::string& target, const std::string& path, Problems& problems) {
  if (parent.bool_or(std::string(name) + "_clear", false)) { target.clear(); return; }
  const json::Value* member = parent.get(name);
  if (member == nullptr) return;
  if (!member->is_string()) { bad(problems, path, "invalid"); return; }
  if (member->text.empty()) return;
  if (member->text.size() > kMaxPasswordText) { bad(problems, path, "too_long"); return; }
  target = member->text;
}

inline void read_bool(const json::Value& parent, const char* name, bool& target, const std::string& path, Problems& problems) {
  const json::Value* member = parent.get(name);
  if (member == nullptr) return;
  if (!member->is_bool()) { bad(problems, path, "invalid"); return; }
  target = member->boolean;
}

inline void read_real(const json::Value& parent, const char* name, double& target, double lowest, double highest, const std::string& path, Problems& problems) {
  const json::Value* member = parent.get(name);
  if (member == nullptr) return;
  if (!member->is_number()) { bad(problems, path, "invalid"); return; }
  if (member->number < lowest || member->number > highest) { bad(problems, path, "range"); return; }
  target = member->number;
}

inline void read_int(const json::Value& parent, const char* name, int& target, long long lowest, long long highest, const std::string& path, Problems& problems) {
  const json::Value* member = parent.get(name);
  if (member == nullptr) return;
  if (!member->is_number()) { bad(problems, path, "invalid"); return; }
  const long long value = parent.integer_or(name, lowest - 1, lowest, highest);
  if (value < lowest || value > highest) { bad(problems, path, "range"); return; }
  target = static_cast<int>(value);
}
}  // namespace detail

// Applies the members present in `document` on top of `settings`; whatever the document omits stays as it was. The result is checked
// with validate(): read_settings() only reports a member of the wrong type or size.
inline void read_settings(const json::Value& document, Settings& s, Problems& problems) {
  using namespace detail;
  if (!document.is_object()) { bad(problems, "", "invalid"); return; }
  if (const json::Value* node = document.get("node"); node != nullptr && node->is_object()) {
    read_text(*node, "id", s.node_id, kMaxNodeIdLength, "node.id", problems);
    read_text(*node, "name", s.node_name, 48, "node.name", problems);
  }
  if (document.get("uplink") != nullptr && !read_choice<Uplink>(document, "uplink", {{"ethernet", Uplink::kEthernet}, {"wifi", Uplink::kWifi}}, s.uplink)) bad(problems, "uplink", "invalid");
  if (const json::Value* ip = document.get("ip"); ip != nullptr && ip->is_object()) {
    read_bool(*ip, "dhcp", s.ip.dhcp, "ip.dhcp", problems);
    read_text(*ip, "address", s.ip.address, 15, "ip.address", problems);
    read_text(*ip, "netmask", s.ip.netmask, 15, "ip.netmask", problems);
    read_text(*ip, "gateway", s.ip.gateway, 15, "ip.gateway", problems);
    read_text(*ip, "dns1", s.ip.dns1, 15, "ip.dns1", problems);
    read_text(*ip, "dns2", s.ip.dns2, 15, "ip.dns2", problems);
    read_text(*ip, "hostname", s.ip.hostname, 32, "ip.hostname", problems);
  }
  if (const json::Value* ap = document.get("ap"); ap != nullptr && ap->is_object()) {
    read_bool(*ap, "enabled", s.ap.enabled, "ap.enabled", problems);
    read_text(*ap, "ssid", s.ap.ssid, 32, "ap.ssid", problems);
    if (!read_choice<WifiSecurity>(*ap, "security", {{"open", WifiSecurity::kOpen}, {"wpa2", WifiSecurity::kWpa2}, {"wpa3", WifiSecurity::kWpa3}, {"wpa2wpa3", WifiSecurity::kWpa2Wpa3}}, s.ap.security)) bad(problems, "ap.security", "invalid");
    read_secret(*ap, "password", s.ap.password, "ap.password", problems);
    read_int(*ap, "channel", s.ap.channel, 0, 13, "ap.channel", problems);
    read_bool(*ap, "hidden", s.ap.hidden, "ap.hidden", problems);
    read_int(*ap, "max_clients", s.ap.max_clients, 1, 10, "ap.max_clients", problems);
    read_int(*ap, "tx_power_dbm", s.ap.tx_power_dbm, 2, 20, "ap.tx_power_dbm", problems);
    read_int(*ap, "bandwidth_mhz", s.ap.bandwidth_mhz, 20, 40, "ap.bandwidth_mhz", problems);
    read_text(*ap, "country", s.ap.country, 2, "ap.country", problems);
    read_bool(*ap, "bridge", s.ap.bridge, "ap.bridge", problems);
  }
  if (const json::Value* sta = document.get("sta"); sta != nullptr && sta->is_object()) {
    read_bool(*sta, "enabled", s.sta.enabled, "sta.enabled", problems);
    read_text(*sta, "ssid", s.sta.ssid, 32, "sta.ssid", problems);
    read_secret(*sta, "password", s.sta.password, "sta.password", problems);
    if (const json::Value* backup = sta->get("backup"); backup != nullptr) {
      if (!backup->is_array() || backup->items.size() > kMaxBackupNetworks) bad(problems, "sta.backup", "invalid");
      else for (std::size_t i = 0; i < backup->items.size(); ++i) {
        const json::Value& item = backup->items[i];
        const std::string base = "sta.backup." + std::to_string(i) + ".";
        Network network;
        if (!item.is_object()) { bad(problems, base + "ssid", "invalid"); continue; }
        read_text(item, "ssid", network.ssid, 32, base + "ssid", problems);
        read_secret(item, "password", network.password, base + "password", problems);
        s.sta.backup.push_back(network);
      }
    }
  }
  if (const json::Value* mqtt = document.get("mqtt"); mqtt != nullptr && mqtt->is_object()) {
    read_bool(*mqtt, "enabled", s.mqtt.enabled, "mqtt.enabled", problems);
    read_text(*mqtt, "uri", s.mqtt.uri, 160, "mqtt.uri", problems);
    read_text(*mqtt, "username", s.mqtt.username, 64, "mqtt.username", problems);
    read_secret(*mqtt, "password", s.mqtt.password, "mqtt.password", problems);
    read_int(*mqtt, "heartbeat_s", s.mqtt.heartbeat_s, 2, 300, "mqtt.heartbeat_s", problems);
    read_int(*mqtt, "telemetry_ms", s.mqtt.telemetry_ms, 200, 5000, "mqtt.telemetry_ms", problems);
    read_text(*mqtt, "ntp", s.mqtt.ntp, 64, "mqtt.ntp", problems);
    if (const json::Value* backup = mqtt->get("backup"); backup != nullptr) {
      if (!backup->is_array() || backup->items.size() > kMaxBackupBrokers) bad(problems, "mqtt.backup", "invalid");
      else for (std::size_t i = 0; i < backup->items.size(); ++i) {
        const json::Value& item = backup->items[i];
        const std::string base = "mqtt.backup." + std::to_string(i) + ".";
        Broker broker;
        if (!item.is_object()) { bad(problems, base + "uri", "invalid"); continue; }
        read_text(item, "uri", broker.uri, 160, base + "uri", problems);
        read_text(item, "username", broker.username, 64, base + "username", problems);
        read_secret(item, "password", broker.password, base + "password", problems);
        s.mqtt.backup.push_back(broker);
      }
    }
  }
  if (const json::Value* radars = document.get("radars"); radars != nullptr) {
    if (!radars->is_array() || radars->items.size() > kRadarCount) bad(problems, "radars", "invalid");
    else for (std::size_t i = 0; i < radars->items.size(); ++i) {
      const json::Value& item = radars->items[i];
      const std::string base = "radars." + std::to_string(i) + ".";
      if (!item.is_object()) { bad(problems, base + "enabled", "invalid"); continue; }
      read_bool(item, "enabled", s.radars[i].enabled, base + "enabled", problems);
      read_text(item, "model", s.radars[i].model, 12, base + "model", problems);
      read_int(item, "baud", s.radars[i].baud, 0, 921600, base + "baud", problems);
      read_text(item, "name", s.radars[i].name, 24, base + "name", problems);
      read_int(item, "rx", s.radars[i].rx, -1, board::kLastGpio, base + "rx", problems);
      read_int(item, "tx", s.radars[i].tx, -1, board::kLastGpio, base + "tx", problems);
    }
  }
  if (const json::Value* sensors = document.get("sensors"); sensors != nullptr && sensors->is_object()) {
    read_bool(*sensors, "veml7700", s.sensors.veml7700, "sensors.veml7700", problems);
    read_int(*sensors, "sda", s.sensors.sda, 0, board::kLastGpio, "sensors.sda", problems);
    read_int(*sensors, "scl", s.sensors.scl, 0, board::kLastGpio, "sensors.scl", problems);
    read_int(*sensors, "lux_fallback", s.sensors.lux_fallback, -1, 200000, "sensors.lux_fallback", problems);
  }
  if (const json::Value* pins = document.get("pins"); pins != nullptr) {
    if (!pins->is_array() || pins->items.size() > kMaxMappedPins) { bad(problems, "pins", pins->is_array() ? "too_many" : "invalid"); }
    else {
      s.pins.clear();
      for (std::size_t i = 0; i < pins->items.size(); ++i) {
        const json::Value& item = pins->items[i];
        const std::string base = "pins." + std::to_string(i) + ".";
        MappedPin pin;
        if (!item.is_object()) { bad(problems, base + "gpio", "invalid"); continue; }
        read_int(item, "gpio", pin.gpio, 0, board::kLastGpio, base + "gpio", problems);
        read_text(item, "name", pin.name, 24, base + "name", problems);
        if (!read_choice<PinMode>(item, "mode", {{"disabled", PinMode::kDisabled}, {"input", PinMode::kInput}, {"output", PinMode::kOutput}, {"pwm", PinMode::kPwm}, {"adc", PinMode::kAdc}}, pin.mode)) bad(problems, base + "mode", "invalid");
        read_bool(item, "invert", pin.invert, base + "invert", problems);
        if (!read_choice<Pull>(item, "pull", {{"none", Pull::kNone}, {"up", Pull::kUp}, {"down", Pull::kDown}}, pin.pull)) bad(problems, base + "pull", "invalid");
        read_bool(item, "initial_on", pin.initial_on, base + "initial_on", problems);
        if (!read_choice<SafeState>(item, "safe", {{"off", SafeState::kOff}, {"on", SafeState::kOn}, {"keep", SafeState::kKeep}}, pin.safe)) bad(problems, base + "safe", "invalid");
        read_int(item, "link_timeout_s", pin.link_timeout_s, 0, 86400, base + "link_timeout_s", problems);
        read_int(item, "pulse_ms", pin.pulse_ms, 0, 3600000, base + "pulse_ms", problems);
        read_int(item, "debounce_ms", pin.debounce_ms, 0, 5000, base + "debounce_ms", problems);
        read_int(item, "period_s", pin.period_s, 1, 3600, base + "period_s", problems);
        read_int(item, "freq_hz", pin.freq_hz, 1, 40000, base + "freq_hz", problems);
        read_text(item, "report", pin.report, 12, base + "report", problems);
        read_real(item, "scale", pin.scale, -1000000.0, 1000000.0, base + "scale", problems);
        read_real(item, "offset", pin.offset, -1000000.0, 1000000.0, base + "offset", problems);
        s.pins.push_back(pin);
      }
    }
  }
  if (const json::Value* ble = document.get("ble"); ble != nullptr && ble->is_object()) {
    if (!read_choice<BleMode>(*ble, "mode", {{"off", BleMode::kOff}, {"setup", BleMode::kSetup}, {"always", BleMode::kAlways}}, s.ble)) bad(problems, "ble.mode", "invalid");
  }
  if (const json::Value* web = document.get("web"); web != nullptr && web->is_object()) {
    if (!read_choice<WebMode>(*web, "mode", {{"http", WebMode::kHttp}, {"both", WebMode::kBoth}, {"https", WebMode::kHttps}}, s.web)) bad(problems, "web.mode", "invalid");
  }
  if (const json::Value* ui = document.get("ui"); ui != nullptr && ui->is_object()) read_text(*ui, "language", s.language, 4, "ui.language", problems);
  if (const json::Value* system = document.get("system"); system != nullptr && system->is_object()) read_int(*system, "auto_restart_hours", s.auto_restart_hours, 0, 48, "system.auto_restart_hours", problems);
}

// ---- checking ------------------------------------------------------------------------------------------------------------------

inline bool language_is_known(std::string_view code) {
  for (const char* known : {"en", "es", "de", "fr", "it", "ja", "zh"}) if (code == known) return true;
  return false;
}

inline bool auto_restart_hours_is_valid(int hours) {
  for (const int known : {0, 1, 2, 3, 4, 6, 12, 24, 48}) if (hours == known) return true;
  return false;
}

inline bool valid_slug(std::string_view name) {
  if (name.empty() || name.size() > 24) return false;
  for (const char c : name) if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_')) return false;
  return true;
}

inline bool broker_uri_is_valid(std::string_view uri) {
  std::string_view rest;
  if (uri.substr(0, 7) == "mqtt://") rest = uri.substr(7);
  else if (uri.substr(0, 8) == "mqtts://") rest = uri.substr(8);
  else return false;
  const std::size_t colon = rest.find(':');
  const std::string_view host = rest.substr(0, colon);
  if (!net::valid_host(host)) return false;
  if (colon == std::string_view::npos) return true;
  const std::string_view port = rest.substr(colon + 1);
  if (port.empty() || port.size() > 5) return false;
  unsigned value = 0;
  for (const char c : port) { if (c < '0' || c > '9') return false; value = value * 10 + static_cast<unsigned>(c - '0'); }
  return value >= 1 && value <= 65535;
}

namespace detail {
// The pins already claimed, to catch two uses of one GPIO.
struct PinClaims {
  std::array<std::string, board::kLastGpio + 1> owner;
  bool claim(int gpio, const std::string& who, std::string& other) {
    if (gpio < 0 || gpio > board::kLastGpio) return true;
    if (!owner[static_cast<std::size_t>(gpio)].empty()) { other = owner[static_cast<std::size_t>(gpio)]; return false; }
    owner[static_cast<std::size_t>(gpio)] = who;
    return true;
  }
};
}  // namespace detail

inline Problems validate(const Settings& s) {
  using detail::bad;
  Problems problems;
  if (!node_id_is_valid(s.node_id)) bad(problems, "node.id", "invalid");
  std::size_t name_characters = 0;
  if (s.node_name.empty()) bad(problems, "node.name", "required");
  else if (!net::valid_utf8(s.node_name, &name_characters) || name_characters > 48) bad(problems, "node.name", "invalid");
  if (!language_is_known(s.language)) bad(problems, "ui.language", "invalid");
  if (!auto_restart_hours_is_valid(s.auto_restart_hours)) bad(problems, "system.auto_restart_hours", "invalid");

  // network
  if (s.uplink == Uplink::kEthernet && !board::kHasEthernet) bad(problems, "uplink", "not_available");   // this board has no cable
  if (!s.ip.dhcp) {   // a fixed address is for whichever connection the node uses, the cable or the Wi-Fi station
    std::uint32_t address = 0, mask = 0, gateway = 0, dns = 0;
    const bool address_ok = net::parse_ipv4(s.ip.address, address), mask_ok = net::parse_ipv4(s.ip.netmask, mask) && net::valid_netmask(mask);
    if (!address_ok || !net::usable_host_address(address)) bad(problems, "ip.address", s.ip.address.empty() ? "required" : "invalid");
    if (!mask_ok) bad(problems, "ip.netmask", s.ip.netmask.empty() ? "required" : "invalid");
    if (address_ok && mask_ok && net::is_network_or_broadcast(address, mask)) bad(problems, "ip.address", "invalid");
    if (!net::parse_ipv4(s.ip.gateway, gateway) || !net::usable_host_address(gateway)) bad(problems, "ip.gateway", s.ip.gateway.empty() ? "required" : "invalid");
    else if (address_ok && mask_ok && !net::same_subnet(address, gateway, mask)) bad(problems, "ip.gateway", "outside_subnet");
    else if (address_ok && gateway == address) bad(problems, "ip.gateway", "conflict");
    if (!s.ip.dns1.empty() && !net::parse_ipv4(s.ip.dns1, dns)) bad(problems, "ip.dns1", "invalid");
    if (!s.ip.dns2.empty() && !net::parse_ipv4(s.ip.dns2, dns)) bad(problems, "ip.dns2", "invalid");
  }
  if (!s.ip.hostname.empty() && !net::valid_hostname(s.ip.hostname)) bad(problems, "ip.hostname", "invalid");

  // Wi-Fi
  if (s.ap.enabled) {
    if (!net::valid_ssid(s.ap.ssid)) bad(problems, "ap.ssid", s.ap.ssid.empty() ? "required" : "invalid");
    if (s.ap.security != WifiSecurity::kOpen && !net::valid_wpa_passphrase(s.ap.password)) bad(problems, "ap.password", s.ap.password.empty() ? "required" : "invalid_key");
  }
  if (s.ap.country.size() != 2 || !(s.ap.country[0] >= 'A' && s.ap.country[0] <= 'Z' && s.ap.country[1] >= 'A' && s.ap.country[1] <= 'Z')) bad(problems, "ap.country", "invalid");
  if (s.sta.enabled) {
    if (!net::valid_ssid(s.sta.ssid)) bad(problems, "sta.ssid", s.sta.ssid.empty() ? "required" : "invalid");
    if (!s.sta.password.empty() && !net::valid_wpa_passphrase(s.sta.password)) bad(problems, "sta.password", "invalid_key");
    for (std::size_t i = 0; i < s.sta.backup.size(); ++i) {
      const std::string base = "sta.backup." + std::to_string(i) + ".";
      const Network& network = s.sta.backup[i];
      if (!net::valid_ssid(network.ssid)) bad(problems, base + "ssid", network.ssid.empty() ? "required" : "invalid");
      if (!network.password.empty() && !net::valid_wpa_passphrase(network.password)) bad(problems, base + "password", "invalid_key");
    }
  }
  if (s.uplink == Uplink::kWifi && !s.sta.enabled) bad(problems, "sta.enabled", "required");

  // broker
  if (s.mqtt.enabled) {
    if (s.mqtt.uri.empty()) bad(problems, "mqtt.uri", "required");
    else if (!broker_uri_is_valid(s.mqtt.uri)) bad(problems, "mqtt.uri", "invalid");
    if (!net::valid_host(s.mqtt.ntp)) bad(problems, "mqtt.ntp", "invalid");
    for (std::size_t i = 0; i < s.mqtt.backup.size(); ++i) {
      const std::string base = "mqtt.backup." + std::to_string(i) + ".";
      if (!broker_uri_is_valid(s.mqtt.backup[i].uri)) bad(problems, base + "uri", s.mqtt.backup[i].uri.empty() ? "required" : "invalid");
    }
  }

  // pins: the radars, the light sensor and the mapped pins may not share a GPIO, and none may be a reserved one
  std::vector<std::string> names;   // the names of the devices this node offers (presence sensors and mapped pins share one namespace)
  detail::PinClaims claims;
  const auto claim = [&](int gpio, const std::string& who, const std::string& path, bool allow_sd) {
    if (!board::assignable(gpio, allow_sd)) { bad(problems, path, board::pin_info(gpio).use == board::PinUse::kSdCard ? "sd_card" : "reserved"); return; }
    std::string other;
    if (!claims.claim(gpio, who, other)) bad(problems, path, "conflict");
  };
  for (std::size_t i = 0; i < kRadarCount; ++i) {
    const RadarLine& radar = s.radars[i];
    if (!radar.enabled) continue;
    const std::string base = "radars." + std::to_string(i) + ".", who = "radar" + std::to_string(i + 1);
    sensors::Model model;
    if (!sensors::from_text(radar.model, model)) bad(problems, base + "model", "invalid");
    else {
      static const int kSpeeds[] = {9600, 19200, 38400, 57600, 115200, 230400, 256000, 460800};
      if (radar.baud != 0 && std::find(std::begin(kSpeeds), std::end(kSpeeds), radar.baud) == std::end(kSpeeds)) bad(problems, base + "baud", "range");
      if (!sensors::info(model).tracker) {   // a presence sensor is a device: it needs a name for its topic
        if (!valid_slug(radar.name)) bad(problems, base + "name", radar.name.empty() ? "required" : "invalid");
        else if (std::find(names.begin(), names.end(), radar.name) != names.end()) bad(problems, base + "name", "conflict");
        else names.push_back(radar.name);
      }
    }
    if (radar.rx < 0) bad(problems, base + "rx", "required"); else claim(radar.rx, who, base + "rx", true);
    if (radar.tx >= 0) claim(radar.tx, who, base + "tx", true);
  }
  if (s.sensors.veml7700) {
    claim(s.sensors.sda, "i2c", "sensors.sda", true);
    claim(s.sensors.scl, "i2c", "sensors.scl", true);
  }
  for (std::size_t i = 0; i < s.pins.size(); ++i) {
    const MappedPin& pin = s.pins[i];
    const std::string base = "pins." + std::to_string(i) + ".";
    if (pin.mode == PinMode::kDisabled) continue;
    if (pin.gpio < 0) { bad(problems, base + "gpio", "required"); continue; }
    claim(pin.gpio, "pin", base + "gpio", true);
    if (!valid_slug(pin.name)) bad(problems, base + "name", pin.name.empty() ? "required" : "invalid");
    else if (std::find(names.begin(), names.end(), pin.name) != names.end()) bad(problems, base + "name", "conflict");
    else names.push_back(pin.name);
    if (pin.mode == PinMode::kAdc && !board::pin_info(pin.gpio).adc1) bad(problems, base + "mode", "no_adc");
    if (!report_allowed(pin.mode, report_of(pin))) bad(problems, base + "report", "invalid");
  }
  if (std::count_if(s.pins.begin(), s.pins.end(), [](const MappedPin& p) { return p.mode == PinMode::kPwm; }) > 8) bad(problems, "pins", "too_many_pwm");
  // the chip has four PWM timers, one frequency each
  std::vector<int> frequencies;
  for (const MappedPin& pin : s.pins) if (pin.mode == PinMode::kPwm && std::find(frequencies.begin(), frequencies.end(), pin.freq_hz) == frequencies.end()) frequencies.push_back(pin.freq_hz);
  if (frequencies.size() > 4) bad(problems, "pins", "too_many_pwm_freq");
  return problems;
}

// ---- writing -------------------------------------------------------------------------------------------------------------------

// `secrets`: true writes the passwords (for flash storage); false replaces them with "password_set" flags (for the panel).
inline std::string to_json(const Settings& s, bool secrets) {
  json::Writer w;
  w.begin_object();
  w.field("v", kVersion);
  w.key("node").begin_object().field("id", s.node_id).field("name", s.node_name).end_object();
  w.field("uplink", to_text(s.uplink));
  w.key("ip").begin_object().field("dhcp", s.ip.dhcp).field("address", s.ip.address).field("netmask", s.ip.netmask).field("gateway", s.ip.gateway)
      .field("dns1", s.ip.dns1).field("dns2", s.ip.dns2).field("hostname", s.ip.hostname).end_object();
  w.key("ap").begin_object().field("enabled", s.ap.enabled).field("ssid", s.ap.ssid).field("security", to_text(s.ap.security));
  if (secrets) w.field("password", s.ap.password); else w.field("password_set", !s.ap.password.empty());
  w.field("channel", s.ap.channel).field("hidden", s.ap.hidden).field("max_clients", s.ap.max_clients).field("tx_power_dbm", s.ap.tx_power_dbm)
      .field("bandwidth_mhz", s.ap.bandwidth_mhz).field("country", s.ap.country).field("bridge", s.ap.bridge).end_object();
  w.key("sta").begin_object().field("enabled", s.sta.enabled).field("ssid", s.sta.ssid);
  if (secrets) w.field("password", s.sta.password); else w.field("password_set", !s.sta.password.empty());
  w.key("backup").begin_array();
  for (const Network& network : s.sta.backup) {
    w.begin_object().field("ssid", network.ssid);
    if (secrets) w.field("password", network.password); else w.field("password_set", !network.password.empty());
    w.end_object();
  }
  w.end_array();
  w.end_object();
  w.key("mqtt").begin_object().field("enabled", s.mqtt.enabled).field("uri", s.mqtt.uri).field("username", s.mqtt.username);
  if (secrets) w.field("password", s.mqtt.password); else w.field("password_set", !s.mqtt.password.empty());
  w.field("heartbeat_s", s.mqtt.heartbeat_s).field("telemetry_ms", s.mqtt.telemetry_ms).field("ntp", s.mqtt.ntp);
  w.key("backup").begin_array();
  for (const Broker& broker : s.mqtt.backup) {
    w.begin_object().field("uri", broker.uri).field("username", broker.username);
    if (secrets) w.field("password", broker.password); else w.field("password_set", !broker.password.empty());
    w.end_object();
  }
  w.end_array();
  w.end_object();
  w.key("radars").begin_array();
  for (const RadarLine& radar : s.radars) w.begin_object().field("enabled", radar.enabled).field("model", radar.model).field("baud", radar.baud).field("name", radar.name).field("rx", radar.rx).field("tx", radar.tx).end_object();
  w.end_array();
  w.key("sensors").begin_object().field("veml7700", s.sensors.veml7700).field("sda", s.sensors.sda).field("scl", s.sensors.scl).field("lux_fallback", s.sensors.lux_fallback).end_object();
  w.key("pins").begin_array();
  for (const MappedPin& pin : s.pins) {
    w.begin_object().field("gpio", pin.gpio).field("name", pin.name).field("mode", to_text(pin.mode)).field("invert", pin.invert).field("pull", to_text(pin.pull))
        .field("initial_on", pin.initial_on).field("safe", to_text(pin.safe)).field("link_timeout_s", pin.link_timeout_s).field("pulse_ms", pin.pulse_ms)
        .field("debounce_ms", pin.debounce_ms).field("period_s", pin.period_s).field("freq_hz", pin.freq_hz).field("report", report_of(pin)).field("scale", pin.scale)
        .field("offset", pin.offset).end_object();
  }
  w.end_array();
  w.key("ble").begin_object().field("mode", to_text(s.ble)).end_object();
  w.key("web").begin_object().field("mode", to_text(s.web)).end_object();
  w.key("ui").begin_object().field("language", s.language).end_object();
  w.key("system").begin_object().field("auto_restart_hours", s.auto_restart_hours).end_object();
  w.end_object();
  return w.str();
}

// Reads a stored or received document on top of `base` and checks the result. Nothing is applied unless `problems` stays empty.
inline bool load(std::string_view text, const Settings& base, Settings& out, Problems& problems) {
  json::Value document;
  if (!json::parse(text, document)) { problems.push_back({"", "not_json"}); return false; }
  out = base;
  read_settings(document, out, problems);
  if (problems.empty()) problems = validate(out);
  return problems.empty();
}

}  // namespace armor::config
