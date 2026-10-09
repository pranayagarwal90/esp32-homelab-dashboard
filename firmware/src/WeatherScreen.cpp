#include <Arduino.h>
#include "WeatherScreen.h"
#include "AppState.h"
#include "Display.h"
#include "PageRouter.h"
#include "UiHelpers.h"
#include "WeatherAnimation.h"

static constexpr int CURRENT_X = 150;
static constexpr int DETAIL_Y = 130;
static constexpr int HOURLY_Y = 160;
static constexpr int HOURLY_W = 52;

static uint32_t drawnTextHash = 0;
static WeatherScene shownScene = WeatherScene::Neutral;
static bool shownOnline = false;
static Page weatherOrigin = PAGE_MORE;

static uint32_t nowEpoch() {
  const WeatherData& w = app.weather;
  return weatherNowEpoch(w.observedAt, w.observedMs, millis());
}

static WeatherScene currentScene() {
  const WeatherData& w = app.weather;
  if (!w.weatherAvailable && w.weatherCode < 0) return WeatherScene::Neutral;
  return weatherSceneFor(weatherConditionFor(w.weatherCode), weatherIsDay(nowEpoch(), w.sunrise, w.sunset));
}

// Prints label (grey) then value (white) at the cursor.
static void printPair(const char* label, const char* value) {
  tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  tft.print(label);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.print(value);
}

static void drawCurrent() {
  const WeatherData& w = app.weather;
  tft.fillRect(CURRENT_X, 40, 320 - CURRENT_X, 86, TFT_BLACK);
  char text[24];
  if (!w.weatherAvailable && w.weatherCode < 0) {
    tft.setTextSize(2);
    tft.setTextColor(TFT_RED, TFT_BLACK);
    tft.setCursor(CURRENT_X, 60);
    tft.print("Weather");
    tft.setCursor(CURRENT_X, 80);
    tft.print("unavailable");
    return;
  }
  formatDegreesC1(toTenths(w.temperatureC), text, sizeof(text));
  tft.setTextSize(strlen(text) > 7 ? 3 : 4);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setCursor(CURRENT_X, 46);
  tft.print(text);

  const char* condition = w.weatherCondition.c_str();
  tft.setTextSize(strlen(condition) > 13 ? 1 : 2);
  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  tft.setCursor(CURRENT_X, 86);
  tft.print(condition);

  tft.setTextSize(1);
  tft.setCursor(CURRENT_X, 108);
  formatDegrees(w.feelsLike10, text, sizeof(text));
  strncat(text, w.feelsLike10 == WEATHER_NO_VALUE ? "" : "C", sizeof(text) - strlen(text) - 1);
  printPair("Feels like ", text);
  if (!w.weatherAvailable) {
    tft.setTextColor(TFT_ORANGE, TFT_BLACK);
    tft.setCursor(CURRENT_X, 118);
    tft.print("Last known");
  }
}

static void drawDetails() {
  const WeatherData& w = app.weather;
  tft.fillRect(0, DETAIL_Y, 320, 24, TFT_BLACK);
  tft.setTextSize(1);
  char value[20];

  tft.setCursor(12, DETAIL_Y);
  formatDegrees(toTenths(w.highC), value, sizeof(value));
  printPair("High ", value);
  formatDegrees(toTenths(w.lowC), value, sizeof(value));
  printPair("  Low ", value);
  formatPercent(w.precipChance, value, sizeof(value));
  printPair("  Rain ", value);
  if (validPercent(w.precipChanceMax)) {
    formatPercent(w.precipChanceMax, value, sizeof(value));
    printPair(" (today ", value);
    printPair(")", "");
  }

  tft.setCursor(12, DETAIL_Y + 14);
  formatPercent(w.humidity, value, sizeof(value));
  printPair("Humidity ", value);
  if (w.wind10 == WEATHER_NO_VALUE) snprintf(value, sizeof(value), "--");
  else snprintf(value, sizeof(value), "%d km/h", (w.wind10 + 5) / 10);
  printPair("  Wind ", value);
  uint32_t now = nowEpoch();
  int32_t offset = weatherUtcOffset(w.localMidnight);
  if (w.sunrise && w.sunset && w.localMidnight) {
    bool sunrise = weatherShowSunrise(now, w.sunrise);
    formatClock12(sunrise ? w.sunrise : w.sunset, offset, value, sizeof(value));
    printPair(sunrise ? "  Sunrise " : "  Sunset ", value);
  }
}

