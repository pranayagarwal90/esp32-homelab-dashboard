#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include "StatusClient.h"
#include "AlertLogic.h"
#include "AlertManager.h"
#include "ApiConfig.h"
#include "AppState.h"
#include "BacklightPwm.h"
#include "CalendarScreen.h"
#include "NetworkManager.h"
#include "PageRouter.h"
#include "WeatherScreen.h"

static const unsigned long REFRESH_INTERVAL = 10000;
static unsigned long lastRefresh = 0; // Main task only.

// A single slot, transferred through queues. The worker cannot reuse it until
// the main task has consumed the result and submitted the next request.
struct StatusSnapshot {
  SolarSchedule solar;
  SystemMetrics metrics;
  DockerData docker;
  ServiceState services;
  TimeData time;
  WeatherData weather;
};

struct StatusRequest { int year, month, day; };
enum class StatusOutcome { Success, ReconnectFailed, HttpFailed, InvalidBody };
struct StatusResult {
  const StatusSnapshot* snapshot;
  StatusOutcome outcome;
};

static StatusSnapshot statusSnapshot;
static QueueHandle_t statusRequests = nullptr;
static QueueHandle_t statusResults = nullptr;
static bool statusInFlight = false; // Main task only.
static constexpr uint32_t STATUS_TIMEOUT_MS = 8000;
static constexpr size_t STATUS_MAX_BODY = 32768;
static constexpr uint32_t STATUS_STACK_BYTES = 8192;

// HTTPClient's timeout is an inactivity timeout. Enforce an absolute deadline
// too, including servers that drip header bytes without finishing a line.
class StatusClient : public WiFiClient {
  unsigned long started = millis();
  bool expired() {
    if (millis() - started < STATUS_TIMEOUT_MS) return false;
    WiFiClient::stop();
    return true;
  }
public:
  bool timedOut() const { return millis() - started >= STATUS_TIMEOUT_MS; }
  int available() override { return expired() ? 0 : WiFiClient::available(); }
  int read() override { return expired() ? -1 : WiFiClient::read(); }
  int read(uint8_t* buffer, size_t size) override {
    return expired() ? -1 : WiFiClient::read(buffer, size);
  }
  uint8_t connected() override { return expired() ? 0 : WiFiClient::connected(); }
};

// Optional rich-weather fields; anything missing or malformed stays unknown.
static void parseRichWeather(JsonVariantConst w, WeatherData& weather) {
  auto tenths = [](JsonVariantConst v) -> int16_t {
    return v.is<float>() ? toTenths(v.as<float>()) : WEATHER_NO_VALUE;
  };
  auto percent = [](JsonVariantConst v) -> int8_t {
    return v.is<int>() && validPercent(v.as<int>()) ? (int8_t)v.as<int>() : -1;
  };
  weather.weatherCode = w["weather_code"].is<int>() ? (int16_t)w["weather_code"].as<int>() : -1;
  weather.feelsLike10 = tenths(w["feels_like_c"]);
  weather.wind10 = tenths(w["wind_kmh"]);
  weather.humidity = percent(w["humidity"]);
  weather.precipChance = percent(w["precip_probability"]);
  weather.precipChanceMax = percent(w["precip_probability_max"]);
  weather.hourlyCount = 0;
  for (JsonVariantConst item : w["hourly"].as<JsonArrayConst>()) {
    if (weather.hourlyCount >= WEATHER_HOURLY_MAX) break;
    if (!item["h"].is<int>() || !validHour(item["h"].as<int>()) || !item["t"].is<float>()) continue;
    HourlyForecast& slot = weather.hourly[weather.hourlyCount++];
    slot.hour = (uint8_t)item["h"].as<int>();
    slot.temp10 = toTenths(item["t"].as<float>());
    slot.precip = percent(item["p"]);
    slot.code = item["c"].is<int>() ? (int16_t)item["c"].as<int>() : -1;
  }
}

