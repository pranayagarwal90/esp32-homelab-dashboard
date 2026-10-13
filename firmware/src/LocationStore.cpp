#include <Arduino.h>
#include <Preferences.h>
#include "LocationStore.h"
#include "LocationNvs.h"
#include "SettingsStore.h"

static const char* LOCATION_NAMESPACE = "location";

static portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;
static Location stored;            // Guarded by lock.
static bool configured = false;    // Guarded by lock.
static volatile uint32_t revision = 0;

static void setRam(const Location* location) {
  portENTER_CRITICAL(&lock);
  configured = location != nullptr;
  stored = location ? *location : Location();
  portEXIT_CRITICAL(&lock);
}

void loadLocation() {
  Location l;
  bool ok = false;
  Preferences prefs;
  if (nvsNamespaceExists(LOCATION_NAMESPACE) && prefs.begin(LOCATION_NAMESPACE, true)) {
    ok = readLocation(prefs, l);
    prefs.end();
  }
  setRam(ok ? &l : nullptr);
  if (ok) {
    Serial.printf("[LOCATION] %s (%s)\n", l.name, l.offsetKnown ? "offset known" : "offset pending");
  } else {
    Serial.println("[LOCATION] Not configured");
  }
}

bool locationConfigured() {
  portENTER_CRITICAL(&lock);
  bool result = configured;
  portEXIT_CRITICAL(&lock);
  return result;
}

bool currentLocation(Location& out) {
  portENTER_CRITICAL(&lock);
  bool result = configured;
  out = stored;
  portEXIT_CRITICAL(&lock);
  return result;
}

uint32_t locationRevision() {
  return revision;
}

bool saveLocation(const Location& input) {
  Location l = input;
  l.offsetKnown = false; // The first forecast for this place supplies it.
  l.utcOffset = 0;
  if (!validLocation(l)) return false;
  Preferences prefs;
  if (!prefs.begin(LOCATION_NAMESPACE, false)) return false;
  bool ok = writeLocation(prefs, l);
  prefs.end();
  if (!ok) {
    Serial.println("[LOCATION] NOT saved (NVS write failed)");
    return false;
  }
  setRam(&l);
  revision = revision + 1;
  Serial.printf("[LOCATION] Saved: %s\n", l.name);
  return true;
}

bool clearLocation() {
  Preferences prefs;
  if (!prefs.begin(LOCATION_NAMESPACE, false)) return false;
  bool ok = prefs.clear(); // This namespace only: Wi-Fi and backend are elsewhere.
  prefs.end();
  if (!ok) return false;
  setRam(nullptr);
  revision = revision + 1;
  Serial.println("[LOCATION] Removed");
  return true;
}

bool locationUtcOffset(int32_t& out) {
  portENTER_CRITICAL(&lock);
  bool known = configured && stored.offsetKnown;
  out = stored.utcOffset;
  portEXIT_CRITICAL(&lock);
  return known;
}

bool saveLocationOffset(int32_t seconds) {
  if (!validUtcOffset(seconds)) return false;
  int32_t current = 0;
  bool known = locationUtcOffset(current);
  if (!locationConfigured()) return false;
  if (known && current == seconds) return true;
  Preferences prefs;
  if (!prefs.begin(LOCATION_NAMESPACE, false)) return false;
  bool ok = writeLocationOffset(prefs, seconds);
  prefs.end();
  if (!ok) return false;
  portENTER_CRITICAL(&lock);
  if (configured) {
    stored.offsetKnown = true;
    stored.utcOffset = seconds;
  }
  portEXIT_CRITICAL(&lock);
  char text[16];
  formatUtcOffset(seconds, text, sizeof(text));
  Serial.printf("[TIME] Local offset %s%s\n", text, known ? " (changed)" : "");
  return true;
}
