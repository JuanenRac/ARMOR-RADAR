// ARMOR-RADAR - checks for the network settings an operator types: IPv4 addresses, netmasks, host names, Wi-Fi names and keys.
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D). GPL-3.0-or-later.
#pragma once
#include <cstdint>
#include <string>
#include <string_view>

namespace armor::net {

// "192.168.0.50" -> host-order integer. Exactly four decimal parts of at most three digits with no leading zero (so "010" is refused
// rather than read as octal by some other tool), each 0..255.
inline bool parse_ipv4(std::string_view text, std::uint32_t& out) {
  std::uint32_t value = 0;
  int parts = 0;
  std::size_t at = 0;
  while (parts < 4) {
    std::size_t digits = 0;
    unsigned part = 0;
    while (at < text.size() && text[at] >= '0' && text[at] <= '9') {
      part = part * 10 + static_cast<unsigned>(text[at] - '0');
      ++at;
      if (++digits > 3) return false;
    }
    if (digits == 0 || part > 255 || (digits > 1 && text[at - digits] == '0')) return false;
    value = (value << 8) | part;
    ++parts;
    if (parts < 4) {
      if (at >= text.size() || text[at] != '.') return false;
      ++at;
    }
  }
  if (at != text.size()) return false;
  out = value;
  return true;
}

inline std::string ipv4_text(std::uint32_t value) {
  return std::to_string((value >> 24) & 255) + "." + std::to_string((value >> 16) & 255) + "." + std::to_string((value >> 8) & 255) + "." + std::to_string(value & 255);
}

// A netmask is a run of ones then a run of zeros; /1 to /30 (a /31 or /32 leaves no room for a gateway and a host).
inline bool valid_netmask(std::uint32_t mask) {
  if (mask == 0) return false;
  const std::uint32_t inverted = ~mask;
  if ((inverted & (inverted + 1)) != 0) return false;  // not contiguous
  return inverted >= 3;                                // at least two host bits
}

// A unicast address a host can hold: not 0.x, not loopback (127.x), not multicast or reserved (224 and above).
inline bool usable_host_address(std::uint32_t address) {
  const unsigned first = address >> 24;
  return first != 0 && first != 127 && first < 224;
}

inline bool same_subnet(std::uint32_t a, std::uint32_t b, std::uint32_t mask) { return (a & mask) == (b & mask); }
inline bool is_network_or_broadcast(std::uint32_t address, std::uint32_t mask) { return (address & ~mask) == 0 || (address & ~mask) == ~mask; }

// A host name (RFC 952/1123 label): letters, digits and hyphens, not starting or ending with a hyphen, 1 to 32 characters.
inline bool valid_hostname(std::string_view name) {
  if (name.empty() || name.size() > 32 || name.front() == '-' || name.back() == '-') return false;
  for (const char c : name) if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-')) return false;
  return true;
}

// A DNS name or an IPv4 address for a broker or a time server: 1 to 253 characters of letters, digits, '.', '-' and '_'.
inline bool valid_host(std::string_view host) {
  if (host.empty() || host.size() > 253) return false;
  for (const char c : host) if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' || c == '-' || c == '_')) return false;
  return host.front() != '.' && host.front() != '-' && host.back() != '.';
}

// Whether the text is well-formed UTF-8 (no overlong forms, no surrogates, nothing beyond U+10FFFF), and how many characters it holds.
inline bool valid_utf8(std::string_view text, std::size_t* characters = nullptr) {
  std::size_t count = 0, i = 0;
  while (i < text.size()) {
    const unsigned char c = static_cast<unsigned char>(text[i]);
    std::size_t extra = 0;
    unsigned code = 0;
    if (c < 0x80) { code = c; }
    else if (c >= 0xC2 && c <= 0xDF) { extra = 1; code = c & 0x1F; }
    else if (c >= 0xE0 && c <= 0xEF) { extra = 2; code = c & 0x0F; }
    else if (c >= 0xF0 && c <= 0xF4) { extra = 3; code = c & 0x07; }
    else return false;
    if (extra > 0 && i + extra >= text.size()) return false;  // the sequence is cut short
    for (std::size_t k = 1; k <= extra; ++k) {
      const unsigned char next = static_cast<unsigned char>(text[i + k]);
      if ((next & 0xC0) != 0x80) return false;
      code = (code << 6) | (next & 0x3F);
    }
    if ((extra == 2 && code < 0x800) || (extra == 3 && code < 0x10000) || code > 0x10FFFF || (code >= 0xD800 && code <= 0xDFFF)) return false;
    i += extra + 1;
    ++count;
  }
  if (characters != nullptr) *characters = count;
  return true;
}

// A Wi-Fi network name is 1 to 32 bytes; control characters would break logs and the panel.
inline bool valid_ssid(std::string_view ssid) {
  if (ssid.empty() || ssid.size() > 32) return false;
  for (const char c : ssid) if (static_cast<unsigned char>(c) < 0x20 || c == 0x7f) return false;
  return true;
}

// A WPA passphrase is 8 to 63 printable ASCII characters (a 64-digit hexadecimal key is not offered).
inline bool valid_wpa_passphrase(std::string_view key) {
  if (key.size() < 8 || key.size() > 63) return false;
  for (const char c : key) if (c < 0x20 || c > 0x7e) return false;
  return true;
}

}  // namespace armor::net
