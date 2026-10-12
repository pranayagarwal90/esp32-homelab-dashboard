#pragma once
#include <stdint.h>

// Standalone vs Full mode. Pure, host-tested.
//
// Wi-Fi is the only thing setup requires. The HomeDeck Backend is optional:
//   Setup       no Wi-Fi network: the setup hotspot (Wi-Fi only).
//   Standalone  Wi-Fi, no backend: the normal UI; local features work and
//               backend features show a friendly "not configured" state.
//   Full        Wi-Fi and a backend: everything, as before.
// One firmware; the mode follows the configuration (device/backend in NVS).
//
// "Not configured" is never "offline": a configured backend that does not
// answer stays Full mode with the usual OFFLINE / unavailable states, so the
// two can be told apart when troubleshooting.

// The backend configuration for this boot (DeviceConfig).
enum class BackendConfig : uint8_t {
  NotConfigured, // Nothing stored, or removed in Settings.
  Invalid,       // A stored address that is not valid: kept for the user to fix, never used.
  Configured     // A valid address: Full mode.
};

enum class DeviceMode : uint8_t { Setup, Standalone, Full };

// Setup is required for Wi-Fi only; the backend never decides it.
inline bool setupRequired(bool hasNetwork) {
  return !hasNetwork;
}

inline DeviceMode deviceMode(bool hasNetwork, BackendConfig backend) {
  if (setupRequired(hasNetwork)) return DeviceMode::Setup;
  return backend == BackendConfig::Configured ? DeviceMode::Full : DeviceMode::Standalone;
}

// Every backend request (status, photos, AI, update checks) asks this first:
// without a backend nothing is sent, not even to an empty or default address.
inline bool backendRequestsAllowed(BackendConfig backend) {
  return backend == BackendConfig::Configured;
}

// The boot log line (printed once).
inline const char* deviceModeLog(DeviceMode mode, BackendConfig backend) {
  switch (mode) {
    case DeviceMode::Setup:
      return backend == BackendConfig::NotConfigured ? "[MODE] Setup - no Wi-Fi network saved"
                                                     : "[MODE] Setup - no Wi-Fi network saved (backend kept)";
    case DeviceMode::Standalone:
      return backend == BackendConfig::Invalid ? "[MODE] Standalone - saved backend address is not valid"
                                               : "[MODE] Standalone - backend not configured";
    case DeviceMode::Full: return "[MODE] Full - backend configured";
  }
  return "";
}

// The LIVE / OFFLINE indicator: hidden without a backend (standalone is not
// an outage).
enum class BackendIndicator : uint8_t { Hidden, Live, Offline };

inline BackendIndicator backendIndicator(BackendConfig backend, bool online) {
  if (backend != BackendConfig::Configured) return BackendIndicator::Hidden;
  return online ? BackendIndicator::Live : BackendIndicator::Offline;
}

// Settings > HomeDeck Backend, "Status".
inline const char* backendStatusText(BackendConfig backend, bool online, bool wifiConnected) {
  switch (backend) {
    case BackendConfig::NotConfigured: return "Not configured (optional)";
    case BackendConfig::Invalid: return "Saved address is not valid";
    case BackendConfig::Configured: break;
  }
  return online ? "Responding" : wifiConnected ? "Not responding" : "Unknown (no Wi-Fi)";
}

// --- Friendly "not configured" copy ------------------------------------------------------
// title: size-2 text (at most 22 characters); hint: size-1 text (at most 48).

enum class BackendFeature : uint8_t { Services, ServerStats, Photos, Ai, Weather, Clocks, Updates, Alerts };

struct BackendNotice {
  const char* title;
  const char* hint;
};

constexpr const char* BACKEND_SETUP_HINT = "Optional: Settings > Connectivity > Backend";

inline BackendNotice backendNotice(BackendFeature feature) {
  switch (feature) {
    case BackendFeature::Services: return {"Not configured", "Services come from a HomeDeck Backend."};
    case BackendFeature::ServerStats: return {"Not configured", "Server stats come from a HomeDeck Backend."};
    case BackendFeature::Photos: return {"No photo source", "Photos come from a HomeDeck Backend."};
    case BackendFeature::Ai: return {"AI not configured", "AI answers come from a HomeDeck Backend."};
    case BackendFeature::Weather: return {"Weather not set up", "Weather comes from a HomeDeck Backend."};
    case BackendFeature::Clocks: return {"Not configured", "World clocks come from a HomeDeck Backend."};
    case BackendFeature::Updates: return {"Not configured", "Update service not configured"};
    case BackendFeature::Alerts: return {"No alerts", "Alerts come from a HomeDeck Backend."};
  }
  return {"", ""};
}
