idf_component_register(SRCS "app_main.cpp" "armor_radar.cpp"
                       INCLUDE_DIRS "."
                       REQUIRES driver esp_event esp_netif mqtt nvs_flash lwip)
