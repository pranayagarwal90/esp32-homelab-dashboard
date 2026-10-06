#pragma once

// Restart and sleep. The CYD has no software-controlled power switch, so true
// power-off is impossible; "Sleep" is ESP32 deep sleep with the display and
// backlight off. RST is the guaranteed wake/recovery method; a screen tap
// (XPT2046 PENIRQ on GPIO36) is attempted but not verified on this CYD.
// Waking is a fresh boot. Main task only.

// Saves pending settings, then reboots immediately.
void restartDevice();
// Starts the sleep sequence; deep sleep begins from updatePower() once the
// touch has been released (nonblocking until then).
void beginSleep();
bool sleepPending();
void updatePower();
// Logs why the chip booted (e.g. woken from Sleep by touch). Call from setup().
void logWakeReason();
