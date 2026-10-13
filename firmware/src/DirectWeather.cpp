#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <esp_heap_caps.h>
#include "DirectWeather.h"
#include "AppState.h"
#include "BacklightPwm.h"
#include "DeviceConfig.h"
#include "HomeScreen.h"
#include "LocalClockStore.h"
#include "LocalTime.h"
#include "LocationStore.h"
#include "LocationUiLogic.h"
#include "OpenMeteoCerts.h"
#include "TimeWeatherScreen.h"
#include "WeatherScreen.h"

static constexpr uint32_t WORKER_STACK_BYTES = 12288;   // TLS handshake + HTTPClient.
static constexpr uint32_t HTTP_TIMEOUT_MS = 10000;
static constexpr uint32_t BODY_DEADLINE_MS = 10000;
// A TLS session needs roughly 40 KB; never start one with less.
static constexpr uint32_t MIN_FREE_HEAP = 60000;
static constexpr uint32_t MIN_LARGEST_BLOCK = 30000;

// --- Shared between the main task, the setup page task and the worker ------------------

static portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;
static bool workerRunning = false;     // Guarded by lock.
static bool weatherPending = false;    // Guarded by lock.
static bool searchPending = false;     // Guarded by lock.
static Location weatherTarget;         // Guarded by lock: what to fetch.
static uint32_t weatherTargetRevision = 0;
static bool weatherDone = false;       // Worker -> main (guarded by lock).
static bool weatherOk = false;
static uint32_t weatherDoneRevision = 0;
static WeatherReport weatherResult;    // Valid when weatherDone && weatherOk.
static LocationSearchSnapshot search;  // Guarded by lock.
// World-clock offsets: the clocks to refresh (indexes into clockTargets,
// which the main task leaves alone while a batch runs) and their results.
struct ClockJob {
  uint8_t target;
  bool ok;
  int32_t offset;
};
static ClockJob clockJobs[LOCAL_CLOCKS_MAX];
static int clockJobCount = 0;
static bool clockPending = false;      // Guarded by lock.
static bool clockDone = false;         // Worker -> main (guarded by lock).
static const char* searchLabel = "Search"; // Guarded by lock: log prefix (phone or postal).

// --- Worker ------------------------------------------------------------------------------

enum class FetchError : uint8_t { None, NoWifi, NoTime, LowMemory, Connect, Http, TooLarge, Body };

static const char* fetchErrorText(FetchError e) {
  switch (e) {
    case FetchError::None: return "ok";
    case FetchError::NoWifi: return "no Wi-Fi";
    case FetchError::NoTime: return "clock not set yet (TLS needs the date)";
    case FetchError::LowMemory: return "not enough free memory";
    case FetchError::Connect: return "could not connect (DNS, network or TLS)";
    case FetchError::Http: return "HTTP error";
    case FetchError::TooLarge: return "response too large";
    case FetchError::Body: return "response incomplete";
  }
  return "";
}

