// ARMOR-RADAR - Ethernet of the Waveshare ESP32-S3-ETH (W5500 over SPI).
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D). GPL-3.0-or-later.
//
// The board wires the W5500 to SPI as MOSI 11, MISO 12, SCLK 13, CS 14, INT 10 and RST 9 (the manufacturer's pin table); the
// pins are menuconfig values so another board can reuse this file. The W5500 has no MAC address of its own: the one the chip
// vendor burned into the ESP32-S3 for Ethernet is used, so two nodes never share one.
#include "board_ethernet.hpp"

#include <cstdint>
#include <cstdio>
#include <cstring>
extern "C" {
#include "sdkconfig.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_eth.h"
#include "esp_eth_mac_spi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
}

namespace armor {
namespace {
constexpr char kTag[] = "armor-eth";
volatile bool g_has_ip = false;
char g_ip_text[16] = "";

void on_eth_event(void*, esp_event_base_t, int32_t event_id, void* data) {
  auto handle = *static_cast<esp_eth_handle_t*>(data);
  switch (event_id) {
    case ETHERNET_EVENT_CONNECTED: {
      std::uint8_t mac[6]{};
      esp_eth_ioctl(handle, ETH_CMD_G_MAC_ADDR, mac);
      ESP_LOGI(kTag, "link up, MAC %02x:%02x:%02x:%02x:%02x:%02x", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
#if CONFIG_ARMOR_ETH_STATIC_IP
      // A fixed address gets no DHCP event: it is usable as soon as the link is.
      std::snprintf(g_ip_text, sizeof g_ip_text, "%s", CONFIG_ARMOR_ETH_IP);
      g_has_ip = true;
      ESP_LOGI(kTag, "fixed address %s", g_ip_text);
#endif
      break;
    }
    case ETHERNET_EVENT_DISCONNECTED:
      g_has_ip = false;
      g_ip_text[0] = '\0';
      ESP_LOGW(kTag, "link down (cable, switch or PoE injector)");
      break;
    case ETHERNET_EVENT_START: ESP_LOGI(kTag, "driver started, waiting for the link"); break;
    case ETHERNET_EVENT_STOP: g_has_ip = false; break;
    default: break;
  }
}

void on_got_ip(void*, esp_event_base_t, int32_t, void* data) {
  const auto* event = static_cast<ip_event_got_ip_t*>(data);
  std::snprintf(g_ip_text, sizeof g_ip_text, IPSTR, IP2STR(&event->ip_info.ip));
  g_has_ip = true;
  ESP_LOGI(kTag, "address %s, gateway " IPSTR ", netmask " IPSTR, g_ip_text, IP2STR(&event->ip_info.gw), IP2STR(&event->ip_info.netmask));
}

#if CONFIG_ARMOR_ETH_STATIC_IP
bool apply_static_address(esp_netif_t* netif) {
  esp_netif_ip_info_t info{};
  if (esp_netif_str_to_ip4(CONFIG_ARMOR_ETH_IP, &info.ip) != ESP_OK || esp_netif_str_to_ip4(CONFIG_ARMOR_ETH_NETMASK, &info.netmask) != ESP_OK ||
      esp_netif_str_to_ip4(CONFIG_ARMOR_ETH_GATEWAY, &info.gw) != ESP_OK) {
    ESP_LOGE(kTag, "the fixed IPv4 address, netmask or gateway in menuconfig is not valid");
    return false;
  }
  esp_netif_dns_info_t dns{};
  if (esp_netif_str_to_ip4(CONFIG_ARMOR_ETH_DNS, &dns.ip.u_addr.ip4) != ESP_OK) {
    ESP_LOGE(kTag, "the DNS address in menuconfig is not valid");
    return false;
  }
  dns.ip.type = ESP_IPADDR_TYPE_V4;
  ESP_ERROR_CHECK(esp_netif_dhcpc_stop(netif));
  ESP_ERROR_CHECK(esp_netif_set_ip_info(netif, &info));
  ESP_ERROR_CHECK(esp_netif_set_dns_info(netif, ESP_NETIF_DNS_MAIN, &dns));
  return true;
}
#endif
}  // namespace

bool ethernet_start() {
  spi_bus_config_t bus{};
  bus.mosi_io_num = CONFIG_ARMOR_ETH_MOSI;
  bus.miso_io_num = CONFIG_ARMOR_ETH_MISO;
  bus.sclk_io_num = CONFIG_ARMOR_ETH_SCLK;
  bus.quadwp_io_num = -1;
  bus.quadhd_io_num = -1;
  const spi_host_device_t host = static_cast<spi_host_device_t>(CONFIG_ARMOR_ETH_SPI_HOST);
  if (spi_bus_initialize(host, &bus, SPI_DMA_CH_AUTO) != ESP_OK) { ESP_LOGE(kTag, "the SPI bus for the W5500 could not be initialised"); return false; }
  esp_err_t err = gpio_install_isr_service(0);
  if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) { ESP_LOGE(kTag, "the GPIO interrupt service failed: %s", esp_err_to_name(err)); return false; }

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
  if (mac == nullptr || phy == nullptr) { ESP_LOGE(kTag, "the W5500 driver could not be created"); return false; }

  esp_eth_config_t config = ETH_DEFAULT_CONFIG(mac, phy);
  static esp_eth_handle_t handle = nullptr;
  err = esp_eth_driver_install(&config, &handle);
  if (err != ESP_OK) { ESP_LOGE(kTag, "no W5500 answers on SPI (check the wiring and the pins): %s", esp_err_to_name(err)); return false; }

  std::uint8_t address[6]{};
  ESP_ERROR_CHECK(esp_read_mac(address, ESP_MAC_ETH));
  ESP_ERROR_CHECK(esp_eth_ioctl(handle, ETH_CMD_S_MAC_ADDR, address));

  esp_netif_config_t netif_config = ESP_NETIF_DEFAULT_ETH();
  esp_netif_t* netif = esp_netif_new(&netif_config);
  ESP_ERROR_CHECK(esp_netif_attach(netif, esp_eth_new_netif_glue(handle)));
  char hostname[40];
  std::snprintf(hostname, sizeof hostname, "armor-%s", CONFIG_ARMOR_NODE_ID);
  esp_netif_set_hostname(netif, hostname);
  ESP_ERROR_CHECK(esp_event_handler_register(ETH_EVENT, ESP_EVENT_ANY_ID, &on_eth_event, nullptr));
  ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_ETH_GOT_IP, &on_got_ip, nullptr));
#if CONFIG_ARMOR_ETH_STATIC_IP
  if (!apply_static_address(netif)) return false;
#endif
  ESP_ERROR_CHECK(esp_eth_start(handle));
  return true;
}

bool ethernet_has_ip() { return g_has_ip; }
const char* ethernet_ip_text() { return g_ip_text; }

}  // namespace armor
