#pragma once
#include <string.h>
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
