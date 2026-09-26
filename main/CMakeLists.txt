idf_component_register(SRCS "api_shared.cpp" "app_main.cpp" "armor_radar.cpp" "ble_provision.cpp" "board_ethernet.cpp" "entropy.cpp" "gpio_manager.cpp" "light_sensor.cpp" "log_buffer.cpp"
                            "mqtt_link.cpp" "network.cpp" "node_store.cpp" "radar_manager.cpp" "tls_cert.cpp" "web_server.cpp"
                       INCLUDE_DIRS "."
                       REQUIRES driver esp_driver_gpio esp_driver_ledc esp_driver_spi esp_driver_i2c esp_driver_uart esp_adc esp_eth esp_event esp_netif esp_timer
                                esp_wifi esp_http_server esp_https_server app_update esp_partition esp_app_format mbedtls bootloader_support mqtt nvs_flash lwip esp_hw_support
                                esp_system heap log bt)

# The web panel (panel/) is compressed at configure time and embedded in the firmware: an update of the firmware updates the panel.
set(ARMOR_PANEL_DIR "${PROJECT_DIR}/panel")
set(ARMOR_PANEL_OUT "${CMAKE_CURRENT_BINARY_DIR}/panel")
find_package(Python3 REQUIRED COMPONENTS Interpreter)
execute_process(COMMAND ${Python3_EXECUTABLE} "${PROJECT_DIR}/tools/pack_panel.py" "${ARMOR_PANEL_DIR}" "${ARMOR_PANEL_OUT}" RESULT_VARIABLE ARMOR_PACK_RESULT)
if(NOT ARMOR_PACK_RESULT EQUAL 0)
  message(FATAL_ERROR "tools/pack_panel.py failed")
endif()
set_property(DIRECTORY "${COMPONENT_DIR}" APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${ARMOR_PANEL_DIR}/text.js")
foreach(asset index.html app.js style.css)
  set_property(DIRECTORY "${COMPONENT_DIR}" APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${ARMOR_PANEL_DIR}/${asset}")
  target_add_binary_data(${COMPONENT_LIB} "${ARMOR_PANEL_OUT}/${asset}.gz" BINARY)
endforeach()

# A CA certificate for mqtts:// is embedded when the operator puts one at certs/ca.pem (never committed: it is git-ignored).
if(EXISTS "${PROJECT_DIR}/certs/ca.pem")
  target_add_binary_data(${COMPONENT_LIB} "${PROJECT_DIR}/certs/ca.pem" TEXT)
  target_compile_definitions(${COMPONENT_LIB} PRIVATE ARMOR_MQTT_HAS_CA=1)
endif()
