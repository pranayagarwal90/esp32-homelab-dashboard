#pragma once

// Optional developer defaults; never needed for a normal build. Copy to
// secrets.h (git-ignored). Used only when the build sets
// -D DASHBOARD_DEV_DEFAULTS (see platformio_override.ini.example and
// DevDefaults.h); normal builds are configured on the device's setup page.

// A built-in Wi-Fi network, tried alongside the one saved by the setup page.
#define WIFI_SSID "YOUR_WIFI_SSID"
#define WIFI_PASSWORD "YOUR_WIFI_PASSWORD"

// Optional: written to NVS once when the device has no dashboard server yet.
// #define DASHBOARD_DEV_BACKEND_URL "http://192.0.2.10:8090"
