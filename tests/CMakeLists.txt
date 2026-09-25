# ARMOR-RADAR host tests: the hardware-independent core, built with any C++17 compiler.
# Copyright (C) 2026 JuanenRac (Electro Hobby 3D). GPL-3.0-or-later.
cmake_minimum_required(VERSION 3.16)
project(armor_radar_host_tests CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
add_compile_options(-Wall -Wextra -Wpedantic -Werror)

add_executable(test_core test_core.cpp)
add_executable(test_node test_node.cpp)
add_executable(emit_samples emit_samples.cpp)

enable_testing()
add_test(NAME core COMMAND test_core)
add_test(NAME node COMMAND test_node)
