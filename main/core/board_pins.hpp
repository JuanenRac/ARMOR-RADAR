// ARMOR-RADAR - which GPIO pins of the Waveshare ESP32-S3-ETH may be handed to the operator, and why the others may not.
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D). GPL-3.0-or-later.
//
// The board carries an ESP32-S3R8 (8 MB of octal PSRAM inside the chip) and 16 MB of external flash. The table below comes from the
// manufacturer's pin map and the chip's datasheet; a pin that is uncertain is reserved rather than offered:
//   - GPIO 22..25 do not exist, and 26..32 are the external flash;
//   - GPIO 33..37 belong to the octal PSRAM (the datasheet is only certain for 35..37; 33 and 34 are kept out too);
//   - GPIO 19 and 20 are the USB pair used for the log and for flashing;
//   - GPIO 9..14 are the W5500 (reset, interrupt, MOSI, MISO, clock, chip select);
//   - GPIO 8 is wired to the camera connector and is not on the pin header;
//   - GPIO 4..7 are the microSD socket: free to use only when no card is meant to be used;
//   - GPIO 0 (BOOT button), 3, 45 and 46 are strapping pins read at reset: usable, but whatever is wired there must not pull them
//     the wrong way while the chip starts.
// The camera connector shares its pins with the header (which ones is not in the documents at hand), so a wiring that uses the
// camera must be checked against the schematic. Nothing here has been checked on a board.
#pragma once
#include <array>
#include <cstdint>

namespace armor::board {

enum class PinUse : std::uint8_t {
  kFree,      // no special role
  kCaution,   // a strapping pin, or the boot button
  kSdCard,    // the microSD socket: free when no card is used
  kReserved,  // taken by the board or by the chip: never offered
};

enum class Reserved : std::uint8_t { kNone, kNoSuchPin, kFlash, kPsram, kUsb, kEthernet, kCamera };

struct PinInfo {
  int gpio;
  PinUse use;
  Reserved reason;  // only meaningful for kReserved
  bool on_header;   // reachable on the pin header (false: soldered on the board only)
  bool adc1;        // an ADC1 input (ADC2 shares the radio and is not used while Wi-Fi runs)
};

constexpr int kFirstGpio = 0;
constexpr int kLastGpio = 48;

constexpr PinInfo pin_info(int gpio) {
  if (gpio < kFirstGpio || gpio > kLastGpio || (gpio >= 22 && gpio <= 25)) return {gpio, PinUse::kReserved, Reserved::kNoSuchPin, false, false};
  if (gpio >= 26 && gpio <= 32) return {gpio, PinUse::kReserved, Reserved::kFlash, false, false};
  if (gpio >= 33 && gpio <= 37) return {gpio, PinUse::kReserved, Reserved::kPsram, true, false};
  if (gpio == 19 || gpio == 20) return {gpio, PinUse::kReserved, Reserved::kUsb, true, false};
  if (gpio >= 9 && gpio <= 14) return {gpio, PinUse::kReserved, Reserved::kEthernet, false, gpio <= 10};
  if (gpio == 8) return {gpio, PinUse::kReserved, Reserved::kCamera, false, true};
  if (gpio >= 4 && gpio <= 7) return {gpio, PinUse::kSdCard, Reserved::kNone, true, true};
  if (gpio == 0 || gpio == 3 || gpio == 45 || gpio == 46) return {gpio, PinUse::kCaution, Reserved::kNone, true, gpio == 3};
  return {gpio, PinUse::kFree, Reserved::kNone, true, gpio >= 1 && gpio <= 10};
}

// Whether the operator may map this pin: not reserved, and an SD pin only when the card is not used.
constexpr bool assignable(int gpio, bool sd_card_unused = true) {
  const PinInfo info = pin_info(gpio);
  if (info.use == PinUse::kReserved) return false;
  if (info.use == PinUse::kSdCard) return sd_card_unused;
  return true;
}

// Where the three radars are wired by default: their TX lines reach the RX pins, and the node's TX pins reach the radars' RX (for the
// configuration commands). All six are free header pins.
constexpr std::array<int, 3> kDefaultRadarRx{16, 17, 18};
constexpr std::array<int, 3> kDefaultRadarTx{15, 21, 38};
constexpr int kDefaultI2cSda = 1;
constexpr int kDefaultI2cScl = 2;

}  // namespace armor::board
