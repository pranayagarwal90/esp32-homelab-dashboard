#include <Arduino.h>
#include <TFT_eSPI.h>
#include "AppState.h"
#include "BacklightPwm.h"
#include "CalendarScreen.h"
#include "Display.h"
#include "NetworkManager.h"
#include "OtaManager.h"
#include "PageRouter.h"
#include "PhotoScreen.h"
#include "Screensaver.h"
#include "StatusClient.h"
#include "TouchHandler.h"
#include "games/ReactionGame.h"

// All TFT drawing happens on this (the Arduino loop) task. The status worker
// created by setupStatusWorker() only fills a snapshot and never draws.
TFT_eSPI tft = TFT_eSPI();

void setup() {
  Serial.begin(115200);
  delay(500);

  tft.init();
  setupBacklight();
  tft.setRotation(1);

  setupTouch();
  setupPhotoDecoder();

  connectWiFi();
  setupOTA();
  randomSeed(micros());

  setupStatusWorker();
  drawCurrentPage();
  fetchHomelabStatus();
  fetchPhotoList();

  setCalendarMonth(app.time.currentYear, app.time.currentMonth);
  app.lastInteraction = millis();
}

void loop() {
  handleOTA();
  handleTouch();
  updateReactionGame();
  updateScreensaver();
  processStatusResult();
  updateBacklight();

  if (statusRefreshDue()) {
    if (app.currentPage != PAGE_TTT && app.currentPage != PAGE_REACTION && app.currentPage != PAGE_GAMES && app.currentPage != PAGE_PHOTOS) {
      fetchHomelabStatus();
    }
  }
}
