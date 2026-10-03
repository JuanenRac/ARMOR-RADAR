// ARMOR-RADAR - host tests for the node's settings, pins, panel logins, mapped pins and the LD2450 command channel.
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D). GPL-3.0-or-later.
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include "../main/core/auth.hpp"
#include "../main/core/ble_dispatch.hpp"
#include "../main/core/web_policy.hpp"
#include "../main/core/board_pins.hpp"
#include "../main/core/gpio_logic.hpp"
#include "../main/core/json.hpp"
#include "../main/core/ld2450_command.hpp"
#include "../main/core/net_text.hpp"
#include "../main/core/network_plan.hpp"
#include "../main/core/node_config.hpp"
#include "../main/core/telemetry_json.hpp"

static int failures = 0;
static int checks = 0;
#define CHECK(condition)                                                              \
  do {                                                                                \
    ++checks;                                                                         \
    if (!(condition)) {                                                               \
      ++failures;                                                                     \
      std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #condition);                \
    }                                                                                 \
  } while (0)

using namespace armor;

static bool has_problem(const config::Problems& problems, const char* path, const char* code) {
  for (const config::Problem& problem : problems) if (problem.path == path && problem.code == code) return true;
  return false;
}

// ---- JSON ----------------------------------------------------------------------------------------------------------------------

static void test_json() {
  json::Value v;
  CHECK(json::parse(R"({"a":1,"b":[true,false,null],"c":{"d":"x\ny"},"e":-2.5e2})", v));
  CHECK(v.is_object() && v.number_or("a", 0) == 1 && v.get("b")->items.size() == 3 && v.get("b")->items[0].boolean);
  CHECK(v.get("c")->string_or("d", "") == "x\ny");
  CHECK(v.number_or("e", 0) == -250.0);
  CHECK(v.get("missing") == nullptr && v.string_or("missing", "fallback") == "fallback");
  CHECK(v.integer_or("a", -1, 0, 10) == 1 && v.integer_or("a", -1, 5, 10) == -1 && v.integer_or("e", 7, -1000, 1000) == -250);

  // unicode escapes, a surrogate pair, and raw UTF-8 pass through
  CHECK(json::parse(R"("\u00e9\ud83d\ude00 ok")", v) && v.text == "\xC3\xA9\xF0\x9F\x98\x80 ok");
  CHECK(json::parse("\"caf\xC3\xA9\"", v) && v.text == "caf\xC3\xA9");

  // strictness
  CHECK(!json::parse("", v));
  CHECK(!json::parse("{", v));
  CHECK(!json::parse("{\"a\":1,}", v));
  CHECK(!json::parse("[1,]", v));
  CHECK(!json::parse("{'a':1}", v));
  CHECK(!json::parse("{\"a\":1} x", v));
  CHECK(!json::parse("01", v));
  CHECK(!json::parse("1.", v));
  CHECK(!json::parse("\"a\nb\"", v));
  CHECK(!json::parse("\"\\x\"", v));
  CHECK(!json::parse("\"\\ud800\"", v));
  CHECK(!json::parse("nul", v));
  CHECK(!json::parse("1e999", v));
  CHECK(json::parse("  [ ]  ", v) && v.is_array() && v.items.empty());

  // depth and size are bounded
  std::string deep(json::kMaxDepth + 3, '[');
  deep += std::string(json::kMaxDepth + 3, ']');
  CHECK(!json::parse(deep, v));
  CHECK(!json::parse(std::string(json::kMaxText + 1, ' ') + "1", v));

  // the first of a repeated name wins, and a non-object has no members
  CHECK(json::parse(R"({"a":1,"a":2})", v) && v.number_or("a", 0) == 1);
  CHECK(json::parse("5", v) && v.get("a") == nullptr);

  // writer
  json::Writer w;
  w.begin_object().field("s", "a\"b\\c\n").field("n", 3).field("f", 0.0015).field("b", true).key("l").begin_array().integer(1).string("x").begin_object().end_object().end_array().key("z").null().end_object();
  CHECK(w.str() == R"({"s":"a\"b\\c\n","n":3,"f":0.0015,"b":true,"l":[1,"x",{}],"z":null})");
  CHECK(json::parse(w.str(), v));
  CHECK(json::quote(std::string("\x01", 1)) == "\"\\u0001\"");
  json::Writer nan_writer;
  nan_writer.begin_array().number(std::nan("")).end_array();
  CHECK(nan_writer.str() == "[null]");
}

// ---- network text --------------------------------------------------------------------------------------------------------------

static void test_net_text() {
  std::uint32_t value = 0;
  CHECK(net::parse_ipv4("192.168.0.180", value) && value == 0xC0A800B4u);
  CHECK(net::ipv4_text(value) == "192.168.0.180");
  CHECK(!net::parse_ipv4("192.168.0", value));
  CHECK(!net::parse_ipv4("192.168.0.256", value));
  CHECK(!net::parse_ipv4("192.168.0.01", value));
  CHECK(!net::parse_ipv4("192.168.0.1.5", value));
  CHECK(!net::parse_ipv4("192.168.0.1 ", value));
  CHECK(!net::parse_ipv4("a.b.c.d", value));
  CHECK(!net::parse_ipv4("", value));
  CHECK(net::parse_ipv4("0.0.0.0", value) && value == 0);
  std::uint32_t mask = 0;
  CHECK(net::parse_ipv4("255.255.255.0", mask) && net::valid_netmask(mask));
  CHECK(net::parse_ipv4("255.255.252.0", mask) && net::valid_netmask(mask));
  CHECK(net::parse_ipv4("255.255.255.252", mask) && net::valid_netmask(mask));
  CHECK(net::parse_ipv4("255.255.255.254", mask) && !net::valid_netmask(mask));
  CHECK(net::parse_ipv4("255.0.255.0", mask) && !net::valid_netmask(mask));
  CHECK(net::parse_ipv4("0.0.0.0", mask) && !net::valid_netmask(mask));
  CHECK(net::usable_host_address(0xC0A80001u) && !net::usable_host_address(0x7F000001u) && !net::usable_host_address(0xE0000001u) && !net::usable_host_address(1));
  CHECK(net::valid_hostname("armor-node1") && !net::valid_hostname("-x") && !net::valid_hostname("x-") && !net::valid_hostname("Upper") && !net::valid_hostname(""));
  CHECK(net::valid_host("broker.example.com") && net::valid_host("192.168.0.180") && !net::valid_host("a b") && !net::valid_host(".x"));
  CHECK(net::valid_ssid("ARMOR") && !net::valid_ssid("") && !net::valid_ssid(std::string(33, 'a')) && net::valid_ssid(std::string(32, 'a')));
  CHECK(net::valid_wpa_passphrase("12345678") && !net::valid_wpa_passphrase("1234567") && !net::valid_wpa_passphrase(std::string(64, 'a')) && !net::valid_wpa_passphrase("abcdefgh\xC3\xA9"));
}

// ---- pins ----------------------------------------------------------------------------------------------------------------------

static void test_pins() {
  using board::PinUse;
  using board::Reserved;
  for (int gpio : {9, 10, 11, 12, 13, 14}) CHECK(board::pin_info(gpio).reason == Reserved::kEthernet && !board::assignable(gpio));
  for (int gpio : {26, 27, 28, 29, 30, 31, 32}) CHECK(board::pin_info(gpio).reason == Reserved::kFlash && !board::assignable(gpio));
  for (int gpio : {33, 34, 35, 36, 37}) CHECK(board::pin_info(gpio).reason == Reserved::kPsram && !board::assignable(gpio));
  CHECK(!board::assignable(19) && !board::assignable(20) && board::pin_info(19).reason == Reserved::kUsb);
  for (int gpio : {22, 23, 24, 25, -1, 49, 100}) CHECK(board::pin_info(gpio).reason == Reserved::kNoSuchPin && !board::assignable(gpio));
  CHECK(board::pin_info(8).reason == Reserved::kCamera && !board::assignable(8));
  for (int gpio : {4, 5, 6, 7}) CHECK(board::pin_info(gpio).use == PinUse::kSdCard && board::assignable(gpio, true) && !board::assignable(gpio, false));
  for (int gpio : {0, 3, 45, 46}) CHECK(board::pin_info(gpio).use == PinUse::kCaution && board::assignable(gpio));
  for (int gpio : {1, 2, 15, 16, 17, 18, 21, 38, 39, 40, 41, 42, 43, 44, 47, 48}) CHECK(board::pin_info(gpio).use == PinUse::kFree && board::assignable(gpio));
  // the defaults of the radars and the light sensor are all assignable and distinct
  std::vector<int> defaults;
  for (int gpio : board::kDefaultRadarRx) defaults.push_back(gpio);
  for (int gpio : board::kDefaultRadarTx) defaults.push_back(gpio);
  defaults.push_back(board::kDefaultI2cSda);
  defaults.push_back(board::kDefaultI2cScl);
  for (std::size_t i = 0; i < defaults.size(); ++i) {
    CHECK(board::assignable(defaults[i]));
    for (std::size_t j = i + 1; j < defaults.size(); ++j) CHECK(defaults[i] != defaults[j]);
  }
  CHECK(board::pin_info(1).adc1 && board::pin_info(3).adc1 && !board::pin_info(21).adc1 && !board::pin_info(38).adc1);
}

// ---- settings ------------------------------------------------------------------------------------------------------------------

static config::Settings valid_settings() {
  config::Settings s = config::default_settings("a1b2c3");
  s.mqtt.enabled = true;
  s.mqtt.uri = "mqtt://192.168.0.180:18883";
  s.mqtt.username = "field-node-a1b2c3";
  s.mqtt.password = "secret-password";
  return s;
}

