// Signed update manifests (ManifestSignature.h, UpdateResponse.h).
//
// The real ECDSA P-256 math runs on the device (mbedTLS, checked there by a
// known-answer test) and in tools/test_release_signing.py (OpenSSL). Here:
//  - the canonical payload must be byte-identical to the OpenSSL-signed
//    vectors in firmware/test/vectors/manifest_signature_v2.json;
//  - a "vector verifier" accepts exactly those (key, payload, signature)
//    triples, so a whole JSON response -> rebuilt payload -> verification
//    path is exercised against real signatures;
//  - a fake scheme (fakeSign) drives tampering and key-rotation logic.
// Needs ArduinoJson from a previous `pio run`; run from the repository root.
#include <ArduinoJson.h>
#include <assert.h>
#include <fstream>
#include <sstream>
#include <stdio.h>
#include <string.h>
#include <string>
#include <vector>
#include "ManifestSignature.h"
#include "ManifestSignatureKat.h"
#include "TrustedUpdateKeys.h"
#include "UpdateResponse.h"

static std::string readFile(const char* name) {
  for (const char* prefix : {"firmware/", "", "../"}) {
    std::ifstream file(std::string(prefix) + name);
    if (file) {
      std::stringstream text;
      text << file.rdbuf();
      return text.str();
    }
  }
  fprintf(stderr, "cannot find %s (run from the repository root)\n", name);
  assert(false);
  return "";
}

static bool contains(const std::string& text, const char* needle) {
  return text.find(needle) != std::string::npos;
}

static std::vector<uint8_t> fromHex(const char* hex) {
  std::vector<uint8_t> out;
  for (size_t i = 0; hex[i] && hex[i + 1]; i += 2) out.push_back(uint8_t(hexValue(hex[i]) << 4 | hexValue(hex[i + 1])));
  return out;
}

// --- Vectors (OpenSSL-signed, TEST ONLY keys) ----------------------------------------------

struct Vector {
  std::string name, keyId, version, filename, sha256, notes, signature;
  uint32_t size;
  std::vector<uint8_t> payload, publicKey, rawSignature;
};

static std::vector<Vector> vectors;
static std::vector<uint8_t> keyA, keyB;

static void loadVectors() {
  JsonDocument doc;
  assert(deserializeJson(doc, readFile("test/vectors/manifest_signature_v2.json")) == DeserializationError::Ok);
  assert(contains(doc["warning"].as<std::string>(), "TEST ONLY"));
  keyA = fromHex(doc["public_keys"]["homedeck-test-a"]);
  keyB = fromHex(doc["public_keys"]["homedeck-test-b"]);
  assert(keyA.size() == 65 && keyB.size() == 65 && keyA[0] == 4 && keyB[0] == 4);
  for (JsonObjectConst item : doc["vectors"].as<JsonArrayConst>()) {
    JsonObjectConst m = item["manifest"];
    Vector v;
    v.name = item["name"].as<std::string>();
    v.keyId = m["key_id"].as<std::string>();
    v.version = m["version"].as<std::string>();
    v.filename = m["filename"].as<std::string>();
    v.sha256 = m["sha256"].as<std::string>();
    v.notes = m["notes"].as<std::string>();
    v.signature = m["signature"].as<std::string>();
    v.size = m["size"].as<uint32_t>();
    assert(m["manifest_version"].as<int>() == 2 && m["product"].as<std::string>() == "homedeck-mini");
    v.payload = fromHex(item["payload_hex"]);
    v.publicKey = doc["public_keys"][v.keyId].is<const char*>() ? fromHex(doc["public_keys"][v.keyId]) : keyA;
    v.rawSignature.resize(64);
    assert(decodeSignatureBase64(v.signature.c_str(), v.rawSignature.data()));
    vectors.push_back(v);
  }
  assert(vectors.size() >= 4);
}

static ManifestFields fieldsOf(const Vector& v) {
  return {2, "homedeck-mini", v.version.c_str(), v.filename.c_str(), v.size, v.sha256.c_str(), v.notes.c_str(),
          v.keyId.c_str()};
}

