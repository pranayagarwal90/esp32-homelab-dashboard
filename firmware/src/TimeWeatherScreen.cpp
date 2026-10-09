#include <Arduino.h>
#include "TimeWeatherScreen.h"
#include "AppState.h"
#include "Display.h"
#include "PageRouter.h"
#include "UiHelpers.h"
#include "WeatherScreen.h"

static void drawTimeRow(const char* label, const String &timeValue, int y) {
  tft.setTextSize(1);
  tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  tft.setCursor(22, y);
  tft.print(label);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setCursor(205, y);
  tft.print(timeValue);
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
    tft.setTextColor(TFT_RED, TFT_BLACK);
    tft.setCursor(15, 95);
    tft.print("Weather unavailable");
  }
  // The weather block opens the WEATHER page.
  tft.setTextSize(1);
  tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
  tft.setCursor(312 - tft.textWidth("FORECAST >"), 95);
  tft.print("FORECAST >");

  tft.drawFastHLine(10, 120, 300, TFT_DARKGREY);
  drawTimeRow("India", time.indiaTime, 135);
  drawTimeRow("Singapore", time.singaporeTime, 157);
  drawTimeRow("London", time.londonTime, 179);
  drawBackBar(nullptr, "BACK", nullptr);
}

void handleTimeTouch(int x, int y) {
  if (y >= 205) showPage(appBackTarget(PAGE_TIME, PAGE_MORE));
  else if (timeWeatherBlockHit(x, y)) openWeather(PAGE_TIME);
}