// GET url over verified HTTPS into a malloc'd, NUL-terminated buffer (the
// caller frees it). HTTP 200 and 400 (Open-Meteo's {"error":true} body) return
// a body; anything else fails.
static FetchError httpsGet(const char* tag, const char* url, char*& body, size_t& length, int& status) {
  body = nullptr;
  length = 0;
  status = 0;
  if (WiFi.status() != WL_CONNECTED) return FetchError::NoWifi;
  if (!hasUtcTime()) return FetchError::NoTime;
  uint32_t heapBefore = ESP.getFreeHeap();
  if (heapBefore < MIN_FREE_HEAP || heap_caps_get_largest_free_block(MALLOC_CAP_8BIT) < MIN_LARGEST_BLOCK) {
    return FetchError::LowMemory;
  }
  WiFiClientSecure client;
  client.setCACert(OPEN_METEO_ROOT_CAS); // Verified; never setInsecure().
  client.setHandshakeTimeout(HTTP_TIMEOUT_MS / 1000);
  HTTPClient http;
  http.setConnectTimeout(HTTP_TIMEOUT_MS);
  http.setTimeout(HTTP_TIMEOUT_MS);
  http.setReuse(false);
  http.useHTTP10(true); // Identity body, no chunk framing.
  if (!http.begin(client, url)) return FetchError::Connect;
  status = http.GET();
  uint32_t heapDuring = ESP.getFreeHeap(); // TLS session up: the peak of the fetch.
  if (status <= 0) {
    char reason[96] = "";
    client.lastError(reason, sizeof(reason));
    Serial.printf("[%s] %s%s%s\n", tag, http.errorToString(status).c_str(), reason[0] ? ": " : "", reason);
    http.end();
    return FetchError::Connect;
  }
  if (status != HTTP_CODE_OK && status != HTTP_CODE_BAD_REQUEST) {
    http.end();
    return FetchError::Http;
  }
  int size = http.getSize();
  if (size == 0 || size > int(OPEN_METEO_BODY_MAX)) {
    http.end();
    return size == 0 ? FetchError::Body : FetchError::TooLarge;
  }
  size_t capacity = size > 0 ? size_t(size) : OPEN_METEO_BODY_MAX;
  body = (char*)malloc(capacity + 1);
  if (!body) {
    http.end();
    return FetchError::LowMemory;
  }
  WiFiClient* stream = http.getStreamPtr();
  unsigned long started = millis();
  bool ok = stream != nullptr;
  while (ok && (size < 0 || length < size_t(size))) {
    if (millis() - started >= BODY_DEADLINE_MS) { ok = false; break; }
    int available = stream->available();
    if (available > 0) {
      if (length >= capacity) { ok = false; break; } // Unknown length and over the cap.
      size_t want = capacity - length;
      if (size_t(available) < want) want = size_t(available);
      int got = stream->read((uint8_t*)body + length, want);
      if (got <= 0) { ok = false; break; }
      length += size_t(got);
    } else if (!stream->connected()) {
      break;
    } else {
      vTaskDelay(pdMS_TO_TICKS(5));
    }
  }
  if (size >= 0 && length != size_t(size)) ok = false;
  http.end();
  Serial.printf("[%s] HTTP %d, %u bytes; heap %u -> %u during, %u after\n", tag, status, unsigned(length),
                unsigned(heapBefore), unsigned(heapDuring), unsigned(ESP.getFreeHeap()));
  if (!ok || length == 0) {
    free(body);
    body = nullptr;
    return FetchError::Body;
  }
  body[length] = '\0';
  return FetchError::None;
}

static void runWeather(const Location& location, uint32_t revision) {
  char url[OPEN_METEO_URL_MAX];
  WeatherReport report;
  bool ok = false;
  if (!forecastUrl(url, sizeof(url), location.latitude, location.longitude)) {
    Serial.println("[WEATHER] Location coordinates are not valid");
  } else {
    char* body = nullptr;
    size_t length = 0;
    int status = 0;
    FetchError error = httpsGet("WEATHER", url, body, length, status);
    if (error != FetchError::None) {
      Serial.printf("[WEATHER] Not updated: %s%s\n", fetchErrorText(error),
                    error == FetchError::Http ? (status >= 500 ? " (server)" : " (request)") : "");
    } else {
      OpenMeteoResult parsed = parseForecast(body, length, report);
      ok = parsed == OpenMeteoResult::Ok;
      if (!ok) Serial.printf("[WEATHER] Not updated: %s\n", openMeteoResultText(parsed));
    }
    free(body);
  }
  portENTER_CRITICAL(&lock);
  weatherResult = report;
  weatherOk = ok;
  weatherDoneRevision = revision;
  weatherDone = true;
  portEXIT_CRITICAL(&lock);
}

static void runSearch(const char* query, uint32_t generation, const char* label) {
  char url[OPEN_METEO_URL_MAX];
  LocationSearchError failure = LocationSearchError::None;
  Location found[LOCATION_RESULTS_MAX];
  int count = 0;
  if (!geocodingUrl(url, sizeof(url), query)) {
    failure = LocationSearchError::Rejected;
  } else {
    char* body = nullptr;
    size_t length = 0;
    int status = 0;
    FetchError error = httpsGet("LOCATION", url, body, length, status);
    if (error == FetchError::NoWifi) failure = LocationSearchError::NoWifi;
    else if (error == FetchError::NoTime) failure = LocationSearchError::NoTime;
    else if (error != FetchError::None) failure = LocationSearchError::Network;
    else {
      OpenMeteoResult parsed = parseGeocoding(body, length, found, LOCATION_RESULTS_MAX, count);
      if (parsed == OpenMeteoResult::ApiError) failure = LocationSearchError::Rejected;
      else if (parsed != OpenMeteoResult::Ok) failure = LocationSearchError::Network;
    }
    free(body);
    if (error != FetchError::None) Serial.printf("[LOCATION] %s failed: %s\n", label, fetchErrorText(error));
    else Serial.printf("[LOCATION] %s: %d result(s)\n", label, count);
  }
  portENTER_CRITICAL(&lock);
  if (search.generation == generation) { // Not superseded by a newer search.
    search.state = failure == LocationSearchError::None ? LocationSearchState::Done : LocationSearchState::Failed;
    search.error = failure;
    search.count = failure == LocationSearchError::None ? count : 0;
    for (int i = 0; i < search.count; i++) search.results[i] = found[i];
  }
  portEXIT_CRITICAL(&lock);
}

