#include <Arduino.h>
#include "TimeWeatherScreen.h"
#include "AppState.h"
#include "DeviceConfig.h"
#include "Display.h"
#include "PageRouter.h"
#include "UiHelpers.h"
#include "WeatherScreen.h"
#include "WorldClockLogic.h"

static int clocksPage = 0; // Kept across status redraws; reset by BACK.

static void drawTimeRow(const char* label, const char* timeValue, int y) {
  tft.setTextSize(1);
  tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  tft.setCursor(22, y);
  tft.print(label);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setCursor(312 - tft.textWidth(timeValue), y);
  tft.print(timeValue);
}

static void printCenteredSmall(const char* text, int y, uint16_t color) {
  tft.setTextColor(color, TFT_BLACK);
  tft.setCursor((320 - tft.textWidth(text)) / 2, y);
  tft.print(text);
}

// The backend's world clocks, CLOCKS_PER_PAGE per page; returns the pages.
static int drawWorldClocks(const WorldClockState& clocks) {
  int pages = pageCountFor(clocks.count, CLOCKS_PER_PAGE);
  clocksPage = clampPageIndex(clocksPage, clocks.count, CLOCKS_PER_PAGE);
  tft.setTextSize(1);
  tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
  tft.setCursor(22, CLOCKS_TITLE_Y);
  tft.print("WORLD CLOCKS");
  if (pages > 1) {
    char page[8];
    snprintf(page, sizeof(page), "%d/%d", clocksPage + 1, pages);
    tft.setCursor(312 - tft.textWidth(page), CLOCKS_TITLE_Y);
    tft.print(page);
  }
  if (clocks.count == 0 && !backendConfigured()) {
    printCenteredSmall(backendNotice(BackendFeature::Clocks).hint, CLOCKS_ROW_Y0 + 4, TFT_LIGHTGREY);
    printCenteredSmall(BACKEND_SETUP_HINT, CLOCKS_ROW_Y0 + 20, TFT_DARKGREY);
    return pages;
  }
  if (clocks.count == 0) {
    printCenteredSmall("No clocks configured.", CLOCKS_ROW_Y0 + 4, TFT_LIGHTGREY);
    printCenteredSmall("Configure clocks on the HomeDeck Mini backend.", CLOCKS_ROW_Y0 + 20, TFT_DARKGREY);
    return pages;
  }
  int first = clocksPage * CLOCKS_PER_PAGE;
  for (int row = 0; row < CLOCKS_PER_PAGE && first + row < clocks.count; row++) {
    const WorldClock& clock = clocks.items[first + row];
    drawTimeRow(clock.label, clock.time, CLOCKS_ROW_Y0 + row * CLOCKS_ROW_PITCH);
  }
  return pages;
}

void drawTimePage() {
  const TimeData& time = app.time;
  const WeatherData& weather = app.weather;
  app.currentPage = PAGE_TIME;
  tft.fillScreen(TFT_BLACK);

  tft.setTextSize(1);
  tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  tft.setCursor(12, 8);
  tft.print(time.localDate);

  if (weather.weatherAvailable) {
    tft.setTextColor(TFT_CYAN, TFT_BLACK);
    tft.setCursor(260, 8);
    tft.printf("%.1fC", weather.temperatureC);
  }

  tft.setTextSize(4);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  int timeWidth = tft.textWidth(time.localTime);
  tft.setCursor((320 - timeWidth) / 2, 35);
  tft.print(time.localTime);

  if (weather.weatherAvailable) {
    tft.setTextSize(1);
    tft.setTextColor(TFT_CYAN, TFT_BLACK);
    tft.setCursor(15, 88);
    tft.print(weather.weatherCondition);
    tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    tft.setCursor(15, 103);
    tft.printf("High %.1fC   Low %.1fC", weather.highC, weather.lowC);
  } else {
    tft.setTextSize(1);
    tft.setTextColor(backendConfigured() ? TFT_RED : TFT_LIGHTGREY, TFT_BLACK);
    tft.setCursor(15, 95);
    tft.print(backendConfigured() ? "Weather unavailable" : backendNotice(BackendFeature::Weather).title);
  }
  // The weather block opens the WEATHER page.
  tft.setTextSize(1);
  tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
  tft.setCursor(312 - tft.textWidth("FORECAST >"), 95);
  tft.print("FORECAST >");

  tft.drawFastHLine(10, 120, 300, TFT_DARKGREY);
  int pages = drawWorldClocks(time.worldClocks);
  drawBackBar(pages > 1 && clocksPage > 0 ? "PREV" : nullptr, "BACK",
              pages > 1 && clocksPage < pages - 1 ? "NEXT" : nullptr);
}

void handleTimeTouch(int x, int y) {
  int pages = pageCountFor(app.time.worldClocks.count, CLOCKS_PER_PAGE);
  switch (pagerBarAt(x, y, pages)) {
    case PagerHit::Prev:
      if (clocksPage > 0) {
        clocksPage--;
        drawTimePage();
      }
      return;
    case PagerHit::Next:
      if (clocksPage < pages - 1) {
        clocksPage++;
        drawTimePage();
      }
      return;
    case PagerHit::Back:
      clocksPage = 0;
      showPage(appBackTarget(PAGE_TIME, PAGE_MORE));
      return;
    case PagerHit::None:
      break;
  }
  if (timeWeatherBlockHit(x, y)) openWeather(PAGE_TIME);
}
