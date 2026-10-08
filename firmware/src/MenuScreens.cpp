#include <Arduino.h>
#include "MenuScreens.h"
#include "AppState.h"
#include "CalendarScreen.h"
#include "Display.h"
#include "PageRouter.h"
#include "SettingsScreen.h"
#include "UiHelpers.h"
#include "games/TicTacToe.h"

void drawMorePage() {
  app.currentPage = PAGE_MORE;
  tft.fillScreen(TFT_BLACK);
  drawHeader("MORE");

  drawMenuButton(15, 44, 140, 44, "TIME / WEATHER");
  drawMenuButton(165, 44, 140, 44, "CALENDAR");
  drawMenuButton(15, 96, 140, 44, "GAMES");
  drawMenuButton(165, 96, 140, 44, "PHOTOS");
  drawMenuButton(15, 148, 290, 44, "SETTINGS");

  drawNavigation();
}

void drawGamesPage() {
  app.currentPage = PAGE_GAMES;
  tft.fillScreen(TFT_BLACK);
  drawHeader("GAMES");
  drawMenuButton(25, 50, 270, 55, "TIC-TAC-TOE");
  drawMenuButton(25, 120, 270, 55, "REACTION TAP");
  drawBackBar(nullptr, "BACK", nullptr);
}

bool handleMoreTouch(int x, int y) {
  if (y >= 40 && y <= 92) {
    if (x < 160) showPage(PAGE_TIME);
    else {
      setCalendarMonth(app.time.currentYear, app.time.currentMonth);
      showPage(PAGE_CALENDAR);
    }
    return true;
  }

  if (y >= 93 && y <= 144) {
    if (x < 160) showPage(PAGE_GAMES);
    else showPage(PAGE_PHOTOS);
    return true;
  }

  if (y >= 145 && y <= 200) {
    openSettings();
    return true;
  }

  return false;
}

void handleGamesMenuTouch(int, int y) {
  if (y >= 45 && y <= 110) {
    resetTTT();
    showPage(PAGE_TTT);
    return;
  }

  if (y >= 115 && y <= 185) {
    showPage(PAGE_REACTION);
    return;
  }

  if (y >= 205) {
    showPage(PAGE_MORE);
    return;
  }
}
