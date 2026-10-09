#pragma once
#include <stdint.h>
#include "SettingsLogic.h"
#include "SettingsStore.h"

// Initial blocking connect; shows the boot (or wake) and Wi-Fi animations
// meanwhile (SystemAnimation). Main task, setup only. Tries the saved network
// (if any) and the built-in secrets.h network alternately until one connects;
// without a saved network this is the original built-in-only loop.
void connectWiFi();
// Reconnects if needed, waiting up to timeoutMs with vTaskDelay. Never draws;
// called from the status worker task. Returns false immediately while the
// main task is switching networks.
bool ensureWiFiConnected(uint32_t timeoutMs);

WifiSource activeWifiSource();

// Nonblocking network switch (main task). A request waits (Pending) while the
// status worker is mid-reconnect, then starts on a later updateWifiSwitch().
enum class WifiSwitchState : uint8_t { Idle, Pending, Connecting, Connected, Failed };
void requestWifiSwitch(const WifiCredentials& target, WifiSource targetSource);
// Call every loop. On success a Saved target is stored in NVS (a BuiltIn
// target forgets the saved network); on failure the previous network is
// restored. Returns the current state.
WifiSwitchState updateWifiSwitch();
WifiSwitchState wifiSwitchState();
void builtInWifiCredentials(WifiCredentials& out);
