// ARMOR-RADAR - Ethernet of the Waveshare ESP32-S3-ETH (W5500 over SPI).
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D). GPL-3.0-or-later.
#pragma once

namespace armor {

// Brings up the W5500, attaches it to the TCP/IP stack and asks for an address (DHCP, or the fixed one from menuconfig).
// It returns as soon as the driver is started; the link and the address arrive later as events. Returns false when the
// hardware could not be initialised (wrong pins, no W5500 answering), after logging why.
bool ethernet_start();

// True from the moment the interface has an IPv4 address until the link drops.
bool ethernet_has_ip();

// The address as text ("" when there is none), for the log.
const char* ethernet_ip_text();

}  // namespace armor
