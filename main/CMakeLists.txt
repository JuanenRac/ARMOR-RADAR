idf_component_register(SRCS "app_main.cpp" "armor_radar.cpp" "board_ethernet.cpp" "light_sensor.cpp"
                       INCLUDE_DIRS "."
                       REQUIRES driver esp_driver_spi esp_driver_i2c esp_eth esp_event esp_netif esp_timer mqtt nvs_flash lwip)

# A CA certificate for mqtts:// is embedded when the operator puts one at certs/ca.pem (never committed: it is git-ignored).
if(EXISTS "${PROJECT_DIR}/certs/ca.pem")
  target_add_binary_data(${COMPONENT_LIB} "${PROJECT_DIR}/certs/ca.pem" TEXT)
  target_compile_definitions(${COMPONENT_LIB} PRIVATE ARMOR_MQTT_HAS_CA=1)
endif()
