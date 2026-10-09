#pragma once

// Restart and sleep. The CYD has no software-controlled power switch, so true
// power-off is impossible; "Sleep" is ESP32 deep sleep with the display and
// backlight off. RST is the guaranteed wake/recovery method; a screen tap
// (XPT2046 PENIRQ on GPIO36) is attempted but not verified on this CYD.
// Waking is a fresh boot. Main task only.

// Saves pending settings, plays the restart animation, then reboots (from
// updatePower()).
void restartDevice();
// Starts the sleep sequence: the good-night animation plays and deep sleep
// begins from updatePower() once it has finished and the touch has been
// released (nonblocking until then).
void beginSleep();
bool sleepPending();
// Call at the top of loop(). True while a restart or sleep sequence owns the
// display; loop() then skips everything else.
bool updatePower();
// Logs why the chip booted (e.g. woken from Sleep by touch). Call from setup().
void logWakeReason();
