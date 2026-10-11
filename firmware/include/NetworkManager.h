#pragma once
#include <stdint.h>
#include "SettingsLogic.h"
#include "ProvisioningLogic.h"
#include "SettingsStore.h"

// Main task, setup only, before anything else here: loads the saved network
// (NVS wifi/ssid, wifi/pass) and prepares the station.
void initNetwork();
// True if there is a network to join: the saved one, or the built-in one of a
// developer build (DevDefaults.h). Public builds have only the saved one.
bool hasWifiNetwork();
bool hasBuiltInWifi();
// Blocking boot connect; shows the boot (or wake) and Wi-Fi animations
// meanwhile (SystemAnimation). Main task, setup only. Tries the saved network
// and the built-in one (if both exist) alternately, BOOT_CONNECT_ATTEMPTS
// times; false if none connected (the caller starts Setup Mode). Never erases
// credentials.
bool connectWiFi();
// Reconnects if needed, waiting up to timeoutMs with vTaskDelay. Never draws;
// called from the status worker task. Returns false immediately while the
// main task is switching networks.
bool ensureWiFiConnected(uint32_t timeoutMs);

WifiSource activeWifiSource();

// Nonblocking network switch (main task). A request waits (Pending) while the
// status worker is mid-reconnect, then starts on a later updateWifiSwitch().
// WifiSwitchState and its transitions are in ProvisioningLogic.h.
void requestWifiSwitch(const WifiCredentials& target, WifiSource targetSource);
// Call every loop. On success a Saved target is stored in NVS (a BuiltIn
// target forgets the saved network); on failure the previous network is
// restored. Returns the current state.
WifiSwitchState updateWifiSwitch();
WifiSwitchState wifiSwitchState();
// The developer build's built-in network; false (out zeroed) in public builds.
bool builtInWifiCredentials(WifiCredentials& out);
