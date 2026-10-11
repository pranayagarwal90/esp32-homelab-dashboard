#pragma once
#include <stddef.h>
#include <stdint.h>
#include "BackendUrl.h"
#include "SettingsLogic.h"

// First-run provisioning decisions. Pure, host-tested.
//
// Boot: the device needs a Wi-Fi network (saved in NVS, or a developer
// build's built-in one) and a valid backend URL (NVS device/backend). If
// either is missing, or the network cannot be joined, it starts Setup Mode
// (the setup hotspot) instead of the dashboard. Whether the backend answers
// is never a reason for setup: that depends on configuration only.

enum class SetupReason : uint8_t {
  None,         // Normal dashboard.
  NoConfig,     // Fresh device: no network and no backend.
  NoWifi,       // Backend set, no network.
  NoBackend,    // Network set (e.g. older firmware), no backend.
  BadBackend,   // The stored backend URL is not valid.
  WifiFailed    // Configured, but the network could not be joined at boot.
};

inline const char* setupReasonText(SetupReason reason) {
  switch (reason) {
    case SetupReason::NoConfig: return "This display is not set up yet.";
    case SetupReason::NoWifi: return "No Wi-Fi network is saved.";
    case SetupReason::NoBackend: return "The dashboard server is not set.";
    case SetupReason::BadBackend: return "The saved dashboard server is not valid.";
    case SetupReason::WifiFailed: return "Could not connect to Wi-Fi.";
    case SetupReason::None: break;
  }
  return "";
}

// Stored backend URL at boot: use it, seed it once from a developer build's
// fallback (only when nothing is stored), or require setup. A stored but
// invalid value is never overwritten automatically.
enum class BackendBootAction : uint8_t { Use, Seed, Setup };

inline BackendBootAction backendBootAction(bool storedPresent, bool storedValid, bool fallbackValid) {
  if (storedPresent && storedValid) return BackendBootAction::Use;
  if (!storedPresent && fallbackValid) return BackendBootAction::Seed;
  return BackendBootAction::Setup;
}

// The backend URL for this boot from what NVS held (storedPresent/stored) and
// a developer fallback (nullptr in public builds). out receives the URL to use
// ("" for Setup); a Seed result must also be written to NVS by the caller.
inline BackendBootAction resolveBootBackend(bool storedPresent, const char* stored, const char* fallback,
                                            char (&out)[BACKEND_URL_MAX]) {
  char normalizedStored[BACKEND_URL_MAX];
  char normalizedFallback[BACKEND_URL_MAX];
  bool storedValid = storedPresent && stored &&
      normalizeBackendUrl(stored, normalizedStored, sizeof(normalizedStored)) == BackendUrlError::None;
  bool fallbackValid = fallback &&
      normalizeBackendUrl(fallback, normalizedFallback, sizeof(normalizedFallback)) == BackendUrlError::None;
  BackendBootAction action = backendBootAction(storedPresent, storedValid, fallbackValid);
  out[0] = '\0';
  if (action == BackendBootAction::Use) memcpy(out, normalizedStored, sizeof(out));
  if (action == BackendBootAction::Seed) memcpy(out, normalizedFallback, sizeof(out));
  return action;
}

// Before connecting: is anything missing?
inline SetupReason bootSetupReason(bool hasNetwork, bool backendPresent, bool backendValid) {
  if (!hasNetwork) return backendValid ? SetupReason::NoWifi : SetupReason::NoConfig;
  if (!backendValid) return backendPresent ? SetupReason::BadBackend : SetupReason::NoBackend;
  return SetupReason::None;
}

// Boot connect: BOOT_CONNECT_ATTEMPTS attempts of BOOT_ATTEMPT_MS (alternating
// saved and built-in networks when both exist), then Setup Mode.
constexpr uint32_t BOOT_ATTEMPT_MS = 15000;
constexpr uint8_t BOOT_CONNECT_ATTEMPTS = 4;

// Setup Mode after a failed connect retries by restarting once nobody has
// used the setup page for SETUP_RETRY_IDLE_MS (e.g. the router was still
// booting after a power cut). Credentials are never erased.
constexpr uint32_t SETUP_RETRY_IDLE_MS = 5UL * 60UL * 1000UL;

inline bool setupShouldRetry(SetupReason reason, bool hotspotIdle, uint32_t idleMs) {
  return reason == SetupReason::WifiFailed && hotspotIdle && idleMs >= SETUP_RETRY_IDLE_MS;
}

// --- Setup form ---------------------------------------------------------------------

enum class SetupFormError : uint8_t { None, NeedNetwork, PasswordWithoutNetwork, Wifi, Backend };

struct SetupForm {
  bool changeWifi = false;                // False: keep the current network.
  BackendUrlError backendError = BackendUrlError::None;
  char backend[BACKEND_URL_MAX] = "";     // Normalized.
};

// Validates everything before anything is saved. Wi-Fi fields may be left
// blank only when a network already exists (keep it); the backend is always
// required (the page pre-fills the current one).
inline SetupFormError validateSetupForm(const char* ssid, size_t ssidLength, size_t passwordLength,
                                        const char* backend, bool haveNetwork, SetupForm& out) {
  out = SetupForm();
  if (ssidLength == 0) {
    if (passwordLength > 0) return SetupFormError::PasswordWithoutNetwork;
    if (!haveNetwork) return SetupFormError::NeedNetwork;
  } else if (!validWifiInput(ssid, ssidLength, passwordLength)) {
    return SetupFormError::Wifi;
  }
  out.backendError = normalizeBackendUrl(backend, out.backend, sizeof(out.backend));
  if (out.backendError != BackendUrlError::None) {
    out.backend[0] = '\0';
    return SetupFormError::Backend;
  }
  out.changeWifi = ssidLength > 0;
  return SetupFormError::None;
}

inline const char* setupFormErrorText(SetupFormError error, BackendUrlError backendError) {
  switch (error) {
    case SetupFormError::NeedNetwork: return "Enter the Wi-Fi network name.";
    case SetupFormError::PasswordWithoutNetwork: return "Enter the network name for this password.";
    case SetupFormError::Wifi:
      return "Network name must be 1-32 characters and the password 8-63 characters (or blank).";
    case SetupFormError::Backend: return backendUrlErrorText(backendError);
    case SetupFormError::None: break;
  }
  return "";
}

// After a valid submission is applied: restart (the backend URL is fixed for
// a boot session) on first run or when the backend changed.
inline bool setupNeedsRestart(bool firstRun, bool backendChanged) {
  return firstRun || backendChanged;
}
