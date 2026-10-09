#include <Arduino.h>
#include "MenuScreens.h"
#include "AlertsScreen.h"
#include "AppState.h"
#include "CalendarScreen.h"
#include "Display.h"
#include "PageRouter.h"
#include "SettingsScreen.h"
#include "UiHelpers.h"
#include "games/TicTacToe.h"

static constexpr int LEFT_X = 15;
static constexpr int RIGHT_X = 165;
static constexpr int HALF_W = 140;
static constexpr int FULL_W = 290;

void drawMorePage() {
  app.currentPage = PAGE_MORE;
  tft.fillScreen(TFT_BLACK);
  drawHeader("MORE");

  static const char* const LABELS[3][2] = {
    {"TIME / WEATHER", "CALENDAR"},
    {"GAMES", "PHOTOS"},
    {"ALERTS", "SETTINGS"},
  };
  for (int row = 0; row < 3; row++) {
    int y = MORE_Y0 + row * MORE_PITCH;
    drawMenuButton(LEFT_X, y, HALF_W, MORE_H, LABELS[row][0]);
    drawMenuButton(RIGHT_X, y, HALF_W, MORE_H, LABELS[row][1]);
  }
  drawMenuButton(LEFT_X, MORE_Y0 + 3 * MORE_PITCH, FULL_W, MORE_H, "TOOLS");

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

void drawToolsPage() {
  app.currentPage = PAGE_TOOLS;
  tft.fillScreen(TFT_BLACK);
  drawHeader("TOOLS");
  drawMenuButton(25, TOOLS_Y, 270, TOOLS_H, "STOPWATCH");
  drawBackBar(nullptr, "BACK", nullptr);
}

void showMenuNode(MenuNode node) {
  switch (node) {
    case MenuNode::More: showPage(PAGE_MORE); break;
    case MenuNode::Tools: showPage(PAGE_TOOLS); break;
    case MenuNode::Stopwatch: showPage(PAGE_STOPWATCH); break;
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

bool handleMoreTouch(int x, int y) {
  switch (moreItemAt(x, y)) {
    case MoreItem::Time: showPage(PAGE_TIME); return true;
    case MoreItem::Calendar:
      setCalendarMonth(app.time.currentYear, app.time.currentMonth);
      showPage(PAGE_CALENDAR);
      return true;
    case MoreItem::Games: showMenuNode(MenuNode::Games); return true;
    case MoreItem::Photos: showPage(PAGE_PHOTOS); return true;
    case MoreItem::Alerts: openAlerts(PAGE_MORE); return true;
    case MoreItem::Settings: openSettings(); return true;
    case MoreItem::Tools: showMenuNode(MenuNode::Tools); return true;
    case MoreItem::None: return false;
  }
  return false;
}

void handleGamesMenuTouch(int x, int y) {
  if (y >= 205) showMenuNode(menuParent(MenuNode::Games));
  else showMenuNode(gamesItemAt(x, y));
}

void handleToolsMenuTouch(int x, int y) {
  if (y >= 205) showMenuNode(menuParent(MenuNode::Tools));
  else showMenuNode(toolsItemAt(x, y));
}
