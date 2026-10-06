#pragma once
#include <stdint.h>

// Temporary Wi-Fi setup hotspot (AP+STA, so the dashboard stays online).
// A phone joins a WPA2 hotspot whose random password is shown only on the
// display, opens http://192.168.4.1 and submits SSID + password.
//
// The web server and captive DNS run in their own FreeRTOS task, so slow
// clients cannot block the main loop. Submitted credentials are passed to the
// main task through a queue; the main task performs the network switch via
// NetworkManager (never the setup task).

enum class WifiSetupPhase : uint8_t { Off, Waiting, Connecting, Connected, Failed, Stopping, StartFailed };

// Main task. Returns false if the hotspot or task could not be started.
bool startWifiSetup();
// Main task. Asynchronous: the hotspot closes once the setup task has exited.
void stopWifiSetup();
// Main task, every loop: forwards submissions, tracks the switch, auto-stops.
void updateWifiSetup();

WifiSetupPhase wifiSetupPhase();
bool wifiSetupRunning();
const char* wifiSetupApSsid();
const char* wifiSetupApPassword();
// SSID being joined / last tried (never the password).
const char* wifiSetupTarget();
// Increments whenever the on-screen setup status should be redrawn.
uint32_t wifiSetupRevision();