// Main-task copy of the local clocks to refresh (identity, name for logs, and
// when each was last refreshed); rebuilt when the list changes.
struct ClockTarget {
  char name[LOCATION_NAME_MAX];
  char timezone[LOCATION_TZ_MAX];
  double latitude;
  double longitude;
  bool refreshed;          // This boot.
  uint32_t refreshedMs;
};
static ClockTarget clockTargets[LOCAL_CLOCKS_MAX];
static int clockTargetCount = 0;

static WorldClockLocation targetPlace(const ClockTarget& t) {
  WorldClockLocation c;
  memcpy(c.name, t.name, sizeof(c.name));
  memcpy(c.timezone, t.timezone, sizeof(c.timezone));
  c.latitude = t.latitude;
  c.longitude = t.longitude;
  return c;
}

// One small request per clock, one after another (never in parallel).
static void runClocks() {
  for (int i = 0; i < clockJobCount; i++) {
    ClockJob& job = clockJobs[i];
    WorldClockLocation place = targetPlace(clockTargets[job.target]);
    job.ok = false;
    char url[OPEN_METEO_URL_MAX];
    if (!clockOffsetUrl(url, sizeof(url), place)) {
      Serial.printf("[WORLD CLOCK] Offset refresh failed: %s (not a valid place)\n", place.name);
      continue;
    }
    char* body = nullptr;
    size_t length = 0;
    int status = 0;
    FetchError error = httpsGet("WORLD CLOCK", url, body, length, status);
    if (error != FetchError::None) {
      Serial.printf("[WORLD CLOCK] Offset refresh failed: %s (%s)\n", place.name, fetchErrorText(error));
    } else {
      OpenMeteoResult parsed = parseUtcOffset(body, length, job.offset);
      job.ok = parsed == OpenMeteoResult::Ok;
      if (!job.ok) Serial.printf("[WORLD CLOCK] Offset refresh failed: %s (%s)\n", place.name, openMeteoResultText(parsed));
    }
    free(body);
  }
  portENTER_CRITICAL(&lock);
  clockDone = true;
  portEXIT_CRITICAL(&lock);
}

// Runs whatever is pending (a search first: someone is waiting on the page),
// then exits; started again on the next request.
static void workerTask(void*) {
  for (;;) {
    bool doSearch = false, doWeather = false, doClocks = false;
    char query[LOCATION_QUERY_MAX];
    const char* label = "";
    uint32_t generation = 0, revision = 0;
    Location target;
    portENTER_CRITICAL(&lock);
    if (searchPending) {
      doSearch = true;
      searchPending = false;
      memcpy(query, search.query, sizeof(query));
      generation = search.generation;
      label = searchLabel;
    } else if (weatherPending) {
      doWeather = true;
      weatherPending = false;
      target = weatherTarget;
      revision = weatherTargetRevision;
    } else if (clockPending) {
      doClocks = true;
      clockPending = false;
    } else {
      workerRunning = false;
    }
    portEXIT_CRITICAL(&lock);
    if (doSearch) runSearch(query, generation, label);
    else if (doWeather) runWeather(target, revision);
    else if (doClocks) runClocks();
    else break;
  }
  vTaskDelete(nullptr);
}

// Any task. Starts the worker unless it is already running.
static bool ensureWorker() {
  portENTER_CRITICAL(&lock);
  bool start = !workerRunning;
  workerRunning = true;
  portEXIT_CRITICAL(&lock);
  if (!start) return true;
  if (xTaskCreate(workerTask, "openmeteo", WORKER_STACK_BYTES, nullptr, 1, nullptr) == pdPASS) return true;
  portENTER_CRITICAL(&lock);
  workerRunning = false;
  portEXIT_CRITICAL(&lock);
  Serial.println("[WEATHER] Could not start the Open-Meteo task");
  return false;
}