static void test_settings_defaults_and_roundtrip() {
  const config::Settings defaults = config::default_settings("a1b2c3");
  CHECK(defaults.node_id == "armor-a1b2c3" && node_id_is_valid(defaults.node_id));
  CHECK(defaults.radars[0].rx == 16 && defaults.radars[1].rx == 17 && defaults.radars[2].rx == 18 && defaults.radars[0].tx == 15 && defaults.radars[1].tx == 21 && defaults.radars[2].tx == 38);
  CHECK(config::validate(defaults).empty());  // a node that was never configured is valid: DHCP, no access point, no broker yet needed

  config::Settings s = valid_settings();
  s.ap.enabled = true; s.ap.ssid = "ARMOR-perimetro"; s.ap.password = "wifi-secret-1"; s.ap.channel = 6; s.ap.hidden = true;
  s.sta.ssid = "home"; s.sta.password = "sta-password";
  s.sta.backup = {{"guest", "guest-password"}, {"office", "office-password"}};
  s.mqtt.backup = {{"mqtt://10.0.0.5:1883", "backup-user", "backup-password"}};
  s.auto_restart_hours = 12;
  s.ip.dhcp = false; s.ip.address = "192.168.0.181"; s.ip.gateway = "192.168.0.1"; s.ip.dns1 = "192.168.0.1"; s.ip.hostname = "armor-1";
  config::MappedPin light; light.gpio = 39; light.name = "garden_light"; light.mode = config::PinMode::kOutput; light.invert = true; light.safe = config::SafeState::kOff; light.link_timeout_s = 60; light.pulse_ms = 800;
  config::MappedPin door; door.gpio = 40; door.name = "gate_contact"; door.mode = config::PinMode::kInput; door.pull = config::Pull::kUp; door.report = "open";
  config::MappedPin battery; battery.gpio = 2; battery.name = "battery"; battery.mode = config::PinMode::kAdc; battery.report = "battery"; battery.scale = 0.0057; battery.offset = -0.1;
  s.pins = {light, door, battery};
  s.sensors.sda = 41; s.sensors.scl = 42;
  CHECK(config::validate(s).empty());

  // stored form keeps the secrets and reads back the same
  const std::string stored = config::to_json(s, true);
  config::Settings back;
  config::Problems problems;
  CHECK(config::load(stored, config::default_settings("000000"), back, problems));
  CHECK(problems.empty());
  CHECK(back.node_id == s.node_id && back.mqtt.password == "secret-password" && back.ap.password == "wifi-secret-1" && back.sta.password == "sta-password");
  CHECK(back.sta.backup.size() == 2 && back.sta.backup[0].ssid == "guest" && back.sta.backup[0].password == "guest-password" && back.sta.backup[1].ssid == "office");
  CHECK(back.mqtt.backup.size() == 1 && back.mqtt.backup[0].uri == "mqtt://10.0.0.5:1883" && back.mqtt.backup[0].username == "backup-user" && back.mqtt.backup[0].password == "backup-password");
  CHECK(back.auto_restart_hours == 12);
  CHECK(back.pins.size() == 3 && back.pins[0].name == "garden_light" && back.pins[0].invert && back.pins[0].safe == config::SafeState::kOff && back.pins[0].link_timeout_s == 60);
  CHECK(back.pins[1].report == "open" && back.pins[1].pull == config::Pull::kUp && back.pins[2].scale == 0.0057 && back.pins[2].offset == -0.1);
  CHECK(!back.ip.dhcp && back.ip.address == "192.168.0.181" && back.ap.channel == 6 && back.ap.hidden);
  CHECK(config::to_json(back, true) == stored);

  // the panel's form never carries a password, only whether there is one
  const std::string shown = config::to_json(s, false);
  CHECK(shown.find("secret-password") == std::string::npos && shown.find("wifi-secret-1") == std::string::npos && shown.find("sta-password") == std::string::npos);
  json::Value document;
  CHECK(json::parse(shown, document));
  CHECK(document.get("mqtt")->bool_or("password_set", false) && document.get("ap")->bool_or("password_set", false));
}

