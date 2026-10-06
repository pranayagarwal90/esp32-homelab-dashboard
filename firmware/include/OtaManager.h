#pragma once

constexpr const char* DEVICE_HOSTNAME = "homelab-display";

// ArduinoOTA as "homelab-display". The progress callbacks draw on the TFT, so
// handleOTA() must be called from loop() on the main task.
void setupOTA();
void handleOTA();
