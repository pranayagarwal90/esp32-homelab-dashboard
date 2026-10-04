#include "UiHelpers.h"
#include "AppState.h"
#include "Display.h"

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

void drawNavigation() {
  tft.drawFastHLine(0, 207, 320, TFT_DARKGREY);

  struct NavItem {
    int x;
    int w;
    const char* label;
    Page page;
  } items[] = {
    {0,   107, "HOME",     PAGE_HOME},
    {107, 107, "SERVICES", PAGE_SERVICES},
    {214, 106, "MORE",     PAGE_MORE}
  };

  for (auto &item : items) {
    uint16_t color = app.currentPage == item.page ? TFT_BLUE : TFT_DARKGREY;
    tft.fillRect(item.x, 208, item.w, 32, color);
    tft.setTextSize(1);
    tft.setTextColor(TFT_WHITE, color);
    int textW = tft.textWidth(item.label);
    tft.setCursor(item.x + (item.w - textW) / 2, 220);
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