// Accepts exactly the vectors' (key, payload, signature): stands in for
// ECDSA, which really did sign these bytes.
static int verifierCalls = 0;
static int vectorVerify(const uint8_t key[65], const uint8_t* message, size_t length, const uint8_t sig[64]) {
  verifierCalls++;
  for (const Vector& v : vectors) {
    if (memcmp(key, v.publicKey.data(), 65) == 0 && length == v.payload.size() &&
        memcmp(message, v.payload.data(), length) == 0 && memcmp(sig, v.rawSignature.data(), 64) == 0) {
      return 1;
    }
  }
  return 0;
}

static void testPayloadMatchesVectors() {
  for (const Vector& v : vectors) {
    char payload[MANIFEST_PAYLOAD_MAX];
    size_t length = buildManifestPayload(fieldsOf(v), payload, sizeof(payload));
    if (length != v.payload.size() || memcmp(payload, v.payload.data(), length) != 0) {
      fprintf(stderr, "payload differs from vector %s\n", v.name.c_str());
      assert(false);
    }
  }
  // The documented format, literally.
  const Vector& a = vectors[0];
  char payload[MANIFEST_PAYLOAD_MAX];
  buildManifestPayload(fieldsOf(a), payload, sizeof(payload));
  std::string expected = "HOMEDECK-MANIFEST-SIGNATURE\nmanifest_version=1:2\nproduct=13:homedeck-mini\n"
                         "version=5:1.0.2\nfilename=23:homedeck-mini-1.0.2.bin\nsize=7:1069000\nsha256=64:" +
                         a.sha256 + "\nnotes=21:Improved reliability.\nkey_id=15:homedeck-test-a\n";
  assert(expected == payload);
}

static void testPayloadRules() {
  ManifestFields f = fieldsOf(vectors[0]);
  char out[MANIFEST_PAYLOAD_MAX];
  // Not ASCII-printable (a line break could fake a field boundary): refused.
  f.notes = "two\nlines";
  assert(buildManifestPayload(f, out, sizeof(out)) == 0 && out[0] == '\0');
  f.notes = "caf\xc3\xa9";
  assert(buildManifestPayload(f, out, sizeof(out)) == 0);
  f = fieldsOf(vectors[0]);
  f.keyId = nullptr;
  assert(buildManifestPayload(f, out, sizeof(out)) == 0);
  // Too small a buffer: refused, never truncated.
  f = fieldsOf(vectors[0]);
  assert(buildManifestPayload(f, out, 100) == 0 && out[0] == '\0');
  // The longest vector (200-character notes, 17-character version) fits.
  for (const Vector& v : vectors) assert(buildManifestPayload(fieldsOf(v), out, sizeof(out)) == v.payload.size());
  // Lengths keep boundaries apart: a value containing "\n<name>=" text
  // cannot reproduce another manifest's payload (the max-notes vector
  // contains "size=7:x\\n and key_id=...").
  bool sawTricky = false;
  for (const Vector& v : vectors) sawTricky |= contains(v.notes, "key_id=");
  assert(sawTricky);
}

static void testBase64() {
  uint8_t raw[64];
  std::string zero(86, 'A');
  assert(decodeSignatureBase64((zero + "==").c_str(), raw) && raw[0] == 0 && raw[63] == 0);
  std::string ones = std::string(85, '/') + "w==";
  assert(decodeSignatureBase64(ones.c_str(), raw) && raw[0] == 0xFF && raw[63] == 0xFF);
  const std::string bad[] = {"", zero, zero + "=", zero + "===", zero + "A=", std::string(86, 'A') + "= ",
                             " " + zero + "=", std::string(85, 'A') + "*==", std::string(85, 'A') + "B==",
                             std::string(85, '/') + "x==", zero.substr(0, 84) + "===="};
  for (const std::string& text : bad) assert(!decodeSignatureBase64(text.c_str(), raw));
  assert(!decodeSignatureBase64(nullptr, raw));
  // Truncated (a 63-byte signature) and over-long ones.
  assert(!decodeSignatureBase64(vectors[0].signature.substr(0, 84).c_str(), raw));
  assert(!decodeSignatureBase64((vectors[0].signature + "AAAA").c_str(), raw));
}

