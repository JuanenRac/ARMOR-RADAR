# ARMOR-RADAR host tests: the hardware-independent core, built with any C++17 compiler.
# Copyright (C) 2026 JuanenRac (Electro Hobby 3D). GPL-3.0-or-later.
cmake_minimum_required(VERSION 3.16)
project(armor_radar_host_tests CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
add_compile_options(-Wall -Wextra -Wpedantic -Werror)

add_executable(test_core test_core.cpp)
add_executable(test_node test_node.cpp)
add_executable(test_sensors test_sensors.cpp)
# the s3-wifi board profile (an ESP32-S3-WROOM-1 N16R8 with no Ethernet): the same core headers compiled for the other board
add_executable(test_board_wifi test_board_wifi.cpp)
target_compile_definitions(test_board_wifi PRIVATE ARMOR_BOARD_S3_WIFI=1)
add_executable(emit_samples emit_samples.cpp)

enable_testing()
add_test(NAME core COMMAND test_core)
add_test(NAME node COMMAND test_node)
add_test(NAME sensors COMMAND test_sensors)
add_test(NAME board_wifi COMMAND test_board_wifi)
