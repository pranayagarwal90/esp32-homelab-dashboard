#pragma once
#include <string.h>
#include "LocationLogic.h"
#include "ProvisioningLogic.h"

// The setup page's form, rendered into any string type with operator+= for
// const char* and char (Arduino String on the device, std::string in host
// tests). Pure, host-tested.
//
// Two contexts, never sharing the backend rules:
//   first run (Setup Mode)  Wi-Fi network + password only; no backend field.
//   from Settings           the same plus the optional HomeDeck Backend field
//                           (never required: blank = no backend).

template <class Out>
void appendHtmlEscaped(Out& out, const char* text) {
  for (const char* p = text ? text : ""; *p; p++) {
    switch (*p) {
      case '&': out += "&amp;"; break;
      case '<': out += "&lt;"; break;
      case '>': out += "&gt;"; break;
      case '"': out += "&quot;"; break;
      case '\'': out += "&#39;"; break;
      default: out += *p; break;
    }
  }
}

struct SetupFormView {
  bool firstRun = true;                     // Setup Mode at boot.
  bool haveNetwork = false;                 // A network is saved (it may be kept).
  const ScannedNetwork* networks = nullptr; // Strongest first.
  int networkCount = 0;
  bool scanning = false;
  bool scanFailed = false;
  const char* ssid = "";                    // Re-fill after a failed or rejected submission.
  const char* backend = "";                 // Settings only: the current or re-filled address.
};

// From <form> to the "Refresh networks" paragraph.
template <class Out>
void renderSetupForm(Out& page, const SetupFormView& v) {
  const char* ssid = v.ssid ? v.ssid : "";
  int listed = findScannedNetwork(v.networks, v.networkCount, ssid, strlen(ssid));
  page += "<form method=post action=/save><label>Wi-Fi network<select name=net><option value=\"\">";
  page += v.networkCount ? (v.haveNetwork ? "Keep the current network" : "Choose a network")
          : v.scanning ? "Scanning... use Refresh networks"
          : v.scanFailed ? "Scan failed: Refresh, or type a name below" : "No networks found: type one below";
  page += "</option>";
  for (int i = 0; i < v.networkCount; i++) {
    page += "<option value=\"";
    appendHtmlEscaped(page, v.networks[i].ssid);
    page += i == listed ? "\" selected>" : "\">";
    appendHtmlEscaped(page, v.networks[i].ssid);
    page += " - ";
    page += signalLabel(v.networks[i].rssi);
    page += v.networks[i].open ? ", open" : "";
    page += "</option>";
  }
  page += "</select></label><label>Or type a network name (hidden or not listed)"
          "<input name=ssid maxlength=32 autocomplete=off autocapitalize=off autocorrect=off "
          "spellcheck=false value=\"";
  if (listed < 0) appendHtmlEscaped(page, ssid);
  page += "\"></label><label>Wi-Fi password (blank for an open network)"
          "<input name=pass type=password maxlength=63 autocomplete=off></label>";
  if (!v.firstRun) {
    page += "<label>HomeDeck Backend (optional)<input name=server maxlength=127 inputmode=url "
            "autocapitalize=off autocorrect=off spellcheck=false placeholder=\"http://server-ip:8090\" value=\"";
    appendHtmlEscaped(page, v.backend);
    page += "\"></label><small>Only if you run a HomeDeck Backend, for example http://server-ip:8090 "
            "(no /api/status). Leave blank to use HomeDeck on its own.</small>";
  }
  page += "<button>";
  page += v.firstRun ? "Connect" : "Save and connect";
  page += "</button></form><p>";
  page += v.scanning ? "Scanning for networks... <a href=/>reload</a> in a few seconds."
                     : "<a href=/?rescan=1>Refresh networks</a>";
  page += "</p>";
}

// The backend value a POST /save is validated with (validateSetupForm):
// first run never takes one, even if a client sends it, and from Settings a
// request without the field keeps the stored backend (nullptr = keep). Only a
// field that was sent from Settings can change or (blank) remove it.
inline const char* setupBackendArg(bool firstRun, bool fieldSent, const char* value) {
  return firstRun || !fieldSent ? nullptr : (value ? value : "");
}

// --- Location page (Settings > Location > SEARCH ON PHONE) ------------------------------
// Search by town, "town, state" or postal code; pick one of up to
// LOCATION_RESULTS_MAX results. No coordinates, timezones or service names
// are shown: the device resolves and stores those itself.

struct LocationPageView {
  bool wifiConnected = false;
  const char* current = "";                 // Saved location label, or "".
  const char* saved = "";                   // Just saved on this page, or "".
  const char* error = "";                   // A rejected search or pick, or "".
  LocationSearchState state = LocationSearchState::Idle;
  LocationSearchError searchError = LocationSearchError::None;
  const char* query = "";
  uint32_t generation = 0;
  const Location* results = nullptr;        // count entries.
  int count = 0;
};

inline const char* locationSearchErrorText(LocationSearchError error) {
  switch (error) {
    case LocationSearchError::NoWifi: return "HomeDeck is not connected to Wi-Fi, so it cannot search.";
    case LocationSearchError::NoTime: return "HomeDeck is still setting its clock. Try again in a minute.";
    case LocationSearchError::Network:
      return "Could not reach the location search. Check the internet connection and try again.";
    case LocationSearchError::Rejected: return "That search did not work. Try different words.";
    case LocationSearchError::None: break;
  }
  return "";
}

