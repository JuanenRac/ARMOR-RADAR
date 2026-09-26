// ARMOR-RADAR - what the node keeps in flash: its settings and its panel users.
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D). GPL-3.0-or-later.
//
// Both are one JSON document each in the NVS partition, written only after they were checked. A build's Kconfig values are the FIRST
// settings (used while the flash holds none); after that the flash is the truth and the panel is how it changes.
#pragma once
#include <string>
#include <string_view>

#include "core/auth.hpp"
#include "core/node_config.hpp"

namespace armor::store {

// Opens the NVS partition (erasing it if it is corrupt or from another version of the format) and loads the settings and the users.
void init();

// The MAC of the node as six hexadecimal digits of its last three bytes, and the sum of the six bytes (to spread Wi-Fi channels).
std::string mac_tail();
unsigned mac_sum();

config::Settings settings();                     // a copy
bool settings_are_stored();                      // false until the panel (or a build) has saved them once
bool save_settings(const config::Settings& s);   // false when flash writing failed

bool users_empty();
std::string users_json();                        // names and roles only, never hashes
auth::Result user_add(std::string_view name, std::string_view password, auth::Role role);
bool user_verify(std::string_view name, std::string_view password, auth::Role& role);
auth::Result user_set_password(std::string_view name, std::string_view password);
auth::Result user_set_role(std::string_view name, auth::Role role);
auth::Result user_remove(std::string_view name);

// Erases the settings and the users: the node starts again in setup. Returns false when flash erasing failed.
bool factory_reset();

// Other things the node keeps in flash (its TLS certificate and key): one named blob each. False when it is not there or flash failed.
bool blob_read(const char* key, std::string& out);
bool blob_write(const char* key, const std::string& value);

// The setup code: the build's fixed one, or a random one made at start. Only meaningful while users_empty().
const std::string& setup_code();

}  // namespace armor::store