// --- Location search ------------------------------------------------------------------------

// The phone page and the device's postal-code keypad share this one search
// (same request, parser, worker and result list); only the log label differs.
static bool startSearch(const char* text, const char* label) {
  char query[LOCATION_QUERY_MAX];
  if (!normalizeLocationQuery(text, query, sizeof(query))) return false;
  portENTER_CRITICAL(&lock);
  searchLabel = label;
  search.generation++;
  search.state = LocationSearchState::Searching;
  search.error = LocationSearchError::None;
  search.count = 0;
  memcpy(search.query, query, sizeof(search.query));
  searchPending = true;
  portEXIT_CRITICAL(&lock);
  if (!ensureWorker()) {
    portENTER_CRITICAL(&lock);
    searchPending = false;
    search.state = LocationSearchState::Failed;
    search.error = LocationSearchError::Network;
    portEXIT_CRITICAL(&lock);
  }
  return true;
}

bool requestLocationSearch(const char* text) {
  return startSearch(text, "Search");
}

bool requestClockSearch(const char* text) {
  return startSearch(text, "World clock search");
}

ClockAddResult addClockFromSearch(uint32_t generation, int index, char* label, size_t size) {
  Location chosen;
  bool found = false;
  portENTER_CRITICAL(&lock);
  if (search.state == LocationSearchState::Done && search.generation == generation && index >= 0 &&
      index < search.count) {
    chosen = search.results[index];
    found = true;
  }
  portEXIT_CRITICAL(&lock);
  if (size) label[0] = '\0';
  if (!found) return ClockAddResult::Stale;
  formatLocationLabel(chosen, label, size);
  return addLocalClockPlace(clockFromLocation(chosen)); // The clock list only, never the device location.
}

bool requestPostalSearch(const char* digits) {
  if (!validPostalCode(digits)) return false;
  Serial.printf("[LOCATION] Postal search: \"%s\"\n", digits);
  return startSearch(digits, "Postal search");
}

LocationSearchState locationSearchState() {
  portENTER_CRITICAL(&lock);
  LocationSearchState state = search.state;
  portEXIT_CRITICAL(&lock);
  return state;
}

void locationSearchSnapshot(LocationSearchSnapshot& out) {
  portENTER_CRITICAL(&lock);
  out = search;
  portEXIT_CRITICAL(&lock);
}

bool chooseLocationResult(uint32_t generation, int index) {
  Location chosen;
  bool ok = false;
  portENTER_CRITICAL(&lock);
  if (search.state == LocationSearchState::Done && search.generation == generation && index >= 0 &&
      index < search.count) {
    chosen = search.results[index];
    ok = true;
  }
  portEXIT_CRITICAL(&lock);
  return ok && saveLocation(chosen);
}

void resetLocationSearch() {
  portENTER_CRITICAL(&lock);
  uint32_t generation = search.generation + 1; // Late results of an old search are ignored.
  search = LocationSearchSnapshot();
  search.generation = generation;
  searchPending = false;
  portEXIT_CRITICAL(&lock);
}

// --- Main task --------------------------------------------------------------------------------

static WeatherSchedule schedule;
static uint32_t appliedRevision = 0xFFFFFFFFu;
static WeatherSource appliedSource = WeatherSource::None;
static bool inFlight = false;
static bool haveData = false;      // Direct data shown (current or last known).
static bool lastFailed = false;
static uint32_t lastSuccessMs = 0;

WeatherSource weatherSource() {
  return weatherSourceFor(locationConfigured(), backendConfigured());
}

WeatherView weatherView() {
  WeatherSource source = weatherSource();
  bool data = source == WeatherSource::Direct ? haveData
              : app.weather.weatherAvailable || app.weather.weatherCode >= 0;
  return weatherViewFor(source, data, lastFailed && !inFlight);
}

void directWeatherInfo(DirectWeatherInfo& out) {
  out.fetching = inFlight;
  out.haveData = haveData;
  out.failed = lastFailed;
  out.ageMs = haveData ? millis() - lastSuccessMs : 0;
}

