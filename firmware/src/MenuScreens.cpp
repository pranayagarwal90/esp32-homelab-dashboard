#include <Arduino.h>
#include "MenuScreens.h"
#include "AlertsScreen.h"
#include "AppState.h"
#include "CalendarScreen.h"
#include "Display.h"
#include "PageRouter.h"
#include "SettingsScreen.h"
#include "UiHelpers.h"
#include "UiIcons.h"
#include "WeatherScreen.h"
#include "games/TicTacToe.h"

static constexpr int LEFT_X = 15;
static constexpr int RIGHT_X = 165;
static constexpr int HALF_W = 140;
static constexpr int FULL_W = 290;

static int morePage = 0;

static void drawMoreTile(int index, int slot) {
  const MoreApp& app_ = MORE_APPS[index];
  int x = moreTileX(slot % MORE_COLS), y = moreTileY(slot / MORE_COLS);
  int boxX = x + (MORE_TILE_W - MORE_ICON_BOX) / 2, boxY = y + 6;
  tft.fillRoundRect(boxX, boxY, MORE_ICON_BOX, MORE_ICON_BOX, 10, app_.tile);
  int inset = (MORE_ICON_BOX - MORE_ICON) / 2;
  drawUiIcon(app_.icon, boxX + inset, boxY + inset, MORE_ICON, glyphColorOn(app_.tile), app_.accent, app_.tile);
  tft.setTextSize(1);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setCursor(x + (MORE_TILE_W - tft.textWidth(app_.label)) / 2, y + MORE_ICON_BOX + 14);
  tft.print(app_.label);
}

void drawMorePage() {
  app.currentPage = PAGE_MORE;
  int pages = morePageCount();
  if (morePage >= pages) morePage = 0;
  tft.fillScreen(TFT_BLACK);
  if (pages > 1) {
    char title[16];
    snprintf(title, sizeof(title), "<  MORE %d/%d  >", morePage + 1, pages);
    drawHeader(title);
  } else {
    drawHeader("MORE");
  }
  for (int slot = 0; slot < MORE_PER_PAGE; slot++) {
    int index = morePage * MORE_PER_PAGE + slot;
    if (index < MORE_APP_COUNT) drawMoreTile(index, slot);
  }
  drawNavigation();
}

void drawGamesPage() {
  app.currentPage = PAGE_GAMES;
  tft.fillScreen(TFT_BLACK);
  drawHeader("GAMES");
  drawMenuButton(LEFT_X, GAMES_Y[0], HALF_W, GAMES_H, "TIC-TAC-TOE");
  drawMenuButton(RIGHT_X, GAMES_Y[0], HALF_W, GAMES_H, "REACTION TAP");
  drawMenuButton(LEFT_X, GAMES_Y[1], HALF_W, GAMES_H, "SNAKE");
  drawMenuButton(RIGHT_X, GAMES_Y[1], HALF_W, GAMES_H, "MEMORY MATCH");
  drawMenuButton(LEFT_X, GAMES_Y[2], FULL_W, GAMES_H, "SIMON SAYS");
  drawBackBar(nullptr, "BACK", nullptr);
}

void showMenuNode(MenuNode node) {
  switch (node) {
    case MenuNode::More: showPage(PAGE_MORE); break;
    case MenuNode::Games: showPage(PAGE_GAMES); break;
    case MenuNode::TicTacToe:
      resetTTT();
      showPage(PAGE_TTT);
      break;
    case MenuNode::Reaction: showPage(PAGE_REACTION); break;
    case MenuNode::Snake: showPage(PAGE_SNAKE); break;
    case MenuNode::Memory: showPage(PAGE_MEMORY); break;
    case MenuNode::Simon: showPage(PAGE_SIMON); break;
    case MenuNode::None: break;
  }
}

static void openMoreApp(Page page) {
  switch (page) {
    case PAGE_ALERTS: openAlerts(PAGE_MORE); break;
    case PAGE_WEATHER: openWeather(PAGE_MORE); break;
    case PAGE_CALENDAR:
      setCalendarMonth(app.time.currentYear, app.time.currentMonth);
      showPage(PAGE_CALENDAR);
      break;
    default: showPage(page); break;
  }
}

bool handleMoreTouch(int x, int y) {
  switch (morePagingAt(x, y)) {
    case MorePaging::Prev:
      morePage = (morePage + morePageCount() - 1) % morePageCount();
      drawMorePage();
      return true;
    case MorePaging::Next:
      morePage = (morePage + 1) % morePageCount();
      drawMorePage();
      return true;
    case MorePaging::None: break;
  }
  int index = moreAppAt(x, y, morePage);
  if (index < 0) return false;
  openMoreApp(MORE_APPS[index].page);
  return true;
}

void handleGamesMenuTouch(int x, int y) {
  if (y >= NAV_Y) showMenuNode(menuParent(MenuNode::Games));
  else showMenuNode(gamesItemAt(x, y));
}
