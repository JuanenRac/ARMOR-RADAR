// ARMOR-RADAR - what the panel does over HTTP and over HTTPS: which servers run, where plain HTTP sends the browser, how the cookie is marked.
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D). GPL-3.0-or-later.
//
// The node makes its own certificate (self-signed, so a browser warns the first time): what HTTPS gives is that the password and the session
// cookie no longer travel in clear on the network. Everything decided here is host-tested; web_server.cpp only applies it.
#pragma once
#include <cctype>
#include <string>
#include <string_view>

#include "node_config.hpp"

namespace armor::webpolicy {

constexpr int kHttpPort = 80;
constexpr int kHttpsPort = 443;

// Port 80 always answers: in https mode it only redirects.
inline bool serves_https(config::WebMode mode) { return mode != config::WebMode::kHttp; }
inline bool http_redirects(config::WebMode mode) { return mode == config::WebMode::kHttps; }

// The host part of a Host header (a name or an IPv4 address, optionally with :port), or "" when it is not one we would send a browser to:
// only letters, digits, dots and hyphens, at most 253 characters, so an odd header can never become a redirect to somewhere else.
inline std::string clean_host(std::string_view header) {
  const std::size_t colon = header.find(':');
  std::string_view host = colon == std::string_view::npos ? header : header.substr(0, colon);
  if (host.empty() || host.size() > 253) return "";
  for (char c : host) if (!std::isalnum(static_cast<unsigned char>(c)) && c != '.' && c != '-') return "";
  if (host.front() == '.' || host.front() == '-' || host.back() == '-') return "";
  return std::string(host);
}

// Where a plain-HTTP request goes in https mode: the same path over HTTPS on the standard port. "" when the host is not usable.
inline std::string redirect_location(std::string_view host_header, std::string_view uri) {
  const std::string host = clean_host(host_header);
  if (host.empty() || uri.empty() || uri.front() != '/' || uri.substr(0, 2) == "//") return "";
  for (char c : uri) if (static_cast<unsigned char>(c) < 0x21 || c == '\\' || static_cast<unsigned char>(c) > 0x7E) return "";
  return "https://" + host + std::string(uri);
}

// The attributes of the session cookie: always HttpOnly and SameSite=Strict, and Secure when the request came over TLS.
inline std::string cookie_attributes(bool tls, int max_age_seconds) {
  return "Path=/; HttpOnly; SameSite=Strict" + std::string(tls ? "; Secure" : "") + "; Max-Age=" + std::to_string(max_age_seconds);
}

}  // namespace armor::webpolicy
