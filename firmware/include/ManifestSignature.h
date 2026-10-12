#pragma once
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

// Signed update manifests (version 2), pure and host-tested. A release is
// signed offline with ECDSA P-256 over SHA-256 of a canonical payload built
// from the manifest's fields (never the JSON text); the device verifies it
// against the PUBLIC keys built into its firmware (TrustedUpdateKeys.h)
// before an update is ever offered. The crypto itself is passed in
// (EcdsaP256Verify): mbedTLS on the device (ManifestSignature.cpp).
//
// Encodings, identical in tools/release_signing.py and
// backend/firmware_updates.py:
//   public key  65 bytes: 04 || X(32) || Y(32)
//   signature   64 bytes: r(32) || s(32), big-endian; padded standard
//               base64 (88 characters) in JSON, canonical only
//   payload     "HOMEDECK-MANIFEST-SIGNATURE\n", then for each field in
//               this order: name "=" decimal-length ":" value "\n"
//                 manifest_version, product, version, filename, size,
//                 sha256 (lower-case hex), notes, key_id
//               All values are validated ASCII; lengths make every field
//               boundary unambiguous.

constexpr int UPDATE_MANIFEST_VERSION = 2;
constexpr size_t UPDATE_KEY_ID_SIZE = 33;          // 32 characters: a-z 0-9 '-'.
constexpr size_t SIGNATURE_RAW_BYTES = 64;
constexpr size_t SIGNATURE_BASE64_CHARS = 88;
constexpr size_t UPDATE_PUBLIC_KEY_BYTES = 65;
constexpr size_t MANIFEST_PAYLOAD_MAX = 640;       // Longest fields: about 430 bytes.

struct TrustedUpdateKey {
  const char* id;           // nullptr: empty slot.
  const uint8_t* publicKey; // UPDATE_PUBLIC_KEY_BYTES.
};

// The fields a signature covers, as received (the device's own product id).
struct ManifestFields {
  int manifestVersion;
  const char* product;
  const char* version;
  const char* filename;
  uint32_t size;
  const char* sha256Hex;  // 64 lower-case hex characters.
  const char* notes;
  const char* keyId;
};

inline bool validUpdateKeyId(const char* id) {
  if (!id || !id[0] || strlen(id) >= UPDATE_KEY_ID_SIZE) return false;
  if (!((id[0] >= 'a' && id[0] <= 'z') || (id[0] >= '0' && id[0] <= '9'))) return false;
  for (const char* p = id; *p; p++) {
    if (!((*p >= 'a' && *p <= 'z') || (*p >= '0' && *p <= '9') || *p == '-')) return false;
  }
  return true;
}

inline int base64Value(char c) {
  if (c >= 'A' && c <= 'Z') return c - 'A';
  if (c >= 'a' && c <= 'z') return c - 'a' + 26;
  if (c >= '0' && c <= '9') return c - '0' + 52;
  if (c == '+') return 62;
  if (c == '/') return 63;
  return -1;
}

// Exactly the canonical padded base64 of 64 bytes (86 symbols + "==", the
// unused low bits zero); anything else is refused, so one signature has one
// spelling.
inline bool decodeSignatureBase64(const char* text, uint8_t out[SIGNATURE_RAW_BYTES]) {
  if (!text || strlen(text) != SIGNATURE_BASE64_CHARS || text[86] != '=' || text[87] != '=') return false;
  uint32_t bits = 0;
  int count = 0;
  size_t n = 0;
  for (int i = 0; i < 86; i++) {
    int value = base64Value(text[i]);
    if (value < 0) return false;
    bits = (bits << 6) | uint32_t(value);
    count += 6;
    if (count >= 8) {
      count -= 8;
      out[n++] = uint8_t(bits >> count);
      bits &= (1u << count) - 1;
    }
  }
  return n == SIGNATURE_RAW_BYTES && bits == 0; // 4 leftover bits, all zero.
}

inline bool appendPayloadField(char* out, size_t size, size_t& n, const char* name, const char* value) {
  if (!value) return false;
  for (const char* p = value; *p; p++) {
    if (*p < ' ' || *p > '~') return false; // ASCII only; no line breaks.
  }
  int written = snprintf(out + n, size - n, "%s=%u:%s\n", name, unsigned(strlen(value)), value);
  if (written < 0 || size_t(written) >= size - n) return false;
  n += size_t(written);
  return true;
}

