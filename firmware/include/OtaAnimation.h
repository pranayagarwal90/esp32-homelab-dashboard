#pragma once
#include <stdint.h>

// Walking-man OTA screen. Called from the ArduinoOTA callbacks, which run on
// the main task inside handleOTA(); loop() is stalled for the whole upload,
// so nothing else draws meanwhile.
void otaAnimationBegin();
void otaAnimationProgress(uint32_t progress, uint32_t total);
void otaAnimationComplete();
void otaAnimationError(int errorCode, const char* detail);
// From loop(): true while the error screen is held (it owns the display);
// afterwards the previous page is redrawn once.
bool otaAnimationService();
