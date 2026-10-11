#include <Arduino.h>
#include <TFT_eSPI.h>
#include "AiAssistantScreen.h"
#include "AppState.h"
#include "BacklightPwm.h"
#include "BluetoothControl.h"
#include "CalendarScreen.h"
#include "DeviceConfig.h"
#include "Display.h"
#include "NetworkManager.h"
#include "OtaManager.h"
#include "PageRouter.h"
#include "PowerManager.h"
#include "PhotoScreen.h"
#include "Screensaver.h"
#include "SettingsScreen.h"
#include "SettingsStore.h"
#include "SetupScreen.h"
#include "StatusClient.h"
#include "StopwatchScreen.h"
#include "TouchHandler.h"
#include "WeatherScreen.h"
#include "games/MemoryGame.h"
#include "games/ReactionGame.h"
#include "games/SimonGame.h"
#include "games/SnakeGame.h"

// All TFT drawing and JPEG decoding happen on this (the Arduino loop) task.
// The status, photo and AI workers only do network I/O and never draw.
TFT_eSPI tft = TFT_eSPI();

void setup() {
  Serial.begin(115200);
  delay(500);
  logWakeReason();

  loadSettings();
  tft.init();
  setupBacklight();
  tft.setRotation(1);

  setupTouch();
  setupPhotoDecoder();

  // Boot decision: configuration first (NVS), then the network. Missing or
  // invalid configuration, or a network that cannot be joined, starts Setup
  // Mode instead of the dashboard; an unreachable backend never does.
  loadDeviceConfig();
  initNetwork();
  SetupReason setupReason = bootSetupReason(hasWifiNetwork(), backendUrlStored(), hasBackendUrl());
  if (setupReason == SetupReason::None && !connectWiFi()) setupReason = SetupReason::WifiFailed;
  if (setupReason != SetupReason::None) {
    enterSetupMode(setupReason);
    return;
  }

  setupOTA();
  randomSeed(micros());

  setupBluetooth();
  setupStatusWorker();
  drawCurrentPage();
  fetchHomelabStatus();
  requestPhotoList();

  setCalendarMonth(app.time.currentYear, app.time.currentMonth);
  app.lastInteraction = millis();
}

void loop() {
  if (setupModeActive()) {
    if (updatePower()) return; // Restart after saving, or RESTART.
    handleTouch();
    updateSetupMode();
    return;
  }
  if (handleOTA()) return;
  if (updatePower()) return;
  handleTouch();
  updateReactionGame();
  updateStopwatch();
  updateSnakeGame();
  updateMemoryGame();
  updateSimonGame();
  updateWeatherPage();
  updateScreensaver();
  updatePhotos();
  updateAiAssistant();
  updateSettings();
  processStatusResult();
  updateBacklight();

  if (statusRefreshDue()) {
    if (!isGamePlayPage(app.currentPage) && app.currentPage != PAGE_GAMES && app.currentPage != PAGE_PHOTOS) {
      fetchHomelabStatus();
    }
  }
}
