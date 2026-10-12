#include <Arduino.h>
#include "OtaManager.h"

#ifdef HOMEDECK_DEV_OTA

#include <ArduinoOTA.h>
#include <Update.h>
#include "DeviceIdentity.h"
#include "OtaAnimation.h"

void setupOTA() {
  ArduinoOTA.setHostname(deviceIdentity().hostname);

  // The upload runs inside ArduinoOTA.handle(); these callbacks draw the
  // walking-man screen (OtaAnimation) while loop() is stalled.
  ArduinoOTA.onStart([]() {
    Serial.println("OTA start");
    otaAnimationBegin();
  });

  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    otaAnimationProgress(progress, total);
  });

  ArduinoOTA.onEnd([]() {
    Serial.println("OTA complete");
    otaAnimationComplete();
  });

  ArduinoOTA.onError([](ota_error_t error) {
    const char* detail = Update.hasError() ? Update.errorString() : "";
    Serial.printf("OTA error: %u %s\n", error, detail);
    otaAnimationError(error, detail);
  });

  ArduinoOTA.begin();
  Serial.printf("Developer OTA ready: %s\n", deviceIdentity().hostname);
}

bool handleOTA() {
  ArduinoOTA.handle();
  return otaAnimationService();
}

#else

// Public build: no network upload listener.
void setupOTA() {}

bool handleOTA() {
  return false;
}

#endif
