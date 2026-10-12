#include <Arduino.h>
#include <mbedtls/ecdsa.h>
#include <mbedtls/ecp.h>
#include <mbedtls/sha256.h>
#include "UpdateVerifier.h"
#include "ManifestSignatureKat.h"
#include "ProductIdentity.h"

// mbedTLS does the elliptic-curve work; nothing here is custom crypto. The
// raw r||s halves are read straight into MPIs (no DER), and the public point
// is checked to be on the curve before use.
static int mbedtlsVerify(const uint8_t publicKey[UPDATE_PUBLIC_KEY_BYTES], const uint8_t* message, size_t length,
                         const uint8_t signature[SIGNATURE_RAW_BYTES]) {
  uint8_t digest[32];
  if (mbedtls_sha256_ret(message, length, digest, 0) != 0) return -1;
  mbedtls_ecp_group group;
  mbedtls_ecp_point point;
  mbedtls_mpi r, s;
  mbedtls_ecp_group_init(&group);
  mbedtls_ecp_point_init(&point);
  mbedtls_mpi_init(&r);
  mbedtls_mpi_init(&s);
  int result = -1;
  if (mbedtls_ecp_group_load(&group, MBEDTLS_ECP_DP_SECP256R1) == 0) {
    if (mbedtls_ecp_point_read_binary(&group, &point, publicKey, UPDATE_PUBLIC_KEY_BYTES) != 0 ||
        mbedtls_ecp_check_pubkey(&group, &point) != 0 ||
        mbedtls_mpi_read_binary(&r, signature, 32) != 0 ||
        mbedtls_mpi_read_binary(&s, signature + 32, 32) != 0) {
      result = 0; // Not a valid key/signature for P-256: it does not verify.
    } else {
      // Also rejects r or s outside 1..n-1.
      result = mbedtls_ecdsa_verify(&group, digest, sizeof(digest), &point, &r, &s) == 0 ? 1 : 0;
    }
  }
  mbedtls_mpi_free(&s);
  mbedtls_mpi_free(&r);
  mbedtls_ecp_point_free(&point);
  mbedtls_ecp_group_free(&group);
  return result;
}

// Known-answer test, once per boot: -1 not run, 0 failed, 1 passed.
static int selfTest = -1;

static bool runSelfTest() {
  unsigned long started = millis();
  ManifestFields fields = {UPDATE_MANIFEST_VERSION, PRODUCT_SLUG, KAT_VERSION, KAT_FILENAME, KAT_SIZE,
                           KAT_SHA256, KAT_NOTES, KAT_KEY_ID};
  char payload[MANIFEST_PAYLOAD_MAX];
  size_t length = buildManifestPayload(fields, payload, sizeof(payload));
  bool valid = length > 0 && mbedtlsVerify(KAT_PUBLIC_KEY, (const uint8_t*)payload, length, KAT_SIGNATURE) == 1;
  payload[length > 0 ? length - 2 : 0] ^= 0x01; // One bit of the key id.
  bool rejects = length > 0 && mbedtlsVerify(KAT_PUBLIC_KEY, (const uint8_t*)payload, length, KAT_SIGNATURE) == 0;
  Serial.printf("[UPDATE] Signature self-test %s (%lu ms)\n", valid && rejects ? "passed" : "FAILED",
                millis() - started);
  return valid && rejects;
}

int deviceEcdsaP256Verify(const uint8_t publicKey[UPDATE_PUBLIC_KEY_BYTES], const uint8_t* message, size_t length,
                          const uint8_t signature[SIGNATURE_RAW_BYTES]) {
  if (selfTest < 0) selfTest = runSelfTest() ? 1 : 0;
  if (selfTest != 1) return -1;
  unsigned long started = millis();
  int result = mbedtlsVerify(publicKey, message, length, signature);
  Serial.printf("[UPDATE] Signature check: %s (%lu ms)\n", result == 1 ? "valid" : "invalid", millis() - started);
  return result;
}
