#pragma once
#include <stddef.h>
#include "BackendUrl.h"

// Device configuration in NVS namespace "device" (separate from the
// "settings" display blob and the "wifi" network):
//   device/backend  dashboard backend base URL, "http://host[:port]".
//
// Loaded once by loadDeviceConfig() in setup(), before any worker task
// starts, and never modified in RAM afterwards: a change is saved to NVS and
// takes effect after a restart. So every task may read it without locking.

void loadDeviceConfig();

// The backend base URL for this boot, or "" if none is configured.
const char* backendUrl();
bool hasBackendUrl();
// Whether NVS held a value at boot (even an invalid one).
bool backendUrlStored();

// Saves an already-normalized URL to NVS (main task). Effective after restart.
bool saveBackendUrl(const char* normalizedUrl);
void clearBackendUrl();

// Endpoint URLs for this boot's backend; false (out = "") when no backend is
// configured or out is too small. Callable from any task.
bool apiUrl(char* out, size_t size, const char* path);
inline bool statusUrl(char* out, size_t size) { return apiUrl(out, size, API_STATUS_PATH); }
inline bool photosUrl(char* out, size_t size) { return apiUrl(out, size, API_PHOTOS_PATH); }
inline bool aiStatusUrl(char* out, size_t size) { return apiUrl(out, size, API_AI_STATUS_PATH); }
inline bool aiExplainUrl(char* out, size_t size) { return apiUrl(out, size, API_AI_EXPLAIN_PATH); }
bool photoUrl(char* out, size_t size, const char* fileName);
