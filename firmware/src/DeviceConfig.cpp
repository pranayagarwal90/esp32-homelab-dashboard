#include <Arduino.h>
#include <Preferences.h>
#include "DeviceConfig.h"
#include "DevDefaults.h"
#include "ProvisioningLogic.h"

static const char* DEVICE_NAMESPACE = "device";
static const char* BACKEND_KEY = "backend";

static char backend[BACKEND_URL_MAX] = ""; // Written only by loadDeviceConfig().
static bool backendStored = false;

void loadDeviceConfig() {
  char stored[BACKEND_URL_MAX] = "";
  Preferences prefs;
  if (prefs.begin(DEVICE_NAMESPACE, true)) {
    backendStored = prefs.isKey(BACKEND_KEY);
    // An overlong value reads as "" and is treated as invalid.
    if (backendStored) prefs.getString(BACKEND_KEY, stored, sizeof(stored));
    prefs.end();
  }

  char resolved[BACKEND_URL_MAX];
  switch (resolveBootBackend(backendStored, stored, DEV_BACKEND_URL, resolved)) {
    case BackendBootAction::Use:
      memcpy(backend, resolved, sizeof(backend));
      break;
    case BackendBootAction::Seed:
      // Developer build upgrading a device that predates device/backend.
      if (saveBackendUrl(resolved)) {
        memcpy(backend, resolved, sizeof(backend));
        backendStored = true;
        Serial.println("Backend URL seeded from the developer default");
      }
      break;
    case BackendBootAction::Setup:
      if (backendStored) Serial.println("Stored backend URL is invalid");
      break;
  }
  Serial.printf("Backend: %s\n", backend[0] ? backend : "(not configured)");
}

const char* backendUrl() {
  return backend;
}

bool hasBackendUrl() {
  return backend[0] != '\0';
}

bool backendUrlStored() {
  return backendStored;
}

bool saveBackendUrl(const char* normalizedUrl) {
  if (!validBackendUrl(normalizedUrl)) return false;
  Preferences prefs;
  if (!prefs.begin(DEVICE_NAMESPACE, false)) return false;
  bool ok = prefs.putString(BACKEND_KEY, normalizedUrl) == strlen(normalizedUrl);
  prefs.end();
  return ok;
}

void clearBackendUrl() {
  Preferences prefs;
  if (!prefs.begin(DEVICE_NAMESPACE, false)) return;
  prefs.remove(BACKEND_KEY);
  prefs.end();
}

bool apiUrl(char* out, size_t size, const char* path) {
  return joinBackendUrl(out, size, backend, path);
}

bool photoUrl(char* out, size_t size, const char* fileName) {
  return joinBackendUrl(out, size, backend, PHOTO_FILE_PATH, fileName);
}