static void test_settings_partial_update_and_secrets() {
  const config::Settings base = valid_settings();
  config::Settings out;
  config::Problems problems;
  // a partial document changes only what it names, and a missing or empty password keeps the stored one
  CHECK(config::load(R"({"mqtt":{"uri":"mqtts://broker.example.com:8883","password":""},"ui":{"language":"es"}})", base, out, problems));
  CHECK(out.mqtt.uri == "mqtts://broker.example.com:8883" && out.mqtt.password == "secret-password" && out.mqtt.username == base.mqtt.username && out.language == "es");
  problems.clear();
  CHECK(config::load(R"({"mqtt":{"password":"a-new-password"}})", base, out, problems) && out.mqtt.password == "a-new-password");
  problems.clear();
  CHECK(config::load(R"({"mqtt":{"password_clear":true}})", base, out, problems) && out.mqtt.password.empty());
  problems.clear();
  // "pins" replaces the whole list; without it the list stays
  config::Settings with_pin = base;
  with_pin.pins.push_back({});
  with_pin.pins[0].gpio = 39; with_pin.pins[0].name = "x"; with_pin.pins[0].mode = config::PinMode::kOutput;
  CHECK(config::load(R"({"language":"x"})", with_pin, out, problems) && out.pins.size() == 1);
  problems.clear();
  CHECK(config::load(R"({"pins":[]})", with_pin, out, problems) && out.pins.empty());
}

static void test_settings_rejections() {
  const config::Settings base = valid_settings();
  config::Settings out;
  const auto rejected = [&](const char* document, const char* path, const char* code) {
    config::Problems problems;
    const bool ok = config::load(document, base, out, problems);
    const bool found = has_problem(problems, path, code);
    if (ok || !found) std::printf("  (document %s -> %zu problem(s))\n", document, problems.size());
    return !ok && found;
  };
  CHECK(rejected("not json", "", "not_json"));
  CHECK(rejected("[]", "", "invalid"));
  CHECK(rejected(R"({"node":{"id":"Bad Id"}})", "node.id", "invalid"));
  CHECK(rejected(R"({"node":{"id":5}})", "node.id", "invalid"));
  CHECK(rejected(R"({"uplink":"lora"})", "uplink", "invalid"));
  CHECK(rejected(R"({"ip":{"dhcp":false,"address":"","gateway":"192.168.0.1"}})", "ip.address", "required"));
  CHECK(rejected(R"({"ip":{"dhcp":false,"address":"10.0.0.5","netmask":"255.255.255.0","gateway":"192.168.0.1"}})", "ip.gateway", "outside_subnet"));
  CHECK(rejected(R"({"ip":{"dhcp":false,"address":"10.0.0.5","netmask":"255.0.255.0","gateway":"10.0.0.1"}})", "ip.netmask", "invalid"));
  CHECK(rejected(R"({"ip":{"dhcp":false,"address":"10.0.0.0","netmask":"255.255.255.0","gateway":"10.0.0.1"}})", "ip.address", "invalid"));
  CHECK(rejected(R"({"ip":{"dhcp":false,"address":"10.0.0.1","netmask":"255.255.255.0","gateway":"10.0.0.1"}})", "ip.gateway", "conflict"));
  CHECK(rejected(R"({"ip":{"dhcp":false,"address":"10.0.0.5","netmask":"255.255.255.0","gateway":"10.0.0.1","dns1":"x"}})", "ip.dns1", "invalid"));
  CHECK(rejected(R"({"ip":{"hostname":"Bad_Name"}})", "ip.hostname", "invalid"));
  // a fixed address is checked whichever connection the node uses: the Wi-Fi station too
  CHECK(rejected(R"({"uplink":"wifi","sta":{"enabled":true,"ssid":"Home"},"ip":{"dhcp":false,"address":"192.168.0.60","netmask":"255.255.255.0","gateway":"192.168.5.1"}})", "ip.gateway", "outside_subnet"));
  CHECK(rejected(R"({"uplink":"wifi","sta":{"enabled":true,"ssid":"Home"},"ip":{"dhcp":false,"address":""}})", "ip.address", "required"));
  CHECK(rejected(R"({"ap":{"enabled":true,"ssid":"","password":"12345678"}})", "ap.ssid", "required"));
  CHECK(rejected(R"({"ap":{"enabled":true,"ssid":"X","password":"short"}})", "ap.password", "invalid_key"));
  CHECK(rejected(R"({"ap":{"enabled":true,"ssid":"X"}})", "ap.password", "required"));
  CHECK(rejected(R"({"ap":{"channel":14}})", "ap.channel", "range"));
  CHECK(rejected(R"({"ap":{"max_clients":11}})", "ap.max_clients", "range"));
  CHECK(rejected(R"({"ap":{"tx_power_dbm":1}})", "ap.tx_power_dbm", "range"));
  CHECK(rejected(R"({"ap":{"security":"wep"}})", "ap.security", "invalid"));
  CHECK(rejected(R"({"ap":{"hidden":"yes"}})", "ap.hidden", "invalid"));
  CHECK(rejected(R"({"ap":{"channel":1.5}})", "ap.channel", "range"));
  CHECK(rejected(R"({"uplink":"wifi"})", "sta.enabled", "required"));
  CHECK(rejected(R"({"sta":{"enabled":true,"ssid":"home","password":"short"}})", "sta.password", "invalid_key"));
  CHECK(rejected(R"({"sta":{"enabled":true,"ssid":"home","backup":[{"ssid":""},{"ssid":"b"},{"ssid":"c"},{"ssid":"d"}]}})", "sta.backup", "invalid"));
  CHECK(rejected(R"({"sta":{"enabled":true,"ssid":"home","backup":[{"ssid":""}]}})", "sta.backup.0.ssid", "required"));
  CHECK(rejected(R"({"sta":{"enabled":true,"ssid":"home","backup":[{"ssid":"guest","password":"short"}]}})", "sta.backup.0.password", "invalid_key"));
  CHECK(rejected(R"({"mqtt":{"enabled":true,"uri":"mqtt://10.0.0.1","backup":[{"uri":""}]}})", "mqtt.backup.0.uri", "required"));
  CHECK(rejected(R"({"mqtt":{"enabled":true,"uri":"mqtt://10.0.0.1","backup":[{"uri":"not-a-broker"}]}})", "mqtt.backup.0.uri", "invalid"));
  CHECK(rejected(R"({"mqtt":{"uri":"http://x"}})", "mqtt.uri", "invalid"));
  CHECK(rejected(R"({"mqtt":{"uri":"mqtt://host:70000"}})", "mqtt.uri", "invalid"));
  CHECK(rejected(R"({"mqtt":{"uri":""}})", "mqtt.uri", "required"));
  CHECK(rejected(R"({"mqtt":{"heartbeat_s":1}})", "mqtt.heartbeat_s", "range"));
  CHECK(rejected(R"({"mqtt":{"telemetry_ms":100}})", "mqtt.telemetry_ms", "range"));
  CHECK(rejected(R"({"system":{"auto_restart_hours":5}})", "system.auto_restart_hours", "invalid"));
  CHECK(rejected(R"({"system":{"auto_restart_hours":49}})", "system.auto_restart_hours", "range"));
  CHECK(rejected(R"({"system":{"auto_restart_hours":-1}})", "system.auto_restart_hours", "range"));
  CHECK(rejected(R"({"radars":[{"rx":9}]})", "radars.0.rx", "reserved"));
  CHECK(rejected(R"({"radars":[{"rx":17}]})", "radars.1.rx", "conflict"));
  CHECK(rejected(R"({"radars":[{"enabled":true,"rx":-1}]})", "radars.0.rx", "required"));
  CHECK(rejected(R"({"radars":[{},{},{},{}]})", "radars", "invalid"));
  CHECK(rejected(R"({"sensors":{"sda":16}})", "sensors.sda", "conflict"));
  CHECK(rejected(R"({"ui":{"language":"xx"}})", "ui.language", "invalid"));
  CHECK(rejected(R"({"pins":[{"gpio":35,"name":"a","mode":"output"}]})", "pins.0.gpio", "reserved"));
  CHECK(rejected(R"({"pins":[{"gpio":16,"name":"a","mode":"output"}]})", "pins.0.gpio", "conflict"));
  CHECK(rejected(R"({"pins":[{"gpio":39,"name":"Bad Name","mode":"output"}]})", "pins.0.name", "invalid"));
  CHECK(rejected(R"({"pins":[{"gpio":39,"name":"a","mode":"output"},{"gpio":40,"name":"a","mode":"output"}]})", "pins.1.name", "conflict"));
  CHECK(rejected(R"({"pins":[{"gpio":39,"name":"a","mode":"output"},{"gpio":39,"name":"b","mode":"output"}]})", "pins.1.gpio", "conflict"));
  CHECK(rejected(R"({"pins":[{"gpio":39,"name":"a","mode":"adc"}]})", "pins.0.mode", "no_adc"));
  CHECK(rejected(R"({"pins":[{"gpio":39,"name":"a","mode":"output","report":"triggered"}]})", "pins.0.report", "invalid"));
  CHECK(rejected(R"({"pins":[{"gpio":39,"name":"a","mode":"turbo"}]})", "pins.0.mode", "invalid"));
  CHECK(rejected(R"({"pins":[{"name":"a","mode":"output"}]})", "pins.0.gpio", "required"));
  CHECK(rejected(R"({"pins":[{"gpio":39,"name":"a","mode":"pwm","freq_hz":0}]})", "pins.0.freq_hz", "range"));
  CHECK(rejected(R"({"pins":"none"})", "pins", "invalid"));
  {
    std::string pwm = R"({"pins":[)";
    for (int i = 0; i < 5; ++i) pwm += std::string(i ? "," : "") + R"({"gpio":)" + std::to_string(38 + i) + R"(,"name":"p)" + std::to_string(i) + R"(","mode":"pwm","freq_hz":)" + std::to_string(1000 * (i + 1)) + "}";
    pwm += "]}";
    CHECK(rejected(pwm.c_str(), "pins", "too_many_pwm_freq"));
  }

  // sixteen pins are allowed, seventeen are not; more than eight PWM pins are not
  std::string many = R"({"pins":[)";
  for (int i = 0; i < 17; ++i) many += std::string(i ? "," : "") + R"({"gpio":39,"name":"p)" + std::to_string(i) + R"(","mode":"disabled"})";
  many += "]}";
  CHECK(rejected(many.c_str(), "pins", "too_many"));
  // a disabled pin claims nothing: two disabled entries on one GPIO are fine
  config::Problems problems;
  CHECK(config::load(R"({"pins":[{"gpio":39,"name":"a","mode":"disabled"},{"gpio":39,"name":"a","mode":"disabled"}]})", base, out, problems));
  // the SD-card pins can be mapped (the card is not used)
  problems.clear();
  CHECK(config::load(R"({"pins":[{"gpio":5,"name":"relay","mode":"output"}]})", base, out, problems));
}

static void test_channel_choice() {
  config::AccessPoint ap;
  CHECK(config::effective_channel(ap, 0) == 1 && config::effective_channel(ap, 1) == 6 && config::effective_channel(ap, 2) == 11 && config::effective_channel(ap, 3) == 1);
  ap.channel = 9;
  CHECK(config::effective_channel(ap, 2) == 9);
}

// ---- mapped pins ---------------------------------------------------------------------------------------------------------------

static void test_gpio_commands() {
  using config::PinMode;
  using gpio::Action;
  CHECK(gpio::parse_command("ON", PinMode::kOutput).action == Action::kOn);
  CHECK(gpio::parse_command("  off\n", PinMode::kOutput).action == Action::kOff);
  CHECK(gpio::parse_command("Toggle", PinMode::kOutput).action == Action::kToggle);
  CHECK(gpio::parse_command("1", PinMode::kOutput).action == Action::kOn && gpio::parse_command("0", PinMode::kOutput).action == Action::kOff);
  CHECK(gpio::parse_command("true", PinMode::kOutput).action == Action::kOn);
  CHECK(gpio::parse_command("pulse", PinMode::kOutput).action == Action::kPulse && gpio::parse_command("pulse", PinMode::kOutput).milliseconds == 0);
  CHECK(gpio::parse_command("pulse:1500", PinMode::kOutput).milliseconds == 1500);
  CHECK(gpio::parse_command("pulse:0", PinMode::kOutput).action == Action::kNone);
  CHECK(gpio::parse_command("pulse:abc", PinMode::kOutput).action == Action::kNone);
  CHECK(gpio::parse_command("pulse:99999999", PinMode::kOutput).action == Action::kNone);
  CHECK(gpio::parse_command(R"({"on":true})", PinMode::kOutput).action == Action::kOn);
  CHECK(gpio::parse_command(R"({"on":false})", PinMode::kOutput).action == Action::kOff);
  CHECK(gpio::parse_command(R"({"pulse":250})", PinMode::kOutput).milliseconds == 250);
  CHECK(gpio::parse_command(R"({"level":40})", PinMode::kOutput).action == Action::kNone);
  CHECK(gpio::parse_command("40", PinMode::kPwm).action == Action::kLevel && gpio::parse_command("40", PinMode::kPwm).percent == 40);
  CHECK(gpio::parse_command("100", PinMode::kPwm).percent == 100 && gpio::parse_command("101", PinMode::kPwm).action == Action::kNone);
  CHECK(gpio::parse_command("-5", PinMode::kPwm).action == Action::kNone && gpio::parse_command("4.5", PinMode::kPwm).action == Action::kNone);
  CHECK(gpio::parse_command(R"({"level":75})", PinMode::kPwm).percent == 75 && gpio::parse_command(R"({"level":101})", PinMode::kPwm).action == Action::kNone);
  CHECK(gpio::parse_command("off", PinMode::kPwm).action == Action::kOff);
  CHECK(gpio::parse_command("pulse", PinMode::kPwm).action == Action::kNone);
  // an input, an analogue value and a disabled pin cannot be commanded; junk switches nothing
  CHECK(gpio::parse_command("on", PinMode::kInput).action == Action::kNone && gpio::parse_command("on", PinMode::kAdc).action == Action::kNone && gpio::parse_command("on", PinMode::kDisabled).action == Action::kNone);
  for (const char* junk : {"", "maybe", "{", "{\"on\":\"yes\"}", "[]", "ONN", "on off"}) CHECK(gpio::parse_command(junk, PinMode::kOutput).action == Action::kNone);
}

static void test_gpio_levels_and_duty() {
  CHECK(gpio::level_for(true, false) && !gpio::level_for(true, true) && gpio::level_for(false, true));
  CHECK(gpio::logical_state(true, false) && !gpio::logical_state(true, true));
  CHECK(gpio::pwm_duty(0, 10) == 0 && gpio::pwm_duty(100, 10) == 1023 && gpio::pwm_duty(50, 10) == 512 && gpio::pwm_duty(150, 10) == 1023 && gpio::pwm_duty(-3, 10) == 0);
}

static void test_debouncer() {
  gpio::Debouncer d;
  d.reset(false, 0);
  CHECK(!d.update(true, 10, 30) && !d.level());     // just changed: not yet
  CHECK(!d.update(false, 20, 30) && !d.level());    // bounced back: the candidate restarts
  CHECK(!d.update(true, 25, 30) && !d.update(true, 54, 30) && !d.level());
  CHECK(d.update(true, 55, 30) && d.level());       // stable for 30 ms: reported once
  CHECK(!d.update(true, 60, 30));                    // nothing new
  CHECK(d.update(false, 200, 0) && !d.level());     // no debounce: immediate
}

static void test_link_watch_and_fallback() {
  gpio::LinkWatch watch;
  watch.update(true, 1000);
  CHECK(!watch.lost() && watch.lost_for_ms(5000) == 0);
  watch.update(false, 6000);
  CHECK(watch.lost() && watch.lost_for_ms(6000) == 0 && watch.lost_for_ms(16000) == 10000);
  watch.update(false, 9000);  // still down: the start of the loss does not move
  CHECK(watch.lost_for_ms(16000) == 10000);
  watch.update(true, 17000);
  CHECK(!watch.lost() && watch.lost_for_ms(99999) == 0);

  config::MappedPin pin;
  pin.mode = config::PinMode::kOutput; pin.safe = config::SafeState::kOff; pin.link_timeout_s = 60;
  CHECK(!gpio::must_fall_back(pin, 59999) && gpio::must_fall_back(pin, 60000));
  pin.safe = config::SafeState::kKeep;
  CHECK(!gpio::must_fall_back(pin, 999999));
  pin.safe = config::SafeState::kOn; pin.link_timeout_s = 0;
  CHECK(!gpio::must_fall_back(pin, 999999));
  pin.link_timeout_s = 5; pin.mode = config::PinMode::kInput;
  CHECK(!gpio::must_fall_back(pin, 999999));
}

static void test_gpio_topics_and_reports() {
  CHECK(gpio::topic("perimetro-1", "garden_light", "state") == "armor/device/perimetro-1/garden_light/state");
  CHECK(gpio::topic("perimetro-1", "garden_light", "set") == "armor/device/perimetro-1/garden_light/set");
  CHECK(gpio::topic("perimetro-1", "garden_light", "other").empty() && gpio::topic("Bad", "x", "set").empty() && gpio::topic("ok", "Bad Name", "set").empty() && gpio::topic("ok", "a/b", "set").empty());
  CHECK(gpio::command_filter("perimetro-1") == "armor/device/perimetro-1/+/set" && gpio::command_filter("bad id").empty());
  CHECK(gpio::pin_of_command_topic("perimetro-1", "armor/device/perimetro-1/garden_light/set") == "garden_light");
  CHECK(gpio::pin_of_command_topic("perimetro-1", "armor/device/perimetro-2/garden_light/set").empty());
  CHECK(gpio::pin_of_command_topic("perimetro-1", "armor/device/perimetro-1/garden_light/state").empty());
  CHECK(gpio::pin_of_command_topic("perimetro-1", "armor/device/perimetro-1/a/b/set").empty());
  CHECK(gpio::pin_of_command_topic("perimetro-1", "armor/device/perimetro-1//set").empty());
  CHECK(gpio::report_boolean("on", true) == R"({"on":true})" && gpio::report_boolean("triggered", false) == R"({"triggered":false})");
  CHECK(gpio::report_number("battery", 12.6125) == R"({"battery":12.613})" || gpio::report_number("battery", 12.6125) == R"({"battery":12.612})");
  config::MappedPin pin;
  pin.scale = 0.0057; pin.offset = 0.5;
  CHECK(gpio::analogue_value(2000, pin) > 11.89 && gpio::analogue_value(2000, pin) < 11.91);
}

// ---- panel users, sessions and the throttle ------------------------------------------------------------------------------------

// A stand-in for PBKDF2: it only has to be deterministic and depend on both inputs.
static std::string fake_hash(std::string_view password, std::string_view salt) {
  std::uint64_t h = 1469598103934665603ULL;
  for (const char c : std::string(salt) + "|" + std::string(password)) { h ^= static_cast<unsigned char>(c); h *= 1099511628211ULL; }
  char text[24];
  std::snprintf(text, sizeof text, "%016llx", static_cast<unsigned long long>(h));
  return text;
}

static void test_users() {
  auth::UserStore store;
  CHECK(store.empty());
  CHECK(store.add("admin", "correct horse", auth::Role::kAdmin, "salt1", fake_hash) == auth::Result::kOk);
  CHECK(store.add("ab", "correct horse", auth::Role::kAdmin, "s", fake_hash) == auth::Result::kInvalidName);
  CHECK(store.add("Admin2", "correct horse", auth::Role::kAdmin, "s", fake_hash) == auth::Result::kInvalidName);
  CHECK(store.add("viewer", "short", auth::Role::kViewer, "s", fake_hash) == auth::Result::kWeakPassword);
  CHECK(store.add("admin", "another password", auth::Role::kViewer, "s", fake_hash) == auth::Result::kExists);
  CHECK(store.add("viewer", "view only pw", auth::Role::kViewer, "salt2", fake_hash) == auth::Result::kOk);
  auth::Role role = auth::Role::kViewer;
  CHECK(store.verify("admin", "correct horse", fake_hash, role) && role == auth::Role::kAdmin);
  CHECK(store.verify("viewer", "view only pw", fake_hash, role) && role == auth::Role::kViewer);
  CHECK(!store.verify("admin", "wrong", fake_hash, role) && !store.verify("nobody", "correct horse", fake_hash, role) && !store.verify("admin", "", fake_hash, role));
  // the hash stored is not the password, and two users with the same password differ by salt
  CHECK(store.find("admin")->hash != "correct horse" && store.to_json().find("correct horse") == std::string::npos);
  CHECK(store.add("third", "correct horse", auth::Role::kViewer, "salt3", fake_hash) == auth::Result::kOk && store.find("third")->hash != store.find("admin")->hash);
  CHECK(store.add("fourth", "password four", auth::Role::kViewer, "salt4", fake_hash) == auth::Result::kOk);
  CHECK(store.add("fifth", "password five", auth::Role::kViewer, "salt5", fake_hash) == auth::Result::kTooMany);

  // the last administrator cannot be demoted or removed
  CHECK(store.set_role("admin", auth::Role::kViewer) == auth::Result::kLastAdmin && store.remove("admin") == auth::Result::kLastAdmin);
  CHECK(store.set_role("viewer", auth::Role::kAdmin) == auth::Result::kOk && store.admins() == 2);
  CHECK(store.remove("admin") == auth::Result::kOk && store.find("admin") == nullptr);
  CHECK(store.remove("ghost") == auth::Result::kNotFound && store.set_role("ghost", auth::Role::kAdmin) == auth::Result::kNotFound);
  CHECK(store.set_password("viewer", "a brand new one", "salt9", fake_hash) == auth::Result::kOk && store.verify("viewer", "a brand new one", fake_hash, role) && !store.verify("viewer", "view only pw", fake_hash, role));
  CHECK(store.set_password("viewer", "short", "s", fake_hash) == auth::Result::kWeakPassword && store.set_password("ghost", "long enough pw", "s", fake_hash) == auth::Result::kNotFound);

  // storage round trip
  auth::UserStore copy;
  CHECK(copy.from_json(store.to_json()) && copy.users().size() == store.users().size() && copy.verify("viewer", "a brand new one", fake_hash, role));
  CHECK(!copy.from_json("nonsense") && !copy.from_json(R"({"users":[{"name":"a","role":"admin","salt":"s","hash":"h"}]})"));
  CHECK(!copy.from_json(R"({"users":[{"name":"abc","role":"root","salt":"s","hash":"h"}]})"));
  CHECK(!copy.from_json(R"({"users":[{"name":"abc","role":"admin","salt":"s","hash":"h"},{"name":"abc","role":"viewer","salt":"s","hash":"h"}]})"));
  CHECK(copy.users().size() == store.users().size());  // a refused document leaves the store as it was
  CHECK(auth::same_text("abc", "abc") && !auth::same_text("abc", "abd") && !auth::same_text("abc", "ab") && auth::same_text("", ""));
}

static void test_sessions() {
  auth::SessionTable table;
  const std::string token(32, 'a');
  CHECK(table.touch(token, 0) == nullptr);
  table.create(token, "admin", auth::Role::kAdmin, 1000);
  const auth::Session* session = table.touch(token, 2000);
  CHECK(session != nullptr && session->user == "admin" && session->role == auth::Role::kAdmin);
  CHECK(table.touch("short", 2000) == nullptr && table.touch(std::string(32, 'b'), 2000) == nullptr);
  // idle for longer than the limit ends it; using it before then extends it
  CHECK(table.touch(token, 2000 + auth::kSessionIdleMs - 1) != nullptr);
  CHECK(table.touch(token, 2000 + auth::kSessionIdleMs - 1 + auth::kSessionIdleMs + 1) == nullptr);
  // "remember me": a session survives well past the ordinary idle limit, but not forever
  table.create(token, "admin", auth::Role::kAdmin, 3000, true);
  CHECK(table.touch(token, 3000 + auth::kSessionIdleMs + 1) != nullptr);
  table.create(token, "admin", auth::Role::kAdmin, 3000, true);
  CHECK(table.touch(token, 3000 + auth::kRememberedIdleMs + 1) == nullptr);
  table.create(token, "admin", auth::Role::kAdmin, 5000);
  table.end(token);
  CHECK(table.touch(token, 5001) == nullptr);
  // ending a user's sessions
  table.create(std::string(32, 'c'), "viewer", auth::Role::kViewer, 100);
  table.create(std::string(32, 'd'), "viewer", auth::Role::kViewer, 100);
  table.create(std::string(32, 'e'), "admin", auth::Role::kAdmin, 100);
  table.end_user("viewer");
  CHECK(table.touch(std::string(32, 'c'), 101) == nullptr && table.touch(std::string(32, 'd'), 101) == nullptr && table.touch(std::string(32, 'e'), 101) != nullptr);
  // a full table replaces the session idle the longest
  table.clear();
  for (std::size_t i = 0; i < auth::kMaxSessions; ++i) table.create(std::string(32, static_cast<char>('f' + i)), "u", auth::Role::kViewer, 1000 + i);
  table.create(std::string(32, 'z'), "u", auth::Role::kViewer, 2000);
  CHECK(table.touch(std::string(32, 'f'), 2001) == nullptr && table.touch(std::string(32, 'z'), 2001) != nullptr && table.touch(std::string(32, 'g'), 2001) != nullptr);
}

static void test_throttle() {
  auth::LoginThrottle throttle;
  const std::uint32_t a = 0xC0A80005u, b = 0xC0A80006u;
  for (int i = 0; i < 4; ++i) throttle.failure(a, 1000 + i);
  CHECK(throttle.allowed(a, 2000));                    // four failures: still allowed
  throttle.failure(a, 2000);
  CHECK(!throttle.allowed(a, 2001) && throttle.wait_s(a, 2001) == 30);  // the fifth blocks for 30 s
  CHECK(throttle.allowed(b, 2001));                    // another address is unaffected
  CHECK(!throttle.allowed(a, 31999) && throttle.allowed(a, 32000));
  throttle.failure(a, 32000);                          // the sixth: twice as long
  CHECK(!throttle.allowed(a, 91999) && throttle.allowed(a, 92000));
  for (int i = 0; i < 10; ++i) throttle.failure(a, 100000 + i);
  CHECK(throttle.wait_s(a, 100010) <= 300);            // never longer than five minutes
  throttle.success(a);
  CHECK(throttle.allowed(a, 100011) && throttle.wait_s(a, 100011) == 0);
  // the table holds eight addresses: a ninth pushes out the oldest
  auth::LoginThrottle small;
  for (std::uint32_t i = 0; i < 9; ++i) for (int j = 0; j < 5; ++j) small.failure(1000 + i, 10 * i + static_cast<std::uint64_t>(j));
  CHECK(small.allowed(1000, 100000) && !small.allowed(1008, 100));
}

// ---- LD2450 command channel ----------------------------------------------------------------------------------------------------

static std::vector<std::uint8_t> bytes_of(const std::uint8_t* data, std::size_t length) { return std::vector<std::uint8_t>(data, data + length); }

static void test_ld2450_commands() {
  std::uint8_t out[ld2450cmd::kMaxFrame];
  // frames as the vendor documents them
  std::size_t n = ld2450cmd::build_enable_configuration(out, sizeof out);
  CHECK(bytes_of(out, n) == (std::vector<std::uint8_t>{0xFD, 0xFC, 0xFB, 0xFA, 0x04, 0x00, 0xFF, 0x00, 0x01, 0x00, 0x04, 0x03, 0x02, 0x01}));
  n = ld2450cmd::build_simple(ld2450cmd::kEndConfiguration, out, sizeof out);
  CHECK(bytes_of(out, n) == (std::vector<std::uint8_t>{0xFD, 0xFC, 0xFB, 0xFA, 0x02, 0x00, 0xFE, 0x00, 0x04, 0x03, 0x02, 0x01}));
  n = ld2450cmd::build_simple(ld2450cmd::kMultiTarget, out, sizeof out);
  CHECK(bytes_of(out, n) == (std::vector<std::uint8_t>{0xFD, 0xFC, 0xFB, 0xFA, 0x02, 0x00, 0x90, 0x00, 0x04, 0x03, 0x02, 0x01}));
  n = ld2450cmd::build_simple(ld2450cmd::kRestart, out, sizeof out);
  CHECK(bytes_of(out, n) == (std::vector<std::uint8_t>{0xFD, 0xFC, 0xFB, 0xFA, 0x02, 0x00, 0xA3, 0x00, 0x04, 0x03, 0x02, 0x01}));
  n = ld2450cmd::build_simple(ld2450cmd::kReadFirmware, out, sizeof out);
  CHECK(bytes_of(out, n) == (std::vector<std::uint8_t>{0xFD, 0xFC, 0xFB, 0xFA, 0x02, 0x00, 0xA0, 0x00, 0x04, 0x03, 0x02, 0x01}));
  n = ld2450cmd::build_bluetooth(false, out, sizeof out);
  CHECK(bytes_of(out, n) == (std::vector<std::uint8_t>{0xFD, 0xFC, 0xFB, 0xFA, 0x04, 0x00, 0xA4, 0x00, 0x00, 0x00, 0x04, 0x03, 0x02, 0x01}));
  n = ld2450cmd::build_baud_rate(7, out, sizeof out);
  CHECK(n == 14 && out[6] == 0xA1 && out[8] == 0x07);
  CHECK(ld2450cmd::build_baud_rate(0, out, sizeof out) == 0 && ld2450cmd::build_baud_rate(9, out, sizeof out) == 0);
  CHECK(ld2450cmd::build_simple(ld2450cmd::kRestart, out, 5) == 0);  // a buffer that is too small yields nothing, not a truncated frame

  // zones: type, then three rectangles of four signed 16-bit values
  ld2450cmd::ZoneFilter filter;
  filter.type = 1;
  filter.zones[0] = {-500, 300, 500, 2500};
  filter.zones[1] = {1000, 0, 2000, 1000};
  n = ld2450cmd::build_set_zones(filter, out, sizeof out);
  CHECK(n == 4 + 2 + 2 + 26 + 4 && out[4] == 28 && out[6] == 0xC2 && out[7] == 0x00 && out[8] == 0x01 && out[9] == 0x00);
  CHECK(out[10] == 0x0C && out[11] == 0xFE && out[12] == 0x2C && out[13] == 0x01);  // -500 = 0xFE0C, 300 = 0x012C
  filter.type = 3;
  CHECK(ld2450cmd::build_set_zones(filter, out, sizeof out) == 0);

  // acknowledgements
  ld2450cmd::AckParser parser;
  ld2450cmd::Ack ack;
  const std::vector<std::uint8_t> enable_ack = {0xFD, 0xFC, 0xFB, 0xFA, 0x08, 0x00, 0xFF, 0x01, 0x00, 0x00, 0x01, 0x00, 0x40, 0x00, 0x04, 0x03, 0x02, 0x01};
  bool got = false;
  // some report bytes first, then the acknowledgement split in the middle
  const std::vector<std::uint8_t> noise = {0xAA, 0xFF, 0x03, 0x00, 0xFD, 0xFD, 0xFC, 0x11};
  for (std::uint8_t byte : noise) CHECK(!parser.feed(byte, ack));
  for (std::size_t i = 0; i < enable_ack.size(); ++i) { if (parser.feed(enable_ack[i], ack)) { got = true; CHECK(i + 1 == enable_ack.size()); } }
  CHECK(got && ack.command == ld2450cmd::kEnableConfiguration && ack.ok && ack.data_length == 4 && ack.data[0] == 0x01 && ack.data[2] == 0x40);

  // the firmware answer of the vendor's tools: version 1.02.22062416
  const std::vector<std::uint8_t> firmware_ack = {0xFD, 0xFC, 0xFB, 0xFA, 0x0C, 0x00, 0xA0, 0x01, 0x00, 0x00, 0x00, 0x00, 0x02, 0x01, 0x16, 0x24, 0x06, 0x22, 0x04, 0x03, 0x02, 0x01};
  got = false;
  for (std::uint8_t byte : firmware_ack) got = parser.feed(byte, ack) || got;
  CHECK(got && ack.command == ld2450cmd::kReadFirmware && ld2450cmd::firmware_text(ack) == "1.02.22062416");

  // a refused command, a wrong footer, an oversized length and an unrelated frame
  const std::vector<std::uint8_t> refused = {0xFD, 0xFC, 0xFB, 0xFA, 0x04, 0x00, 0xA3, 0x01, 0x01, 0x00, 0x04, 0x03, 0x02, 0x01};
  got = false;
  for (std::uint8_t byte : refused) got = parser.feed(byte, ack) || got;
  CHECK(got && !ack.ok && ack.command == ld2450cmd::kRestart && ld2450cmd::firmware_text(ack).empty());
  std::vector<std::uint8_t> bad_footer = refused;
  bad_footer[13] = 0x00;
  got = false;
  for (std::uint8_t byte : bad_footer) got = parser.feed(byte, ack) || got;
  CHECK(!got);
  const std::vector<std::uint8_t> oversized = {0xFD, 0xFC, 0xFB, 0xFA, 0xFF, 0x7F, 0xA3, 0x01};
  for (std::uint8_t byte : oversized) CHECK(!parser.feed(byte, ack));
  got = false;
  for (std::uint8_t byte : refused) got = parser.feed(byte, ack) || got;  // the parser recovered
  CHECK(got);
  const std::vector<std::uint8_t> not_an_answer = {0xFD, 0xFC, 0xFB, 0xFA, 0x04, 0x00, 0xA3, 0x00, 0x00, 0x00, 0x04, 0x03, 0x02, 0x01};
  got = false;
  for (std::uint8_t byte : not_an_answer) got = parser.feed(byte, ack) || got;
  CHECK(!got);

  // tracking mode and zones from their answers
  ld2450cmd::Ack mode;
  mode.command = ld2450cmd::kQueryTrackingMode; mode.ok = true; mode.data[0] = 2; mode.data_length = 2;
  CHECK(ld2450cmd::tracking_mode(mode) == 2);
  mode.data[0] = 5;
  CHECK(ld2450cmd::tracking_mode(mode) == 0);
  ld2450cmd::Ack zones;
  zones.command = ld2450cmd::kQueryZones; zones.ok = true; zones.data_length = 26;
  zones.data[0] = 2;
  zones.data[2] = 0x0C; zones.data[3] = 0xFE; zones.data[4] = 0x2C; zones.data[5] = 0x01;
  ld2450cmd::ZoneFilter read;
  CHECK(ld2450cmd::zones_from_ack(zones, read) && read.type == 2 && read.zones[0].x1 == -500 && read.zones[0].y1 == 300 && read.zones[1].x2 == 0);
  zones.data_length = 10;
  CHECK(!ld2450cmd::zones_from_ack(zones, read));
}

static void test_network_plan() {
  config::Settings s = valid_settings();
  netplan::Plan plan = netplan::plan_network(s, false, "", "a1b2c3", 1);
  CHECK(plan.layout == netplan::Layout::kEthernet && !plan.ap.enabled && plan.hostname == "armor-a1b2c3");
  s.ap.enabled = true; s.ap.ssid = "ARMOR"; s.ap.password = "wifi-secret-1";
  plan = netplan::plan_network(s, false, "", "a1b2c3", 1);
  CHECK(plan.layout == netplan::Layout::kEthernetBridgedAp && plan.ap.bridged && plan.ap.ssid == "ARMOR" && plan.ap.channel == 6 && plan.ap.country == "ES");
  plan = netplan::plan_network(s, false, "", "a1b2c3", 2);
  CHECK(plan.ap.channel == 11);   // the same SSID on a different channel on the next node
  s.ap.channel = 4;
  CHECK(netplan::plan_network(s, false, "", "a1b2c3", 2).ap.channel == 4);
  s.ap.bridge = false;
  CHECK(netplan::plan_network(s, false, "", "a1b2c3", 0).layout == netplan::Layout::kEthernetWithAp);
  s.uplink = config::Uplink::kWifi; s.sta.enabled = true; s.sta.ssid = "home"; s.ap.bridge = true;
  plan = netplan::plan_network(s, false, "", "a1b2c3", 0);
  CHECK(plan.layout == netplan::Layout::kWifiStationWithAp && !plan.ap.bridged);   // a station cannot be bridged
  s.ap.enabled = false;
  CHECK(netplan::plan_network(s, false, "", "a1b2c3", 0).layout == netplan::Layout::kWifiStation);
  // setup mode: its own access point, protected by the code, never bridged, whatever the settings say
  s = valid_settings();
  plan = netplan::plan_network(s, true, "K7M2QX9P", "a1b2c3", 1);
  CHECK(plan.ap.enabled && plan.ap.setup && !plan.ap.bridged && plan.ap.ssid == "ARMOR-SETUP-A1B2C3" && plan.ap.password == "K7M2QX9P" && plan.ap.security == config::WifiSecurity::kWpa2);
  CHECK(net::valid_ssid(plan.ap.ssid) && net::valid_wpa_passphrase(plan.ap.password) && plan.layout == netplan::Layout::kEthernetWithAp);
  // host names
  s.node_id = "perimetro_1";
  CHECK(netplan::hostname_for(s) == "armor-perimetro-1");
  s.node_id = std::string(60, 'a');
  CHECK(netplan::hostname_for(s).size() == 32);
  s.node_id = "x-"; 
  CHECK(netplan::hostname_for(s) == "armor-x");
  s.ip.hostname = "gate";
  CHECK(netplan::hostname_for(s) == "gate");
  // the country code
  config::Settings country = valid_settings();
  country.ap.country = "es";
  CHECK(has_problem(config::validate(country), "ap.country", "invalid"));
  country.ap.country = "DE";
  CHECK(config::validate(country).empty());
}

static void test_codes() {
  // The set-up code of a board is made from HMAC-SHA256(fleet secret, MAC): the digest below is what openssl and Python give for the secret
  // "fleet-secret-for-tests-0123456789" and the MAC "a1b2c3d4e5f6", and tools/adopt_node.py must reach the same ten symbols.
  const std::uint8_t digest[10] = {0x63, 0x41, 0x5b, 0x8e, 0x16, 0x45, 0x19, 0xff, 0x5e, 0xc4};
  CHECK(auth::setup_code_from(digest, 10) == "GD8VZH4HBM");
  const std::uint8_t bytes[8] = {0, 1, 30, 31, 62, 255, 100, 7};
  const std::string code = auth::setup_code_from(bytes, 8);
  CHECK(code.size() == 8 && net::valid_wpa_passphrase(code));
  for (char c : code) CHECK(std::string("ABCDEFGHJKMNPQRSTUVWXYZ23456789").find(c) != std::string::npos);
  CHECK(std::string("0O1IL").find(code[0]) == std::string::npos);
  CHECK(auth::to_hex(bytes, 3) == "00011e");
}

static void test_info_message() {
  char out[512];
  std::size_t length = 0;
  CHECK(build_info("north-1", 5, "North gate", "0.2.3", "192.168.0.181", 80, out, sizeof out, length) == JsonResult::kOk);
  CHECK(std::string(out, length) == R"({"node_id":"north-1","timestamp_ms":5,"name":"North gate","firmware":"0.2.3","ip":"192.168.0.181","port":80})");
  CHECK(build_info("North", 5, "n", "0.2.3", "10.0.0.1", 80, out, sizeof out, length) == JsonResult::kInvalidNodeId);
  CHECK(build_info("n1", 5, "", "0.2.3", "10.0.0.1", 80, out, sizeof out, length) == JsonResult::kInvalidInfo);
  CHECK(build_info("n1", 5, std::string(49, 'x'), "0.2.3", "10.0.0.1", 80, out, sizeof out, length) == JsonResult::kInvalidInfo);
  CHECK(build_info("n1", 5, std::string(48, 'x'), "0.2.3", "10.0.0.1", 80, out, sizeof out, length) == JsonResult::kOk);
  CHECK(build_info("n1", 5, "\xC3\x28", "0.2.3", "10.0.0.1", 80, out, sizeof out, length) == JsonResult::kInvalidInfo);   // broken UTF-8
  CHECK(build_info("n1", 5, "n", "v0.2.3", "10.0.0.1", 80, out, sizeof out, length) == JsonResult::kInvalidInfo);
  CHECK(build_info("n1", 5, "n", "0.2", "10.0.0.1", 80, out, sizeof out, length) == JsonResult::kInvalidInfo);
  CHECK(build_info("n1", 5, "n", "0.2.3.4", "10.0.0.1", 80, out, sizeof out, length) == JsonResult::kInvalidInfo);
  CHECK(build_info("n1", 5, "n", "0.2.", "10.0.0.1", 80, out, sizeof out, length) == JsonResult::kInvalidInfo);
  CHECK(build_info("n1", 5, "n", "0.2.3", "10.0.0", 80, out, sizeof out, length) == JsonResult::kInvalidInfo);
  CHECK(build_info("n1", 5, "n", "0.2.3", "10.0.0.01", 80, out, sizeof out, length) == JsonResult::kInvalidInfo);
  CHECK(build_info("n1", 5, "n", "0.2.3", "10.0.0.1", 0, out, sizeof out, length) == JsonResult::kInvalidInfo);
  CHECK(build_info("n1", 5, "n", "0.2.3", "10.0.0.1", 65536, out, sizeof out, length) == JsonResult::kInvalidInfo);
  CHECK(build_info("n1", 5, "n", "0.2.3", "10.0.0.1", 80, out, 20, length) == JsonResult::kBufferTooSmall && length == 0);
  // UTF-8: what the panel may put in a node's name
  std::size_t characters = 0;
  CHECK(net::valid_utf8("Per\xC3\xADmetro", &characters) && characters == 9);
  CHECK(net::valid_utf8("\xE6\x97\xA5\xE6\x9C\xAC", &characters) && characters == 2);
  CHECK(net::valid_utf8("\xF0\x9F\x98\x80", &characters) && characters == 1);
  CHECK(!net::valid_utf8("\xC0\x80") && !net::valid_utf8("\xED\xA0\x80") && !net::valid_utf8("\xF5\x80\x80\x80") && !net::valid_utf8("\xE6\x97") && !net::valid_utf8("\xFF"));
  config::Settings s = valid_settings();
  s.node_name = std::string(49, 'x');
  CHECK(has_problem(config::validate(s), "node.name", "invalid"));
  s.node_name = "\xFF";
  CHECK(has_problem(config::validate(s), "node.name", "invalid"));
  s.node_name = "Per\xC3\xADmetro norte";
  CHECK(config::validate(s).empty());
}

// ---- Bluetooth configuration channel -------------------------------------------------------------------------------------------

namespace {
// A backend that remembers what it was asked, and holds two users.
class FakeBackend : public ble::Backend {
 public:
  bool users = true;
  std::uint64_t clock = 1000;
  int restarts = 0;
  std::string last_put;
  int put_code = 0;
  bool scan_ok = true;
  auth::UserStore store;
  bool has_users() override { return users; }
  std::string hello_json() override { return R"({"node_id":"armor-a1b2c3","setup":)" + std::string(users ? "false" : "true") + "}"; }
  bool setup_code_ok(std::string_view code) override { return code == "TESTCODE01"; }
  auth::Result add_administrator(std::string_view user, std::string_view password) override {
    const auth::Result r = store.add(user, password, auth::Role::kAdmin, "salt", [](std::string_view p, std::string_view) { return std::string(p); });
    if (r == auth::Result::kOk) users = true;
    return r;
  }
  bool verify(std::string_view user, std::string_view password, auth::Role& role) override {
    return store.verify(user, password, [](std::string_view p, std::string_view) { return std::string(p); }, role);
  }
  std::string config_json() override { return R"({"config":{"node":{"id":"armor-a1b2c3"}},"channel_auto":6})"; }
  int config_put(std::string_view document, std::string& data) override {
    last_put = std::string(document);
    data = put_code == 0 ? R"({"restart_required":true})" : R"({"problems":[{"path":"ap.ssid","code":"required"}]})";
    return put_code;
  }
  std::string status_json() override { return R"({"uptime_s":5})"; }
  bool wifi_scan(std::string& data, std::string& error) override {
    if (!scan_ok) { error = "wifi_busy"; return false; }
    data = R"({"networks":[{"ssid":"home","rssi":-50}]})";
    return true;
  }
  void restart_soon() override { ++restarts; }
  std::uint64_t now_ms() override { return clock; }
};

std::string ask(FakeBackend& backend, ble::Session& session, auth::LoginThrottle& throttle, const std::string& request_text) {
  ble::Request request;
  if (!ble::parse_request(request_text, request)) return "NOT_A_REQUEST";
  return ble::handle(backend, session, throttle, request);
}
}  // namespace

static void test_ble_framing() {
  const std::string message = R"({"id":1,"op":"hello"})";
  const std::string framed = ble::frame(message);
  CHECK(framed.size() == message.size() + 2 && static_cast<unsigned char>(framed[0]) == 0 && static_cast<unsigned char>(framed[1]) == message.size());
  CHECK(ble::frame("").empty() && ble::frame(std::string(ble::kMaxMessage + 1, 'x')).empty() && !ble::frame(std::string(ble::kMaxMessage, 'x')).empty());
  // any way of cutting the stream gives the same message back
  for (std::size_t cut : {1u, 2u, 3u, 7u, 20u, 500u}) {
    ble::Assembler assembler;
    std::string got;
    int messages = 0;
    for (const std::string& piece : ble::chunks_of(framed, cut)) {
      if (assembler.feed(reinterpret_cast<const std::uint8_t*>(piece.data()), piece.size(), got)) ++messages;
    }
    CHECK(messages == 1 && got == message && assembler.pending() == 0);
  }
  CHECK(ble::chunks_of(framed, 0).empty());
  // two messages written back to back come out one by one
  ble::Assembler two;
  const std::string both = framed + ble::frame(R"({"id":2,"op":"status"})");
  std::string first, second;
  CHECK(two.feed(reinterpret_cast<const std::uint8_t*>(both.data()), both.size(), first) && first == message);
  CHECK(two.take(second) && second == R"({"id":2,"op":"status"})" && !two.take(second));
  // a length of zero, or too large, is dropped, and the next message is understood
  ble::Assembler bad;
  std::string out;
  const std::uint8_t zero[2] = {0, 0};
  CHECK(!bad.feed(zero, 2, out) && bad.errors() == 1);
  const std::uint8_t huge[2] = {0xFF, 0xFF};
  CHECK(!bad.feed(huge, 2, out) && bad.errors() == 2);
  CHECK(bad.feed(reinterpret_cast<const std::uint8_t*>(framed.data()), framed.size(), out) && out == message);
}

static void test_ble_requests() {
  ble::Request r;
  CHECK(ble::parse_request(R"({"id":7,"op":"config.get"})", r) && r.id == 7 && r.op == "config.get" && r.args.is_object());
  CHECK(ble::parse_request(R"({"id":8,"op":"login","args":{"user":"a","password":"b"}})", r) && r.args.string_or("user", "") == "a");
  for (const char* bad : {"", "[]", R"({"op":"x"})", R"({"id":1})", R"({"id":-1,"op":"x"})", R"({"id":1.5,"op":"x"})", R"({"id":"1","op":"x"})", R"({"id":1,"op":""})", R"({"id":1,"op":5})"}) CHECK(!ble::parse_request(bad, r));
  CHECK(ble::ok_response(3) == R"({"id":3,"ok":true,"data":{}})");
  CHECK(ble::ok_response(3, R"({"a":1})") == R"({"id":3,"ok":true,"data":{"a":1}})");
  CHECK(ble::error_response(4, "forbidden") == R"({"id":4,"ok":false,"error":"forbidden"})");
  CHECK(ble::error_response(4, "invalid", R"({"problems":[]})") == R"({"id":4,"ok":false,"error":"invalid","data":{"problems":[]}})");
}

static void test_ble_dispatch() {
  FakeBackend backend;
  ble::Session session;
  auth::LoginThrottle throttle;
  // a node with users: hello is open, everything else needs a login
  CHECK(ask(backend, session, throttle, R"({"id":1,"op":"hello"})").find(R"("ok":true)") != std::string::npos);
  CHECK(ask(backend, session, throttle, R"({"id":2,"op":"config.get"})") == R"({"id":2,"ok":false,"error":"unauthorized"})");
  CHECK(ask(backend, session, throttle, R"({"id":3,"op":"setup","args":{"code":"TESTCODE01","user":"admin","password":"long enough pw"}})") == R"({"id":3,"ok":false,"error":"forbidden"})");
  CHECK(backend.store.add("admin", "correct horse", auth::Role::kAdmin, "s", [](std::string_view p, std::string_view) { return std::string(p); }) == auth::Result::kOk);
  CHECK(backend.store.add("viewer", "view only pw", auth::Role::kViewer, "s", [](std::string_view p, std::string_view) { return std::string(p); }) == auth::Result::kOk);
  CHECK(ask(backend, session, throttle, R"({"id":4,"op":"login","args":{"user":"admin","password":"nope"}})") == R"({"id":4,"ok":false,"error":"wrong_credentials"})");
  CHECK(!session.authenticated);
  // a viewer can read but not change
  CHECK(ask(backend, session, throttle, R"({"id":5,"op":"login","args":{"user":"viewer","password":"view only pw"}})") == R"({"id":5,"ok":true,"data":{"role":"viewer"}})");
  CHECK(ask(backend, session, throttle, R"({"id":6,"op":"config.get"})").find("channel_auto") != std::string::npos);
  CHECK(ask(backend, session, throttle, R"({"id":7,"op":"status"})").find("uptime_s") != std::string::npos);
  CHECK(ask(backend, session, throttle, R"({"id":8,"op":"config.put","args":{"config":{"node":{"name":"x"}}}})") == R"({"id":8,"ok":false,"error":"forbidden"})");
  CHECK(ask(backend, session, throttle, R"({"id":9,"op":"wifi.scan"})") == R"({"id":9,"ok":false,"error":"forbidden"})");
  CHECK(ask(backend, session, throttle, R"({"id":10,"op":"reboot"})") == R"({"id":10,"ok":false,"error":"forbidden"})" && backend.restarts == 0);
  // an administrator can
  CHECK(ask(backend, session, throttle, R"({"id":11,"op":"login","args":{"user":"admin","password":"correct horse"}})") == R"({"id":11,"ok":true,"data":{"role":"admin"}})");
  CHECK(ask(backend, session, throttle, R"({"id":12,"op":"config.put","args":{"config":{"node":{"name":"North \"gate\""},"ap":{"enabled":true},"pins":[{"gpio":39,"scale":0.5}]}}})") == R"({"id":12,"ok":true,"data":{"restart_required":true}})");
  json::Value put;
  CHECK(json::parse(backend.last_put, put) && put.get("node")->string_or("name", "") == "North \"gate\"" && put.get("ap")->bool_or("enabled", false) && put.get("pins")->items[0].number_or("scale", 0) == 0.5);
  CHECK(ask(backend, session, throttle, R"({"id":13,"op":"config.put","args":{}})") == R"({"id":13,"ok":false,"error":"invalid"})");
  backend.put_code = 422;
  CHECK(ask(backend, session, throttle, R"({"id":14,"op":"config.put","args":{"config":{"ap":{"enabled":true}}}})").find(R"("error":"invalid","data":{"problems")") != std::string::npos);
  CHECK(ask(backend, session, throttle, R"({"id":15,"op":"wifi.scan"})").find("home") != std::string::npos);
  backend.scan_ok = false;
  CHECK(ask(backend, session, throttle, R"({"id":16,"op":"wifi.scan"})") == R"({"id":16,"ok":false,"error":"wifi_busy"})");
  CHECK(ask(backend, session, throttle, R"({"id":17,"op":"reboot"})") == R"({"id":17,"ok":true,"data":{}})" && backend.restarts == 1);
  CHECK(ask(backend, session, throttle, R"({"id":18,"op":"logout"})") == R"({"id":18,"ok":true,"data":{}})" && !session.authenticated);
  CHECK(ask(backend, session, throttle, R"({"id":19,"op":"config.get"})").find("unauthorized") != std::string::npos);
  CHECK(ask(backend, session, throttle, R"({"id":20,"op":"login","args":{"user":"admin","password":"correct horse"}})").find("ok\":true") != std::string::npos);
  CHECK(ask(backend, session, throttle, R"({"id":21,"op":"frobnicate"})") == R"({"id":21,"ok":false,"error":"unknown_op"})");

  // five wrong passwords lock the channel, whatever the next one is; time lets it through again
  ble::Session other;
  for (int i = 0; i < 5; ++i) ask(backend, other, throttle, R"({"id":30,"op":"login","args":{"user":"admin","password":"wrong"}})");
  const std::string locked = ask(backend, other, throttle, R"({"id":31,"op":"login","args":{"user":"admin","password":"correct horse"}})");
  CHECK(locked.find("too_many_attempts") != std::string::npos && locked.find("wait_s") != std::string::npos && !other.authenticated);
  backend.clock += 31000;
  CHECK(ask(backend, other, throttle, R"({"id":32,"op":"login","args":{"user":"admin","password":"correct horse"}})").find("ok\":true") != std::string::npos);

  // a node with no user: only hello and set-up answer, and the set-up needs the code
  FakeBackend fresh;
  fresh.users = false;
  ble::Session first;
  auth::LoginThrottle fresh_throttle;
  CHECK(ask(fresh, first, fresh_throttle, R"({"id":1,"op":"config.get"})") == R"({"id":1,"ok":false,"error":"setup_required"})");
  CHECK(ask(fresh, first, fresh_throttle, R"({"id":2,"op":"login","args":{"user":"a","password":"b"}})") == R"({"id":2,"ok":false,"error":"setup_required"})");
  CHECK(ask(fresh, first, fresh_throttle, R"({"id":3,"op":"setup","args":{"code":"WRONGCODE1","user":"admin","password":"long enough pw"}})") == R"({"id":3,"ok":false,"error":"wrong_code"})" && !fresh.users);
  CHECK(ask(fresh, first, fresh_throttle, R"({"id":4,"op":"setup","args":{"code":"TESTCODE01","user":"ab","password":"long enough pw"}})") == R"({"id":4,"ok":false,"error":"invalid_name"})");
  CHECK(ask(fresh, first, fresh_throttle, R"({"id":5,"op":"setup","args":{"code":"TESTCODE01","user":"admin","password":"short"}})") == R"({"id":5,"ok":false,"error":"weak_password"})");
  CHECK(ask(fresh, first, fresh_throttle, R"({"id":6,"op":"setup","args":{"code":"TESTCODE01","user":"admin","password":"long enough pw"}})") == R"({"id":6,"ok":true,"data":{"restart_required":true}})");
  CHECK(fresh.users && fresh.restarts == 0 && first.authenticated && first.role == auth::Role::kAdmin);
  // the code is throttled too
  FakeBackend guessing;
  guessing.users = false;
  ble::Session guess;
  auth::LoginThrottle guess_throttle;
  for (int i = 0; i < 5; ++i) ask(guessing, guess, guess_throttle, R"({"id":1,"op":"setup","args":{"code":"NOPENOPE01","user":"admin","password":"long enough pw"}})");
  CHECK(ask(guessing, guess, guess_throttle, R"({"id":2,"op":"setup","args":{"code":"TESTCODE01","user":"admin","password":"long enough pw"}})").find("too_many_attempts") != std::string::npos && !guessing.users);
}

static void test_ble_setting() {
  config::Settings s = valid_settings();
  CHECK(s.ble == config::BleMode::kSetup);
  config::Settings out;
  config::Problems problems;
  CHECK(config::load(R"({"ble":{"mode":"always"}})", s, out, problems) && out.ble == config::BleMode::kAlways);
  problems.clear();
  CHECK(config::load(R"({"ble":{"mode":"off"}})", s, out, problems) && out.ble == config::BleMode::kOff && config::to_json(out, true).find(R"("ble":{"mode":"off"})") != std::string::npos);
  problems.clear();
  CHECK(!config::load(R"({"ble":{"mode":"loud"}})", s, out, problems) && has_problem(problems, "ble.mode", "invalid"));
}
static void test_sensor_models_in_settings() {
  const config::Settings base = valid_settings();
  config::Settings out;
  config::Problems problems;
  // the default is what the node always had: an LD2450 on each line
  CHECK(base.radars[0].model == "ld2450" && base.radars[0].baud == 0 && base.radars[0].name.empty());
  // a tracker of another model needs nothing more; the speed 0 means the model's own
  CHECK(config::load(R"({"radars":[{"model":"ld2461"}]})", base, out, problems) && out.radars[0].model == "ld2461" && out.radars[1].model == "ld2450");
  problems.clear();
  CHECK(config::load(R"({"radars":[{"model":"ld2461","baud":115200}]})", base, out, problems) && out.radars[0].baud == 115200);
  problems.clear();
  // a presence sensor is a device: it needs a name, which may not clash with a pin's or another sensor's
  CHECK(!config::load(R"({"radars":[{"model":"ld2410"}]})", base, out, problems) && has_problem(problems, "radars.0.name", "required"));
  problems.clear();
  CHECK(config::load(R"({"radars":[{"model":"ld2410","name":"garage_presence"},{"model":"mr24hpc1","name":"hall"}]})", base, out, problems) && out.radars[1].name == "hall");
  problems.clear();
  CHECK(!config::load(R"({"radars":[{"model":"ld2410","name":"same"},{"model":"ld2412","name":"same"}]})", base, out, problems) && has_problem(problems, "radars.1.name", "conflict"));
  problems.clear();
  CHECK(!config::load(R"({"radars":[{"model":"ld2410","name":"same"}],"pins":[{"gpio":39,"name":"same","mode":"output"}]})", base, out, problems) && has_problem(problems, "pins.0.name", "conflict"));
  problems.clear();
  CHECK(!config::load(R"({"radars":[{"model":"ld2410","name":"Bad Name"}]})", base, out, problems) && has_problem(problems, "radars.0.name", "invalid"));
  problems.clear();
  CHECK(!config::load(R"({"radars":[{"model":"ld9999"}]})", base, out, problems) && has_problem(problems, "radars.0.model", "invalid"));
  problems.clear();
  CHECK(!config::load(R"({"radars":[{"baud":12345}]})", base, out, problems) && has_problem(problems, "radars.0.baud", "range"));
  problems.clear();
  // a disabled line is not checked, and everything survives a round trip
  CHECK(config::load(R"({"radars":[{"enabled":false,"model":"ld2410"}]})", base, out, problems));
  problems.clear();
  config::Settings mixed = base;
  mixed.radars[0].model = "ld2461"; mixed.radars[0].baud = 9600;
  mixed.radars[1].model = "mr24hpc1"; mixed.radars[1].name = "hall";
  mixed.radars[0].offset_x_mm = 150; mixed.radars[0].offset_y_mm = -80; mixed.radars[0].yaw_deg = 120; mixed.radars[0].pitch_deg = -10;
  const std::string stored = config::to_json(mixed, true);
  config::Settings back;
  CHECK(config::load(stored, base, back, problems) && back.radars[0].model == "ld2461" && back.radars[0].baud == 9600 && back.radars[1].model == "mr24hpc1" && back.radars[1].name == "hall" && config::to_json(back, true) == stored);
  CHECK(back.radars[0].offset_x_mm == 150 && back.radars[0].offset_y_mm == -80 && back.radars[0].yaw_deg == 120 && back.radars[0].pitch_deg == -10);
  problems.clear();
  CHECK(!config::load(R"({"radars":[{"yaw_deg":200}]})", base, out, problems) && has_problem(problems, "radars.0.yaw_deg", "range"));
  problems.clear();
  CHECK(!config::load(R"({"radars":[{"offset_x_mm":9000}]})", base, out, problems) && has_problem(problems, "radars.0.offset_x_mm", "range"));
  problems.clear();
  CHECK(config::load(R"({"fusion":{"merge_mm":300}})", base, out, problems) && out.fusion_merge_mm == 300);
  problems.clear();
  CHECK(!config::load(R"({"fusion":{"merge_mm":3000}})", base, out, problems) && has_problem(problems, "fusion.merge_mm", "range"));
}

static void test_web_policy() {
  using namespace armor::webpolicy;
  CHECK(serves_https(config::WebMode::kBoth) && serves_https(config::WebMode::kHttps) && !serves_https(config::WebMode::kHttp));
  CHECK(http_redirects(config::WebMode::kHttps) && !http_redirects(config::WebMode::kBoth) && !http_redirects(config::WebMode::kHttp));
  CHECK(clean_host("192.168.0.181") == "192.168.0.181" && clean_host("armor-a1b2c3.local:80") == "armor-a1b2c3.local" && clean_host("node-1") == "node-1");
  CHECK(clean_host("").empty() && clean_host(":80").empty() && clean_host("evil.example/x").empty() && clean_host("a b").empty() && clean_host("-a").empty() && clean_host(".a").empty());
  CHECK(clean_host("[::1]:80").empty() && clean_host(std::string(254, 'a')).empty() && clean_host("a@b").empty());
  CHECK(redirect_location("192.168.0.181:80", "/") == "https://192.168.0.181/");
  CHECK(redirect_location("armor.local", "/api/v1/status?x=1") == "https://armor.local/api/v1/status?x=1");
  CHECK(redirect_location("evil.example/", "/").empty() && redirect_location("a.b", "").empty() && redirect_location("a.b", "//evil.example/x").empty());
  CHECK(redirect_location("a.b", "/x y").empty() && redirect_location("a.b", "/x\\y").empty() && redirect_location("a.b", "x").empty());
  CHECK(cookie_attributes(false, 1800) == "Path=/; HttpOnly; SameSite=Strict; Max-Age=1800");
  CHECK(cookie_attributes(true, 0) == "Path=/; HttpOnly; SameSite=Strict; Secure; Max-Age=0");
  // the setting: default both, the three words, anything else refused, and it survives a round trip
  const config::Settings base = valid_settings();
  config::Settings out;
  config::Problems problems;
  CHECK(base.web == config::WebMode::kBoth);
  CHECK(config::load(R"({"web":{"mode":"https"}})", base, out, problems) && out.web == config::WebMode::kHttps);
  problems.clear();
  CHECK(config::load(R"({"web":{"mode":"http"}})", base, out, problems) && out.web == config::WebMode::kHttp);
  problems.clear();
  CHECK(!config::load(R"({"web":{"mode":"ftp"}})", base, out, problems) && has_problem(problems, "web.mode", "invalid"));
  problems.clear();
  config::Settings mixed = base;
  mixed.web = config::WebMode::kHttps;
  const std::string stored = config::to_json(mixed, true);
  config::Settings back;
  CHECK(config::load(stored, base, back, problems) && back.web == config::WebMode::kHttps && config::to_json(back, true) == stored);
}

int main() {
  test_web_policy();
  test_sensor_models_in_settings();
  test_ble_framing();
  test_ble_requests();
  test_ble_dispatch();
  test_ble_setting();
  test_info_message();
  test_network_plan();
  test_codes();
  test_json();
  test_net_text();
  test_pins();
  test_settings_defaults_and_roundtrip();
  test_settings_partial_update_and_secrets();
  test_settings_rejections();
  test_channel_choice();
  test_gpio_commands();
  test_gpio_levels_and_duty();
  test_debouncer();
  test_link_watch_and_fallback();
  test_gpio_topics_and_reports();
  test_users();
  test_sessions();
  test_throttle();
  test_ld2450_commands();
  std::printf("%d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
