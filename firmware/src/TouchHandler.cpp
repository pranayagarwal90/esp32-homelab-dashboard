#include <Arduino.h>
#include <SPI.h>
#include <XPT2046_Touchscreen.h>
#include "TouchHandler.h"
#include "AlertLogic.h"
#include "AlertsScreen.h"
#include "AppState.h"
#include "BacklightPwm.h"
#include "CalendarScreen.h"
#include "MenuScreens.h"
#include "PageRouter.h"
#include "PhotoScreen.h"
#include "Screensaver.h"
#include "SettingsScreen.h"
#include "StopwatchScreen.h"
#include "TimeWeatherScreen.h"
#include "WeatherScreen.h"
#include "games/MemoryGame.h"
#include "games/ReactionGame.h"
#include "games/SimonGame.h"
#include "games/SnakeGame.h"
#include "games/TicTacToe.h"

#define TOUCH_CLK 25
#define TOUCH_MISO 39
#define TOUCH_MOSI 32
#define TOUCH_CS 33

#define RAW_X_MIN 328
#define RAW_X_MAX 3630
#define RAW_Y_MIN 526
#define RAW_Y_MAX 3613

static SPIClass touchSPI(HSPI);
static XPT2046_Touchscreen touch(TOUCH_CS);
static unsigned long lastTouchTime = 0;

void setupTouch() {
  touchSPI.begin(TOUCH_CLK, TOUCH_MISO, TOUCH_MOSI, TOUCH_CS);
  touch.begin(touchSPI);
  touch.setRotation(1);
}

void handleTouch() {
  if (!touch.touched()) return;
  if (millis() - lastTouchTime < 250) return;
  lastTouchTime = millis();

  TS_Point p = touch.getPoint();
  if (p.z < 200) return;

  int x = constrain(map(p.x, RAW_X_MIN, RAW_X_MAX, 0, 319), 0, 319);
  int y = constrain(map(p.y, RAW_Y_MIN, RAW_Y_MAX, 0, 239), 0, 239);

  app.lastInteraction = millis();
  backlightAcceptedTouch(app.lastInteraction);
  updateBacklight();
  Serial.printf("Touch X=%d Y=%d\n", x, y);

  if (app.currentPage == PAGE_SCREENSAVER) {
    exitScreensaver();
    return;
  }
  if (app.currentPage == PAGE_TTT) {
    handleTTTTouch(x, y);
    return;
  }
  if (app.currentPage == PAGE_REACTION) {
    handleReactionTouch(x, y);
    return;
  }
  if (app.currentPage == PAGE_CALENDAR) {
    handleCalendarTouch(x, y);
    return;
  }
  if (app.currentPage == PAGE_PHOTOS) {
    handlePhotosTouch(x, y);
    return;
  }
  if (app.currentPage == PAGE_GAMES) {
    handleGamesMenuTouch(x, y);
    return;
  }
  if (app.currentPage == PAGE_WEATHER) {
    handleWeatherTouch(x, y);
    return;
  }
  if (app.currentPage == PAGE_TIME) {
    handleTimeTouch(x, y);
    return;
  }
  if (app.currentPage == PAGE_SETTINGS) {
    handleSettingsTouch(x, y);
    return;
  }
  if (app.currentPage == PAGE_TOOLS) {
    handleToolsMenuTouch(x, y);
    return;
  }
  if (app.currentPage == PAGE_SIMON) {
    handleSimonTouch(x, y);
    return;
  }
  if (app.currentPage == PAGE_MEMORY) {
    handleMemoryTouch(x, y);
    return;
  }
  if (app.currentPage == PAGE_SNAKE) {
    handleSnakeTouch(x, y);
    return;
  }
  if (app.currentPage == PAGE_STOPWATCH) {
    handleStopwatchTouch(x, y);
    return;
  }
  if (app.currentPage == PAGE_ALERTS) {
    handleAlertsTouch(x, y);
    return;
  }
  if (app.currentPage == PAGE_HOME && alertBadgeHit(x, y)) {
    openAlerts(PAGE_HOME);
    return;
  }
  // MORE menu touches outside its buttons fall through to the nav bar.
  if (app.currentPage == PAGE_MORE && handleMoreTouch(x, y)) return;

  if (y >= 205) {
    if (x < 107) showPage(PAGE_HOME);
    else if (x < 214) showPage(PAGE_SERVICES);
    else showPage(PAGE_MORE);
  }
}

bool touchPressed() {
  return touch.touched();
}
