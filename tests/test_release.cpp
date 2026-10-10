// ARMOR-RADAR - host tests of the comparison of versions and of the choice of a release's firmware image: the update from GitHub never installs what is not this board's.
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D). GPL-3.0-or-later.
#include <cstdio>
#include <string>

#include "../main/core/json.hpp"
#include "../main/core/release_assets.hpp"
#include "../main/core/semver.hpp"

static int failures = 0;
static int checks = 0;
#define CHECK(condition)                                                              \
  do {                                                                                \
    ++checks;                                                                         \
    if (!(condition)) {                                                               \
      ++failures;                                                                     \
      std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #condition);                \
    }                                                                                 \
  } while (0)

using namespace armor;

static void test_semver() {
  using namespace semver;
  CHECK(is_newer("v0.4.6", "0.4.5") && is_newer("0.5.0", "0.4.9") && is_newer("1.0.0", "0.4.5") && is_newer("0.10.0", "0.9.9"));
  CHECK(!is_newer("0.4.5", "0.4.5") && !is_newer("0.4.4", "0.4.5") && !is_newer("0.9.9", "0.10.0"));
  CHECK(!is_newer("0.4.6-rc1", "0.4.5"));   // a pre-release is never offered
  CHECK(!is_newer("not-a-version", "0.4.5") && !is_newer("0.4.6", "also-not-a-version"));
  CHECK(!parse("").ok && !parse("1.2").ok && !parse("1.2.3.4").ok && !parse("1.2.x").ok && !parse("v").ok && !parse("..").ok);
  CHECK(parse("v2.10.3").ok && parse("v2.10.3").major == 2 && parse("v2.10.3").minor == 10 && parse("v2.10.3").patch == 3);
}

static json::Value fake_release(const std::string& names) {
  // names: "a,b,c" -> assets with those names and a url "https://x/<name>"
  std::string text = "[";
  std::size_t at = 0;
  bool first = true;
  while (at <= names.size()) {
    std::size_t comma = names.find(',', at);
    const std::string name = names.substr(at, comma == std::string::npos ? std::string::npos : comma - at);
    if (!name.empty()) { text += std::string(first ? "" : ",") + "{\"name\":\"" + name + "\",\"browser_download_url\":\"https://x/" + name + "\"}"; first = false; }
    if (comma == std::string::npos) break;
    at = comma + 1;
  }
  text += "]";
  json::Value document;
  json::parse(text, document);
  return document;
}

static void test_assets() {
  const std::string p = release::kAssetPrefix;
  // the names
  CHECK(release::asset_name("s3-wifi") == p + "-s3-wifi.bin" && release::asset_name("s3-eth") == p + "-s3-eth.bin" && release::asset_name(nullptr) == p + "-.bin");
  CHECK(release::legacy_asset_name("s3-eth") == p + ".bin" && release::legacy_asset_name("lcd7box") == p + ".bin" && release::legacy_asset_name("s3-wifi").empty() && release::legacy_asset_name(nullptr).empty());
  // a release with an image per board: each board gets its own, with the checksum next to it
  const json::Value both = fake_release(p + "-s3-eth.bin," + p + "-s3-eth.bin.sha256," + p + "-s3-wifi.bin," + p + "-s3-wifi.bin.sha256," + p + ".bin," + p + ".bin.sha256,notes.txt");
  const release::Picked wifi = release::pick(&both, "s3-wifi"), eth = release::pick(&both, "s3-eth");
  CHECK(wifi.image_url == "https://x/" + p + "-s3-wifi.bin" && wifi.checksum_url == "https://x/" + p + "-s3-wifi.bin.sha256" && !wifi.legacy);
  CHECK(eth.image_url == "https://x/" + p + "-s3-eth.bin" && eth.checksum_url == "https://x/" + p + "-s3-eth.bin.sha256" && !eth.legacy);
  // a release from before the naming: only the default board finds its image; the Wi-Fi board never takes it
  const json::Value old = fake_release(p + ".bin," + p + ".bin.sha256," + p + "-s3-wifi.bin.sha256");
  const release::Picked old_eth = release::pick(&old, "s3-eth"), old_wifi = release::pick(&old, "s3-wifi");
  CHECK(old_eth.image_url == "https://x/" + p + ".bin" && old_eth.checksum_url == "https://x/" + p + ".bin.sha256" && old_eth.legacy);
  CHECK(old_wifi.image_url.empty() && old_wifi.checksum_url.empty() && !old_wifi.legacy);
  CHECK(release::pick(&old, "lcd7box").image_url == "https://x/" + p + ".bin");
  // the checksum of one image is never used for another: an image with no checksum file is picked WITHOUT one (the caller refuses to install it)
  const json::Value no_sum = fake_release(p + "-s3-eth.bin," + p + ".bin.sha256");
  CHECK(release::pick(&no_sum, "s3-eth").image_url == "https://x/" + p + "-s3-eth.bin" && release::pick(&no_sum, "s3-eth").checksum_url.empty());
  // an image of another board is never taken, nothing at all gives nothing
  const json::Value other = fake_release("armor_other-s3-eth.bin," + p + "-lcd7box.bin");
  CHECK(release::pick(&other, "s3-eth").image_url.empty() && release::pick(&other, "s3-wifi").image_url.empty());
  const json::Value none = fake_release("");
  CHECK(release::pick(&none, "s3-eth").image_url.empty() && release::pick(nullptr, "s3-eth").image_url.empty());
  json::Value not_array; json::parse("{\"a\":1}", not_array);
  CHECK(release::pick(&not_array, "s3-eth").image_url.empty());
}

int main() {
  test_semver();
  test_assets();
  std::printf("%d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
