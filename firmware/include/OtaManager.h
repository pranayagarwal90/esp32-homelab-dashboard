#pragma once

// ArduinoOTA as "homelab-display". The progress callbacks draw on the TFT, so
// handleOTA() must be called from loop() on the main task.
void setupOTA();
void handleOTA();
