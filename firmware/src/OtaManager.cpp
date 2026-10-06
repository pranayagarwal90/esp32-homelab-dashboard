#include <Arduino.h>
#include <ArduinoOTA.h>
#include "OtaManager.h"
#include "Display.h"

void setupOTA() {
  ArduinoOTA.setHostname(DEVICE_HOSTNAME);

  ArduinoOTA.onStart([]() {
    tft.fillScreen(TFT_BLACK);
    tft.setTextSize(2);
    tft.setTextColor(TFT_CYAN, TFT_BLACK);
    tft.setCursor(65, 70);
    tft.print("OTA UPDATE");
  });

  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    int percent = (progress * 100U) / total;
    tft.fillRect(30, 120, 260, 50, TFT_BLACK);
    tft.setTextSize(3);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setCursor(115, 125);
    tft.printf("%d%%", percent);
  });

  ArduinoOTA.onEnd([]() {
    tft.fillScreen(TFT_BLACK);
    tft.setTextSize(2);
    tft.setTextColor(TFT_GREEN, TFT_BLACK);
    tft.setCursor(70, 100);
    tft.print("COMPLETE");
  });

  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("OTA error: %u\n", error);
  });

  ArduinoOTA.begin();
  Serial.printf("OTA ready: %s\n", DEVICE_HOSTNAME);
}

void handleOTA() {
  ArduinoOTA.handle();
}
