#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string.h>

// Pure monitored-service list (host-tested). The backend owns which services
// exist and what they are called: /api/status `services_list` is
// [{id, label, status}] for the configured services only, and the SERVICES
// page, alerts and AI codes all work from that list. Fixed-size storage,
// no heap.

// Bounds: the list fits two SERVICES pages and its alert slots stay small.
constexpr int MAX_SERVICES = 16;
constexpr size_t SERVICE_ID_SIZE = 24;     // 23 characters: [a-z0-9_-].
constexpr size_t SERVICE_LABEL_SIZE = 24;  // 23 printable characters (one size-2 row).

constexpr int8_t SERVICE_UP = 1;
constexpr int8_t SERVICE_DOWN = 0;
constexpr int8_t SERVICE_UNKNOWN = -1;     // Shown grey; never an alert.

struct ServiceStatus {
  char id[SERVICE_ID_SIZE];        // Stable key (alerts, AI codes).
  char label[SERVICE_LABEL_SIZE];  // From the backend.
  int8_t status;
};

struct ServiceState {
  ServiceStatus items[MAX_SERVICES];
  uint8_t count = 0;
  bool received = false;  // A status has been parsed (SERVICES: "waiting" until then).
};

// Ids are keys in JSON and alert codes: lower-case letters, digits, '_' '-'.
inline bool validServiceId(const char* id) {
  if (!id || !id[0] || strlen(id) >= SERVICE_ID_SIZE) return false;
  for (const char* p = id; *p; p++) {
    if (!((*p >= 'a' && *p <= 'z') || (*p >= '0' && *p <= '9') || *p == '_' || *p == '-')) return false;
  }
  return true;
}

// Printable ASCII (others become '?'), cut to fit; empty -> the id.
inline void copyServiceLabel(char* out, const char* label, const char* id) {
  const char* text = label && label[0] ? label : id;
  size_t n = 0;
  for (const char* p = text; *p && n + 1 < SERVICE_LABEL_SIZE; p++) out[n++] = (*p >= ' ' && *p <= '~') ? *p : '?';
  out[n] = '\0';
}

inline int findService(const ServiceState& state, const char* id) {
  for (int i = 0; i < state.count; i++) {
    if (strcmp(state.items[i].id, id) == 0) return i;
  }
  return -1;
}

// Adds one entry; false (ignored) for an invalid or repeated id, or once the
// list is full (extra services are dropped, never overflow).
inline bool addService(ServiceState& state, const char* id, const char* label, int8_t status) {
  if (state.count >= MAX_SERVICES || !validServiceId(id) || findService(state, id) >= 0) return false;
  ServiceStatus& item = state.items[state.count++];
  strcpy(item.id, id);
  copyServiceLabel(item.label, label, id);
  item.status = status == SERVICE_UP || status == SERVICE_DOWN ? status : SERVICE_UNKNOWN;
  return true;
}

enum class ServiceDot : uint8_t { Online, Offline, Unknown };

inline ServiceDot serviceDot(int8_t status) {
  return status == SERVICE_UP ? ServiceDot::Online : status == SERVICE_DOWN ? ServiceDot::Offline : ServiceDot::Unknown;
}

// SERVICES page: eight 20 px rows from y 42 per page, above the bottom bar.
constexpr int SERVICES_PER_PAGE = 8;
constexpr int SERVICES_Y0 = 42;
constexpr int SERVICES_PITCH = 20;
