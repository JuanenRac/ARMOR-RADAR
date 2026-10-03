// ARMOR-RADAR - see github_update.hpp.
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D). GPL-3.0-or-later.
#include "github_update.hpp"

#include <cstring>
extern "C" {
#include "esp_app_desc.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "mbedtls/sha256.h"
}
#include "core/json.hpp"
#include "core/semver.hpp"

namespace armor::github_update {
namespace {
constexpr const char* kTag = "github_update";
constexpr std::size_t kMaxApiResponse = 24 * 1024;   // the release's own JSON, a changelog included; anything bigger is not trusted
constexpr std::size_t kMinFirmware = 100 * 1024;
constexpr std::size_t kMaxFirmware = 4 * 1024 * 1024;

// A GET with no body, read fully into a bounded buffer. False on any network, HTTP or size problem.
bool get_bounded(const std::string& url, const char* accept, std::string& out, std::string& error) {
  esp_http_client_config_t config{};
  config.url = url.c_str();
  config.crt_bundle_attach = esp_crt_bundle_attach;
  config.timeout_ms = 15000;
  esp_http_client_handle_t client = esp_http_client_init(&config);
  if (client == nullptr) { error = "client_init"; return false; }
  esp_http_client_set_header(client, "User-Agent", "ARMOR-RADAR");
  if (accept != nullptr) esp_http_client_set_header(client, "Accept", accept);
  bool ok = false;
  if (esp_http_client_open(client, 0) == ESP_OK) {
    const int length = esp_http_client_fetch_headers(client);
    const int status = esp_http_client_get_status_code(client);
    if (status != 200) { error = "http_" + std::to_string(status); }
    else if (length > 0 && static_cast<std::size_t>(length) > kMaxApiResponse) { error = "too_large"; }
    else {
      char chunk[512];
      ok = true;
      for (;;) {
        const int n = esp_http_client_read(client, chunk, sizeof chunk);
        if (n < 0) { ok = false; error = "read_error"; break; }
        if (n == 0) break;
        if (out.size() + static_cast<std::size_t>(n) > kMaxApiResponse) { ok = false; error = "too_large"; break; }
        out.append(chunk, static_cast<std::size_t>(n));
      }
    }
  } else {
    error = "connect_failed";
  }
  esp_http_client_close(client);
  esp_http_client_cleanup(client);
  return ok;
}
}  // namespace

CheckResult check() {
  CheckResult result;
  std::string body;
  const std::string url = std::string("https://api.github.com/repos/") + kRepo + "/releases/latest";
  if (!get_bounded(url, "application/vnd.github+json", body, result.error)) return result;
  json::Value document;
  if (!json::parse(body, document) || !document.is_object()) { result.error = "bad_json"; return result; }
  const json::Value* tag = document.get("tag_name");
  if (tag == nullptr || !tag->is_string() || tag->text.empty()) { result.error = "no_release"; return result; }
  std::string_view tag_text = tag->text;
  if (!tag_text.empty() && tag_text[0] == 'v') tag_text.remove_prefix(1);
  result.latest_version = std::string(tag_text);
  const json::Value* assets = document.get("assets");
  if (assets != nullptr && assets->is_array()) {
    for (const json::Value& asset : assets->items) {
      if (asset.string_or("name", "") == kAssetName) { result.asset_url = asset.string_or("browser_download_url", ""); break; }
    }
  }
  result.ok = true;
  result.update_available = !result.asset_url.empty() && semver::is_newer(result.latest_version, esp_app_get_description()->version);
  return result;
}

InstallResult install(const std::string& asset_url) {
  InstallResult result;
  if (asset_url.empty()) { result.error = "no_asset"; return result; }
  esp_http_client_config_t config{};
  config.url = asset_url.c_str();
  config.crt_bundle_attach = esp_crt_bundle_attach;
  config.timeout_ms = 20000;
  esp_http_client_handle_t client = esp_http_client_init(&config);
  if (client == nullptr) { result.error = "client_init"; return result; }
  esp_http_client_set_header(client, "User-Agent", "ARMOR-RADAR");
  if (esp_http_client_open(client, 0) != ESP_OK) { result.error = "connect_failed"; esp_http_client_cleanup(client); return result; }
  const int length = esp_http_client_fetch_headers(client);
  const int status = esp_http_client_get_status_code(client);
  if (status != 200 || length <= 0 || static_cast<std::size_t>(length) < kMinFirmware || static_cast<std::size_t>(length) > kMaxFirmware) {
    result.error = status != 200 ? "http_" + std::to_string(status) : "bad_size";
    esp_http_client_close(client); esp_http_client_cleanup(client);
    return result;
  }
  const std::size_t total = static_cast<std::size_t>(length);
  const esp_partition_t* target = esp_ota_get_next_update_partition(nullptr);
  if (target == nullptr) { result.error = "no_partition"; esp_http_client_close(client); esp_http_client_cleanup(client); return result; }
  esp_ota_handle_t handle = 0;
  if (esp_ota_begin(target, total, &handle) != ESP_OK) { result.error = "ota_begin"; esp_http_client_close(client); esp_http_client_cleanup(client); return result; }
  mbedtls_sha256_context sha;
  mbedtls_sha256_init(&sha);
  mbedtls_sha256_starts(&sha, 0);
  static std::uint8_t chunk[2048];
  std::size_t got = 0;
  bool first = true, failed = false;
  while (got < total) {
    const int n = esp_http_client_read(client, reinterpret_cast<char*>(chunk), sizeof chunk);
    if (n < 0) { result.error = "read_error"; failed = true; break; }
    if (n == 0) { result.error = "short_read"; failed = true; break; }
    if (first) {
      first = false;
      if (chunk[0] != 0xE9) { result.error = "not_firmware"; failed = true; break; }   // the magic byte of an ESP32 image
    }
    if (esp_ota_write(handle, chunk, static_cast<std::size_t>(n)) != ESP_OK) { result.error = "ota_write"; failed = true; break; }
    mbedtls_sha256_update(&sha, chunk, static_cast<std::size_t>(n));
    got += static_cast<std::size_t>(n);
  }
  esp_http_client_close(client);
  esp_http_client_cleanup(client);
  if (failed) { esp_ota_abort(handle); mbedtls_sha256_free(&sha); return result; }
  std::uint8_t digest[32];
  mbedtls_sha256_finish(&sha, digest);
  mbedtls_sha256_free(&sha);
  if (esp_ota_end(handle) != ESP_OK) { result.error = "image_invalid"; return result; }
  esp_app_desc_t description{};
  if (esp_ota_get_partition_description(target, &description) != ESP_OK || std::strcmp(description.project_name, esp_app_get_description()->project_name) != 0) {
    result.error = "wrong_firmware";
    return result;
  }
  if (esp_ota_set_boot_partition(target) != ESP_OK) { result.error = "ota_boot"; return result; }
  ESP_LOGW(kTag, "firmware %s (%u bytes) installed from GitHub in %s", description.version, static_cast<unsigned>(total), target->label);
  result.ok = true;
  result.version = description.version;
  result.bytes = total;
  return result;
}

}  // namespace armor::github_update
