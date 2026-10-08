#pragma once
#include "DeviceSettings.h"

// NVS persistence for DeviceSettings (namespace "settings") and the optional
// saved Wi-Fi network (namespace "wifi"). Main task only, except
// bluetoothPreferenceAtBoot() which runs inside initArduino().

void loadSettings();
const DeviceSettings& settings();
// Edit through this reference, then call settingsChanged().
DeviceSettings& editSettings();
// Marks settings dirty; they are written ~3 s after the last change, or by
// flushSettings(). Writes happen only when the encoded bytes differ.
void settingsChanged();
void serviceSettingsStore();
void flushSettings();

struct WifiCredentials {
  char ssid[33];
  char password[65];
};

bool loadSavedWifi(WifiCredentials& out);
bool saveWifi(const WifiCredentials& credentials);
void forgetSavedWifi();
bool hasSavedWifi();

// True if Bluetooth controller memory was kept at boot (setting was ON then).
bool bluetoothReservedAtBoot();
