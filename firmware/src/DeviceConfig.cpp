#include <Arduino.h>
#include <Preferences.h>
#include "DeviceConfig.h"
#include "DevDefaults.h"
#include "SettingsStore.h"
#include "ProvisioningLogic.h"

static const char* DEVICE_NAMESPACE = "device";
static const char* BACKEND_KEY = "backend";

static char backend[BACKEND_URL_MAX] = ""; // Written only by loadDeviceConfig().
static BackendConfig config = BackendConfig::NotConfigured;

void loadDeviceConfig() {
  char stored[BACKEND_URL_MAX] = "";
  bool storedPresent = false, storedReadable = true;
  Preferences prefs;
  if (nvsNamespaceExists(DEVICE_NAMESPACE) && prefs.begin(DEVICE_NAMESPACE, true)) {
    storedPresent = prefs.isKey(BACKEND_KEY);
    if (storedPresent) {
      String value = prefs.getString(BACKEND_KEY);
      storedReadable = value.length() < sizeof(stored); // An overlong value is invalid.
      if (storedReadable) memcpy(stored, value.c_str(), value.length() + 1);
    }
    prefs.end();
  }

  char resolved[BACKEND_URL_MAX];
  BackendBootAction action = resolveBootBackend(storedPresent, storedReadable ? stored : nullptr,
                                                DEV_BACKEND_URL, resolved);
  if (action == BackendBootAction::Seed) {
    // Developer build on a device whose backend key was never written.
    if (saveBackendUrl(resolved)) Serial.println("[BACKEND] URL seeded from the developer default");
    else action = BackendBootAction::None;
  }
  config = backendConfigFor(action);
  if (config == BackendConfig::Configured) memcpy(backend, resolved, sizeof(backend));
  if (config == BackendConfig::Invalid) Serial.println("[BACKEND] Stored URL is not valid; not used");
  Serial.printf("[BACKEND] %s\n", backend[0] ? backend : "Not configured");
}

BackendConfig backendConfig() {
  return config;
}

bool backendConfigured() {
  return backendRequestsAllowed(config);
}

const char* backendUrl() {
  return backend;
}

bool saveBackendUrl(const char* normalizedUrl) {
  if (!validBackendUrl(normalizedUrl)) return false;
  Preferences prefs;
  if (!prefs.begin(DEVICE_NAMESPACE, false)) return false;
  bool ok = prefs.putString(BACKEND_KEY, normalizedUrl) == strlen(normalizedUrl);
  prefs.end();
  return ok;
}

bool clearBackendUrl() {
  Preferences prefs;
  if (!prefs.begin(DEVICE_NAMESPACE, false)) return false;
  prefs.putString(BACKEND_KEY, ""); // Returns 0 for "" either way: read it back.
  bool ok = prefs.isKey(BACKEND_KEY) && prefs.getString(BACKEND_KEY).length() == 0;
  prefs.end();
  return ok;
}

// Never an endpoint without a configured backend (joinBackendUrl also
// refuses an empty base).
bool apiUrl(char* out, size_t size, const char* path) {
  if (out && size) out[0] = '\0';
  return backendConfigured() && joinBackendUrl(out, size, backend, path);
}

bool photoUrl(char* out, size_t size, const char* fileName) {
  if (out && size) out[0] = '\0';
  return backendConfigured() && joinBackendUrl(out, size, backend, PHOTO_FILE_PATH, fileName);
}
