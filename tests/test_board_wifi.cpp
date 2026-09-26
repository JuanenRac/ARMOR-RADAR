// ARMOR-RADAR - host tests of the s3-wifi board profile (an ESP32-S3-WROOM-1 N16R8 with no Ethernet): its pin table, its defaults, the refusal of the Ethernet way in and the
// layouts of its network. It is built with ARMOR_BOARD_S3_WIFI defined (tests/CMakeLists.txt); test_node.cpp covers the s3-eth profile (the Waveshare board).
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D). GPL-3.0-or-later.
#include <cstdio>
#include <string>

#include "../main/core/network_plan.hpp"
#include "../main/core/node_config.hpp"

#ifndef ARMOR_BOARD_S3_WIFI
#error "this test is for the s3-wifi profile: build it with -DARMOR_BOARD_S3_WIFI=1"
#endif

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

static bool has(const config::Problems& problems, const std::string& path, const std::string& code) {
  for (const config::Problem& p : problems) if (p.path == path && p.code == code) return true;
  return false;
}

static void test_profile_and_pins() {
  CHECK(!board::kHasEthernet && std::string(board::kId) == "s3-wifi");
  // there is no W5500, no camera connector and no microSD socket: GPIO 4 to 14 are ordinary header pins
  for (int gpio = 4; gpio <= 14; ++gpio) CHECK(board::assignable(gpio, false) && board::pin_info(gpio).use == board::PinUse::kFree);
  // the flash, the PSRAM and the native USB stay reserved
  for (int gpio : {26, 30, 33, 35, 37, 19, 20, 22, 49, -1}) CHECK(!board::assignable(gpio));
  CHECK(board::pin_info(19).reason == board::Reserved::kUsb && board::pin_info(33).reason == board::Reserved::kPsram && board::pin_info(27).reason == board::Reserved::kFlash);
  // the strapping pins, the boot button, the USB-serial pair and the LED come with a warning
  for (int gpio : {0, 3, 45, 46, 43, 44, 48}) CHECK(board::assignable(gpio) && board::pin_info(gpio).use == board::PinUse::kCaution);
  for (int gpio : {1, 2, 15, 16, 17, 18, 21, 38, 39, 40, 41, 42, 47}) CHECK(board::pin_info(gpio).use == board::PinUse::kFree);
  // the defaults of the three radars and the light sensor are usable here too
  for (int gpio : board::kDefaultRadarRx) CHECK(board::assignable(gpio));
  for (int gpio : board::kDefaultRadarTx) CHECK(board::assignable(gpio));
  CHECK(board::assignable(board::kDefaultI2cSda) && board::assignable(board::kDefaultI2cScl));
}

static void test_settings() {
  config::Settings s = config::default_settings("a1b2c3");
  CHECK(s.uplink == config::Uplink::kWifi);
  // a node on Wi-Fi has no other way in: it is not valid until it has a station to join
  CHECK(has(config::validate(s), "sta.enabled", "required"));
  s.sta.enabled = true; s.sta.ssid = "casa"; s.sta.password = "una-clave-larga";
  CHECK(config::validate(s).empty());
  // the cable does not exist on this board
  s.uplink = config::Uplink::kEthernet;
  CHECK(has(config::validate(s), "uplink", "not_available"));
  config::Settings back;
  config::Problems problems;
  CHECK(!config::load("{\"uplink\":\"ethernet\"}", config::default_settings("000000"), back, problems) && has(problems, "uplink", "not_available"));
  problems.clear();
  CHECK(config::load("{\"uplink\":\"wifi\",\"sta\":{\"enabled\":true,\"ssid\":\"casa\",\"password\":\"una-clave-larga\"}}", config::default_settings("000000"), back, problems) && back.uplink == config::Uplink::kWifi);
  CHECK(config::to_json(config::default_settings("000000"), false).find("\"uplink\":\"wifi\"") != std::string::npos);
  // the pins the W5500 would use on the other board are ordinary pins here: a radar can sit on GPIO 9 to 14
  config::Settings pins = config::default_settings("a1b2c3");
  pins.sta.enabled = true; pins.sta.ssid = "casa";
  pins.radars[0].rx = 9; pins.radars[0].tx = 10;
  pins.radars[1].rx = 11; pins.radars[1].tx = 12;
  CHECK(config::validate(pins).empty());
}

static void test_layouts() {
  config::Settings s = config::default_settings("a1b2c3");
  s.sta.enabled = true; s.sta.ssid = "casa"; s.sta.password = "una-clave-larga";
  // set-up: only the setup network
  netplan::Plan plan = netplan::plan_network(s, true, "CODE1234", "a1b2c3", 5);
  CHECK(plan.ap.setup && plan.ap.ssid == "ARMOR-SETUP-A1B2C3" && plan.ap.password == "CODE1234" && !plan.ap.bridged);
  // after the set-up: a station
  plan = netplan::plan_network(s, false, "", "a1b2c3", 5);
  CHECK(plan.layout == netplan::Layout::kWifiStation && !plan.ap.enabled && std::string(netplan::to_text(plan.layout)) == "wifi-station");
  // and its own network as well, never bridged (there is no wire to bridge it to), even if the settings still say so
  s.ap.enabled = true; s.ap.ssid = "ARMOR-A1B2C3"; s.ap.password = "otra-clave-larga"; s.ap.bridge = true;
  plan = netplan::plan_network(s, false, "", "a1b2c3", 5);
  CHECK(plan.layout == netplan::Layout::kWifiStationWithAp && plan.ap.enabled && !plan.ap.bridged && plan.ap.channel == 11);
  // a stored "ethernet" on a board with no cable still comes up on Wi-Fi
  s.uplink = config::Uplink::kEthernet;
  plan = netplan::plan_network(s, false, "", "a1b2c3", 5);
  CHECK(plan.layout == netplan::Layout::kWifiStationWithAp && !plan.ap.bridged);
}

int main() {
  test_profile_and_pins();
  test_settings();
  test_layouts();
  std::printf("%d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
