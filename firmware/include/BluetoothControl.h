#pragma once

// Bluetooth is a boot-time preference. The Arduino core asks btInUse()
// (overridden in SettingsStore.cpp) during startup: when it is false the
// controller memory is released and Bluetooth cannot start until the next
// boot. So toggling the setting takes effect after a restart; nothing is
// faked at runtime. Only the controller is started (no host stack, profiles,
// pairing or services).
//
// Compile-time opt-in: the controller library costs ~149 KB flash and ~5 KB
// static RAM even when switched off, so it is excluded unless the build sets
//   -D DASHBOARD_BLUETOOTH=1
// (e.g. under build_flags in platformio.ini). Without it the core's default
// applies (Bluetooth memory released at boot) and Settings shows that
// Bluetooth support is disabled in this build.
#ifndef DASHBOARD_BLUETOOTH
#define DASHBOARD_BLUETOOTH 0
#endif
constexpr bool BLUETOOTH_SUPPORTED = DASHBOARD_BLUETOOTH != 0;

// Main task, from setup(): starts the controller if enabled at boot.
void setupBluetooth();
bool bluetoothControllerEnabled();
// True when the stored setting differs from what this boot applied.
bool bluetoothRestartPending();
// Stops the controller before deep sleep (no-op when not running/compiled).
void stopBluetoothForSleep();