static void redrawWeatherViews() {
  switch (app.currentPage) {
    case PAGE_HOME: refreshHomePage(); break;
    case PAGE_WEATHER: refreshWeatherPage(); break;
    case PAGE_TIME: drawTimePage(); break;
    default: break;
  }
}

// Source or location changed: forget what was shown; a backend refills its
// own weather with the next status, a new location with its first forecast.
static void resetWeather() {
  app.weather = WeatherData();
  backlightSetSchedule(SolarSchedule(), millis()); // Full brightness until valid data.
  updateBacklight();
  schedule = WeatherSchedule();
  haveData = false;
  lastFailed = false;
  invalidateLocalClock();
}

static void applyReport(const WeatherReport& r) {
  uint32_t now = utcNow();
  WeatherData& w = app.weather;
  w.temperatureC = r.temperatureC;
  w.highC = r.highC;
  w.lowC = r.lowC;
  w.weatherCode = r.weatherCode;
  w.weatherCondition = weatherDescription(r.weatherCode);
  w.weatherAvailable = true;
  w.feelsLike10 = r.feelsLike10;
  w.wind10 = r.wind10;
  w.humidity = r.humidity;
  w.precipChance = r.precipChance;
  w.precipChanceMax = r.precipChanceMax;
  w.hourlyCount = r.hourlyCount;
  for (int i = 0; i < WEATHER_HOURLY_MAX; i++) w.hourly[i] = r.hourly[i];
  w.sunrise = r.sunrise;
  w.sunset = r.sunset;
  w.localMidnight = r.dayStart;
  w.observedAt = now ? now : r.observedAt;
  w.observedMs = millis();
  backlightSetSchedule(solarScheduleFor(r, w.observedAt), millis());
  updateBacklight();
  saveLocationOffset(r.utcOffset); // DST changes arrive here; stored only if changed.
  invalidateLocalClock();
}

// --- World-clock offsets (main task) ------------------------------------------------------------

static ClockOffsetSchedule clockSchedule;
static bool clockInFlight = false;
static uint32_t clockTargetsRevision = 0xFFFFFFFFu;
static WorldClockSource loggedClockSource = WorldClockSource::None;
static bool clockSourceLogged = false;

// The store changed (added or removed): rebuild the targets, keeping when
// each known clock was refreshed; a new clock is fetched at once.
static void rebuildClockTargets() {
  LocalClockList list;
  localClocks(list);
  ClockTarget next[LOCAL_CLOCKS_MAX];
  bool added = false;
  for (int i = 0; i < list.count; i++) {
    const WorldClockLocation& c = list.items[i];
    ClockTarget& t = next[i];
    memcpy(t.name, c.name, sizeof(t.name));
    memcpy(t.timezone, c.timezone, sizeof(t.timezone));
    t.latitude = c.latitude;
    t.longitude = c.longitude;
    t.refreshed = false;
    t.refreshedMs = 0;
    for (int j = 0; j < clockTargetCount; j++) {
      if (sameClockPlace(targetPlace(clockTargets[j]), c)) {
        t.refreshed = clockTargets[j].refreshed;
        t.refreshedMs = clockTargets[j].refreshedMs;
      }
    }
    added |= !t.refreshed;
  }
  memcpy(clockTargets, next, sizeof(next));
  clockTargetCount = list.count;
  if (added) clockBatchReset(clockSchedule);
}

