#pragma once

constexpr const char* DEVICE_HOSTNAME = "homelab-display";

// ArduinoOTA as "homelab-display". The progress callbacks draw on the TFT, so
// handleOTA() must be called from loop() on the main task. It returns true
// while a failed update's error screen owns the display; loop() then skips
// everything else (touch, screensaver, status redraws) until it is released.
void setupOTA();
bool handleOTA();
