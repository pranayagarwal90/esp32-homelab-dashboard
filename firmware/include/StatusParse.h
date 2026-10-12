#pragma once
#include <ArduinoJson.h>
#include <string.h>
#include "ServicesLogic.h"
#include "WorldClockLogic.h"

// /api/status -> services and world clocks (host-tested; used by
// StatusClient). The dynamic arrays (backend api_version 2) are preferred;
// without them the legacy fields are read, so this firmware also works with
// an older backend. The legacy readers are a transition shim: they know the
// names older backends used and can go once those backends are gone.

// --- Services -------------------------------------------------------------------------

struct LegacyService {
  const char* id;
  const char* label;
};

// The services older firmware showed, in its order. Older backends send
// true/false for monitored services and null for the rest.
static const LegacyService LEGACY_SERVICES[] = {
  {"jellyfin", "Jellyfin"}, {"navidrome", "Navidrome"}, {"metube", "MeTube"},
  {"ollama", "Ollama"}, {"cloudflare", "Cloudflare"}, {"nextcloud", "Nextcloud"},
  {"immich", "Immich"}, {"technical_blog", "Technical Blog"},
};

inline int8_t serviceStatusFrom(JsonVariantConst value) {
  if (!value.is<bool>()) return SERVICE_UNKNOWN;
  return value.as<bool>() ? SERVICE_UP : SERVICE_DOWN;
}

// services_list: malformed items (not an object, bad or repeated id) are
// skipped, extra items dropped, a non-boolean status is unknown.
inline void parseServices(JsonVariantConst doc, ServiceState& out) {
  out.count = 0;
  out.received = true;
  JsonVariantConst list = doc["services_list"];
  if (list.is<JsonArrayConst>()) {
    for (JsonVariantConst item : list.as<JsonArrayConst>()) {
      if (out.count >= MAX_SERVICES) break;
      if (!item.is<JsonObjectConst>() || !item["id"].is<const char*>()) continue;
      const char* label = item["label"].is<const char*>() ? item["label"].as<const char*>() : nullptr;
      addService(out, item["id"].as<const char*>(), label, serviceStatusFrom(item["status"]));
    }
    return;
  }
  JsonVariantConst legacy = doc["services"];
  for (const LegacyService& service : LEGACY_SERVICES) {
    JsonVariantConst value = legacy[service.id];
    if (value.is<bool>()) addService(out, service.id, service.label, serviceStatusFrom(value));
  }
}

// --- World clocks ---------------------------------------------------------------------------

inline const char* legacyClockLabel(const char* key) {
  if (strcmp(key, "india") == 0) return "India";
  if (strcmp(key, "singapore") == 0) return "Singapore";
  if (strcmp(key, "london") == 0) return "London";
  return key;
}

// world_clocks: items without a label are skipped, extra items dropped.
// Legacy: every timezones entry except "local", skipping the "--:--"
// placeholders a newer backend sends for unconfigured legacy names.
inline void parseWorldClocks(JsonVariantConst doc, WorldClockState& out) {
  out.count = 0;
  JsonVariantConst list = doc["world_clocks"];
  if (list.is<JsonArrayConst>()) {
    for (JsonVariantConst item : list.as<JsonArrayConst>()) {
      if (out.count >= MAX_WORLD_CLOCKS) break;
      if (!item.is<JsonObjectConst>() || !item["label"].is<const char*>()) continue;
      const char* time = item["time"].is<const char*>() ? item["time"].as<const char*>() : nullptr;
      addWorldClock(out, item["label"].as<const char*>(), time);
    }
    return;
  }
  for (JsonPairConst entry : doc["timezones"].as<JsonObjectConst>()) {
    if (out.count >= MAX_WORLD_CLOCKS) break;
    const char* key = entry.key().c_str();
    JsonVariantConst value = entry.value();
    if (strcmp(key, "local") == 0 || !value.is<JsonObjectConst>()) continue;
    const char* time = value["time"].is<const char*>() ? value["time"].as<const char*>() : nullptr;
    if (!time || strcmp(time, "--:--") == 0) continue;
    const char* label = value["label"].is<const char*>() ? value["label"].as<const char*>() : legacyClockLabel(key);
    addWorldClock(out, label, time);
  }
}
