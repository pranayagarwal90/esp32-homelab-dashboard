#include <Arduino.h>
#include <Preferences.h>
#include <nvs.h>
#include "SettingsStore.h"
#include "BluetoothControl.h"

static const char* SETTINGS_NAMESPACE = "settings";
static const char* SETTINGS_KEY = "device";
static const char* WIFI_NAMESPACE = "wifi";
static constexpr uint32_t SAVE_DELAY_MS = 3000;

static DeviceSettings current;
static uint8_t savedBlob[SETTINGS_BLOB_MAX];
static size_t savedLength = 0;
static bool dirty = false;
static unsigned long lastChange = 0;
static bool btReserved = false;

#if DASHBOARD_BLUETOOTH
// Called by the Arduino core from initArduino(), after nvs_flash_init() and
// before setup(). Overrides the core's weak default (false), which releases
// the Bluetooth controller memory for good. Returning the stored preference
// keeps that memory only when Bluetooth is enabled. Without DASHBOARD_BLUETOOTH
// the core's default applies, exactly as before Settings existed.
extern "C" bool btInUse() {
  nvs_handle_t handle;
  if (nvs_open(SETTINGS_NAMESPACE, NVS_READONLY, &handle) != ESP_OK) return false;
  uint8_t blob[SETTINGS_BLOB_MAX];
  size_t length = sizeof(blob);
  bool enabled = false;
  if (nvs_get_blob(handle, SETTINGS_KEY, blob, &length) == ESP_OK) {
    DeviceSettings stored;
    decodeSettings(blob, length, stored);
    enabled = stored.bluetoothEnabled;
  }
  nvs_close(handle);
  btReserved = enabled;
  return enabled;
}
#endif

void loadSettings() {
  Preferences prefs;
  uint8_t blob[SETTINGS_BLOB_MAX];
  size_t length = 0;
  if (prefs.begin(SETTINGS_NAMESPACE, true)) {
    if (prefs.isKey(SETTINGS_KEY)) length = prefs.getBytes(SETTINGS_KEY, blob, sizeof(blob));
    prefs.end();
  }
  bool valid = decodeSettings(length ? blob : nullptr, length, current);
  if (length && !valid) Serial.println("Stored settings invalid; using defaults for bad fields");
  // Remember what is on flash so unchanged settings are never rewritten. A
  // repaired blob differs and is rewritten on the next change only.
  memcpy(savedBlob, blob, length);
  savedLength = length;
}

const DeviceSettings& settings() {
  return current;
}

DeviceSettings& editSettings() {
  return current;
}

void settingsChanged() {
  dirty = true;
  lastChange = millis();
}

void flushSettings() {
  if (!dirty) return;
  dirty = false;
  uint8_t blob[SETTINGS_BLOB_MAX];
  size_t length = encodeSettings(current, blob);
  if (length == savedLength && memcmp(blob, savedBlob, length) == 0) return;
  Preferences prefs;
  if (!prefs.begin(SETTINGS_NAMESPACE, false)) {
    Serial.println("Settings save failed: NVS unavailable");
    return;
  }
  if (prefs.putBytes(SETTINGS_KEY, blob, length) == length) {
    memcpy(savedBlob, blob, length);
    savedLength = length;
  } else {
    Serial.println("Settings save failed");
  }
  prefs.end();
}

void serviceSettingsStore() {
  if (dirty && millis() - lastChange >= SAVE_DELAY_MS) flushSettings();
}

bool loadSavedWifi(WifiCredentials& out) {
  memset(&out, 0, sizeof(out));
  Preferences prefs;
  if (!prefs.begin(WIFI_NAMESPACE, true)) return false;
  bool ok = prefs.isKey("ssid") && prefs.getString("ssid", out.ssid, sizeof(out.ssid)) > 0;
  if (ok && prefs.isKey("pass")) prefs.getString("pass", out.password, sizeof(out.password));
  prefs.end();
  if (!ok || out.ssid[0] == '\0') {
    memset(&out, 0, sizeof(out));
    return false;
  }
  return true;
}

bool saveWifi(const WifiCredentials& credentials) {
  Preferences prefs;
  if (!prefs.begin(WIFI_NAMESPACE, false)) return false;
  bool ok = prefs.putString("ssid", credentials.ssid) > 0;
  // An empty password (open network) stores zero bytes, which is not an error.
  prefs.putString("pass", credentials.password);
  prefs.end();
  return ok;
}

void forgetSavedWifi() {
  Preferences prefs;
  if (!prefs.begin(WIFI_NAMESPACE, false)) return;
  prefs.clear();
  prefs.end();
}

bool hasSavedWifi() {
  WifiCredentials credentials;
  return loadSavedWifi(credentials);
}

bool bluetoothReservedAtBoot() {
  return btReserved;
}
