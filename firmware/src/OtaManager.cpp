#include <Arduino.h>
#include <ArduinoOTA.h>
#include <Update.h>
#include "OtaManager.h"
#include "OtaAnimation.h"

void setupOTA() {
  ArduinoOTA.setHostname(DEVICE_HOSTNAME);

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
  Serial.printf("OTA ready: %s\n", DEVICE_HOSTNAME);
}

bool handleOTA() {
  ArduinoOTA.handle();
  return otaAnimationService();
}
