#include <Arduino.h>
#include "HomeScreen.h"
#include "AlertManager.h"
#include "AlertsScreen.h"
#include "AppState.h"
#include "Display.h"
#include "HomeLogic.h"
#include "PageRouter.h"
#include "UiHelpers.h"
#include "UiIcons.h"
#include "UiTheme.h"
#include "WeatherScreen.h"

// Fingerprints of what each area shows, so a refresh redraws only changes.
static uint32_t shownClock = 0, shownWeather = 0, shownHealth = 0, shownMetrics = 0;

static uint32_t fnv(const char* text, uint32_t h = 2166136261u) {
  while (*text) h = (h ^ (uint8_t)*text++) * 16777619u;
  return h;
}

static uint32_t fnvInt(int value, uint32_t h) {
  char buffer[12];
  snprintf(buffer, sizeof(buffer), "%d|", value);
  return fnv(buffer, h);
}

// --- Date and time ---------------------------------------------------------------------

static void drawClock() {
  const TimeData& time = app.time;
  tft.fillRect(0, 0, 320, HOME_WEATHER_TOP - 2, TFT_BLACK);
  char date[24];
  snprintf(date, sizeof(date), "%s", time.localDate.c_str());
  for (char* c = date; *c; c++) *c = toupper(*c);
  tft.setTextSize(2);
  tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  tft.setCursor(12, 8);
  tft.print(date);

  bool live = app.serverOnline;
  const char* state = live ? "LIVE" : "OFFLINE";
  tft.setTextSize(1);
  int w = tft.textWidth(state);
  tft.fillCircle(304 - w - 8, 15, 3, live ? UiColor::Green : UiColor::Red);
  tft.setTextColor(live ? UiColor::Green : UiColor::Red, TFT_BLACK);
  tft.setCursor(308 - w, 12);
  tft.print(state);

  // "10:42 AM": large digits, small AM/PM.
  char clock[16];
  snprintf(clock, sizeof(clock), "%s", time.localTime.c_str());
  char* suffix = strchr(clock, ' ');
  if (suffix) *suffix++ = '\0';
  tft.setTextSize(5);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setCursor(12, HOME_TIME_Y);
  tft.print(clock);
  if (suffix) {
    int x = 12 + tft.textWidth(clock) + 8;
    tft.setTextSize(2);
    tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    tft.setCursor(x, HOME_TIME_Y + 24);
    tft.print(suffix);
  }
}

static uint32_t clockHash() {
  uint32_t h = fnv(app.time.localTime.c_str());
  h = fnv(app.time.localDate.c_str(), h);
  return fnvInt(app.serverOnline, h);
}

// --- Weather -----------------------------------------------------------------------------

static void drawWeatherSummary() {
  const WeatherData& w = app.weather;
  tft.fillRect(0, HOME_WEATHER_TOP, 320, HOME_WEATHER_BOTTOM - HOME_WEATHER_TOP + 1, TFT_BLACK);
  drawUiIcon(UiIcon::Weather, 12, HOME_WEATHER_TOP + 6, 34, TFT_WHITE, UiColor::Yellow, TFT_BLACK);
  int x = 58;
  if (!w.weatherAvailable && w.weatherCode < 0) {
    tft.setTextSize(2);
    tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
    tft.setCursor(x, HOME_WEATHER_TOP + 14);
    tft.print("Weather unavailable");
    return;
  }
  char text[24];
  formatDegrees(toTenths(w.temperatureC), text, sizeof(text));
  strncat(text, "C", sizeof(text) - strlen(text) - 1);
  tft.setTextSize(3);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setCursor(x, HOME_WEATHER_TOP + 6);
  tft.print(text);
  int conditionX = x + tft.textWidth(text) + 12;
  const char* condition = w.weatherCondition.c_str();
  tft.setTextSize(conditionX + (int)strlen(condition) * 12 <= 294 ? 2 : 1); // Clear of the ">".
  tft.setTextColor(UiColor::Cyan, TFT_BLACK);
  tft.setCursor(conditionX, HOME_WEATHER_TOP + 10);
  tft.print(condition);

  tft.setTextSize(1);
  tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  tft.setCursor(x, HOME_WEATHER_TOP + 34);
  uint32_t now = weatherNowEpoch(w.observedAt, w.observedMs, millis());
  if (w.sunrise && w.sunset && w.localMidnight) {
    bool sunrise = weatherShowSunrise(now, w.sunrise);
    formatClock12(sunrise ? w.sunrise : w.sunset, weatherUtcOffset(w.localMidnight), text, sizeof(text));
    tft.print(sunrise ? "Sunrise " : "Sunset ");
    tft.print(text);
  }
  if (!w.weatherAvailable) tft.print("  (last known)");
  tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
  tft.setCursor(306, HOME_WEATHER_TOP + 20);
  tft.print(">");
}