static void testKeyIds() {
  assert(validUpdateKeyId("homedeck-release-2026") && validUpdateKeyId("a") && validUpdateKeyId("2027"));
  assert(validUpdateKeyId(std::string(32, 'k').c_str()));
  const char* bad[] = {"", "-start", "Upper", "under_score", "dot.ted", "sp ace", "k\n",
                       "123456789012345678901234567890123"};
  for (const char* id : bad) assert(!validUpdateKeyId(id));
  assert(!validUpdateKeyId(nullptr));
}

// --- Fake signature scheme: logic tests ----------------------------------------------------------

static void fakeSign(const uint8_t key[65], const char* message, size_t length, uint8_t out[64]) {
  uint64_t h = 1469598103934665603ull;
  for (int i = 0; i < 65; i++) h = (h ^ key[i]) * 1099511628211ull;
  for (size_t i = 0; i < length; i++) h = (h ^ uint8_t(message[i])) * 1099511628211ull;
  for (int i = 0; i < 64; i++) {
    h = (h ^ uint64_t(i)) * 1099511628211ull;
    out[i] = uint8_t(h >> 32);
  }
}

static int fakeVerify(const uint8_t key[65], const uint8_t* message, size_t length, const uint8_t sig[64]) {
  uint8_t expected[64];
  fakeSign(key, (const char*)message, length, expected);
  return memcmp(expected, sig, 64) == 0 ? 1 : 0;
}

static std::string base64(const uint8_t* data, size_t length) {
  static const char table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string out;
  for (size_t i = 0; i < length; i += 3) {
    uint32_t n = uint32_t(data[i]) << 16 | (i + 1 < length ? uint32_t(data[i + 1]) << 8 : 0) |
                 (i + 2 < length ? data[i + 2] : 0);
    out += table[n >> 18 & 63];
    out += table[n >> 12 & 63];
    out += i + 1 < length ? table[n >> 6 & 63] : '=';
    out += i + 2 < length ? table[n & 63] : '=';
  }
  return out;
}

static const uint8_t KEY_A[65] = {0x04, 0xA1};
static const uint8_t KEY_B[65] = {0x04, 0xB2};
static const uint8_t KEY_C[65] = {0x04, 0xC3};

struct Release {
  ManifestFields fields;
  std::string signature;
};

static Release signedBy(const uint8_t* key, const char* keyId) {
  Release r = {fieldsOf(vectors[0]), ""};
  r.fields.keyId = keyId;
  char payload[MANIFEST_PAYLOAD_MAX];
  size_t length = buildManifestPayload(r.fields, payload, sizeof(payload));
  assert(length > 0);
  uint8_t sig[64];
  fakeSign(key, payload, length, sig);
  r.signature = base64(sig, 64);
  return r;
}

static SignatureCheck check(const Release& r, const TrustedUpdateKey* keys, size_t count,
                            EcdsaP256Verify verify = fakeVerify) {
  return checkManifestSignature(r.fields, r.signature.c_str(), keys, count, verify);
}

