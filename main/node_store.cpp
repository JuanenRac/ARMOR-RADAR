// ARMOR-RADAR - what the node keeps in flash: its settings and its panel users.
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D). GPL-3.0-or-later.
#include "node_store.hpp"

#include <cstring>
#include <mutex>
extern "C" {
#include "esp_log.h"
#include "esp_mac.h"
#include "mbedtls/md.h"
#include "mbedtls/pkcs5.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "sdkconfig.h"
}
#include "entropy.hpp"

namespace armor::store {
namespace {
constexpr char kTag[] = "armor-store";
constexpr char kNamespace[] = "armor";
constexpr char kSettingsKey[] = "config";
constexpr char kUsersKey[] = "users";
constexpr unsigned kHashIterations = 10000;

std::mutex g_lock;
config::Settings g_settings;
bool g_stored = false;
auth::UserStore g_users;
std::string g_setup_code;
std::uint8_t g_mac[6]{};

// PBKDF2-HMAC-SHA256, 10 000 rounds, 32 bytes, written as hexadecimal. The salt (text) is used as it is.
std::string hash_password(std::string_view password, std::string_view salt) {
  std::uint8_t out[32];
  const int rc = mbedtls_pkcs5_pbkdf2_hmac_ext(MBEDTLS_MD_SHA256, reinterpret_cast<const unsigned char*>(password.data()), password.size(),
                                              reinterpret_cast<const unsigned char*>(salt.data()), salt.size(), kHashIterations, sizeof out, out);
  if (rc != 0) return "";
  return auth::to_hex(out, sizeof out);
}

std::string new_salt() {
  std::uint8_t bytes[16];
  random_bytes(bytes, sizeof bytes);
  return auth::to_hex(bytes, sizeof bytes);
}

bool read_blob(const char* key, std::string& out) {
  nvs_handle_t handle;
  if (nvs_open(kNamespace, NVS_READONLY, &handle) != ESP_OK) return false;
  std::size_t size = 0;
  bool ok = false;
  if (nvs_get_blob(handle, key, nullptr, &size) == ESP_OK && size > 0 && size <= 32 * 1024) {
    out.resize(size);
    ok = nvs_get_blob(handle, key, out.data(), &size) == ESP_OK;
  }
  nvs_close(handle);
  return ok;
}

bool write_blob(const char* key, const std::string& value) {
  nvs_handle_t handle;
  if (nvs_open(kNamespace, NVS_READWRITE, &handle) != ESP_OK) return false;
  bool ok = nvs_set_blob(handle, key, value.data(), value.size()) == ESP_OK && nvs_commit(handle) == ESP_OK;
  nvs_close(handle);
  return ok;
}

// The build's values as the first settings.
config::Settings first_settings() {
  config::Settings s = config::default_settings(mac_tail());
  if (std::strlen(CONFIG_ARMOR_NODE_ID) > 0 && node_id_is_valid(CONFIG_ARMOR_NODE_ID)) { s.node_id = CONFIG_ARMOR_NODE_ID; s.node_name = CONFIG_ARMOR_NODE_ID; }
  if (std::strlen(CONFIG_ARMOR_MQTT_URI) > 0) {
    s.mqtt.enabled = true;
    s.mqtt.uri = CONFIG_ARMOR_MQTT_URI;
    s.mqtt.username = CONFIG_ARMOR_MQTT_USERNAME;
    s.mqtt.password = CONFIG_ARMOR_MQTT_PASSWORD;
  }
  s.mqtt.ntp = CONFIG_ARMOR_NTP_SERVER;
  s.mqtt.heartbeat_s = CONFIG_ARMOR_HEARTBEAT_S;
  s.mqtt.telemetry_ms = CONFIG_ARMOR_TELEMETRY_PERIOD_MS;
  for (std::size_t i = 0; i < config::kRadarCount; ++i) s.radars[i].enabled = i < static_cast<std::size_t>(CONFIG_ARMOR_RADAR_COUNT);
  s.sensors.veml7700 = CONFIG_ARMOR_LIGHT_VEML7700;
  s.sensors.lux_fallback = CONFIG_ARMOR_LUX_FALLBACK;
  return s;
}
}  // namespace

void init() {
  esp_err_t err = nvs_flash_init();
  if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_LOGW(kTag, "the NVS partition is unusable (%s): erasing it", esp_err_to_name(err));
    ESP_ERROR_CHECK(nvs_flash_erase());
    err = nvs_flash_init();
  }
  ESP_ERROR_CHECK(err);
  esp_read_mac(g_mac, ESP_MAC_ETH);