// Runs on the status worker task. Must not touch AppState or the TFT.
static StatusOutcome readStatus(const StatusRequest& request, StatusSnapshot& snapshot) {
  if (!ensureWiFiConnected(STATUS_TIMEOUT_MS)) return StatusOutcome::ReconnectFailed;

  StatusClient client;
  HTTPClient http;
  http.setConnectTimeout(STATUS_TIMEOUT_MS);
  http.setTimeout(STATUS_TIMEOUT_MS);
  http.setReuse(false);
  // HTTP/1.0 requests an identity body, avoiding raw chunk framing.
  http.useHTTP10(true);
  if (!http.begin(client, API_STATUS)) return StatusOutcome::HttpFailed;
  int code = http.GET();
  if (code != HTTP_CODE_OK) {
    Serial.printf("HTTP error: %d\n", code);
    http.end();
    return StatusOutcome::HttpFailed;
  }

  int length = http.getSize();
  String body;
  bool valid = length != 0 && length <= (int)STATUS_MAX_BODY;
  if (valid) valid = body.reserve(length > 0 ? length : 1024);
  unsigned long bodyStart = millis();
  while (valid && (length < 0 || body.length() < (size_t)length)) {
    if (millis() - bodyStart >= STATUS_TIMEOUT_MS) { valid = false; break; }
    int available = client.available();
    if (available > 0) {
      uint8_t buffer[512];
      size_t count = min((size_t)available, sizeof(buffer));
      if (length >= 0) count = min(count, (size_t)length - body.length());
      int received = client.read(buffer, count);
      if (received <= 0 || body.length() + received > STATUS_MAX_BODY ||
          !body.concat((const char*)buffer, received)) { valid = false; break; }
    } else if (!client.connected()) {
      break;
    } else {
      vTaskDelay(pdMS_TO_TICKS(1));
    }
  }
  if (client.timedOut() || (length >= 0 && body.length() != (size_t)length)) valid = false;
  http.end();
  if (!valid) return StatusOutcome::InvalidBody;

  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, body);
  if (error) {
    Serial.print("JSON error: ");
    Serial.println(error.c_str());
    return StatusOutcome::InvalidBody;
  }

  SystemMetrics& metrics = snapshot.metrics;
  DockerData& docker = snapshot.docker;
  ServiceState& services = snapshot.services;
  TimeData& time = snapshot.time;
  WeatherData& weather = snapshot.weather;

  time.currentYear = request.year;
  time.currentMonth = request.month;
  time.currentDay = request.day;
  // Older backends without the key always reported live host metrics.
  metrics.hostAvailable = doc["host_available"] | true;
  metrics.uptimeHours = doc["uptime_hours"] | 0.0;
  metrics.cpuPercent = doc["cpu"]["percent"] | 0.0;

  metrics.memUsed = doc["memory"]["used_gb"] | 0.0;
  metrics.memTotal = doc["memory"]["total_gb"] | 0.0;
  metrics.memPercent = doc["memory"]["percent"] | 0.0;

  metrics.diskCount = 0;
  for (JsonObject disk : doc["disks"].as<JsonArray>()) {
    if (metrics.diskCount >= MAX_DISKS) break;
    metrics.diskNames[metrics.diskCount] = disk["name"].as<String>();
    metrics.diskLabels[metrics.diskCount] = disk["label"].as<String>();
    metrics.diskUsedGb[metrics.diskCount] = disk["used_gb"] | 0.0;
    metrics.diskTotalGb[metrics.diskCount] = disk["total_gb"] | 0.0;
    metrics.diskPercentages[metrics.diskCount] = disk["percent"] | 0.0;
    metrics.diskCount++;
  }

  metrics.gpuPercent = doc["gpu"]["percent"] | 0.0;
  metrics.gpuName = doc["gpu"]["name"].as<String>();

  metrics.wifiAvailable = doc["wifi"]["available"] | false;
  metrics.wifiLinkMbps = doc["wifi"]["link_mbps"] | 0.0;
  metrics.wifiRxMbps = doc["wifi"]["receive_mbps"] | 0.0;
  metrics.wifiTxMbps = doc["wifi"]["send_mbps"] | 0.0;

  if (doc["wifi"]["signal_percent"].is<int>()) {
    metrics.wifiSignalPercent = doc["wifi"]["signal_percent"].as<int>();
  } else {
    metrics.wifiSignalPercent = -1;
  }

  docker.totalRunningContainers = doc["docker"]["running"] | 0;
  docker.displayedContainers = 0;
  for (JsonObject container : doc["docker"]["containers"].as<JsonArray>()) {
    if (docker.displayedContainers >= MAX_CONTAINERS) break;
    docker.containerNames[docker.displayedContainers] = container["name"].as<String>();
    docker.containerRunning[docker.displayedContainers] = container["running"] | false;
    docker.displayedContainers++;
  }

  services.serviceJellyfin = doc["services"]["jellyfin"] | false;
  services.serviceNavidrome = doc["services"]["navidrome"] | false;
  services.serviceNextcloud = doc["services"]["nextcloud"] | false;
  services.serviceImmich = doc["services"]["immich"] | false;
  services.serviceOllama = doc["services"]["ollama"] | false;
  services.serviceCloudflare = doc["services"]["cloudflare"] | false;
  services.serviceTechnicalBlog = doc["services"]["technical_blog"] | false;
  services.serviceMetube = doc["services"]["metube"] | false;

  static const struct { const char* key; AlertService service; } MONITORED[] = {
    {"jellyfin", ALERT_JELLYFIN}, {"navidrome", ALERT_NAVIDROME}, {"metube", ALERT_METUBE},
    {"ollama", ALERT_OLLAMA}, {"cloudflare", ALERT_CLOUDFLARE},
  };
  services.alertReported = 0;
  for (const auto& monitored : MONITORED) {
    if (doc["services"][monitored.key].is<bool>()) services.alertReported |= 1u << monitored.service;
  }

  time.localTime = doc["timezones"]["local"]["time"].as<String>();
  time.localDate = doc["timezones"]["local"]["date"].as<String>();
  time.indiaTime = doc["timezones"]["india"]["time"].as<String>();
  time.singaporeTime = doc["timezones"]["singapore"]["time"].as<String>();
  time.londonTime = doc["timezones"]["london"]["time"].as<String>();

  time.currentYear = doc["timezones"]["local"]["year"] | time.currentYear;
  time.currentMonth = doc["timezones"]["local"]["month"] | time.currentMonth;
  time.currentDay = doc["timezones"]["local"]["day"] | time.currentDay;

  weather.weatherAvailable = doc["weather"]["available"] | false;
  // Strict numeric parsing: missing/null/string/negative values invalidate the
  // entire schedule rather than interpreting absent sunrise as midnight.
  auto epochField = [](JsonVariantConst value) -> uint32_t {
    return value.is<uint32_t>() ? value.as<uint32_t>() : 0;
  };
  snapshot.solar.sunrise = epochField(doc["weather"]["sunrise_timestamp"]);
  snapshot.solar.sunset = epochField(doc["weather"]["sunset_timestamp"]);
  snapshot.solar.dayStart = epochField(doc["weather"]["solar_day_start"]);
  snapshot.solar.dayEnd = epochField(doc["weather"]["solar_day_end"]);
  snapshot.solar.validUntil = epochField(doc["weather"]["solar_valid_until"]);
  snapshot.solar.timestamp = epochField(doc["timestamp"]);

  if (weather.weatherAvailable) {
    weather.temperatureC = doc["weather"]["temperature_c"] | 0.0;
    weather.highC = doc["weather"]["high_c"] | 0.0;
    weather.lowC = doc["weather"]["low_c"] | 0.0;
    weather.weatherCondition = doc["weather"]["condition"].as<String>();
    parseRichWeather(doc["weather"], weather);
    weather.sunrise = snapshot.solar.sunrise;
    weather.sunset = snapshot.solar.sunset;
    weather.localMidnight = snapshot.solar.dayStart;
    weather.observedAt = snapshot.solar.timestamp;
  }

  return StatusOutcome::Success;
}

