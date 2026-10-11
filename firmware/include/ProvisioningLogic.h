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

// --- Network switch (setup page, USE BUILT-IN) ------------------------------------------
// requestWifiSwitch() only marks a switch Pending; updateWifiSwitch() starts
// it (WiFi.begin) and drives it to Connected or Failed. Whoever waits for a
// switch must keep calling it: updateWifiSetup() does, in Setup Mode and
// Settings alike.

enum class WifiSwitchState : uint8_t { Idle, Pending, Connecting, Connected, Failed };

constexpr uint32_t WIFI_SWITCH_TIMEOUT_MS = 20000;

// Joined = got an IP, the station reports connected, and on the requested SSID.
inline bool wifiSwitchJoined(bool gotIp, bool stationConnected, bool ssidMatches) {
  return gotIp && stationConnected && ssidMatches;
}

struct WifiSwitchStep {
  WifiSwitchState next;
  bool saveCredentials;  // Only after joining, and only for a saved-network switch.
  bool restorePrevious;  // Timed out: go back to the previous network (if any).
};

inline WifiSwitchStep wifiSwitchStep(WifiSwitchState state, bool joined, uint32_t elapsedMs, bool savedTarget) {
  if (state != WifiSwitchState::Connecting) return {state, false, false};
  if (joined) return {WifiSwitchState::Connected, savedTarget, false};
  if (elapsedMs >= WIFI_SWITCH_TIMEOUT_MS) return {WifiSwitchState::Failed, false, true};
  return {WifiSwitchState::Connecting, false, false};
}

// What the setup flow does once a submitted network switch reports back.
enum class SetupJoinOutcome : uint8_t { Waiting, Apply, ConnectedOnly, Failed };

inline SetupJoinOutcome setupAfterSwitch(WifiSwitchState state, bool firstRun, bool backendChanged) {
  switch (state) {
    case WifiSwitchState::Connected:
      return firstRun || backendChanged ? SetupJoinOutcome::Apply : SetupJoinOutcome::ConnectedOnly;
    case WifiSwitchState::Failed: return SetupJoinOutcome::Failed;
    default: return SetupJoinOutcome::Waiting; // Pending or Connecting.
  }
}

// --- Scanned networks (setup page dropdown) --------------------------------------------
// Scanned once when the hotspot starts and on "Refresh networks"; results are
// cached in a fixed array (no per-request Strings): hidden/empty and overlong
// SSIDs are skipped, a repeated SSID (several access points) keeps its
// strongest signal, the list is ordered strongest first and capped at
// SETUP_SCAN_MAX entries (the weakest are dropped).

constexpr int SETUP_SCAN_MAX = 15;
constexpr uint32_t SETUP_SCAN_TIMEOUT_MS = 15000;    // An unfinished scan is abandoned.
constexpr uint32_t SETUP_SCAN_START_DELAY_MS = 750;  // Let the hotspot settle first.

// Scan lifecycle (setup task). The scan is started directly and its end is
// the driver's scan-done event: the Arduino async wrapper's scanComplete()
// reports FAILED once 20 x max_ms_per_chan (6 s) have passed, which a scan
// with the hotspot active can exceed.
enum class ScanState : uint8_t { WaitingForAp, Idle, Running };
enum class ScanAction : uint8_t { None, Start, Collect, Timeout };

struct ScanMachine {
  ScanState state = ScanState::WaitingForAp;
  uint32_t since = 0;      // Hotspot start, or scan start.
  bool requested = true;   // The first scan; later ones come from Refresh.
};

inline void scanBegin(ScanMachine& m, uint32_t now) {
  m = ScanMachine();
  m.since = now;
}

// Refresh: false (ignored) while a scan is running.
inline bool scanRequest(ScanMachine& m) {
  if (m.state == ScanState::Running) return false;
  m.requested = true;
  return true;
}

// What to do now. doneEvent: the driver reported the scan finished; joining:
// a network switch is in progress (never scan then).
inline ScanAction scanStep(ScanMachine& m, uint32_t now, bool doneEvent, bool joining) {
  if (m.state == ScanState::WaitingForAp) {
    if (now - m.since < SETUP_SCAN_START_DELAY_MS) return ScanAction::None;
    m.state = ScanState::Idle; // Then start right away if requested.
  }
  if (m.state == ScanState::Idle) return m.requested && !joining ? ScanAction::Start : ScanAction::None;
  if (doneEvent) {
    m.state = ScanState::Idle;
    return ScanAction::Collect;
  }
  if (now - m.since >= SETUP_SCAN_TIMEOUT_MS) {
    m.state = ScanState::Idle;
    return ScanAction::Timeout;
  }
  return ScanAction::None;
}