static void testTampering() {
  const TrustedUpdateKey trusted[] = {{"key-a", KEY_A}, {"key-b", KEY_B}};
  Release good = signedBy(KEY_A, "key-a");
  assert(check(good, trusted, 2) == SignatureCheck::Verified);

  // Every signed field, changed after signing, breaks the signature.
  std::string sha = good.fields.sha256Hex;
  sha[0] = sha[0] == 'a' ? 'b' : 'a';
  Release t = good;
  t.fields.product = "homedeck-maxi";
  assert(check(t, trusted, 2) == SignatureCheck::BadSignature);
  t = good;
  t.fields.version = "1.0.3";
  assert(check(t, trusted, 2) == SignatureCheck::BadSignature);
  t = good;
  t.fields.filename = "homedeck-mini-evil.bin";
  assert(check(t, trusted, 2) == SignatureCheck::BadSignature);
  t = good;
  t.fields.size = good.fields.size + 1;
  assert(check(t, trusted, 2) == SignatureCheck::BadSignature);
  t = good;
  t.fields.sha256Hex = sha.c_str();
  assert(check(t, trusted, 2) == SignatureCheck::BadSignature);
  t = good;
  t.fields.notes = "Improved reliability!";
  assert(check(t, trusted, 2) == SignatureCheck::BadSignature);
  t = good;
  t.fields.notes = "";
  assert(check(t, trusted, 2) == SignatureCheck::BadSignature);
  t = good;
  t.fields.keyId = "key-b";  // An A signature claiming key B.
  assert(check(t, trusted, 2) == SignatureCheck::BadSignature);
  t = good;
  t.fields.keyId = "key-c";
  assert(check(t, trusted, 2) == SignatureCheck::UnknownKey);
  t = good;
  t.fields.manifestVersion = 1;
  assert(check(t, trusted, 2) == SignatureCheck::Unsupported);
  t = good;
  t.fields.manifestVersion = 3;
  assert(check(t, trusted, 2) == SignatureCheck::Unsupported);

  // The signature itself.
  uint8_t raw[64];
  assert(decodeSignatureBase64(good.signature.c_str(), raw));
  for (int bit : {0, 7, 255, 511}) {
    uint8_t flipped[64];
    memcpy(flipped, raw, 64);
    flipped[bit / 8] ^= uint8_t(1u << (bit % 8));
    t = good;
    t.signature = base64(flipped, 64);
    assert(check(t, trusted, 2) == SignatureCheck::BadSignature);
  }
  t = good;
  t.signature = good.signature.substr(0, 84);  // Truncated.
  assert(check(t, trusted, 2) == SignatureCheck::BadEncoding);
  t = good;
  t.signature[10] = '!';  // Invalid base64.
  assert(check(t, trusted, 2) == SignatureCheck::BadEncoding);
  t = good;
  t.fields.keyId = "Key-A";  // Malformed id.
  assert(check(t, trusted, 2) == SignatureCheck::BadEncoding);

  // Unsigned (no version, no key id, or no signature).
  t = good;
  t.fields.manifestVersion = 0;
  assert(check(t, trusted, 2) == SignatureCheck::Unsigned);
  t = good;
  t.fields.keyId = nullptr;
  assert(check(t, trusted, 2) == SignatureCheck::Unsigned);
  assert(checkManifestSignature(good.fields, nullptr, trusted, 2, fakeVerify) == SignatureCheck::Unsigned);

  // A verifier that cannot run (failed self-test) never means "verified".
  auto broken = [](const uint8_t*, const uint8_t*, size_t, const uint8_t*) { return -1; };
  assert(check(good, trusted, 2, broken) == SignatureCheck::VerifierUnavailable);
  assert(check(good, trusted, 2, nullptr) == SignatureCheck::VerifierUnavailable);
}

static void testRotation() {
  Release byA = signedBy(KEY_A, "key-a");
  Release byB = signedBy(KEY_B, "key-b");
  Release byC = signedBy(KEY_C, "key-c");
  Release aClaimingB = signedBy(KEY_A, "key-b");

  const TrustedUpdateKey onlyA[2] = {{"key-a", KEY_A}, {nullptr, nullptr}};
  const TrustedUpdateKey both[2] = {{"key-a", KEY_A}, {"key-b", KEY_B}};
  const TrustedUpdateKey onlyB[2] = {{"key-b", KEY_B}, {nullptr, nullptr}};

  // Firmware 1.x: [A].
  assert(check(byA, onlyA, 2) == SignatureCheck::Verified);
  assert(check(byB, onlyA, 2) == SignatureCheck::UnknownKey);
  // Transition firmware (still signed by A): [A, B].
  assert(check(byA, both, 2) == SignatureCheck::Verified);
  assert(check(byB, both, 2) == SignatureCheck::Verified);
  // After rotation: [B].
  assert(check(byA, onlyB, 2) == SignatureCheck::UnknownKey);
  assert(check(byB, onlyB, 2) == SignatureCheck::Verified);
  // Never: an unknown key, or a signature by one key claiming another.
  for (const TrustedUpdateKey* keys : {onlyA, both, onlyB}) {
    assert(check(byC, keys, 2) == SignatureCheck::UnknownKey);
    assert(check(aClaimingB, keys, 2) != SignatureCheck::Verified);
  }
  // Empty slots never match, even an empty or null id.
  const TrustedUpdateKey empty[2] = {{nullptr, nullptr}, {"", KEY_A}};
  assert(findTrustedUpdateKey(empty, 2, "") == nullptr && findTrustedUpdateKey(empty, 2, nullptr) == nullptr);
  assert(check(byA, empty, 2) == SignatureCheck::UnknownKey);
}