static void statusWorker(void*) {
  StatusRequest request;
  for (;;) {
    if (xQueueReceive(statusRequests, &request, portMAX_DELAY) != pdTRUE) continue;
    StatusOutcome outcome = readStatus(request, statusSnapshot);
    StatusResult result = {
      outcome == StatusOutcome::Success ? &statusSnapshot : nullptr, outcome
    };
    xQueueSend(statusResults, &result, portMAX_DELAY);
    // No snapshot access after publication until a new request arrives.
  }
}

void setupStatusWorker() {
  statusRequests = xQueueCreate(1, sizeof(StatusRequest));
  statusResults = xQueueCreate(1, sizeof(StatusResult));
  if (statusRequests && statusResults &&
      xTaskCreate(statusWorker, "status", STATUS_STACK_BYTES, nullptr, 1, nullptr) == pdPASS) return;
  if (statusRequests) vQueueDelete(statusRequests);
  if (statusResults) vQueueDelete(statusResults);
  statusRequests = statusResults = nullptr;
  Serial.println("Could not create status worker");
}

void fetchHomelabStatus() {
  if (!statusRequests || statusInFlight) return;
  StatusRequest request = {app.time.currentYear, app.time.currentMonth, app.time.currentDay};
  if (xQueueSend(statusRequests, &request, 0) == pdTRUE) {
    unsigned long now = millis();
    // Refreshes overdue beyond the interval were skipped on purpose (games,
    // photos); that time is not a backend failure.
    if (now - lastRefresh > REFRESH_INTERVAL) alertsOnFetchPaused(now - lastRefresh - REFRESH_INTERVAL);
    statusInFlight = true;
    lastRefresh = now;
  }
}