// After a Start: started or not, the request is consumed (Refresh retries).
inline void scanStarted(ScanMachine& m, bool ok, uint32_t now) {
  m.requested = false;
  if (ok) {
    m.state = ScanState::Running;
    m.since = now;
  }
}

// "Scanning..." on the page: running, or about to start the first scan.
inline bool scanBusy(const ScanMachine& m) {
  return m.state == ScanState::Running || (m.state == ScanState::WaitingForAp && m.requested);
}

// The cached list is replaced only by a scan that completed successfully;
// a failed or timed-out scan keeps the previous list.
inline bool scanReplacesList(ScanAction action, bool driverSuccess) {
  return action == ScanAction::Collect && driverSuccess;
}

struct ScannedNetwork {
  char ssid[33];
  int8_t rssi;
  bool open;
};

inline int findScannedNetwork(const ScannedNetwork* list, int count, const char* ssid, size_t length) {
  for (int i = 0; i < count; i++) {
    if (strlen(list[i].ssid) == length && memcmp(list[i].ssid, ssid, length) == 0) return i;
  }
  return -1;
}

// Adds one scan result to list (count entries, room for max). Returns the new count.
inline int addScannedNetwork(ScannedNetwork* list, int count, int max, const char* ssid, size_t length,
                             int rssi, bool open) {
  if (!ssid || length == 0 || length > 32 || max <= 0) return count;
  if (rssi < -128) rssi = -128;
  if (rssi > 0) rssi = 0;
  int existing = findScannedNetwork(list, count, ssid, length);
  if (existing >= 0) {
    if (rssi <= list[existing].rssi) return count; // Keep the stronger access point.
    for (int i = existing; i + 1 < count; i++) list[i] = list[i + 1];
    count--;
  }
  int position = 0;
  while (position < count && list[position].rssi >= rssi) position++;
  if (position >= max) return count;              // Weaker than a full list.
  if (count == max) count--;                      // Drop the weakest.
  for (int i = count; i > position; i--) list[i] = list[i - 1];
  ScannedNetwork& entry = list[position];
  memcpy(entry.ssid, ssid, length);
  entry.ssid[length] = '\0';
  entry.rssi = int8_t(rssi);
  entry.open = open;
  return count + 1;
}

inline const char* signalLabel(int rssi) {
  return rssi >= -60 ? "strong" : rssi >= -72 ? "good" : "weak";
}

// The network the user chose: a deliberately typed name wins over the
// dropdown; otherwise the dropdown; otherwise none (length 0).
inline const char* chooseSetupSsid(const char* typed, size_t typedLength, const char* selected,
                                   size_t selectedLength, size_t& length) {
  if (typed && typedLength > 0) {
    length = typedLength;
    return typed;
  }
  length = selected ? selectedLength : 0;
  return selected ? selected : "";
}

// --- Setup form ---------------------------------------------------------------------

enum class SetupFormError : uint8_t { None, NeedNetwork, PasswordWithoutNetwork, Wifi, Backend, NeedPassword };

struct SetupForm {
  bool changeWifi = false;                // False: keep the current network.
  BackendUrlError backendError = BackendUrlError::None;
  char backend[BACKEND_URL_MAX] = "";     // Normalized.
};

// Validates everything before anything is saved. Wi-Fi fields may be left
// blank only when a network already exists (keep it); the backend is always
// required (the page pre-fills the current one). A blank password joins an
// open network; securedNetwork (the scan says it is not open) requires one.
inline SetupFormError validateSetupForm(const char* ssid, size_t ssidLength, size_t passwordLength,
                                        const char* backend, bool haveNetwork, SetupForm& out,
                                        bool securedNetwork = false) {
  out = SetupForm();
  if (ssidLength == 0) {
    if (passwordLength > 0) return SetupFormError::PasswordWithoutNetwork;
    if (!haveNetwork) return SetupFormError::NeedNetwork;
  } else if (securedNetwork && passwordLength == 0) {
    return SetupFormError::NeedPassword;
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
    case SetupFormError::NeedNetwork: return "Choose a Wi-Fi network or type its name.";
    case SetupFormError::NeedPassword: return "This network is secured: enter its password.";
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
