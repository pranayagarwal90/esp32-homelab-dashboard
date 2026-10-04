#include <Arduino.h>
#include "Screensaver.h"
#include "AppState.h"
#include "Display.h"
#include "PageRouter.h"
#include "PhotoScreen.h"

static const unsigned long SCREENSAVER_TIMEOUT = 180000;
static const unsigned long SCREENSAVER_ROTATE = 30000;

static Page pageBeforeScreensaver = PAGE_HOME;
static unsigned long screensaverLastRotate = 0;
static bool screensaverShowingPhoto = false;

void drawScreensaverClock() {
  const TimeData& time = app.time;
  const WeatherData& weather = app.weather;
  app.currentPage = PAGE_SCREENSAVER;
  screensaverShowingPhoto = false;
  tft.fillScreen(TFT_BLACK);

  tft.setTextSize(1);
  tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  int dateW = tft.textWidth(time.localDate);
  tft.setCursor((320 - dateW) / 2, 18);
  tft.print(time.localDate);

  tft.setTextSize(5);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  int timeW = tft.textWidth(time.localTime);
  tft.setCursor((320 - timeW) / 2, 62);
  tft.print(time.localTime);

  if (weather.weatherAvailable) {
    tft.setTextSize(2);
    tft.setTextColor(TFT_CYAN, TFT_BLACK);
    String temp = String(weather.temperatureC, 1) + " C";
    int tempW = tft.textWidth(temp);
    tft.setCursor((320 - tempW) / 2, 130);
    tft.print(temp);

    tft.setTextSize(1);
    tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    int condW = tft.textWidth(weather.weatherCondition);
    tft.setCursor((320 - condW) / 2, 162);
    tft.print(weather.weatherCondition);
    String range = "H " + String(weather.highC, 1) + "C  L " + String(weather.lowC, 1) + "C";
    int rangeW = tft.textWidth(range);
    tft.setCursor((320 - rangeW) / 2, 182);
    tft.print(range);
  }

  tft.setTextSize(1);
  tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
  const char* wakeText = "Touch to wake";
  int wakeW = tft.textWidth(wakeText);
  tft.setCursor((320 - wakeW) / 2, 225);
  tft.print(wakeText);
}

static void enterScreensaver() {
  if (app.currentPage == PAGE_SCREENSAVER || app.currentPage == PAGE_TTT || app.currentPage == PAGE_REACTION) return;
  pageBeforeScreensaver = app.currentPage;
  screensaverLastRotate = millis();
  drawScreensaverClock();
}

void exitScreensaver() {
  app.currentPage = pageBeforeScreensaver;
  app.lastInteraction = millis();
  drawCurrentPage();
}

void updateScreensaver() {
  if (app.currentPage != PAGE_SCREENSAVER) {
    if (millis() - app.lastInteraction >= SCREENSAVER_TIMEOUT) enterScreensaver();
    return;
  }

  if (millis() - screensaverLastRotate < SCREENSAVER_ROTATE) return;
  // Never start a rotation while the previous photo is still downloading.
  if (isScreensaverPhotoPending()) return;
  screensaverLastRotate = millis();

  // The clock stays up until the photo arrives; onScreensaverPhotoResult()
  // completes the rotation.
  if (!screensaverShowingPhoto && requestScreensaverPhoto()) return;
  drawScreensaverClock();
}

void onScreensaverPhotoResult(bool shown) {
  if (shown) screensaverShowingPhoto = true;
  else drawScreensaverClock();
}