// The signed bytes; returns their length, or 0 (out = "") if a field is
// missing, not ASCII, or the result does not fit.
inline size_t buildManifestPayload(const ManifestFields& f, char* out, size_t size) {
  if (!out || size == 0) return 0;
  out[0] = '\0';
  char manifestVersion[8], fileSize[12];
  snprintf(manifestVersion, sizeof(manifestVersion), "%d", f.manifestVersion);
  snprintf(fileSize, sizeof(fileSize), "%u", unsigned(f.size));
  static const char header[] = "HOMEDECK-MANIFEST-SIGNATURE\n";
  if (sizeof(header) > size) return 0;
  memcpy(out, header, sizeof(header));
  size_t n = sizeof(header) - 1;
  bool ok = appendPayloadField(out, size, n, "manifest_version", manifestVersion) &&
            appendPayloadField(out, size, n, "product", f.product) &&
            appendPayloadField(out, size, n, "version", f.version) &&
            appendPayloadField(out, size, n, "filename", f.filename) &&
            appendPayloadField(out, size, n, "size", fileSize) &&
            appendPayloadField(out, size, n, "sha256", f.sha256Hex) &&
            appendPayloadField(out, size, n, "notes", f.notes) &&
            appendPayloadField(out, size, n, "key_id", f.keyId);
  if (!ok) {
    out[0] = '\0';
    return 0;
  }
  return n;
}

// The trusted key with this id, or nullptr (empty slots never match).
inline const TrustedUpdateKey* findTrustedUpdateKey(const TrustedUpdateKey* keys, size_t count, const char* id) {
  if (!id || !id[0]) return nullptr;
  for (size_t i = 0; i < count; i++) {
    if (keys[i].id && keys[i].publicKey && strcmp(keys[i].id, id) == 0) return &keys[i];
  }
  return nullptr;
}

enum class SignatureCheck : uint8_t {
  Verified,
  Unsigned,             // No manifest_version / key_id / signature (older backend).
  Unsupported,          // A manifest version or product this firmware does not accept.
  BadEncoding,          // Malformed key id, signature base64 or fields.
  UnknownKey,           // key_id is not one of this firmware's trusted keys.
  BadSignature,         // Does not verify under that key.
  VerifierUnavailable   // The device's crypto self-test failed.
};

// For the serial log only; users see "Update verification failed".
inline const char* signatureCheckText(SignatureCheck check) {
  switch (check) {
    case SignatureCheck::Verified: return "signature verified";
    case SignatureCheck::Unsigned: return "manifest is not signed";
    case SignatureCheck::Unsupported: return "manifest version or product not supported";
    case SignatureCheck::BadEncoding: return "malformed key id, signature or fields";
    case SignatureCheck::UnknownKey: return "unknown signing key";
    case SignatureCheck::BadSignature: return "bad signature";
    case SignatureCheck::VerifierUnavailable: return "signature verifier unavailable";
  }
  return "verification failed";
}

// ECDSA P-256 over SHA-256(message): 1 = valid, 0 = invalid, -1 = could not run.
typedef int (*EcdsaP256Verify)(const uint8_t publicKey[UPDATE_PUBLIC_KEY_BYTES], const uint8_t* message,
                               size_t length, const uint8_t signature[SIGNATURE_RAW_BYTES]);

// Verification order: format -> trusted key -> canonical payload -> ECDSA.
// signatureBase64 nullptr means the manifest carried none.
inline SignatureCheck checkManifestSignature(const ManifestFields& fields, const char* signatureBase64,
                                             const TrustedUpdateKey* keys, size_t keyCount, EcdsaP256Verify verify) {
  if (fields.manifestVersion == 0 || !fields.keyId || !signatureBase64) return SignatureCheck::Unsigned;
  if (fields.manifestVersion != UPDATE_MANIFEST_VERSION) return SignatureCheck::Unsupported;
  uint8_t signature[SIGNATURE_RAW_BYTES];
  if (!validUpdateKeyId(fields.keyId) || !decodeSignatureBase64(signatureBase64, signature)) {
    return SignatureCheck::BadEncoding;
  }
  const TrustedUpdateKey* key = findTrustedUpdateKey(keys, keyCount, fields.keyId);
  if (!key) return SignatureCheck::UnknownKey;
  char payload[MANIFEST_PAYLOAD_MAX];
  size_t length = buildManifestPayload(fields, payload, sizeof(payload));
  if (length == 0) return SignatureCheck::BadEncoding;
  if (!verify) return SignatureCheck::VerifierUnavailable;
  int result = verify(key->publicKey, (const uint8_t*)payload, length, signature);
  if (result < 0) return SignatureCheck::VerifierUnavailable;
  return result == 1 ? SignatureCheck::Verified : SignatureCheck::BadSignature;
}

inline void sha256ToHex(const uint8_t digest[32], char out[65]) {
  static const char hex[] = "0123456789abcdef";
  for (int i = 0; i < 32; i++) {
    out[2 * i] = hex[digest[i] >> 4];
    out[2 * i + 1] = hex[digest[i] & 15];
  }
  out[64] = '\0';
}