// --- Whole responses against the OpenSSL vectors ----------------------------------------------------

static std::string responseFor(const Vector& v) {
  JsonDocument doc;
  doc["available"] = true;
  doc["manifest_version"] = 2;
  doc["product"] = "homedeck-mini";
  doc["version"] = v.version;
  doc["filename"] = v.filename;
  doc["size"] = v.size;
  doc["sha256"] = v.sha256;
  doc["notes"] = v.notes;
  doc["key_id"] = v.keyId;
  doc["signature"] = v.signature;
  doc["url"] = "/firmware/" + v.filename;
  std::string out;
  serializeJson(doc, out);
  return out;
}

static UpdateState evaluate(const std::string& body, const TrustedUpdateKey* keys, size_t count,
                            SignatureCheck& result, UpdateOffer& offer, const char* current = "1.0.0") {
  UpdateError error;
  UpdateState state = parseUpdateResponse(body.c_str(), body.size(), current, 0x400000, keys, count, vectorVerify,
                                          offer, error, result);
  assert((state == UpdateState::Available) == (result == SignatureCheck::Verified));
  if (state == UpdateState::CheckFailed && result != SignatureCheck::Verified && result != SignatureCheck::Unsigned) {
    assert(error == UpdateError::VerificationFailed);
  }
  return state;
}

static std::string replaced(std::string body, const std::string& from, const std::string& to) {
  size_t at = body.find(from);
  assert(at != std::string::npos);
  return body.replace(at, from.size(), to);
}

