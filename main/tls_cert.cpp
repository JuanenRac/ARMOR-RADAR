// ARMOR-RADAR - the node's own TLS certificate.
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D). GPL-3.0-or-later.
#include "tls_cert.hpp"

#include <cstdio>
#include <cstring>
extern "C" {
#include "esp_log.h"
#include "mbedtls/ctr_drbg.h"
#include "mbedtls/ecp.h"
#include "mbedtls/entropy.h"
#include "mbedtls/error.h"
#include "mbedtls/pk.h"
#include "mbedtls/sha256.h"
#include "mbedtls/x509_crt.h"
}
#include "core/auth.hpp"
#include "node_store.hpp"

namespace armor::tlscert {
namespace {
constexpr char kTag[] = "armor-tls";
constexpr char kCertificateKey[] = "tls_cert";
constexpr char kPrivateKey[] = "tls_key";
// Not a date of this project: the span of validity a certificate needs (a browser compares it with its own clock).
constexpr char kNotBefore[] = "20240101000000";
constexpr char kNotAfter[] = "20741231235959";

bool fingerprint_of(const std::string& certificate_pem, std::string& hex) {
  mbedtls_x509_crt crt;
  mbedtls_x509_crt_init(&crt);
  const int parsed = mbedtls_x509_crt_parse(&crt, reinterpret_cast<const unsigned char*>(certificate_pem.data()), certificate_pem.size());
  bool ok = false;
  if (parsed == 0) {
    unsigned char digest[32];
    ok = mbedtls_sha256(crt.raw.p, crt.raw.len, digest, 0) == 0;
    if (ok) hex = auth::to_hex(digest, sizeof digest);
  }
  mbedtls_x509_crt_free(&crt);
  return ok;
}

bool make(const std::string& node_id, std::string& certificate_pem, std::string& key_pem) {
  mbedtls_entropy_context entropy;
  mbedtls_ctr_drbg_context drbg;
  mbedtls_pk_context key;
  mbedtls_x509write_cert crt;
  mbedtls_entropy_init(&entropy);
  mbedtls_ctr_drbg_init(&drbg);
  mbedtls_pk_init(&key);
  mbedtls_x509write_crt_init(&crt);
  bool ok = false;
  do {
    const char* personalisation = "armor-node-tls";
    if (mbedtls_ctr_drbg_seed(&drbg, mbedtls_entropy_func, &entropy, reinterpret_cast<const unsigned char*>(personalisation), std::strlen(personalisation)) != 0) break;
    if (mbedtls_pk_setup(&key, mbedtls_pk_info_from_type(MBEDTLS_PK_ECKEY)) != 0) break;
    if (mbedtls_ecp_gen_key(MBEDTLS_ECP_DP_SECP256R1, mbedtls_pk_ec(key), mbedtls_ctr_drbg_random, &drbg) != 0) break;
    const std::string subject = "CN=" + node_id + ",O=A.R.M.O.R.";
    unsigned char serial[16];
    if (mbedtls_ctr_drbg_random(&drbg, serial, sizeof serial) != 0) break;
    serial[0] &= 0x7F;   // a positive number
    serial[0] |= 0x01;   // and never zero
    mbedtls_x509write_crt_set_version(&crt, MBEDTLS_X509_CRT_VERSION_3);
    mbedtls_x509write_crt_set_md_alg(&crt, MBEDTLS_MD_SHA256);
    mbedtls_x509write_crt_set_subject_key(&crt, &key);
    mbedtls_x509write_crt_set_issuer_key(&crt, &key);
    if (mbedtls_x509write_crt_set_subject_name(&crt, subject.c_str()) != 0) break;
    if (mbedtls_x509write_crt_set_issuer_name(&crt, subject.c_str()) != 0) break;
    if (mbedtls_x509write_crt_set_serial_raw(&crt, serial, sizeof serial) != 0) break;
    if (mbedtls_x509write_crt_set_validity(&crt, kNotBefore, kNotAfter) != 0) break;
    if (mbedtls_x509write_crt_set_basic_constraints(&crt, 0, -1) != 0) break;
    unsigned char buffer[1600];
    if (mbedtls_x509write_crt_pem(&crt, buffer, sizeof buffer, mbedtls_ctr_drbg_random, &drbg) != 0) break;
    certificate_pem.assign(reinterpret_cast<char*>(buffer), std::strlen(reinterpret_cast<char*>(buffer)) + 1);
    if (mbedtls_pk_write_key_pem(&key, buffer, sizeof buffer) != 0) break;
    key_pem.assign(reinterpret_cast<char*>(buffer), std::strlen(reinterpret_cast<char*>(buffer)) + 1);
    ok = true;
  } while (false);
  mbedtls_x509write_crt_free(&crt);
  mbedtls_pk_free(&key);
  mbedtls_ctr_drbg_free(&drbg);
  mbedtls_entropy_free(&entropy);
  return ok;
}
}  // namespace

bool load_or_create(const std::string& node_id, Material& out) {
  if (store::blob_read(kCertificateKey, out.certificate_pem) && store::blob_read(kPrivateKey, out.key_pem) && !out.certificate_pem.empty() && !out.key_pem.empty()
      && out.certificate_pem.back() == '\0' && out.key_pem.back() == '\0' && fingerprint_of(out.certificate_pem, out.sha256)) return true;
  ESP_LOGI(kTag, "making the certificate of this node");
  if (!make(node_id, out.certificate_pem, out.key_pem) || !fingerprint_of(out.certificate_pem, out.sha256)) { ESP_LOGE(kTag, "the certificate could not be made"); return false; }
  if (!store::blob_write(kCertificateKey, out.certificate_pem) || !store::blob_write(kPrivateKey, out.key_pem)) ESP_LOGW(kTag, "the certificate could not be kept in flash: a new one is made at the next start");
  return true;
}

}  // namespace armor::tlscert
