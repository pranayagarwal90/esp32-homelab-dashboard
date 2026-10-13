#pragma once
#include <stdint.h>

// Temporary setup hotspot (AP+STA, so the dashboard stays online when it is
// started from Settings). A phone joins a WPA2 hotspot whose random password
// is shown only on the display, opens http://192.168.4.1 and submits the
// Wi-Fi network. First run asks for nothing else; from Settings the page
// also has the optional HomeDeck Backend address (blank = none).
//
// The web server and captive DNS run in their own FreeRTOS task, so slow
// clients cannot block the main loop. The page validates everything first;
// a valid submission is passed to the main task through a queue. The main
// task joins the network via NetworkManager (Wi-Fi is saved only once
// joined), then saves the backend URL if it changed and, on first run or when
// the backend changed, restarts (the backend URL is fixed for a boot session).

enum class WifiSetupPhase : uint8_t {
  Off, Waiting, Connecting, Connected, Failed, Stopping, StartFailed,
  Restarting,  // Configuration saved; the device restarts shortly.
  NoChange,    // Valid submission that changed nothing.
  SaveFailed,  // NVS write failed; nothing applied.
  LocationSaved // Location page: a place was picked and saved.
};

// Result of the quick GET <backend>/api/status after saving.
enum class ServerCheck : uint8_t { NotChecked, Reachable, NotReachable };

// Main task. firstRun: Setup Mode at boot (Wi-Fi page; never closes by itself
// and always restarts after saving); otherwise the Settings page (Wi-Fi + the
// optional HomeDeck Backend). Each start sets the portal mode (SetupPage.h)
// that decides what "/" and every captive-portal probe show; a session that
// is already open switches to it (or, while busy joining/saving, refuses).
// Returns false if the hotspot or task could not start.
bool startWifiSetup(bool firstRun = false);
// Main task. The same hotspot for Settings > Location: the page searches for
// a place (DirectWeather) and saves the chosen one; Wi-Fi and backend are not
// offered there. Needs the device online (AP+STA).
bool startLocationSetup();
bool wifiSetupForLocation();
// Main task. Asynchronous: the hotspot closes once the setup task has exited.
void stopWifiSetup();
// Main task, every loop: forwards submissions, tracks the switch, saves,
// restarts, auto-stops.
void updateWifiSetup();

WifiSetupPhase wifiSetupPhase();
bool wifiSetupRunning();
const char* wifiSetupApSsid();
const char* wifiSetupApPassword();
// SSID being joined / last tried (never the password).
const char* wifiSetupTarget();
ServerCheck wifiSetupServerCheck();
// Milliseconds since the last setup page request or submission.
uint32_t wifiSetupIdleMs();
// Increments whenever the on-screen setup status should be redrawn.
uint32_t wifiSetupRevision();
