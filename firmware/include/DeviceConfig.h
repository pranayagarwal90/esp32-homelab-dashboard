#pragma once
#include <stddef.h>
#include "BackendMode.h"
#include "BackendUrl.h"

// Device configuration in NVS namespace "device" (separate from the
// "settings" display blob and the "wifi" network):
//   device/backend  HomeDeck Backend base URL, "http://host[:port]", or ""
//                   once removed in Settings. Optional (BackendMode.h).
//
// Loaded once by loadDeviceConfig() in setup(), before any worker task
// starts, and never modified in RAM afterwards: a change is saved to NVS and
// takes effect after a restart. So every task may read it without locking.
// Wi-Fi lives in its own namespace: changing one never touches the other.

void loadDeviceConfig();

// This boot's backend configuration. backendConfigured() is the one check
// every backend feature uses; without it nothing is requested.
BackendConfig backendConfig();
bool backendConfigured();
// The backend base URL for this boot, or "" unless configured.
const char* backendUrl();

// Saves an already-normalized URL to NVS (main task). Effective after restart.
bool saveBackendUrl(const char* normalizedUrl);
// Removes the backend (main task): stores "" so that a developer build's
// default is not seeded again. Wi-Fi is untouched. Effective after restart.
bool clearBackendUrl();

// Endpoint URLs for this boot's backend; false (out = "") when no backend is
// configured or out is too small. Callable from any task.
bool apiUrl(char* out, size_t size, const char* path);
inline bool statusUrl(char* out, size_t size) { return apiUrl(out, size, API_STATUS_PATH); }
inline bool photosUrl(char* out, size_t size) { return apiUrl(out, size, API_PHOTOS_PATH); }
inline bool aiStatusUrl(char* out, size_t size) { return apiUrl(out, size, API_AI_STATUS_PATH); }
inline bool aiExplainUrl(char* out, size_t size) { return apiUrl(out, size, API_AI_EXPLAIN_PATH); }
bool photoUrl(char* out, size_t size, const char* fileName);
