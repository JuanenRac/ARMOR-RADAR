// ARMOR-RADAR - the Ethernet hardware of the Waveshare ESP32-S3-ETH (W5500 over SPI).
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D). GPL-3.0-or-later.
#pragma once
extern "C" {
#include "esp_eth.h"
}

namespace armor {

// Initialises the SPI bus and the W5500 driver and gives the chip the node's own MAC address. The driver is NOT started: network.cpp
// attaches it to the TCP/IP stack (alone, or bridged with the access point) and starts it. Returns nullptr, after logging why, when
// the hardware could not be initialised (wrong pins, no W5500 answering).
esp_eth_handle_t ethernet_driver_create();

}  // namespace armor
