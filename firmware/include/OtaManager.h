#pragma once

// Developer-only network upload (ArduinoOTA / espota, used by
// deploy-esp32.sh). Compiled in only with -D HOMEDECK_DEV_OTA, which the
// git-ignored platformio_override.ini sets for our own builds; public builds
// have no upload listener at all and update by pulling a release from their
// dashboard server (FirmwareUpdate). The developer listener has no password:
// it is for a trusted development LAN only.
//
// With HOMEDECK_DEV_OTA, ArduinoOTA runs as the device hostname
// (homedeck-mini-xxxx). The progress callbacks draw on the TFT, so
// handleOTA() must be called from loop() on the main task. It returns true
// while a failed update's error screen owns the display; loop() then skips
// everything else (touch, screensaver, status redraws) until it is released.
// Without it both are no-ops (handleOTA() returns false).
void setupOTA();
bool handleOTA();
