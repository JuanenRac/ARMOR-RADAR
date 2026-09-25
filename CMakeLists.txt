# ARMOR-RADAR ESP-IDF project entry point.
# Copyright (C) 2026 JuanenRac (Electro Hobby 3D). GPL-3.0-or-later.
cmake_minimum_required(VERSION 3.16)
# The firmware's version is the project's: it is read from armor.project.json, the one place that says it.
file(READ "${CMAKE_CURRENT_LIST_DIR}/armor.project.json" ARMOR_MANIFEST)
string(REGEX MATCH "\"version\"[ \t]*:[ \t]*\"([0-9]+\.[0-9]+\.[0-9]+)\"" ARMOR_VERSION_MATCH "${ARMOR_MANIFEST}")
set(PROJECT_VER "${CMAKE_MATCH_1}")
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(armor_radar)