static void applyStatusSnapshot(const StatusSnapshot& snapshot) {
  backlightSetSchedule(snapshot.solar, millis());
  updateBacklight();
  app.metrics = snapshot.metrics;
  app.docker = snapshot.docker;
  app.services = snapshot.services;
  app.time = snapshot.time;
  // Unavailable weather keeps the last known values for display.
  if (snapshot.weather.weatherAvailable) {
    app.weather = snapshot.weather;
    app.weather.observedMs = millis();
  }
  else app.weather.weatherAvailable = false;
  syncCalendarIfUnset(app.time.currentYear, app.time.currentMonth);
  app.serverOnline = true;
}

void processStatusResult() {
  StatusResult result;
  if (!statusResults || xQueueReceive(statusResults, &result, 0) != pdTRUE) return;
  if (result.snapshot) {
    bool wasOnline = app.serverOnline;
    applyStatusSnapshot(*result.snapshot);
    bool alertsChanged = alertsOnStatus();
    // ALERTS redraws only when something it shows changed.
    bool redraw = app.currentPage != PAGE_ALERTS || alertsChanged || !wasOnline;
    // WEATHER updates its text in place and keeps its animation running.
    if (app.currentPage == PAGE_WEATHER) {
      refreshWeatherPage();
      redraw = false;
    }
    // The stopwatch refreshes itself and shows no status.
    if (redraw && !isGamePlayPage(app.currentPage) && app.currentPage != PAGE_GAMES &&
        app.currentPage != PAGE_PHOTOS && app.currentPage != PAGE_SCREENSAVER &&
        app.currentPage != PAGE_STOPWATCH) drawCurrentPage();
  } else {
    bool alertsChanged = alertsOnFetchFailed();
    bool redrawn = false;
    if (result.outcome == StatusOutcome::ReconnectFailed || result.outcome == StatusOutcome::HttpFailed) {
      app.serverOnline = false;
      if (result.outcome == StatusOutcome::ReconnectFailed && app.currentPage != PAGE_SCREENSAVER &&
          !isGamePlayPage(app.currentPage) && app.currentPage != PAGE_STOPWATCH) {
        drawCurrentPage();
        redrawn = true;
      }
    }
    if (alertsChanged && !redrawn && (app.currentPage == PAGE_HOME || app.currentPage == PAGE_ALERTS)) {
      drawCurrentPage();
    }
  }
  // The slot is released only after every String has been copied and drawing is done.
  lastRefresh = millis();
  statusInFlight = false;
}

bool statusRefreshDue() {
  return millis() - lastRefresh >= REFRESH_INTERVAL;
}