  std::lock_guard<std::mutex> guard(g_lock);
  std::string text;
  g_settings = first_settings();
  if (read_blob(kSettingsKey, text)) {
    config::Settings loaded;
    config::Problems problems;
    if (config::load(text, first_settings(), loaded, problems)) { g_settings = loaded; g_stored = true; }
    else ESP_LOGE(kTag, "the stored settings are not valid (%s at %s): using the first settings", problems.empty() ? "?" : problems[0].code.c_str(), problems.empty() ? "" : problems[0].path.c_str());
  }
  if (read_blob(kUsersKey, text) && !g_users.from_json(text)) ESP_LOGE(kTag, "the stored users could not be read: the node is back in setup");
  if (g_users.empty()) {
    const std::size_t secret_length = std::strlen(CONFIG_ARMOR_FLEET_SECRET);
    if (secret_length >= 16) {
      // The code of this board: HMAC-SHA256 of its MAC (twelve lowercase hexadecimal digits) with the fleet secret, ten symbols of it.
      const std::string mac_text = auth::to_hex(g_mac, sizeof g_mac);
      std::uint8_t digest[32];
      if (mbedtls_md_hmac(mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), reinterpret_cast<const unsigned char*>(CONFIG_ARMOR_FLEET_SECRET), secret_length,
                          reinterpret_cast<const unsigned char*>(mac_text.data()), mac_text.size(), digest) == 0) g_setup_code = auth::setup_code_from(digest, 10);
    }
    if (!g_setup_code.empty()) { /* derived from the fleet secret */ }
    else if (std::strlen(CONFIG_ARMOR_SETUP_CODE) >= 8) g_setup_code = CONFIG_ARMOR_SETUP_CODE;
    else { std::uint8_t bytes[8]; random_bytes(bytes, sizeof bytes); g_setup_code = auth::setup_code_from(bytes, sizeof bytes); }
  }
}

std::string mac_tail() { return auth::to_hex(g_mac + 3, 3); }
unsigned mac_sum() { unsigned sum = 0; for (std::uint8_t b : g_mac) sum += b; return sum; }

config::Settings settings() {
  std::lock_guard<std::mutex> guard(g_lock);
  return g_settings;
}
bool settings_are_stored() {
  std::lock_guard<std::mutex> guard(g_lock);
  return g_stored;
}

bool save_settings(const config::Settings& s) {
  std::lock_guard<std::mutex> guard(g_lock);
  if (!write_blob(kSettingsKey, config::to_json(s, true))) return false;
  g_settings = s;
  g_stored = true;
  return true;
}

bool blob_read(const char* key, std::string& out) { return read_blob(key, out); }
bool blob_write(const char* key, const std::string& value) { return write_blob(key, value); }

bool users_empty() {
  std::lock_guard<std::mutex> guard(g_lock);
  return g_users.empty();
}

std::string users_json() {
  std::lock_guard<std::mutex> guard(g_lock);
  json::Writer w;
  w.begin_array();
  for (const auth::User& user : g_users.users()) w.begin_object().field("name", user.name).field("role", auth::to_text(user.role)).end_object();
  w.end_array();
  return w.str();
}

namespace {
bool persist_users() { return write_blob(kUsersKey, g_users.to_json()); }
}  // namespace

auth::Result user_add(std::string_view name, std::string_view password, auth::Role role) {
  std::lock_guard<std::mutex> guard(g_lock);
  const auth::Result result = g_users.add(name, password, role, new_salt(), hash_password);
  if (result == auth::Result::kOk && !persist_users()) { g_users.remove(name); return auth::Result::kTooMany; }
  if (result == auth::Result::kOk) g_setup_code.clear();
  return result;
}

bool user_verify(std::string_view name, std::string_view password, auth::Role& role) {
  std::lock_guard<std::mutex> guard(g_lock);
  return g_users.verify(name, password, hash_password, role);
}

auth::Result user_set_password(std::string_view name, std::string_view password) {
  std::lock_guard<std::mutex> guard(g_lock);
  const auth::Result result = g_users.set_password(name, password, new_salt(), hash_password);
  if (result == auth::Result::kOk) persist_users();
  return result;
}

auth::Result user_set_role(std::string_view name, auth::Role role) {
  std::lock_guard<std::mutex> guard(g_lock);
  const auth::Result result = g_users.set_role(name, role);
  if (result == auth::Result::kOk) persist_users();
  return result;
}

auth::Result user_remove(std::string_view name) {
  std::lock_guard<std::mutex> guard(g_lock);
  const auth::Result result = g_users.remove(name);
  if (result == auth::Result::kOk) persist_users();
  return result;
}

bool factory_reset() {
  std::lock_guard<std::mutex> guard(g_lock);
  nvs_handle_t handle;
  if (nvs_open(kNamespace, NVS_READWRITE, &handle) != ESP_OK) return false;
  const bool ok = nvs_erase_all(handle) == ESP_OK && nvs_commit(handle) == ESP_OK;
  nvs_close(handle);
  return ok;
}

const std::string& setup_code() { return g_setup_code; }

}  // namespace armor::store
