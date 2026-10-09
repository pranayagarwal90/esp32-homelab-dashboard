#include "UiHelpers.h"
#include "AppState.h"
#include "Display.h"
#include "UiIcons.h"
#include "UiTheme.h"

void drawHeader(const char* title) {
  tft.fillRect(0, 0, 320, 36, TFT_DARKGREY);
  tft.setTextSize(2);
  tft.setTextColor(TFT_WHITE, TFT_DARKGREY);
  tft.setCursor(10, 10);
  tft.print(title);

  if (app.serverOnline) {
    tft.setTextColor(TFT_GREEN, TFT_DARKGREY);
    tft.setCursor(248, 10);
    tft.print("LIVE");
  } else {
    tft.setTextColor(TFT_RED, TFT_DARKGREY);
    tft.setCursor(225, 10);
    tft.print("OFFLINE");
  }
}

void drawTitleBar(const char* title, const char* right) {
  tft.fillRect(0, 0, 320, 36, TFT_DARKGREY);
  tft.setTextSize(2);
  tft.setTextColor(TFT_WHITE, TFT_DARKGREY);
  tft.setCursor(10, 10);
  tft.print(title);
  if (right) drawTitleBarValue(right);
}

void drawTitleBarValue(const char* right) {
  tft.fillRect(176, 8, 144, 20, TFT_DARKGREY);
  tft.setTextSize(2);
  tft.setTextColor(TFT_WHITE, TFT_DARKGREY);
  tft.setCursor(310 - tft.textWidth(right), 10);
  tft.print(right);
}

void drawNavigation() {
  tft.drawFastHLine(0, 207, 320, TFT_DARKGREY);
  struct NavItem {
    int x, w;
    const char* label;
    UiIcon icon;
    NavTab tab;
  } items[] = {
    {0, NAV_SPLIT_1, "HOME", UiIcon::Home, NavTab::Home},
    {NAV_SPLIT_1, NAV_SPLIT_2 - NAV_SPLIT_1, "MORE", UiIcon::More, NavTab::More},
    {NAV_SPLIT_2, 320 - NAV_SPLIT_2, "SETTINGS", UiIcon::Settings, NavTab::Settings},
  };
  NavTab active = navTabForPage(app.currentPage);
  for (const NavItem& item : items) {
    bool on = item.tab == active;
    uint16_t iconColor = on ? UiColor::NavActive : UiColor::NavIdle;
    tft.fillRect(item.x, 208, item.w, 32, UiColor::NavBar);
    if (on) tft.fillRect(item.x + item.w / 2 - 14, 208, 28, 2, UiColor::NavActive);
    drawUiIcon(item.icon, item.x + (item.w - 16) / 2, 211, 16, iconColor, iconColor, UiColor::NavBar);
    tft.setTextSize(1);
    tft.setTextColor(on ? TFT_WHITE : UiColor::NavIdle, UiColor::NavBar);
    tft.setCursor(item.x + (item.w - tft.textWidth(item.label)) / 2, 230);
    tft.print(item.label);
  }
}

void drawBackBar(const char* left, const char* center, const char* right) {
  tft.fillRect(0, 208, 320, 32, TFT_DARKGREY);
  tft.setTextSize(1);
  tft.setTextColor(TFT_WHITE, TFT_DARKGREY);
  if (left) {
    tft.setCursor(35, 220);
    tft.print(left);
  }
  if (center) {
    int w = tft.textWidth(center);
    tft.setCursor((320 - w) / 2, 220);
    tft.print(center);
  }
  if (right) {
    int w = tft.textWidth(right);
    tft.setCursor(285 - w, 220);
    tft.print(right);
  }
}

void drawMenuButton(int x, int y, int w, int h, const char* label) {
  tft.fillRoundRect(x, y, w, h, 8, TFT_DARKGREY);
  tft.drawRoundRect(x, y, w, h, 8, TFT_LIGHTGREY);
  tft.setTextSize(1);
  tft.setTextColor(TFT_WHITE, TFT_DARKGREY);
  int textWidth = tft.textWidth(label);
  tft.setCursor(x + (w - textWidth) / 2, y + (h / 2) - 3);
  tft.print(label);
}
