// ARMOR-RADAR - the node's web panel and its JSON API.
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D). GPL-3.0-or-later.
//
// One dispatcher serves /api/v1/*: it checks the session, reads a bounded body, calls the handler for the method and path and answers
// JSON. Errors are short codes ({"error":"invalid"}), never sentences: the panel translates them into the seven languages.
// The panel itself (index.html, app.js, style.css) is embedded compressed in the firmware, so an update of the firmware updates it too.
#include "web_server.hpp"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
extern "C" {
#include "esp_app_desc.h"
#include "esp_heap_caps.h"
#include "esp_http_server.h"
#include "esp_https_server.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "mbedtls/sha256.h"
#include "sdkconfig.h"
}
#include "core/auth.hpp"
#include "core/gpio_logic.hpp"
#include "core/json.hpp"
#include "core/web_policy.hpp"
#include "core/ld2450_command.hpp"
#include "entropy.hpp"
#include "gpio_manager.hpp"
#include "light_sensor.hpp"
#include "log_buffer.hpp"
#include "mqtt_link.hpp"
#include "api_shared.hpp"
#include "network.hpp"
#include "node_store.hpp"
#include "radar_manager.hpp"
#include "tls_cert.hpp"

extern const std::uint8_t index_html_gz_start[] asm("_binary_index_html_gz_start");
extern const std::uint8_t index_html_gz_end[] asm("_binary_index_html_gz_end");
extern const std::uint8_t app_js_gz_start[] asm("_binary_app_js_gz_start");
extern const std::uint8_t app_js_gz_end[] asm("_binary_app_js_gz_end");
extern const std::uint8_t style_css_gz_start[] asm("_binary_style_css_gz_start");
extern const std::uint8_t style_css_gz_end[] asm("_binary_style_css_gz_end");

