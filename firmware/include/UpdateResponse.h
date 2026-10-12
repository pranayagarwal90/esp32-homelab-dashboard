#pragma once
#include <ArduinoJson.h>
#include "ManifestSignature.h"
#include "ProductIdentity.h"
#include "UpdateLogic.h"

// GET /api/firmware/update -> what the device does (host-tested). A 200 is
//   {"available": false}
// or the signed release:
//   {"available": true, "manifest_version": 2, "product": "homedeck-mini",
//    "version": "1.1.0", "filename": "homedeck-mini-1.1.0.bin",
//    "size": 1047421, "sha256": "<64 hex>", "notes": "...",
//    "key_id": "homedeck-release-2026", "signature": "<88 base64>",
//    "url": "/firmware/homedeck-mini-1.1.0.bin"}
//
// Order: JSON -> schema -> newer than the running version? -> signature
// (trusted key_id, canonical payload, ECDSA). Only a verified release is
// Available; a same or older one is UpToDate (and never verified); an
// unsigned or untrusted one is CheckFailed with VerificationFailed, and
// `check` says why (serial only). The offer is cleared unless Available.
constexpr size_t UPDATE_RESPONSE_MAX = 1024;

inline const char* jsonText(JsonVariantConst value) {
  return value.is<const char*>() ? value.as<const char*>() : nullptr;
}

inline UpdateState parseUpdateResponse(const char* body, size_t length, const char* current, uint32_t maxSize,
                                       const TrustedUpdateKey* keys, size_t keyCount, EcdsaP256Verify verify,
                                       UpdateOffer& offer, UpdateError& error, SignatureCheck& check) {
  memset(&offer, 0, sizeof(offer));
  error = UpdateError::BadResponse;
  check = SignatureCheck::Unsigned;
  JsonDocument doc;
  if (!body || length > UPDATE_RESPONSE_MAX || deserializeJson(doc, body, length) || !doc.is<JsonObject>()) {
    return UpdateState::CheckFailed;
  }
  JsonVariantConst available = doc["available"];
  if (!available.is<bool>()) return UpdateState::CheckFailed;
  if (!available.as<bool>()) {
    error = UpdateError::None;
    return UpdateState::UpToDate;
  }
  JsonVariantConst size = doc["size"];
  JsonVariantConst notes = doc["notes"];
  if (!size.is<uint32_t>() || (!notes.isNull() && !notes.is<const char*>())) return UpdateState::CheckFailed;
  const char* notesText = jsonText(notes) ? jsonText(notes) : "";
  OfferError why = validateUpdateOffer(current, jsonText(doc["version"]), size.as<uint32_t>(),
                                       jsonText(doc["sha256"]), jsonText(doc["url"]), notesText, maxSize, offer);
  if (why == OfferError::NotNewer) {
    memset(&offer, 0, sizeof(offer));
    error = UpdateError::None;
    return UpdateState::UpToDate;
  }
  if (why != OfferError::None) {
    error = why == OfferError::TooLarge ? UpdateError::TooLarge : UpdateError::BadResponse;
    memset(&offer, 0, sizeof(offer));
    return UpdateState::CheckFailed;
  }

  // The signed fields. The download name must be the signed filename, and the
  // product this device.
  JsonVariantConst manifestVersion = doc["manifest_version"];
  const char* product = jsonText(doc["product"]);
  const char* filename = jsonText(doc["filename"]);
  char sha256Hex[65];
  sha256ToHex(offer.sha256, sha256Hex);
  ManifestFields fields = {manifestVersion.is<int>() ? manifestVersion.as<int>() : 0, product, offer.version,
                           filename, offer.size, sha256Hex, notesText, jsonText(doc["key_id"])};
  if (fields.manifestVersion != 0 && (!product || !filename)) {
    check = SignatureCheck::BadEncoding;
  } else if (fields.manifestVersion != 0 && strcmp(product, PRODUCT_SLUG) != 0) {
    check = SignatureCheck::Unsupported;
  } else if (fields.manifestVersion != 0 && strcmp(filename, offer.file) != 0) {
    check = SignatureCheck::BadEncoding;
  } else {
    check = checkManifestSignature(fields, jsonText(doc["signature"]), keys, keyCount, verify);
  }
  if (check != SignatureCheck::Verified) {
    memset(&offer, 0, sizeof(offer));
    error = UpdateError::VerificationFailed;
    return UpdateState::CheckFailed;
  }
  snprintf(offer.keyId, sizeof(offer.keyId), "%s", fields.keyId);
  error = UpdateError::None;
  return UpdateState::Available;
}
