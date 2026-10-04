#include <Arduino.h>
#include "MenuScreens.h"
#include "AppState.h"
#include "CalendarScreen.h"
#include "Display.h"
#include "PageRouter.h"
#include "UiHelpers.h"
#include "games/TicTacToe.h"

static void drawMenuButton(int x, int y, int w, int h, const char* label) {
  tft.fillRoundRect(x, y, w, h, 8, TFT_DARKGREY);
  tft.drawRoundRect(x, y, w, h, 8, TFT_LIGHTGREY);
  tft.setTextSize(1);
  tft.setTextColor(TFT_WHITE, TFT_DARKGREY);
  int textWidth = tft.textWidth(label);
  tft.setCursor(x + (w - textWidth) / 2, y + (h / 2) - 3);
  tft.print(label);
}

void drawMorePage() {
  app.currentPage = PAGE_MORE;
  tft.fillScreen(TFT_BLACK);
  drawHeader("MORE");

  drawMenuButton(15, 50, 140, 55, "TIME / WEATHER");
  drawMenuButton(165, 50, 140, 55, "CALENDAR");
  drawMenuButton(15, 120, 140, 55, "GAMES");
  drawMenuButton(165, 120, 140, 55, "PHOTOS");

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
  if (y >= 45 && y <= 110) {
    if (x < 160) showPage(PAGE_TIME);
    else {
      setCalendarMonth(app.time.currentYear, app.time.currentMonth);
      showPage(PAGE_CALENDAR);
    }
    return true;
  }

  if (y >= 115 && y <= 190) {
    if (x < 160) showPage(PAGE_GAMES);
    else showPage(PAGE_PHOTOS);
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