static void testResponsesAgainstVectors() {
  const TrustedUpdateKey trusted[] = {{"homedeck-test-a", keyA.data()}, {"homedeck-test-b", keyB.data()}};
  SignatureCheck result;
  UpdateOffer offer;
  for (const Vector& v : vectors) {
    std::string body = responseFor(v);
    assert(evaluate(body, trusted, 2, result, offer) == UpdateState::Available);
    assert(strcmp(offer.version, v.version.c_str()) == 0 && strcmp(offer.notes, v.notes.c_str()) == 0);
    // JSON formatting and key order do not matter (fields are signed, not text).
    std::string spaced = replaced(body, "{\"available\":true,", "{ \"available\" : true ,\n  ");
    assert(evaluate(spaced, trusted, 2, result, offer) == UpdateState::Available);
  }

  // Tampering with the response of a real signature.
  const Vector& a = vectors[0];
  std::string body = responseFor(a);
  std::string sha = a.sha256;
  sha[5] = sha[5] == '0' ? '1' : '0';
  const std::string tampered[] = {
    replaced(body, "\"version\":\"1.0.2\"", "\"version\":\"1.0.9\""),
    replaced(replaced(body, "\"filename\":\"homedeck-mini-1.0.2.bin\"", "\"filename\":\"homedeck-mini-x.bin\""),
             "/firmware/homedeck-mini-1.0.2.bin", "/firmware/homedeck-mini-x.bin"),
    replaced(body, "\"size\":1069000", "\"size\":1069001"),
    replaced(body, a.sha256, sha),
    replaced(body, "Improved reliability.", "Improved reliability!"),
    replaced(body, "\"key_id\":\"homedeck-test-a\"", "\"key_id\":\"homedeck-test-b\""),
  };
  for (const std::string& t : tampered) {
    assert(evaluate(t, trusted, 2, result, offer) == UpdateState::CheckFailed);
    assert(result == SignatureCheck::BadSignature && offer.version[0] == '\0');
  }
  assert(evaluate(replaced(body, "\"product\":\"homedeck-mini\"", "\"product\":\"homedeck-maxi\""), trusted, 2,
                  result, offer) == UpdateState::CheckFailed && result == SignatureCheck::Unsupported);
  assert(evaluate(replaced(body, "\"manifest_version\":2", "\"manifest_version\":1"), trusted, 2, result, offer) ==
         UpdateState::CheckFailed && result == SignatureCheck::Unsupported);
  // The download path must be the signed filename.
  assert(evaluate(replaced(body, "/firmware/homedeck-mini-1.0.2.bin", "/firmware/other.bin"), trusted, 2, result,
                  offer) == UpdateState::CheckFailed && result == SignatureCheck::BadEncoding);
  // Signature encodings.
  uint8_t flipped[64];
  memcpy(flipped, a.rawSignature.data(), 64);
  flipped[40] ^= 0x10;
  assert(evaluate(replaced(body, a.signature, base64(flipped, 64)), trusted, 2, result, offer) ==
         UpdateState::CheckFailed && result == SignatureCheck::BadSignature);
  assert(evaluate(replaced(body, a.signature, a.signature.substr(0, 80)), trusted, 2, result, offer) ==
         UpdateState::CheckFailed && result == SignatureCheck::BadEncoding);
  assert(evaluate(replaced(body, a.signature, "not base64!"), trusted, 2, result, offer) ==
         UpdateState::CheckFailed && result == SignatureCheck::BadEncoding);
  // Untrusted keys: an unknown id, or the vectors' real key B not in this keyset.
  assert(evaluate(replaced(body, "\"key_id\":\"homedeck-test-a\"", "\"key_id\":\"homedeck-test-c\""), trusted, 2,
                  result, offer) == UpdateState::CheckFailed && result == SignatureCheck::UnknownKey);
  assert(evaluate(responseFor(vectors[1]), trusted, 1, result, offer) == UpdateState::CheckFailed &&
         result == SignatureCheck::UnknownKey);

  // Unsigned (an older backend): verification failed, never "available".
  std::string old = std::string("{\"available\":true,\"version\":\"1.0.2\",\"size\":1069000,\"sha256\":\"") + a.sha256 +
                    "\",\"notes\":\"x\",\"url\":\"/firmware/homedeck-mini-1.0.2.bin\"}";
  UpdateError error;
  assert(parseUpdateResponse(old.c_str(), old.size(), "1.0.0", 0x400000, trusted, 2, vectorVerify, offer, error,
                             result) == UpdateState::CheckFailed);
  assert(result == SignatureCheck::Unsigned && error == UpdateError::VerificationFailed);

  // No crypto at all for an answer that is not a newer release, or malformed.
  verifierCalls = 0;
  assert(evaluate(body, trusted, 2, result, offer, "1.0.2") == UpdateState::UpToDate);
  assert(evaluate(body, trusted, 2, result, offer, "2.0.0") == UpdateState::UpToDate);
  assert(evaluate(replaced(body, "\"size\":1069000", "\"size\":\"big\""), trusted, 2, result, offer) ==
         UpdateState::CheckFailed);
  assert(evaluate(replaced(body, "\"key_id\":\"homedeck-test-a\"", "\"key_id\":\"homedeck-test-c\""), trusted, 2,
                  result, offer) == UpdateState::CheckFailed);
  assert(verifierCalls == 0);

  // This firmware's real keyset (no release key generated yet): nothing verifies.
  for (const Vector& v : vectors) {
    assert(evaluate(responseFor(v), TRUSTED_UPDATE_KEYS, TRUSTED_UPDATE_KEY_SLOTS, result, offer) ==
           UpdateState::CheckFailed);
    assert(result == SignatureCheck::UnknownKey);
  }
}

// --- Device known-answer test and key hygiene --------------------------------------------------------