static void serviceWorldClocks() {
  WorldClockSource source = worldClockSourceFor(localClockCount(), backendConfigured(), app.time.worldClocks.count);
  if (!clockSourceLogged || source != loggedClockSource) {
    clockSourceLogged = true;
    loggedClockSource = source;
    Serial.printf("[WORLD CLOCK] Source: %s\n", worldClockSourceName(source));
  }

  bool done = false;
  portENTER_CRITICAL(&lock);
  if (clockDone) {
    done = true;
    clockDone = false;
  }
  portEXIT_CRITICAL(&lock);
  if (done && clockInFlight) {
    clockInFlight = false;
    bool allOk = true;
    for (int i = 0; i < clockJobCount; i++) {
      const ClockJob& job = clockJobs[i];
      ClockTarget& t = clockTargets[job.target];
      // A failure keeps the last known offset; a clock removed meanwhile is skipped.
      if (job.ok && applyLocalClockOffset(targetPlace(t), job.offset)) {
        t.refreshed = true;
        t.refreshedMs = millis();
        char text[16];
        formatUtcOffset(job.offset, text, sizeof(text));
        Serial.printf("[WORLD CLOCK] Offset %s: %s\n", t.name, text);
      } else if (!job.ok) {
        allOk = false;
      }
    }
    clockBatchFinished(clockSchedule, allOk, millis());
  }
  if (clockInFlight) return;
  if (localClocksRevision() != clockTargetsRevision) {
    clockTargetsRevision = localClocksRevision();
    rebuildClockTargets();
  }
  if (clockTargetCount == 0 || WiFi.status() != WL_CONNECTED || !hasUtcTime()) return;
  if (!clockBatchAllowed(clockSchedule, millis())) return;
  int count = 0;
  for (int i = 0; i < clockTargetCount; i++) {
    if (!clockOffsetDue(clockTargets[i].refreshed, clockTargets[i].refreshedMs, millis())) continue;
    clockJobs[count].target = uint8_t(i);
    clockJobs[count].ok = false;
    clockJobs[count].offset = 0;
    count++;
  }
  if (count == 0) return;
  clockJobCount = count;
  portENTER_CRITICAL(&lock);
  clockPending = true;
  portEXIT_CRITICAL(&lock);
  if (ensureWorker()) {
    clockInFlight = true;
  } else {
    portENTER_CRITICAL(&lock);
    clockPending = false;
    portEXIT_CRITICAL(&lock);
    clockBatchFinished(clockSchedule, false, millis());
  }
}

void serviceDirectWeather() {
  serviceWorldClocks(); // Independent of the weather source (needs only Wi-Fi and NTP).
  WeatherSource source = weatherSource();
  uint32_t revision = locationRevision();
  if (source != appliedSource || revision != appliedRevision) {
    bool wasDirect = appliedSource == WeatherSource::Direct;
    bool first = appliedRevision == 0xFFFFFFFFu;
    appliedSource = source;
    appliedRevision = revision;
    if (!first && (wasDirect || source == WeatherSource::Direct)) {
      resetWeather();
      redrawWeatherViews();
    }
    if (source == WeatherSource::Direct) Serial.println("[WEATHER] Source: direct (Open-Meteo)");
    else if (source == WeatherSource::Backend) Serial.println("[WEATHER] Source: HomeDeck Backend");
    else Serial.println("[WEATHER] Source: none (set a location)");
  }
  if (source != WeatherSource::Direct) return;

  bool done = false, ok = false;
  uint32_t doneRevision = 0;
  WeatherReport report;
  portENTER_CRITICAL(&lock);
  if (weatherDone) {
    done = true;
    ok = weatherOk;
    doneRevision = weatherDoneRevision;
    report = weatherResult;
    weatherDone = false;
  }
  portEXIT_CRITICAL(&lock);
  if (done && inFlight) {
    inFlight = false;
    if (doneRevision != revision) {
      schedule = WeatherSchedule(); // For an older location: fetch the new one now.
    } else {
      weatherFetchFinished(schedule, ok);
      lastFailed = !ok;
      if (ok) {
        applyReport(report);
        haveData = true;
        lastSuccessMs = millis();
        Serial.printf("[WEATHER] Updated: %s\n", app.weather.weatherCondition.c_str());
      }
      redrawWeatherViews();
    }
  }

  // Last known, after missed refreshes: still shown, marked as such.
  if (haveData && app.weather.weatherAvailable && millis() - lastSuccessMs >= WEATHER_STALE_MS) {
    app.weather.weatherAvailable = false;
    redrawWeatherViews();
  }

  if (inFlight || WiFi.status() != WL_CONNECTED || !hasUtcTime()) return;
  if (!weatherFetchDue(schedule, millis())) return;
  Location target;
  if (!currentLocation(target)) return;
  weatherFetchStarted(schedule, millis());
  portENTER_CRITICAL(&lock);
  weatherTarget = target;
  weatherTargetRevision = revision;
  weatherPending = true;
  portEXIT_CRITICAL(&lock);
  if (ensureWorker()) {
    inFlight = true;
    if (!haveData) redrawWeatherViews(); // "Loading..."
  } else {
    portENTER_CRITICAL(&lock);
    weatherPending = false;
    portEXIT_CRITICAL(&lock);
    weatherFetchFinished(schedule, false);
    lastFailed = true;
  }
}
