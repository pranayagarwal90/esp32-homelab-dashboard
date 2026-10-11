#pragma once

// Developer-only defaults. A normal (public) build contains none: no SSID,
// password or backend address. They are compiled in only when the build sets
//   -D DASHBOARD_DEV_DEFAULTS
// (e.g. in the git-ignored firmware/platformio_override.ini), from:
//   - include/secrets.h (git-ignored, optional): WIFI_SSID / WIFI_PASSWORD, a
//     built-in network tried alongside the saved one;
//   - DASHBOARD_DEV_BACKEND_URL (a -D flag or secrets.h): written to NVS once
//     when no backend URL is stored, so an existing device upgrades in place.
// secrets.h is never required: without it the build has no built-in network.

#if defined(DASHBOARD_DEV_DEFAULTS) && __has_include("secrets.h")
#include "secrets.h"
#endif

#if defined(DASHBOARD_DEV_DEFAULTS) && defined(WIFI_SSID) && defined(WIFI_PASSWORD)
constexpr bool DEV_BUILTIN_WIFI = true;
constexpr const char* DEV_WIFI_SSID = WIFI_SSID;
constexpr const char* DEV_WIFI_PASSWORD = WIFI_PASSWORD;
#else
constexpr bool DEV_BUILTIN_WIFI = false;
constexpr const char* DEV_WIFI_SSID = "";
constexpr const char* DEV_WIFI_PASSWORD = "";
#endif

#if defined(DASHBOARD_DEV_DEFAULTS) && defined(DASHBOARD_DEV_BACKEND_URL)
constexpr const char* DEV_BACKEND_URL = DASHBOARD_DEV_BACKEND_URL;
#else
constexpr const char* DEV_BACKEND_URL = nullptr;
#endif