static void testKatAndTrustedKeys() {
  // The device's self-test data is exactly the first OpenSSL vector.
  const Vector& a = vectors[0];
  assert(a.name == "signed-by-a" && a.keyId == KAT_KEY_ID && a.version == KAT_VERSION);
  assert(a.filename == KAT_FILENAME && a.size == KAT_SIZE && a.sha256 == KAT_SHA256 && a.notes == KAT_NOTES);
  assert(memcmp(KAT_PUBLIC_KEY, keyA.data(), 65) == 0 && memcmp(KAT_SIGNATURE, a.rawSignature.data(), 64) == 0);
  ManifestFields kat = {UPDATE_MANIFEST_VERSION, "homedeck-mini", KAT_VERSION, KAT_FILENAME, KAT_SIZE, KAT_SHA256,
                        KAT_NOTES, KAT_KEY_ID};
  assert(vectorVerify(KAT_PUBLIC_KEY, a.payload.data(), a.payload.size(), KAT_SIGNATURE) == 1);
  char payload[MANIFEST_PAYLOAD_MAX];
  assert(buildManifestPayload(kat, payload, sizeof(payload)) == a.payload.size());

  // Test keys are never trusted for updates, and slots hold only public points.
  assert(TRUSTED_UPDATE_KEY_SLOTS >= 2);
  for (size_t i = 0; i < TRUSTED_UPDATE_KEY_SLOTS; i++) {
    const TrustedUpdateKey& key = TRUSTED_UPDATE_KEYS[i];
    if (!key.id) continue;
    assert(validUpdateKeyId(key.id) && strncmp(key.id, "homedeck-test", 13) != 0);
    assert(key.publicKey[0] == 0x04);
    assert(memcmp(key.publicKey, keyA.data(), 65) != 0 && memcmp(key.publicKey, keyB.data(), 65) != 0);
  }
  std::string trustedSource = readFile("include/TrustedUpdateKeys.h");
  assert(!contains(trustedSource, "ManifestSignatureKat") && !contains(trustedSource, "homedeck-test"));
  assert(contains(readFile("include/ManifestSignatureKat.h"), "TEST ONLY - NOT A PRODUCTION RELEASE KEY"));
}

static void testSourceOrder() {
  std::string update = readFile("src/FirmwareUpdate.cpp");
  // The offer is published only from the verified parse.
  size_t parse = update.find("parseUpdateResponse(body, length, firmwareVersion(), otaSlotSize(), TRUSTED_UPDATE_KEYS,");
  size_t offer = update.find("publishOffer(state, error, &offer);");
  assert(parse != std::string::npos && offer != std::string::npos && parse < offer);
  assert(contains(update, "TRUSTED_UPDATE_KEY_SLOTS, deviceEcdsaP256Verify, offer, error, check)"));
  // Only one place publishes an offer.
  assert(update.find("&offer);", offer + strlen("publishOffer(state, error, &offer);")) == std::string::npos);
  // Install keeps hash-before-activate.
  assert(update.find("memcmp(digest, offer.sha256") < update.find("Update.end()"));
  // The device verifier uses mbedTLS ECDSA and runs its self-test first.
  std::string verifier = readFile("src/UpdateVerifier.cpp");
  assert(contains(verifier, "mbedtls_ecdsa_verify(") && contains(verifier, "mbedtls_ecp_check_pubkey("));
  assert(contains(verifier, "if (selfTest < 0) selfTest = runSelfTest()") && contains(verifier, "if (selfTest != 1) return -1;"));
  // Firmware sources never hold private key material.
  for (const char* name : {"include/TrustedUpdateKeys.h", "include/ManifestSignatureKat.h", "include/ManifestSignature.h",
                           "src/UpdateVerifier.cpp", "src/FirmwareUpdate.cpp", "test/vectors/manifest_signature_v2.json"}) {
    std::string text = readFile(name);
    assert(!contains(text, "PRIVATE KEY") && !contains(text, "\"d\"") && !contains(text, "private_key"));
  }
}

int main() {
  loadVectors();
  testPayloadMatchesVectors();
  testPayloadRules();
  testBase64();
  testKeyIds();
  testTampering();
  testRotation();
  testResponsesAgainstVectors();
  testKatAndTrustedKeys();
  testSourceOrder();
  puts("manifest signature tests passed: vectors, payload, base64, key ids, tampering, rotation, responses, KAT, sources");
  return 0;
}
