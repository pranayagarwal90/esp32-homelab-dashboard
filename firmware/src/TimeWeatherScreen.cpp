#include <Arduino.h>
#include "TimeWeatherScreen.h"
#include "AppState.h"
#include "DeviceConfig.h"
#include "DirectWeather.h"
#include "Display.h"
#include "LocalClockStore.h"
#include "LocalTime.h"
#include "PageRouter.h"
#include "SettingsScreen.h"
#include "UiHelpers.h"
#include "WeatherScreen.h"
#include "WorldClockLogic.h"
#include "WorldClockUi.h"

static int clocksPage = 0; // Kept across status redraws; reset by BACK.
// What the world clocks showed when drawn (updateTimePage redraws on change).
static WorldClockSource drawnSource = WorldClockSource::None;
static uint32_t drawnClockRevision = 0;
static uint32_t drawnMinute = 0;

// Local clocks win; otherwise the backend's, as before (LocalClockLogic.h).
static WorldClockSource clockListSource() {
  return worldClockSourceFor(localClockCount(), backendConfigured(), app.time.worldClocks.count);
}

static int clockPages() {
  switch (clockListSource()) {
    case WorldClockSource::Local: return pageCountFor(localClockCount(), LOCAL_CLOCKS_PER_PAGE);
    case WorldClockSource::Backend: return pageCountFor(app.time.worldClocks.count, CLOCKS_PER_PAGE);
    case WorldClockSource::None: break;
  }
  return 1;
}

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

// Clocks added on the device: NTP UTC + each city's offset, its own date.
static void drawLocalClocks() {
  LocalClockList list;
  localClocks(list);
  clocksPage = clampPageIndex(clocksPage, list.count, LOCAL_CLOCKS_PER_PAGE);
  uint32_t utc = utcNow();
  int first = clocksPage * LOCAL_CLOCKS_PER_PAGE;
  for (int row = 0; row < LOCAL_CLOCKS_PER_PAGE && first + row < list.count; row++) {
    const WorldClockLocation& c = list.items[first + row];
    int y = LOCAL_CLOCK_ROW_Y0 + row * LOCAL_CLOCK_ROW_PITCH;
    char label[24], time[16], date[24];
    clockDisplayLabel(c.name, label, sizeof(label));
    bool ready = formatWorldClock(c, utc, time, sizeof(time), date, sizeof(date));
    tft.setTextSize(1);
    tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    tft.setCursor(22, y);
    tft.print(label);
    tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
    tft.setCursor(22, y + 13);
    tft.print(ready ? date : "");
    // Never a guessed time: "Syncing..." until NTP and the city's offset are known.
    tft.setTextSize(ready ? 2 : 1);
    tft.setTextColor(ready ? TFT_WHITE : TFT_LIGHTGREY, TFT_BLACK);
    const char* right = ready ? time : "Syncing...";
    tft.setCursor(312 - tft.textWidth(right), ready ? y + 2 : y + 6);
    tft.print(right);
  }
}

// The backend's world clocks, CLOCKS_PER_PAGE per page (unchanged).
static void drawBackendClocks(const WorldClockState& clocks) {
  clocksPage = clampPageIndex(clocksPage, clocks.count, CLOCKS_PER_PAGE);
  int first = clocksPage * CLOCKS_PER_PAGE;
  for (int row = 0; row < CLOCKS_PER_PAGE && first + row < clocks.count; row++) {
    const WorldClock& clock = clocks.items[first + row];
    drawTimeRow(clock.label, clock.time, CLOCKS_ROW_Y0 + row * CLOCKS_ROW_PITCH);
  }
}

// "WORLD CLOCKS  1/2  EDIT >", then the clocks; returns the pages.
static int drawWorldClocks(const WorldClockState& backendClocks) {
  WorldClockSource source = clockListSource();
  int pages = clockPages();
  drawnSource = source;
  drawnClockRevision = localClocksDisplayRevision();
  drawnMinute = utcNow() / 60;
  tft.setTextSize(1);
  tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
  tft.setCursor(22, CLOCKS_TITLE_Y);
  tft.print("WORLD CLOCKS");
  int right = 312 - tft.textWidth("EDIT >");
  tft.setCursor(right, CLOCKS_TITLE_Y);
  tft.print("EDIT >");
  if (pages > 1) {
    char page[8];
    snprintf(page, sizeof(page), "%d/%d", clocksPage + 1, pages);
    tft.setCursor(right - 12 - tft.textWidth(page), CLOCKS_TITLE_Y);
    tft.print(page);
  }
  switch (source) {
    case WorldClockSource::Local:
      drawLocalClocks();
      break;
    case WorldClockSource::Backend:
      drawBackendClocks(backendClocks);
      break;
    case WorldClockSource::None:
      printCenteredSmall("No clocks added", CLOCKS_ROW_Y0 + 2, TFT_LIGHTGREY);
      drawMenuButton(CLOCKS_ADD_X, CLOCKS_ADD_Y, CLOCKS_ADD_W, CLOCKS_ADD_H, "ADD CLOCK");
      break;
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
    WeatherView view = weatherView();
    bool calm = view == WeatherView::SetLocation || view == WeatherView::Loading;
    tft.setTextColor(calm ? TFT_LIGHTGREY : TFT_RED, TFT_BLACK);
    tft.setCursor(15, 95);
    tft.print(view == WeatherView::SetLocation ? backendNotice(BackendFeature::Weather).title
              : view == WeatherView::Loading ? "Loading weather..." : "Weather unavailable");
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

void updateTimePage() {
  if (app.currentPage != PAGE_TIME) return;
  WorldClockSource source = clockListSource();
  bool tick = source == WorldClockSource::Local && utcNow() / 60 != drawnMinute;
  if (source != drawnSource || localClocksDisplayRevision() != drawnClockRevision || tick) drawTimePage();
}

void handleTimeTouch(int x, int y) {
  int pages = clockPages();
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
  if (timeWeatherBlockHit(x, y)) {
    openWeather(PAGE_TIME);
  } else if (clocksEditHit(x, y)) {
    openSettingsFromClocks(SettingsView::WorldClocks);
  } else if (clockListSource() == WorldClockSource::None && clocksAddHit(x, y)) {
    openSettingsFromClocks(SettingsView::AddClock);
  }
}