static uint32_t weatherHash() {
  const WeatherData& w = app.weather;
  uint32_t h = fnv(w.weatherCondition.c_str());
  h = fnvInt(toTenths(w.temperatureC), h);
  h = fnvInt(w.weatherAvailable, h);
  h = fnvInt(w.weatherCode, h);
  h = fnvInt((int)w.sunrise, h);
  h = fnvInt((int)w.sunset, h);
  return fnvInt(weatherShowSunrise(weatherNowEpoch(w.observedAt, w.observedMs, millis()), w.sunrise), h);
}

// --- Health ---------------------------------------------------------------------------------

static uint16_t healthColor(HomeHealth health) {
  switch (health) {
    case HomeHealth::Healthy: return UiColor::Green;
    case HomeHealth::Attention: return UiColor::Orange;
    case HomeHealth::Critical: return UiColor::Red;
    default: return UiColor::Grey;
  }
}

static void drawHealth() {
  const AlertTable& table = alerts();
  int critical = alertsCount(table, AlertSeverity::Critical);
  int warnings = alertsCount(table, AlertSeverity::Warning);
  HomeHealth health = homeHealth(table.haveStatus, critical, warnings);
  uint16_t color = healthColor(health);
  int top = HOME_HEALTH_TOP + 2, h = HOME_HEALTH_BOTTOM - HOME_HEALTH_TOP - 2;
  tft.fillRect(0, HOME_HEALTH_TOP, 320, HOME_HEALTH_BOTTOM - HOME_HEALTH_TOP + 1, TFT_BLACK);
  tft.fillRoundRect(8, top, 304, h, 8, UiColor::Tile);
  tft.fillCircle(26, top + h / 2, 7, color);
  tft.setTextSize(2);
  tft.setTextColor(color, UiColor::Tile);
  tft.setCursor(44, top + 5);
  tft.print(homeHealthLabel(health));
  char text[40];
  formatAlertCounts(table.haveStatus, critical, warnings, text, sizeof(text));
  tft.setTextSize(1);
  tft.setTextColor(TFT_LIGHTGREY, UiColor::Tile);
  tft.setCursor(44, top + 25);
  tft.print(text);
  tft.setTextSize(2);
  tft.setTextColor(TFT_DARKGREY, UiColor::Tile);
  tft.setCursor(292, top + h / 2 - 7);
  tft.print(">");
}

static uint32_t healthHash() {
  const AlertTable& table = alerts();
  uint32_t h = fnvInt(table.haveStatus, 2166136261u);
  h = fnvInt(alertsCount(table, AlertSeverity::Critical), h);
  return fnvInt(alertsCount(table, AlertSeverity::Warning), h);
}

// --- CPU / RAM -------------------------------------------------------------------------------

static bool metricsCurrent() {
  return app.serverOnline && app.metrics.hostAvailable;
}

static void drawMetrics() {
  tft.fillRect(0, HOME_METRICS_TOP, 320, HOME_METRICS_BOTTOM - HOME_METRICS_TOP + 1, TFT_BLACK);
  char text[32];
  formatCpuRam(metricsCurrent(), app.metrics.cpuPercent, app.metrics.memPercent, text, sizeof(text));
  tft.setTextSize(2);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setCursor(12, HOME_METRICS_TOP + 8);
  tft.print(text);
  tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
  tft.setCursor(292, HOME_METRICS_TOP + 8);
  tft.print(">");
}

static uint32_t metricsHash() {
  char text[32];
  formatCpuRam(metricsCurrent(), app.metrics.cpuPercent, app.metrics.memPercent, text, sizeof(text));
  return fnv(text);
}

// --- Page --------------------------------------------------------------------------------------

void drawHomePage() {
  app.currentPage = PAGE_HOME;
  tft.fillScreen(TFT_BLACK);
  drawClock();
  drawWeatherSummary();
  drawHealth();
  drawMetrics();
  drawNavigation();
  shownClock = clockHash();
  shownWeather = weatherHash();
  shownHealth = healthHash();
  shownMetrics = metricsHash();
}

void refreshHomePage() {
  if (app.currentPage != PAGE_HOME) return;
  uint32_t h;
  if ((h = clockHash()) != shownClock) { drawClock(); shownClock = h; }
  if ((h = weatherHash()) != shownWeather) { drawWeatherSummary(); shownWeather = h; }
  if ((h = healthHash()) != shownHealth) { drawHealth(); shownHealth = h; }
  if ((h = metricsHash()) != shownMetrics) { drawMetrics(); shownMetrics = h; }
}

bool handleHomeTouch(int x, int y) {
  switch (homeHitAt(x, y)) {
    case HomeHit::Weather: openWeather(PAGE_HOME); return true;
    case HomeHit::Alerts: openAlerts(PAGE_HOME); return true;
    case HomeHit::HomeServer: showPage(PAGE_HOMESERVER); return true;
    case HomeHit::None: return false;
  }
  return false;
}