static void drawHourly() {
  const WeatherData& w = app.weather;
  tft.fillRect(0, HOURLY_Y - 4, 320, 48, TFT_BLACK);
  tft.drawFastHLine(10, HOURLY_Y - 4, 300, TFT_DARKGREY);
  int count = min((int)w.hourlyCount, WEATHER_HOURLY_SHOWN);
  if (count == 0) {
    tft.setTextSize(1);
    tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
    tft.setCursor(12, HOURLY_Y + 14);
    tft.print("Hourly forecast unavailable");
    return;
  }
  char text[12];
  for (int i = 0; i < count; i++) {
    const HourlyForecast& h = w.hourly[i];
    int cx = 6 + i * HOURLY_W + HOURLY_W / 2;
    tft.setTextSize(1);
    tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    formatHour12(h.hour, text, sizeof(text));
    tft.setCursor(cx - tft.textWidth(text) / 2, HOURLY_Y + 2);
    tft.print(text);
    tft.setTextSize(2);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    formatDegrees(h.temp10, text, sizeof(text));
    tft.setCursor(cx - tft.textWidth(text) / 2, HOURLY_Y + 14);
    tft.print(text);
    tft.setTextSize(1);
    tft.setTextColor(TFT_CYAN, TFT_BLACK);
    formatPercent(h.precip, text, sizeof(text));
    tft.setCursor(cx - tft.textWidth(text) / 2, HOURLY_Y + 34);
    tft.print(text);
  }
}

// Cheap fingerprint of everything the text areas show (FNV-1a).
static uint32_t textHash() {
  const WeatherData& w = app.weather;
  uint32_t h = 2166136261u;
  auto mix = [&h](const void* data, size_t size) {
    const uint8_t* p = (const uint8_t*)data;
    for (size_t i = 0; i < size; i++) h = (h ^ p[i]) * 16777619u;
  };
  int16_t t[3] = {toTenths(w.temperatureC), toTenths(w.highC), toTenths(w.lowC)};
  mix(t, sizeof(t));
  mix(w.weatherCondition.c_str(), w.weatherCondition.length());
  mix(&w.weatherAvailable, 1);
  mix(&w.weatherCode, sizeof(w.weatherCode));
  mix(&w.feelsLike10, sizeof(w.feelsLike10));
  mix(&w.wind10, sizeof(w.wind10));
  mix(&w.humidity, 1);
  mix(&w.precipChance, 1);
  mix(&w.precipChanceMax, 1);
  mix(&w.hourlyCount, 1);
  mix(w.hourly, sizeof(w.hourly[0]) * w.hourlyCount);
  mix(&w.sunrise, sizeof(w.sunrise));
  mix(&w.sunset, sizeof(w.sunset));
  bool sunriseNext = weatherShowSunrise(nowEpoch(), w.sunrise);
  mix(&sunriseNext, 1);
  return h;
}

static void drawText() {
  drawCurrent();
  drawDetails();
  drawHourly();
  drawnTextHash = textHash();
}

void drawWeatherPage() {
  app.currentPage = PAGE_WEATHER;
  tft.fillScreen(TFT_BLACK);
  drawHeader("WEATHER");
  shownOnline = app.serverOnline;
  drawText();
  drawBackBar(nullptr, "BACK", nullptr);
  shownScene = currentScene();
  weatherAnimationShow(shownScene);
}

void refreshWeatherPage() {
  if (app.currentPage != PAGE_WEATHER) return;
  if (shownOnline != app.serverOnline) {
    shownOnline = app.serverOnline;
    drawHeader("WEATHER"); // LIVE / OFFLINE.
  }
  if (textHash() != drawnTextHash) drawText();
  WeatherScene scene = currentScene();
  if (scene != shownScene) {
    shownScene = scene;
    weatherAnimationShow(scene);
  }
}

void openWeather(Page origin) {
  weatherOrigin = origin;
  showPage(PAGE_WEATHER);
}

void handleWeatherTouch(int x, int y) {
  if (weatherHitAt(x, y) == WeatherHit::Back) showPage(appBackTarget(PAGE_WEATHER, weatherOrigin));
}

void updateWeatherPage() {
  if (app.currentPage != PAGE_WEATHER) return;
  // Day/night can change between status updates.
  static unsigned long lastCheck = 0;
  if (millis() - lastCheck >= 5000) {
    lastCheck = millis();
    refreshWeatherPage();
  }
  weatherAnimationUpdate();
}
