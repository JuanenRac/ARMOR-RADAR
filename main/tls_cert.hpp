// ARMOR-RADAR - the node's own TLS certificate: made once, kept in flash, used by the HTTPS panel.
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D). GPL-3.0-or-later.
#pragma once
#include <string>

namespace armor::tlscert {

struct Material {
  std::string certificate_pem;   // including the terminating NUL, as the HTTPS server wants it
  std::string key_pem;           // including the terminating NUL
  std::string sha256;            // the certificate's SHA-256 fingerprint, 64 lowercase hexadecimal digits, to compare with what a browser shows
};

// Loads the certificate and its key from flash or, the first time, makes them: an ECDSA P-256 key and a self-signed certificate for the node's
// id, valid for fifty years. False when they could not be made or stored. A factory reset erases them, and the next start makes new ones.
bool load_or_create(const std::string& node_id, Material& out);

}  // namespace armor::tlscert
