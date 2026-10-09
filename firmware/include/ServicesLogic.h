#pragma once
#include <stdint.h>

// Pure SERVICES list: which health keys are shown and how a row's dot reads.
// Only services with real health checks; a missing key is unknown (grey),
// never offline.

enum ServiceId : uint8_t {
  SVC_JELLYFIN, SVC_NAVIDROME, SVC_METUBE, SVC_OLLAMA, SVC_CLOUDFLARE,
  SVC_NEXTCLOUD, SVC_IMMICH, SVC_TECH_BLOG, SVC_COUNT
};

struct ServiceInfo {
  const char* key;    // /api/status services key.
  const char* label;
};

static const ServiceInfo SERVICE_LIST[SVC_COUNT] = {
  {"jellyfin", "Jellyfin"}, {"navidrome", "Navidrome"}, {"metube", "MeTube"},
  {"ollama", "Ollama"}, {"cloudflare", "Cloudflare"}, {"nextcloud", "Nextcloud"},
  {"immich", "Immich"}, {"technical_blog", "Technical Blog"},
};

enum class ServiceDot : uint8_t { Online, Offline, Unknown };

inline ServiceDot serviceDot(uint8_t reported, uint8_t online, int id) {
  if (id < 0 || id >= SVC_COUNT || !(reported & (1u << id))) return ServiceDot::Unknown;
  return online & (1u << id) ? ServiceDot::Online : ServiceDot::Offline;
}

// Compact rows: 20 px apart from y 42, all eight above the BACK bar.
constexpr int SERVICES_Y0 = 42;
constexpr int SERVICES_PITCH = 20;