namespace armor::web {
namespace {
using api::status_json;
using api::version_text;
constexpr char kTag[] = "armor-web";
constexpr char kCookie[] = "armor_session";
constexpr std::size_t kMaxBody = 12 * 1024;
constexpr std::size_t kMaxFirmware = 0x2E0000;   // a slot of the partition table is 0x300000

std::mutex g_lock;
auth::SessionTable g_sessions;
auth::LoginThrottle g_throttle;
config::Settings g_started_with;
httpd_handle_t g_server = nullptr;       // plain HTTP, port 80
httpd_handle_t g_tls_server = nullptr;   // HTTPS, port 443, when the settings ask for it
tlscert::Material g_material;

// Whether a request came over TLS: it arrived on the HTTPS server.
bool is_tls(httpd_req_t* r) { return g_tls_server != nullptr && r->handle == g_tls_server; }

std::uint64_t now_ms() { return static_cast<std::uint64_t>(esp_timer_get_time()) / 1000ULL; }

// ---- answering -----------------------------------------------------------------------------------------------------------------

const char* status_line(int code) {
  switch (code) {
    case 200: return "200 OK";
    case 400: return "400 Bad Request";
    case 401: return "401 Unauthorized";
    case 403: return "403 Forbidden";
    case 404: return "404 Not Found";
    case 409: return "409 Conflict";
    case 413: return "413 Payload Too Large";
    case 422: return "422 Unprocessable Entity";
    case 429: return "429 Too Many Requests";
    case 500: return "500 Internal Server Error";
    case 503: return "503 Service Unavailable";
    default: return "500 Internal Server Error";
  }
}

void security_headers(httpd_req_t* r) {
  httpd_resp_set_hdr(r, "Cache-Control", "no-store");
  httpd_resp_set_hdr(r, "X-Content-Type-Options", "nosniff");
  httpd_resp_set_hdr(r, "X-Frame-Options", "DENY");
  httpd_resp_set_hdr(r, "Referrer-Policy", "no-referrer");
  httpd_resp_set_hdr(r, "Content-Security-Policy", "default-src 'self'; img-src 'self' data:; style-src 'self'; script-src 'self'; frame-ancestors 'none'");
}

esp_err_t send_json(httpd_req_t* r, int code, const std::string& body) {
  security_headers(r);
  httpd_resp_set_status(r, status_line(code));
  httpd_resp_set_type(r, "application/json");
  return httpd_resp_send(r, body.c_str(), static_cast<ssize_t>(body.size()));
}

esp_err_t send_error(httpd_req_t* r, int code, const char* error) {
  json::Writer w;
  w.begin_object().field("error", error).end_object();
  return send_json(r, code, w.str());
}

esp_err_t send_ok(httpd_req_t* r, bool restart_required = false) {
  json::Writer w;
  w.begin_object().field("ok", true).field("restart_required", restart_required).end_object();
  return send_json(r, 200, w.str());
}

esp_err_t send_asset(httpd_req_t* r, const std::uint8_t* begin, const std::uint8_t* end, const char* type) {
  security_headers(r);
  httpd_resp_set_hdr(r, "Cache-Control", "no-cache");
  httpd_resp_set_hdr(r, "Content-Encoding", "gzip");
  httpd_resp_set_type(r, type);
  return httpd_resp_send(r, reinterpret_cast<const char*>(begin), static_cast<ssize_t>(end - begin));
}

// ---- reading the request ------------------------------------------------------------------------------------------------------

std::string header(httpd_req_t* r, const char* name) {
  const std::size_t length = httpd_req_get_hdr_value_len(r, name);
  if (length == 0 || length > 1024) return "";
  std::string value(length + 1, '\0');
  if (httpd_req_get_hdr_value_str(r, name, value.data(), value.size()) != ESP_OK) return "";
  value.resize(std::strlen(value.c_str()));
  return value;
}

// The session token of a request: the cookie, or "Authorization: Bearer".
std::string token_of(httpd_req_t* r) {
  const std::string cookies = header(r, "Cookie");
  const std::string key = std::string(kCookie) + "=";
  std::size_t at = 0;
  while (at < cookies.size()) {
    while (at < cookies.size() && (cookies[at] == ' ' || cookies[at] == ';')) ++at;
    if (cookies.compare(at, key.size(), key) == 0) {
      const std::size_t start = at + key.size(), end = cookies.find(';', start);
      return cookies.substr(start, end == std::string::npos ? std::string::npos : end - start);
    }
    at = cookies.find(';', at);
    if (at == std::string::npos) break;
  }
  const std::string bearer = header(r, "Authorization");
  if (bearer.compare(0, 7, "Bearer ") == 0) return bearer.substr(7);
  return "";
}

std::uint32_t source_of(httpd_req_t* r) {
  sockaddr_in6 address{};
  socklen_t length = sizeof address;
  if (getpeername(httpd_req_to_sockfd(r), reinterpret_cast<sockaddr*>(&address), &length) != 0) return 0;
  if (address.sin6_family == AF_INET) return ntohl(reinterpret_cast<sockaddr_in*>(&address)->sin_addr.s_addr);
  // an IPv4 address that lwIP reports in its IPv6 form (::ffff:a.b.c.d)
  return ntohl(*reinterpret_cast<const std::uint32_t*>(&address.sin6_addr.s6_addr[12]));
}

bool read_body(httpd_req_t* r, std::string& body, std::size_t limit = kMaxBody) {
  body.clear();
  if (r->content_len > limit) return false;
  body.resize(r->content_len);
  std::size_t got = 0;
  while (got < r->content_len) {
    const int n = httpd_req_recv(r, body.data() + got, r->content_len - got);
    if (n == HTTPD_SOCK_ERR_TIMEOUT) continue;
    if (n <= 0) return false;
    got += static_cast<std::size_t>(n);
  }
  return true;
}

// The body as a JSON object, or an error already sent.
bool read_json(httpd_req_t* r, json::Value& document) {
  std::string body;
  if (!read_body(r, body)) { send_error(r, 413, "too_large"); return false; }
  if (!json::parse(body, document) || !document.is_object()) { send_error(r, 400, "not_json"); return false; }
  return true;
}

// ---- sessions -------------------------------------------------------------------------------------------------------------------

struct Who {
  bool ok = false;
  std::string user;
  auth::Role role = auth::Role::kViewer;
  std::string token;
};

Who authenticate(httpd_req_t* r) {
  Who who;
  who.token = token_of(r);
  if (who.token.empty()) return who;
  std::lock_guard<std::mutex> guard(g_lock);
  const auth::Session* session = g_sessions.touch(who.token, now_ms());
  if (session == nullptr) return who;
  who.ok = true;
  who.user = session->user;
  who.role = session->role;
  return who;
}

// Answers the request with an error and returns false unless the caller is signed in (and an administrator when `admin`).
bool require(httpd_req_t* r, Who& who, bool admin, bool writes) {
  if (store::users_empty()) { send_error(r, 403, "setup_required"); return false; }
  who = authenticate(r);
  if (!who.ok) { send_error(r, 401, "unauthorized"); return false; }
  // A request that changes anything must carry a header a web page from another site cannot add without permission.
  if (writes && header(r, "X-Requested-With") != "armor") { send_error(r, 403, "forbidden"); return false; }
  if (admin && who.role != auth::Role::kAdmin) { send_error(r, 403, "forbidden"); return false; }
  return true;
}

// httpd_resp_set_hdr keeps only a pointer to the header's text, not a copy of it, so that text must stay alive until the response is actually
// sent - which happens back in the caller, after this function has returned. The cookie is therefore written into a string the CALLER owns
// (cookie_out), not one of this function's own locals (one was tried first: the cookie came out as a few bytes of whatever used that stack
// slot next - the login looked to succeed but no browser ever kept a session, found for real on a radar node's own panel).
void start_session(httpd_req_t* r, const std::string& user, auth::Role role, std::string& cookie_out) {
  std::uint8_t bytes[24];
  random_bytes(bytes, sizeof bytes);
  const std::string token = auth::to_hex(bytes, sizeof bytes);
  {
    std::lock_guard<std::mutex> guard(g_lock);
    g_sessions.create(token, user, role, now_ms());
  }
  cookie_out = std::string(kCookie) + "=" + token + "; " + webpolicy::cookie_attributes(is_tls(r), 1800);
  httpd_resp_set_hdr(r, "Set-Cookie", cookie_out.c_str());
}

// ---- handlers -----------------------------------------------------------------------------------------------------------------------

esp_err_t get_session(httpd_req_t* r) {
  const Who who = authenticate(r);
  const config::Settings s = store::settings();
  const network::Status n = network::status();
  json::Writer w;
  w.begin_object().field("setup", store::users_empty()).field("authenticated", who.ok).field("user", who.ok ? who.user : "").field("role", who.ok ? auth::to_text(who.role) : "")
      .field("node_id", s.node_id).field("language", s.language).field("version", version_text()).field("setup_ssid", n.ap_setup ? n.ap_ssid : "").field("mac", n.mac)
      .field("board", board::kId).field("ethernet", board::kHasEthernet).end_object();
  return send_json(r, 200, w.str());
}

esp_err_t post_setup(httpd_req_t* r) {
  if (!store::users_empty()) return send_error(r, 403, "forbidden");
  json::Value in;
  if (!read_json(r, in)) return ESP_OK;
  const std::uint32_t source = source_of(r);
  {
    std::lock_guard<std::mutex> guard(g_lock);
    if (!g_throttle.allowed(source, now_ms())) return send_error(r, 429, "too_many_attempts");
  }
  const std::string code = in.string_or("code", "");
  if (!auth::same_text(code, store::setup_code())) {
    std::lock_guard<std::mutex> guard(g_lock);
    g_throttle.failure(source, now_ms());
    return send_error(r, 403, "wrong_code");
  }
  const std::string user = in.string_or("user", ""), password = in.string_or("password", "");
  config::Settings s = store::settings();
  bool changed = false;
  if (!board::kHasEthernet) {
    // A board with no cable needs a way in for after the setup: the Wi-Fi network it is to join, and its own network (named after the board and keyed with the
    // setup code the administrator has just used; it can be changed in the panel) so it can always be reached. Nothing is created if this is refused.
    const std::string wifi_ssid = in.string_or("wifi_ssid", ""), wifi_password = in.string_or("wifi_password", "");
    if (wifi_ssid.empty()) return send_error(r, 422, "wifi_required");
    s.uplink = config::Uplink::kWifi;
    s.sta.enabled = true; s.sta.ssid = wifi_ssid; s.sta.password = wifi_password;
    if (!s.ap.enabled) {
      s.ap.enabled = true;
      s.ap.ssid = "ARMOR-" + netplan::upper(store::mac_tail());
      s.ap.security = config::WifiSecurity::kWpa2;
      s.ap.password = code;
    }
    const config::Problems problems = config::validate(s);
    if (!problems.empty()) {
      json::Writer w;
      w.begin_object().field("error", "invalid").key("problems").raw(api::problems_json(problems)).end_object();
      return send_json(r, 422, w.str());
    }
    changed = true;
  }
  const auth::Result result = store::user_add(user, password, auth::Role::kAdmin);
  if (result == auth::Result::kInvalidName) return send_error(r, 422, "invalid_name");
  if (result == auth::Result::kWeakPassword) return send_error(r, 422, "weak_password");
  if (result != auth::Result::kOk) return send_error(r, 500, "storage");
  const std::string language = in.string_or("language", "");
  if (config::language_is_known(language) && language != s.language) { s.language = language; changed = true; }
  if (changed) store::save_settings(s);
  else if (!store::settings_are_stored()) store::save_settings(s);   // keep the first settings from now on
  {
    std::lock_guard<std::mutex> guard(g_lock);
    g_throttle.success(source);
  }
  ESP_LOGI(kTag, "setup complete: the administrator \"%s\" exists; the node restarts to close the setup network", user.c_str());
  std::string cookie;
  start_session(r, user, auth::Role::kAdmin, cookie);
  restart_after(4000);
  return send_ok(r, true);
}

esp_err_t post_login(httpd_req_t* r) {
  if (store::users_empty()) return send_error(r, 403, "setup_required");
  json::Value in;
  if (!read_json(r, in)) return ESP_OK;
  const std::uint32_t source = source_of(r);
  {
    std::lock_guard<std::mutex> guard(g_lock);
    if (!g_throttle.allowed(source, now_ms())) {
      json::Writer w;
      w.begin_object().field("error", "too_many_attempts").field("wait_s", static_cast<int>(g_throttle.wait_s(source, now_ms()))).end_object();
      return send_json(r, 429, w.str());
    }
  }
  const std::string user = in.string_or("user", ""), password = in.string_or("password", "");
  auth::Role role = auth::Role::kViewer;
  if (!store::user_verify(user, password, role)) {
    std::lock_guard<std::mutex> guard(g_lock);
    g_throttle.failure(source, now_ms());
    return send_error(r, 401, "wrong_credentials");
  }
  {
    std::lock_guard<std::mutex> guard(g_lock);
    g_throttle.success(source);
  }
  std::string cookie;
  start_session(r, user, role, cookie);
  return send_ok(r);
}

esp_err_t post_logout(httpd_req_t* r) {
  const std::string token = token_of(r);
  if (!token.empty()) {
    std::lock_guard<std::mutex> guard(g_lock);
    g_sessions.end(token);
  }
  const std::string expired = std::string(kCookie) + "=; " + webpolicy::cookie_attributes(is_tls(r), 0);
  httpd_resp_set_hdr(r, "Set-Cookie", expired.c_str());
  return send_ok(r);
}

esp_err_t get_config(httpd_req_t* r) {
  Who who;
  if (!require(r, who, false, false)) return ESP_OK;
  return send_json(r, 200, api::config_get_json());
}

esp_err_t put_config(httpd_req_t* r) {
  Who who;
  if (!require(r, who, true, true)) return ESP_OK;
  std::string body;
  if (!read_body(r, body)) return send_error(r, 413, "too_large");
  config::Problems problems;
  bool restart_required = false;
  switch (api::put_config(body, problems, restart_required)) {
    case api::PutResult::kSaved:
      ESP_LOGI(kTag, "settings saved by \"%s\"", who.user.c_str());
      return send_ok(r, restart_required);
    case api::PutResult::kInvalid: {
      json::Writer w;
      w.begin_object().field("error", "invalid").key("problems").raw(api::problems_json(problems)).end_object();
      return send_json(r, 422, w.str());
    }
    case api::PutResult::kStorage: break;
  }
  return send_error(r, 500, "storage");
}

esp_err_t get_wifi_scan(httpd_req_t* r) {
  Who who;
  if (!require(r, who, true, false)) return ESP_OK;
  std::string data, error;
  if (!api::wifi_scan_json(data, error)) return send_error(r, 503, error.empty() ? "scan_failed" : error.c_str());
  return send_json(r, 200, data);
}

esp_err_t get_status(httpd_req_t* r) {
  Who who;
  if (!require(r, who, false, false)) return ESP_OK;
  return send_json(r, 200, status_json());
}

esp_err_t get_pins(httpd_req_t* r) {
  Who who;
  if (!require(r, who, false, false)) return ESP_OK;
  const config::Settings s = store::settings();
  json::Writer w;
  w.begin_object().key("catalog").begin_array();
  for (int gpio = board::kFirstGpio; gpio <= board::kLastGpio; ++gpio) {
    const board::PinInfo info = board::pin_info(gpio);
    if (info.reason == board::Reserved::kNoSuchPin) continue;
    const char* use = info.use == board::PinUse::kFree ? "free" : info.use == board::PinUse::kCaution ? "caution" : info.use == board::PinUse::kSdCard ? "sd" : "reserved";
    const char* reason = info.reason == board::Reserved::kFlash ? "flash" : info.reason == board::Reserved::kPsram ? "psram" : info.reason == board::Reserved::kUsb ? "usb"
                       : info.reason == board::Reserved::kEthernet ? "ethernet" : info.reason == board::Reserved::kCamera ? "camera" : "";
    w.begin_object().field("gpio", gpio).field("use", use).field("reason", reason).field("on_header", info.on_header).field("adc1", info.adc1).end_object();
  }
  w.end_array().key("live").begin_array();
  for (const pins::Live& live : pins::snapshot(s.node_id)) {
    w.begin_object().field("name", live.name).field("gpio", live.gpio).field("mode", live.mode).field("report", live.report).field("on", live.on).field("percent", live.percent)
        .key("value").number(live.value, 3).field("has_value", live.has_value).field("fell_back", live.fell_back).field("topic_state", live.topic_state).field("topic_set", live.topic_set).end_object();
  }
  w.end_array().end_object();
  return send_json(r, 200, w.str());
}

esp_err_t post_pin_command(httpd_req_t* r) {
  Who who;
  if (!require(r, who, true, true)) return ESP_OK;
  json::Value in;
  if (!read_json(r, in)) return ESP_OK;
  const pins::Outcome outcome = pins::command(in.string_or("name", ""), in.string_or("command", ""));
  if (outcome == pins::Outcome::kUnknownPin) return send_error(r, 404, "unknown_pin");
  if (outcome == pins::Outcome::kRefused) return send_error(r, 422, "invalid_command");
  return send_ok(r);
}

esp_err_t get_radars(httpd_req_t* r) {
  Who who;
  if (!require(r, who, false, false)) return ESP_OK;
  return send_json(r, 200, api::radars_json());
}

bool read_zones(const json::Value& in, ld2450cmd::ZoneFilter& filter) {
  const json::Value* zones = in.get("zones");
  if (zones == nullptr || !zones->is_object()) return false;
  const long long type = zones->integer_or("type", -1, 0, 2);
  if (type < 0) return false;
  filter.type = static_cast<std::uint16_t>(type);
  const json::Value* list = zones->get("list");
  if (list == nullptr || !list->is_array() || list->items.size() > 3) return false;
  for (std::size_t i = 0; i < list->items.size(); ++i) {
    const json::Value& item = list->items[i];
    const long long x1 = item.integer_or("x1", 100000, -10000, 10000), y1 = item.integer_or("y1", 100000, -10000, 10000), x2 = item.integer_or("x2", 100000, -10000, 10000), y2 = item.integer_or("y2", 100000, -10000, 10000);
    if (x1 == 100000 || y1 == 100000 || x2 == 100000 || y2 == 100000) return false;
    filter.zones[i] = {static_cast<std::int16_t>(x1), static_cast<std::int16_t>(y1), static_cast<std::int16_t>(x2), static_cast<std::int16_t>(y2)};
  }
  return true;
}

esp_err_t post_radar_command(httpd_req_t* r) {
  Who who;
  if (!require(r, who, true, true)) return ESP_OK;
  json::Value in;
  if (!read_json(r, in)) return ESP_OK;
  const long long number = in.integer_or("radar", 0, 1, 3);
  if (number < 1) return send_error(r, 422, "invalid_radar");
  const std::string name = in.string_or("op", "");
  radar::Op op;
  ld2450cmd::ZoneFilter zones;
  bool has_zones = false;
  if (name == "read_info") op = radar::Op::kReadInfo;
  else if (name == "single") op = radar::Op::kSingleTarget;
  else if (name == "multi") op = radar::Op::kMultiTarget;
  else if (name == "restart") op = radar::Op::kRestart;
  else if (name == "factory") op = radar::Op::kFactoryReset;
  else if (name == "bluetooth") op = radar::Op::kBluetooth;
  else if (name == "set_zones") { op = radar::Op::kSetZones; has_zones = read_zones(in, zones); if (!has_zones) return send_error(r, 422, "invalid_zones"); }
  else return send_error(r, 422, "invalid_op");
  ESP_LOGI(kTag, "radar %lld: \"%s\" by \"%s\"", number, name.c_str(), who.user.c_str());
  const radar::CommandResult result = radar::run(static_cast<std::size_t>(number - 1), op, in.bool_or("flag", false), has_zones ? &zones : nullptr);
  json::Writer w;
  w.begin_object().field("ok", result.ok).field("error", result.error).field("firmware", result.firmware).field("tracking_mode", result.tracking_mode).field("last_answer", result.last_answer_hex);
  if (result.has_zones) {
    w.key("zones").begin_object().field("type", static_cast<int>(result.zones.type)).key("list").begin_array();
    for (const ld2450cmd::Zone& zone : result.zones.zones) w.begin_object().field("x1", static_cast<int>(zone.x1)).field("y1", static_cast<int>(zone.y1)).field("x2", static_cast<int>(zone.x2)).field("y2", static_cast<int>(zone.y2)).end_object();
    w.end_array().end_object();
  }
  w.end_object();
  return send_json(r, 200, w.str());
}

const char* result_code(auth::Result result) {
  switch (result) {
    case auth::Result::kOk: return "ok";
    case auth::Result::kInvalidName: return "invalid_name";
    case auth::Result::kWeakPassword: return "weak_password";
    case auth::Result::kExists: return "exists";
    case auth::Result::kNotFound: return "not_found";
    case auth::Result::kTooMany: return "too_many_users";
    case auth::Result::kLastAdmin: return "last_admin";
    case auth::Result::kWrongPassword: return "wrong_password";
  }
  return "error";
}

esp_err_t answer_user_result(httpd_req_t* r, auth::Result result) {
  if (result == auth::Result::kOk) return send_ok(r);
  return send_error(r, result == auth::Result::kNotFound ? 404 : (result == auth::Result::kExists || result == auth::Result::kLastAdmin) ? 409 : 422, result_code(result));
}

esp_err_t get_users(httpd_req_t* r) {
  Who who;
  if (!require(r, who, true, false)) return ESP_OK;
  return send_json(r, 200, store::users_json());
}

esp_err_t post_user(httpd_req_t* r) {
  Who who;
  if (!require(r, who, true, true)) return ESP_OK;
  json::Value in;
  if (!read_json(r, in)) return ESP_OK;
  const std::string role = in.string_or("role", "viewer");
  if (role != "admin" && role != "viewer") return send_error(r, 422, "invalid_role");
  return answer_user_result(r, store::user_add(in.string_or("name", ""), in.string_or("password", ""), role == "admin" ? auth::Role::kAdmin : auth::Role::kViewer));
}

esp_err_t put_user(httpd_req_t* r, const std::string& name) {
  Who who;
  if (!require(r, who, true, true)) return ESP_OK;
  json::Value in;
  if (!read_json(r, in)) return ESP_OK;
  const std::string role = in.string_or("role", ""), password = in.string_or("password", "");
  if (!role.empty()) {
    if (role != "admin" && role != "viewer") return send_error(r, 422, "invalid_role");
    const auth::Result result = store::user_set_role(name, role == "admin" ? auth::Role::kAdmin : auth::Role::kViewer);
    if (result != auth::Result::kOk) return answer_user_result(r, result);
  }
  if (!password.empty()) {
    const auth::Result result = store::user_set_password(name, password);
    if (result != auth::Result::kOk) return answer_user_result(r, result);
  }
  std::lock_guard<std::mutex> guard(g_lock);
  g_sessions.end_user(name);   // a changed password or role ends that user's sessions
  return send_ok(r);
}

esp_err_t delete_user(httpd_req_t* r, const std::string& name) {
  Who who;
  if (!require(r, who, true, true)) return ESP_OK;
  const auth::Result result = store::user_remove(name);
  if (result == auth::Result::kOk) { std::lock_guard<std::mutex> guard(g_lock); g_sessions.end_user(name); }
  return answer_user_result(r, result);
}

// Anyone signed in may change their own password, with the current one.
esp_err_t put_account(httpd_req_t* r) {
  Who who;
  if (!require(r, who, false, true)) return ESP_OK;
  json::Value in;
  if (!read_json(r, in)) return ESP_OK;
  auth::Role role;
  if (!store::user_verify(who.user, in.string_or("current", ""), role)) return send_error(r, 403, "wrong_password");
  const auth::Result result = store::user_set_password(who.user, in.string_or("password", ""));
  if (result != auth::Result::kOk) return answer_user_result(r, result);
  std::lock_guard<std::mutex> guard(g_lock);
  g_sessions.end_user(who.user);
  return send_ok(r);
}

esp_err_t post_reboot(httpd_req_t* r) {
  Who who;
  if (!require(r, who, true, true)) return ESP_OK;
  ESP_LOGI(kTag, "restart requested by \"%s\"", who.user.c_str());
  restart_after(1500);
  return send_ok(r);
}

esp_err_t post_factory_reset(httpd_req_t* r) {
  Who who;
  if (!require(r, who, true, true)) return ESP_OK;
  json::Value in;
  if (!read_json(r, in)) return ESP_OK;
  if (in.string_or("confirm", "") != "RESET") return send_error(r, 422, "confirm_required");
  ESP_LOGW(kTag, "factory reset requested by \"%s\"", who.user.c_str());
  if (!store::factory_reset()) return send_error(r, 500, "storage");
  restart_after(1500);
  return send_ok(r, true);
}

esp_err_t get_log(httpd_req_t* r) {
  Who who;
  if (!require(r, who, false, false)) return ESP_OK;
  std::uint32_t from = 0;
  char query[48];
  if (httpd_req_get_url_query_str(r, query, sizeof query) == ESP_OK) {
    char value[16];
    if (httpd_query_key_value(query, "from", value, sizeof value) == ESP_OK) from = static_cast<std::uint32_t>(std::strtoul(value, nullptr, 10));
  }
  std::uint32_t next = 0;
  const std::string text = logbuf::read_from(from, next);
  json::Writer w;
  w.begin_object().field("next", static_cast<long long>(next)).field("text", text).end_object();
  return send_json(r, 200, w.str());
}

// The firmware update: the raw image as the body. The image is checked by the OTA machinery (its own hash) and by its project name
// before the boot partition changes; the new firmware must then come up and mark itself valid, or the boot loader goes back.
esp_err_t post_ota(httpd_req_t* r) {
  Who who;
  if (!require(r, who, true, true)) return ESP_OK;
  const std::size_t total = r->content_len;
  if (total < 100 * 1024 || total > kMaxFirmware) return send_error(r, 413, "bad_size");
  const esp_partition_t* target = esp_ota_get_next_update_partition(nullptr);
  if (target == nullptr) return send_error(r, 500, "no_partition");
  esp_ota_handle_t handle = 0;
  if (esp_ota_begin(target, total, &handle) != ESP_OK) return send_error(r, 500, "ota_begin");
  mbedtls_sha256_context sha;
  mbedtls_sha256_init(&sha);
  mbedtls_sha256_starts(&sha, 0);
  static std::uint8_t chunk[2048];
  std::size_t got = 0;
  bool first = true;
  while (got < total) {
    const int n = httpd_req_recv(r, reinterpret_cast<char*>(chunk), sizeof chunk < total - got ? sizeof chunk : total - got);
    if (n == HTTPD_SOCK_ERR_TIMEOUT) continue;
    if (n <= 0) { esp_ota_abort(handle); mbedtls_sha256_free(&sha); return send_error(r, 400, "upload_interrupted"); }
    if (first) {
      first = false;
      if (chunk[0] != 0xE9) { esp_ota_abort(handle); mbedtls_sha256_free(&sha); return send_error(r, 422, "not_firmware"); }   // the magic byte of an ESP32 image
    }
    if (esp_ota_write(handle, chunk, static_cast<std::size_t>(n)) != ESP_OK) { esp_ota_abort(handle); mbedtls_sha256_free(&sha); return send_error(r, 500, "ota_write"); }
    mbedtls_sha256_update(&sha, chunk, static_cast<std::size_t>(n));
    got += static_cast<std::size_t>(n);
  }
  std::uint8_t digest[32];
  mbedtls_sha256_finish(&sha, digest);
  mbedtls_sha256_free(&sha);
  if (esp_ota_end(handle) != ESP_OK) return send_error(r, 422, "image_invalid");
  esp_app_desc_t description{};
  if (esp_ota_get_partition_description(target, &description) != ESP_OK || std::strcmp(description.project_name, esp_app_get_description()->project_name) != 0) return send_error(r, 422, "wrong_firmware");
  if (esp_ota_set_boot_partition(target) != ESP_OK) return send_error(r, 500, "ota_boot");
  ESP_LOGW(kTag, "firmware %s (%u bytes) installed in %s by \"%s\"; restarting", description.version, static_cast<unsigned>(total), target->label, who.user.c_str());
  json::Writer w;
  w.begin_object().field("ok", true).field("restart_required", true).field("version", description.version).field("bytes", static_cast<long long>(total)).field("sha256", auth::to_hex(digest, sizeof digest)).end_object();
  restart_after(2000);
  return send_json(r, 200, w.str());
}

// ---- dispatch -----------------------------------------------------------------------------------------------------------------------

esp_err_t api_handler(httpd_req_t* r) {
  const std::string uri = r->uri;
  const std::string path = uri.substr(0, uri.find('?'));
  const std::string prefix = "/api/v1/";
  if (path.compare(0, prefix.size(), prefix) != 0) return send_error(r, 404, "not_found");
  const std::string route = path.substr(prefix.size());
  const int method = r->method;
  if (method == HTTP_GET) {
    if (route == "session") return get_session(r);
    if (route == "status") return get_status(r);
    if (route == "wifi/scan") return get_wifi_scan(r);
    if (route == "config") return get_config(r);
    if (route == "pins") return get_pins(r);
    if (route == "radars") return get_radars(r);
    if (route == "users") return get_users(r);
    if (route == "log") return get_log(r);
  } else if (method == HTTP_POST) {
    if (route == "setup") return post_setup(r);
    if (route == "login") return post_login(r);
    if (route == "logout") return post_logout(r);
    if (route == "pins/command") return post_pin_command(r);
    if (route == "radars/command") return post_radar_command(r);
    if (route == "users") return post_user(r);
    if (route == "reboot") return post_reboot(r);
    if (route == "factory-reset") return post_factory_reset(r);
    if (route == "ota") return post_ota(r);
  } else if (method == HTTP_PUT) {
    if (route == "config") return put_config(r);
    if (route == "account") return put_account(r);
    if (route.compare(0, 6, "users/") == 0) return put_user(r, route.substr(6));
  } else if (method == HTTP_DELETE) {
    if (route.compare(0, 6, "users/") == 0) return delete_user(r, route.substr(6));
  }
  return send_error(r, 404, "not_found");
}

esp_err_t page_handler(httpd_req_t* r) {
  const std::string uri = r->uri;
  if (uri == "/app.js") return send_asset(r, app_js_gz_start, app_js_gz_end, "text/javascript");
  if (uri == "/style.css") return send_asset(r, style_css_gz_start, style_css_gz_end, "text/css");
  return send_asset(r, index_html_gz_start, index_html_gz_end, "text/html");
}

// In https mode the plain-HTTP port only sends the browser to the same address over HTTPS.
esp_err_t redirect_handler(httpd_req_t* r) {
  const std::string location = webpolicy::redirect_location(header(r, "Host"), r->uri);
  if (location.empty()) return send_error(r, 400, "bad_host");
  httpd_resp_set_status(r, "308 Permanent Redirect");
  httpd_resp_set_hdr(r, "Location", location.c_str());
  httpd_resp_set_hdr(r, "Cache-Control", "no-store");
  return httpd_resp_send(r, nullptr, 0);
}

void register_handlers(httpd_handle_t server, bool redirect_only) {
  const httpd_method_t methods[] = {HTTP_GET, HTTP_POST, HTTP_PUT, HTTP_DELETE};
  if (redirect_only) {
    for (httpd_method_t method : methods) {
      httpd_uri_t any{};
      any.uri = "/*";
      any.method = method;
      any.handler = redirect_handler;
      httpd_register_uri_handler(server, &any);
    }
    return;
  }
  for (httpd_method_t method : methods) {
    httpd_uri_t api{};
    api.uri = "/api/*";
    api.method = method;
    api.handler = api_handler;
    httpd_register_uri_handler(server, &api);
  }
  for (const char* path : {"/", "/app.js", "/style.css"}) {
    httpd_uri_t page{};
    page.uri = path;
    page.method = HTTP_GET;
    page.handler = page_handler;
    httpd_register_uri_handler(server, &page);
  }
}

void restart_now(void*) { network::disconnect_before_restart(); esp_restart(); }
}  // namespace

void restart_after(unsigned delay_ms) {
  static esp_timer_handle_t timer = nullptr;
  if (timer == nullptr) {
    esp_timer_create_args_t args{};
    args.callback = &restart_now;
    args.name = "restart";
    if (esp_timer_create(&args, &timer) != ESP_OK) { esp_restart(); return; }
  }
  esp_timer_stop(timer);
  esp_timer_start_once(timer, static_cast<std::uint64_t>(delay_ms) * 1000ULL);
}

TlsStatus tls_status() {
  TlsStatus s;
  s.mode = to_text(g_started_with.web);
  s.running = g_tls_server != nullptr;
  s.fingerprint = g_material.sha256;
  return s;
}

bool start(const config::Settings& settings) {
  g_started_with = settings;
  const bool tls_wanted = webpolicy::serves_https(settings.web);
  const bool redirect = webpolicy::http_redirects(settings.web);
  bool tls_ok = false;
  if (tls_wanted) {
    // The certificate is made the first time (a second or two) and kept; without one the node falls back to plain HTTP rather than lose its panel.
    if (tlscert::load_or_create(settings.node_id, g_material)) {
      httpd_ssl_config_t ssl = HTTPD_SSL_CONFIG_DEFAULT();
      ssl.httpd.stack_size = 10240;
      ssl.httpd.max_uri_handlers = 12;
      ssl.httpd.max_open_sockets = 4;
      ssl.httpd.lru_purge_enable = true;
      ssl.httpd.recv_wait_timeout = 10;
      ssl.httpd.send_wait_timeout = 10;
      ssl.httpd.ctrl_port = 32769;   // the plain server keeps the default control port
      ssl.httpd.uri_match_fn = httpd_uri_match_wildcard;
      ssl.servercert = reinterpret_cast<const uint8_t*>(g_material.certificate_pem.data());
      ssl.servercert_len = g_material.certificate_pem.size();
      ssl.prvtkey_pem = reinterpret_cast<const uint8_t*>(g_material.key_pem.data());
      ssl.prvtkey_len = g_material.key_pem.size();
      if (httpd_ssl_start(&g_tls_server, &ssl) == ESP_OK) {
        register_handlers(g_tls_server, false);
        tls_ok = true;
        ESP_LOGI(kTag, "the panel is also on port 443 (HTTPS), certificate SHA-256 %s", g_material.sha256.c_str());
      } else {
        g_tls_server = nullptr;
        ESP_LOGE(kTag, "the HTTPS server could not be started: the panel stays on plain HTTP");
      }
    }
  }
  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.stack_size = 10240;
  config.max_uri_handlers = 12;
  config.max_open_sockets = 4;
  config.lru_purge_enable = true;
  config.recv_wait_timeout = 10;
  config.send_wait_timeout = 10;
  config.uri_match_fn = httpd_uri_match_wildcard;
  if (httpd_start(&g_server, &config) != ESP_OK) { ESP_LOGE(kTag, "the web server could not be started"); return tls_ok; }
  register_handlers(g_server, redirect && tls_ok);   // never a redirect to a server that is not there
  ESP_LOGI(kTag, "the panel is on port 80%s", redirect && tls_ok ? " (it sends the browser to HTTPS)" : "");
  return true;
}

}  // namespace armor::web