template <class Out>
void renderLocationPage(Out& page, const LocationPageView& v) {
  page += "<h3>Location</h3>";
  if (v.saved && v.saved[0]) {
    page += "<p><b>Location saved: ";
    appendHtmlEscaped(page, v.saved);
    page += ".</b> The clock and weather update in a moment. You can close this page.</p>";
  } else if (v.current && v.current[0]) {
    page += "<p>Current location: <b>";
    appendHtmlEscaped(page, v.current);
    page += "</b></p>";
  } else {
    page += "<p>Set your location for the local time and weather.</p>";
  }
  if (!v.wifiConnected) {
    page += "<p class=err><b>";
    page += locationSearchErrorText(LocationSearchError::NoWifi);
    page += " Set up Wi-Fi first.</b></p>";
  }
  if (v.error && v.error[0]) {
    page += "<p class=err><b>";
    appendHtmlEscaped(page, v.error);
    page += "</b></p>";
  }
  page += "<form method=post action=/location/search><label>Town, city or postal code"
          "<input name=q maxlength=63 autocomplete=off autocapitalize=words spellcheck=false value=\"";
  appendHtmlEscaped(page, v.query);
  page += "\"></label><small>For example: Springfield, IL or 10001 or London</small>"
          "<button>Search</button></form>";
  switch (v.state) {
    case LocationSearchState::Searching:
      // Re-check in two seconds (the device searches in the background).
      page += "<meta http-equiv=refresh content=\"2;url=/\"><p>Searching for &quot;";
      appendHtmlEscaped(page, v.query);
      page += "&quot;... <a href=/>Refresh</a></p>";
      break;
    case LocationSearchState::Failed:
      page += "<p class=err><b>";
      page += locationSearchErrorText(v.searchError);
      page += "</b></p>";
      break;
    case LocationSearchState::Done:
      if (v.count == 0) {
        page += "<p>No places found for &quot;";
        appendHtmlEscaped(page, v.query);
        page += "&quot;. Try another spelling, add the state or country, or use a postal code.</p>";
      }
      for (int i = 0; i < v.count; i++) {
        char label[LOCATION_NAME_MAX + 2 * LOCATION_REGION_MAX + 8];
        formatLocationLabel(v.results[i], label, sizeof(label));
        char hidden[96];
        snprintf(hidden, sizeof(hidden), "<input type=hidden name=g value=%lu><input type=hidden name=i value=%d>",
                 (unsigned long)v.generation, i);
        page += "<form method=post action=/location/use><p><b>";
        appendHtmlEscaped(page, label);
        page += "</b></p>";
        page += hidden;
        page += "<button>Use this location</button></form>";
      }
      break;
    case LocationSearchState::Idle:
      break;
  }
}

// --- Portal modes and routing -----------------------------------------------------------
// Why the hotspot was started decides what every page shows. Phones open "/"
// or a captive-portal probe (any path), so the mode, not the URL, picks the
// page: the device routes every request through portalRoute() and draws "/"
// with renderPortalRoot(). Runtime only; never stored.
//   FirstRunWifi    Setup Mode at boot: Wi-Fi network + password only.
//   WifiAndBackend  Settings (Wi-Fi CHANGE, HomeDeck Backend SET UP): Wi-Fi
//                   plus the optional HomeDeck Backend field.
//   Location        Settings > Location: the location search only.
enum class PortalMode : uint8_t { FirstRunWifi, WifiAndBackend, Location };

inline const char* portalModeName(PortalMode mode) {
  switch (mode) {
    case PortalMode::FirstRunWifi: return "Wi-Fi";
    case PortalMode::WifiAndBackend: return "Backend";
    case PortalMode::Location: return "Location";
  }
  return "";
}

enum class PortalAction : uint8_t {
  RootPage,        // GET /: the page for the mode.
  SaveWifi,        // POST /save (Wi-Fi modes only).
  LocationSearch,  // POST /location/search (Location mode only).
  LocationUse,     // POST /location/use (Location mode only).
  RedirectRoot     // Everything else (captive probes, unknown paths, a form
                   // from another mode): back to "/", nothing changes.
};

inline PortalAction portalRoute(PortalMode mode, bool post, const char* path) {
  if (!path) return PortalAction::RedirectRoot;
  bool location = mode == PortalMode::Location;
  if (!post && strcmp(path, "/") == 0) return PortalAction::RootPage;
  if (post && strcmp(path, "/save") == 0) return location ? PortalAction::RedirectRoot : PortalAction::SaveWifi;
  if (post && strcmp(path, "/location/search") == 0) {
    return location ? PortalAction::LocationSearch : PortalAction::RedirectRoot;
  }
  if (post && strcmp(path, "/location/use") == 0) {
    return location ? PortalAction::LocationUse : PortalAction::RedirectRoot;
  }
  return PortalAction::RedirectRoot;
}

// The body of "/" for the mode (after the page head). setup.firstRun is
// taken from the mode, so the two Wi-Fi pages can never be mixed up.
template <class Out>
void renderPortalRoot(Out& page, PortalMode mode, const SetupFormView& setup, const LocationPageView& location) {
  if (mode == PortalMode::Location) {
    renderLocationPage(page, location);
    return;
  }
  SetupFormView view = setup;
  view.firstRun = mode == PortalMode::FirstRunWifi;
  renderSetupForm(page, view);
}
