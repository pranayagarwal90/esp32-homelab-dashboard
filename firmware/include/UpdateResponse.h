#pragma once
#include <ArduinoJson.h>
#include "UpdateLogic.h"

// Parses a 200 answer from GET /api/firmware/update (host-tested):
//   {"available": false}
//   {"available": true, "version": "1.1.0", "size": 1047421,
//    "sha256": "<64 hex>", "notes": "...", "url": "/firmware/<file>"}
// Returns UpToDate, Available (offer filled) or CheckFailed (error set). A
// same or older version is treated as "up to date", never offered.
constexpr size_t UPDATE_RESPONSE_MAX = 1024;

inline UpdateState parseUpdateResponse(const char* body, size_t length, const char* current, uint32_t maxSize,
                                       UpdateOffer& offer, UpdateError& error) {
  memset(&offer, 0, sizeof(offer));
  error = UpdateError::BadResponse;
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
  OfferError why = validateUpdateOffer(current, doc["version"].as<const char*>(), size.as<uint32_t>(),
                                       doc["sha256"].as<const char*>(), doc["url"].as<const char*>(),
                                       notes.as<const char*>(), maxSize, offer);
  switch (why) {
    case OfferError::None:
      error = UpdateError::None;
      return UpdateState::Available;
    case OfferError::NotNewer:
      memset(&offer, 0, sizeof(offer));
      error = UpdateError::None;
      return UpdateState::UpToDate;
    case OfferError::TooLarge:
      error = UpdateError::TooLarge;
      return UpdateState::CheckFailed;
    default:
      return UpdateState::CheckFailed;
  }
}
