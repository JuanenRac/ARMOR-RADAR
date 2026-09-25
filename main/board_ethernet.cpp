// ARMOR-RADAR - the Ethernet hardware of the Waveshare ESP32-S3-ETH (W5500 over SPI).
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D). GPL-3.0-or-later.
//
// The board wires the W5500 to SPI as MOSI 11, MISO 12, SCLK 13, CS 14, INT 10 and RST 9 (the manufacturer's pin table); the
// pins are menuconfig values so another board can reuse this file. The W5500 has no MAC address of its own: the one the chip
// vendor burned into the ESP32-S3 for Ethernet is used, so two nodes never share one.
#include "board_ethernet.hpp"

#include <cstdint>
extern "C" {
#include "sdkconfig.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_eth_mac_spi.h"
#include "esp_log.h"
#include "esp_mac.h"
}

namespace armor {
namespace {
constexpr char kTag[] = "armor-eth";
}

esp_eth_handle_t ethernet_driver_create() {
  spi_bus_config_t bus{};
  bus.mosi_io_num = CONFIG_ARMOR_ETH_MOSI;
  bus.miso_io_num = CONFIG_ARMOR_ETH_MISO;
  bus.sclk_io_num = CONFIG_ARMOR_ETH_SCLK;
  bus.quadwp_io_num = -1;
  bus.quadhd_io_num = -1;
  const spi_host_device_t host = static_cast<spi_host_device_t>(CONFIG_ARMOR_ETH_SPI_HOST);
  if (spi_bus_initialize(host, &bus, SPI_DMA_CH_AUTO) != ESP_OK) { ESP_LOGE(kTag, "the SPI bus for the W5500 could not be initialised"); return nullptr; }
  esp_err_t err = gpio_install_isr_service(0);
  if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) { ESP_LOGE(kTag, "the GPIO interrupt service failed: %s", esp_err_to_name(err)); return nullptr; }

  spi_device_interface_config_t device{};
  device.mode = 0;
  device.clock_speed_hz = CONFIG_ARMOR_ETH_SPI_MHZ * 1000 * 1000;
  device.spics_io_num = CONFIG_ARMOR_ETH_CS;
  device.queue_size = 20;
  eth_w5500_config_t w5500 = ETH_W5500_DEFAULT_CONFIG(host, &device);
  w5500.int_gpio_num = CONFIG_ARMOR_ETH_INT;
  eth_mac_config_t mac_config = ETH_MAC_DEFAULT_CONFIG();
  eth_phy_config_t phy_config = ETH_PHY_DEFAULT_CONFIG();
  phy_config.phy_addr = 1;
  phy_config.reset_gpio_num = CONFIG_ARMOR_ETH_RST;
  esp_eth_mac_t* mac = esp_eth_mac_new_w5500(&w5500, &mac_config);
  esp_eth_phy_t* phy = esp_eth_phy_new_w5500(&phy_config);
  if (mac == nullptr || phy == nullptr) { ESP_LOGE(kTag, "the W5500 driver could not be created"); return nullptr; }

  esp_eth_config_t config = ETH_DEFAULT_CONFIG(mac, phy);
  esp_eth_handle_t handle = nullptr;
  err = esp_eth_driver_install(&config, &handle);
  if (err != ESP_OK) { ESP_LOGE(kTag, "no W5500 answers on SPI (check the wiring and the pins): %s", esp_err_to_name(err)); return nullptr; }

  std::uint8_t address[6]{};
  ESP_ERROR_CHECK(esp_read_mac(address, ESP_MAC_ETH));
  ESP_ERROR_CHECK(esp_eth_ioctl(handle, ETH_CMD_S_MAC_ADDR, address));
  return handle;
}

}  // namespace armor
