// Direct Open-Meteo weather and location search: URLs, parsing (recorded
// London / New York responses), failure handling, memory bounds, source
// precedence, the refresh schedule and night-dimming input.
// Needs ArduinoJson (header-only, from a previous `pio run`), like test_update.
#include "OpenMeteoParse.h"
#include <assert.h>
#include <fstream>
#include <sstream>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>

static std::string readFile(const char* name) {
  for (const char* prefix : {"firmware/test/vectors/", "test/vectors/", "vectors/"}) {
    std::ifstream file(std::string(prefix) + name, std::ios::binary);
    if (file) {
      std::stringstream text;
      text << file.rdbuf();
      return text.str();
    }
  }
  fprintf(stderr, "cannot find %s (run from the repository root)\n", name);
  assert(false);
  return "";
}

// Counts what ArduinoJson allocates: current and peak bytes.
class CountingAllocator : public ArduinoJson::Allocator {
 public:
  size_t current = 0, peak = 0;
  void* allocate(size_t size) override {
    size_t* block = (size_t*)malloc(size + sizeof(size_t));
    *block = size;
    current += size;
    if (current > peak) peak = current;
    return block + 1;
  }
  void deallocate(void* pointer) override {
    if (!pointer) return;
    size_t* block = (size_t*)pointer - 1;
    current -= *block;
    free(block);
  }
  void* reallocate(void* pointer, size_t size) override {
    if (!pointer) return allocate(size);
    size_t* block = (size_t*)pointer - 1;
    current -= *block;
    block = (size_t*)realloc(block, size + sizeof(size_t));
    *block = size;
    current += size;
    if (current > peak) peak = current;
    return block + 1;
  }
};

static size_t forecastPeak = 0, geocodingPeak = 0, offsetPeak = 0;

static void testUrls() {
  char url[OPEN_METEO_URL_MAX];
  assert(forecastUrl(url, sizeof(url), 51.5085, -0.1257));
  assert(strcmp(url,
      "https://api.open-meteo.com/v1/forecast?latitude=51.5085&longitude=-0.1257"
      "&current=temperature_2m,apparent_temperature,relative_humidity_2m,weather_code,wind_speed_10m"
      "&hourly=temperature_2m,precipitation_probability,weather_code"
      "&daily=temperature_2m_max,temperature_2m_min,sunrise,sunset,precipitation_probability_max"
      "&timezone=auto&timeformat=unixtime&forecast_days=2&forecast_hours=8"
      "&temperature_unit=celsius&wind_speed_unit=kmh") == 0);
  // No API key, ever; HTTPS only.
  assert(!strstr(url, "apikey") && strncmp(url, "https://", 8) == 0);
  assert(forecastUrl(url, sizeof(url), -33.8688, 151.2093) && strstr(url, "latitude=-33.8688&longitude=151.2093"));
  assert(forecastUrl(url, sizeof(url), 1, 2, TemperatureUnit::Fahrenheit) && strstr(url, "temperature_unit=fahrenheit"));
  assert(!forecastUrl(url, sizeof(url), 91, 0) && url[0] == '\0');
  assert(!forecastUrl(url, sizeof(url), 0, -180.5) && url[0] == '\0');
  assert(!forecastUrl(url, 40, 1, 2) && url[0] == '\0'); // Too small: never a cut URL.

  assert(geocodingUrl(url, sizeof(url), "  London ") &&
         strcmp(url, "https://geocoding-api.open-meteo.com/v1/search?name=London&count=5&language=en&format=json") == 0);
  assert(geocodingUrl(url, sizeof(url), "Los Angeles, CA") && strstr(url, "name=Los%20Angeles%2C%20CA&"));
  assert(geocodingUrl(url, sizeof(url), "10001") && strstr(url, "name=10001&count=5"));
  assert(geocodingUrl(url, sizeof(url), "Z\xC3\xBCrich") && strstr(url, "name=Z%C3%BCrich&"));
  assert(geocodingUrl(url, sizeof(url), "a&count=100") && strstr(url, "name=a%26count%3D100&count=5&")); // No injection.
  assert(!geocodingUrl(url, sizeof(url), "x") && url[0] == '\0');                  // Under 2 characters.
  assert(!geocodingUrl(url, sizeof(url), "   ") && !geocodingUrl(url, sizeof(url), ""));
  assert(!geocodingUrl(url, sizeof(url), "line\nbreak"));
  std::string longQuery(64, 'a');
  assert(!geocodingUrl(url, sizeof(url), longQuery.c_str()));
  assert(geocodingUrl(url, sizeof(url), longQuery.substr(0, 63).c_str()));
}

static void testForecast() {
  std::string body = readFile("open_meteo_forecast_london.json");
  WeatherReport r;
  CountingAllocator counter;
  assert(parseForecast(body.data(), body.size(), r, &counter) == OpenMeteoResult::Ok);
  assert(counter.current == 0); // Everything freed.
  forecastPeak = counter.peak;
  assert(r.utcOffset == 3600 && r.observedAt == 1791836100u);
  assert(r.temperatureC > 17.2f && r.temperatureC < 17.4f && r.highC > 17.6f && r.lowC < r.highC);
  assert(r.weatherCode == 3 && weatherConditionFor(r.weatherCode) == WeatherCondition::Cloudy);
  assert(r.feelsLike10 == 176 && r.wind10 == 112 && r.humidity == 90 && r.precipChanceMax == 45);
  // Hours: 8 (forecast_hours), local hours from 21:00 BST, current-hour rain chance.
  assert(r.hourlyCount == 8 && r.hourly[0].hour == 21 && r.hourly[1].hour == 22 && r.hourly[3].hour == 0);
  assert(r.precipChance == 3 && r.hourly[0].precip == 3 && r.hourly[0].code >= 0);
  // Solar day: local midnight (BST), today's sunrise/sunset, tomorrow's sunrise.
  assert(r.dayStart == 1791759600u && r.dayEnd == 1791846000u);
  assert(r.sunrise == 1791785956u && r.sunset == 1791825236u && r.validUntil == 1791872458u);
  assert(weatherUtcOffset(r.dayStart) == 3600); // The UI's existing offset helper agrees.
  // Night dimming input: valid, and night at observation time (after sunset).
  SolarSchedule s = solarScheduleFor(r, r.observedAt);
  assert(s.valid());
  BacklightControl backlight;
  backlight.setSchedule(s, 0);
  assert(backlight.isNight(0) && backlight.duty(0) == BacklightControl::NIGHT_DUTY);
  backlight.setSchedule(solarScheduleFor(r, r.sunrise + 3600), 0);
  assert(!backlight.isNight(0)); // Daytime.
}

static std::string replaced(std::string text, const char* from, const char* to) {
  size_t at = text.find(from);
  assert(at != std::string::npos);
  return text.replace(at, strlen(from), to);
}

static void testForecastFailures() {
  std::string good = readFile("open_meteo_forecast_london.json");
  WeatherReport r;
  r.temperatureC = 99;
  // Malformed, truncated, empty, not an object: nothing applied.
  assert(parseForecast(good.data(), good.size() / 2, r) == OpenMeteoResult::BadJson && r.temperatureC == 0);
  assert(parseForecast("", 0, r) == OpenMeteoResult::BadJson);
  assert(parseForecast(nullptr, 0, r) == OpenMeteoResult::BadJson);
  assert(parseForecast("[1,2]", 5, r) == OpenMeteoResult::BadJson);
  assert(parseForecast("<html>502</html>", 16, r) == OpenMeteoResult::BadJson);
  // HTTP 400 body.
  std::string error = readFile("open_meteo_error.json");
  assert(parseForecast(error.data(), error.size(), r) == OpenMeteoResult::ApiError);
  // Required fields.
  const char* breaks[][2] = {
    {"\"utc_offset_seconds\":3600", "\"ignored\":3600"},
    {"\"temperature_2m_max\":[", "\"ignored\":["},
    {"\"weather_code\":3,", "\"weather_code\":\"x\","},
    {"\"current\":{\"time\":1791836100", "\"current\":{\"time\":12"},
  };
  for (auto& b : breaks) {
    std::string broken = replaced(good, b[0], b[1]);
    assert(parseForecast(broken.data(), broken.size(), r) == OpenMeteoResult::MissingFields);
  }
  std::string badOffset = replaced(good, "\"utc_offset_seconds\":3600", "\"utc_offset_seconds\":3601");
  assert(parseForecast(badOffset.data(), badOffset.size(), r) == OpenMeteoResult::MissingFields);
  // Optional fields missing: still Ok, values unknown.
  std::string noExtras = replaced(replaced(good, "\"apparent_temperature\":17", "\"x1\":17"), "\"sunrise\":[", "\"x2\":[");
  assert(parseForecast(noExtras.data(), noExtras.size(), r) == OpenMeteoResult::Ok);
  assert(r.feelsLike10 == WEATHER_NO_VALUE && r.sunrise == 0 && r.dayStart == 0);
  assert(!solarScheduleFor(r, r.observedAt).valid()); // No dimming without solar data.
  // Inconsistent solar day (sunset before sunrise): dropped, not used.
  std::string swapped = replaced(good, "\"sunset\":[1791825236", "\"sunset\":[1791785000");
  assert(parseForecast(swapped.data(), swapped.size(), r) == OpenMeteoResult::Ok && r.sunrise == 0);
}

static void testGeocoding() {
  Location results[LOCATION_RESULTS_MAX];
  int count = -1;
  std::string london = readFile("open_meteo_geocoding_london.json");
  CountingAllocator counter;
  assert(parseGeocoding(london.data(), london.size(), results, LOCATION_RESULTS_MAX, count, &counter) ==
         OpenMeteoResult::Ok);
  geocodingPeak = counter.peak;
  assert(count == 5);
  const Location& first = results[0];
  assert(strcmp(first.name, "London") == 0 && strcmp(first.admin1, "England") == 0 &&
         strcmp(first.country, "United Kingdom") == 0 && strcmp(first.timezone, "Europe/London") == 0);
  assert(first.latitude > 51.5 && first.latitude < 51.6 && first.longitude < 0 && !first.offsetKnown);
  char label[96];
  formatLocationLabel(first, label, sizeof(label));
  assert(strcmp(label, "London, England, United Kingdom") == 0);

  // Postal code search; a larger response (postcode lists) is fine.
  std::string postal = readFile("open_meteo_geocoding_10001.json");
  assert(postal.size() < OPEN_METEO_BODY_MAX);
  counter = CountingAllocator();
  assert(parseGeocoding(postal.data(), postal.size(), results, LOCATION_RESULTS_MAX, count, &counter) ==
         OpenMeteoResult::Ok && count >= 1 && strcmp(results[0].name, "New York") == 0);
  if (counter.peak > geocodingPeak) geocodingPeak = counter.peak;

  // Bounds: never more than asked for.
  assert(parseGeocoding(london.data(), london.size(), results, 2, count) == OpenMeteoResult::Ok && count == 2);
  // No results: "results" omitted -> Ok, 0.
  std::string none = readFile("open_meteo_geocoding_none.json");
  assert(parseGeocoding(none.data(), none.size(), results, LOCATION_RESULTS_MAX, count) == OpenMeteoResult::Ok &&
         count == 0);
  // Errors.
  std::string error = readFile("open_meteo_error.json");
  assert(parseGeocoding(error.data(), error.size(), results, 5, count) == OpenMeteoResult::ApiError && count == 0);
  assert(parseGeocoding("{\"results\":", 11, results, 5, count) == OpenMeteoResult::BadJson);
  // A non-array "results" is dropped by the filter: no results, nothing applied.
  assert(parseGeocoding("{\"results\":7}", 13, results, 5, count) == OpenMeteoResult::Ok && count == 0);
  // Bad entries skipped; long names cut at a UTF-8 boundary; bad timezone dropped.
  std::string mixed = "{\"results\":[{\"name\":\"NoCoords\"},{\"name\":\"Far\",\"latitude\":95,\"longitude\":0},"
                      "{\"name\":\"" + std::string(60, 'x') + "\xC3\xBC\",\"latitude\":1,\"longitude\":2,"
                      "\"timezone\":\"Bad Zone;\"},{\"name\":\"Ok\",\"latitude\":-1.5,\"longitude\":-2.5}]}";
  assert(parseGeocoding(mixed.data(), mixed.size(), results, 5, count) == OpenMeteoResult::Ok && count == 2);
  assert(strlen(results[0].name) == LOCATION_NAME_MAX - 1 && results[0].timezone[0] == '\0');
  assert(strcmp(results[1].name, "Ok") == 0);
}

static void testLocationValidation() {
  Location l;
  assert(!validLocation(l)); // No name.
  snprintf(l.name, sizeof(l.name), "Reykjavik");
  assert(validLocation(l));
  l.latitude = 90.0001;
  assert(!validLocation(l));
  l.latitude = -90;
  l.longitude = 180;
  assert(validLocation(l));
  l.longitude = -180.01;
  assert(!validLocation(l));
  l.longitude = 0;
  l.latitude = 0.0 / 0.0;  // NaN.
  assert(!validLocation(l));
  l.latitude = 64.1;
  snprintf(l.timezone, sizeof(l.timezone), "Atlantic/Reykjavik");
  assert(validLocation(l));
  snprintf(l.timezone, sizeof(l.timezone), "../etc");
  assert(!validLocation(l));
  snprintf(l.timezone, sizeof(l.timezone), "Etc/GMT+5");
  assert(validLocation(l));
  l.offsetKnown = true;
  l.utcOffset = 20700;  // +5:45 (Nepal).
  assert(validLocation(l));
  l.utcOffset = 15 * 3600;
  assert(!validLocation(l));
  l.utcOffset = 0;
  snprintf(l.name, sizeof(l.name), "<script>");
  assert(!validLocation(l));
  snprintf(l.name, sizeof(l.name), "   ");
  assert(!validLocation(l));
  char query[LOCATION_QUERY_MAX];
  assert(normalizeLocationQuery("  Springfield, IL  ", query, sizeof(query)) && strcmp(query, "Springfield, IL") == 0);
}

static void testSourcePrecedence() {
  assert(weatherSourceFor(true, false) == WeatherSource::Direct);
  assert(weatherSourceFor(true, true) == WeatherSource::Direct);     // A location wins over the backend.
  assert(weatherSourceFor(false, true) == WeatherSource::Backend);   // Existing users: unchanged.
  assert(weatherSourceFor(false, false) == WeatherSource::None);
  // What the weather areas show.
  assert(weatherViewFor(WeatherSource::None, true, false) == WeatherView::SetLocation);
  assert(weatherViewFor(WeatherSource::Direct, false, false) == WeatherView::Loading);
  assert(weatherViewFor(WeatherSource::Direct, false, true) == WeatherView::Unavailable);
  assert(weatherViewFor(WeatherSource::Direct, true, true) == WeatherView::Data);   // Cached data stays.
  assert(weatherViewFor(WeatherSource::Backend, false, false) == WeatherView::Unavailable);
  assert(weatherViewFor(WeatherSource::Backend, true, false) == WeatherView::Data);
}

static void testSchedule() {
  WeatherSchedule s;
  assert(weatherFetchDue(s, 0));                       // First fetch at once.
  weatherFetchStarted(s, 1000);
  assert(!weatherFetchDue(s, 2000));                   // One in flight: not again.
  weatherFetchFinished(s, true);
  assert(!weatherFetchDue(s, 1000 + WEATHER_REFRESH_MS - 1) && weatherFetchDue(s, 1000 + WEATHER_REFRESH_MS));
  // Failures back off and never go below 2 minutes or above 30.
  uint32_t expected[] = {2, 5, 10, 20, 30, 30, 30};
  uint32_t now = 1000;
  for (uint32_t minutes : expected) {
    weatherFetchStarted(s, now);
    weatherFetchFinished(s, false);
    assert(s.waitMs == minutes * 60000UL);
    assert(!weatherFetchDue(s, now + s.waitMs - 1) && weatherFetchDue(s, now + s.waitMs));
    now += s.waitMs;
  }
  // An hour of failures: at most 6 requests (2+5+10+20+30 > 60 min after 5).
  WeatherSchedule t;
  int requests = 0;
  for (uint32_t ms = 0; ms < 3600000UL; ms += 1000) {
    if (weatherFetchDue(t, ms)) {
      weatherFetchStarted(t, ms);
      weatherFetchFinished(t, false);
      requests++;
    }
  }
  assert(requests <= 6);
  weatherFetchStarted(s, now);
  weatherFetchFinished(s, true);
  assert(s.failures == 0 && s.waitMs == WEATHER_REFRESH_MS);
  // Millis rollover.
  WeatherSchedule u;
  weatherFetchStarted(u, 0xFFFFFF00u);
  weatherFetchFinished(u, true);
  assert(!weatherFetchDue(u, 0x100) && weatherFetchDue(u, 0xFFFFFF00u + WEATHER_REFRESH_MS));
}

static void testWeatherCodes() {
  struct { int code; WeatherCondition condition; } table[] = {
    {0, WeatherCondition::Clear}, {1, WeatherCondition::Clear}, {2, WeatherCondition::PartlyCloudy},
    {3, WeatherCondition::Cloudy}, {45, WeatherCondition::Fog}, {48, WeatherCondition::Fog},
    {51, WeatherCondition::Drizzle}, {57, WeatherCondition::Drizzle}, {61, WeatherCondition::Rain},
    {66, WeatherCondition::Rain}, {81, WeatherCondition::Rain}, {65, WeatherCondition::HeavyRain},
    {67, WeatherCondition::HeavyRain}, {82, WeatherCondition::HeavyRain}, {71, WeatherCondition::Snow},
    {77, WeatherCondition::Snow}, {86, WeatherCondition::Snow}, {95, WeatherCondition::Thunderstorm},
    {96, WeatherCondition::Thunderstorm}, {97, WeatherCondition::Thunderstorm},
    {99, WeatherCondition::Thunderstorm}, {4, WeatherCondition::Unknown}, {-1, WeatherCondition::Unknown},
  };
  for (auto& row : table) assert(weatherConditionFor(row.code) == row.condition);
  // Every documented WMO code has a word, and the backend's words are kept.
  for (int code : {0, 1, 2, 3, 45, 48, 51, 53, 55, 56, 57, 61, 63, 65, 66, 67, 71, 73, 75, 77, 80, 81, 82, 85, 86,
                   95, 96, 97, 99}) {
    assert(strcmp(weatherDescription(code), "Unknown") != 0 && strlen(weatherDescription(code)) <= 16);
    assert(weatherConditionFor(code) != WeatherCondition::Unknown);
  }
  assert(strcmp(weatherDescription(1), "Mostly clear") == 0 && strcmp(weatherDescription(3), "Cloudy") == 0);
  assert(strcmp(weatherDescription(42), "Unknown") == 0);
}

// World clocks: the metadata-only response (recorded 2026-10-13 from
// /v1/forecast?latitude=..&longitude=..&timezone=<zone>, no variables).
static void testUtcOffset() {
  const char* kathmandu = "{\"latitude\":27.732864,\"longitude\":85.34831,\"generationtime_ms\":0.0017881393432617188,"
                          "\"utc_offset_seconds\":20700,\"timezone\":\"Asia/Kathmandu\",\"timezone_abbreviation\":"
                          "\"GMT+5:45\",\"elevation\":1293.0}";
  const char* newYork = "{\"latitude\":40.710335,\"longitude\":-73.99308,\"generationtime_ms\":0.0059604644775390625,"
                        "\"utc_offset_seconds\":-14400,\"timezone\":\"America/New_York\",\"timezone_abbreviation\":"
                        "\"GMT-4\",\"elevation\":0.0}";
  int32_t offset = 1;
  CountingAllocator counter;
  assert(parseUtcOffset(kathmandu, strlen(kathmandu), offset, &counter) == OpenMeteoResult::Ok && offset == 20700);
  assert(counter.current == 0);
  offsetPeak = counter.peak;
  assert(parseUtcOffset(newYork, strlen(newYork), offset) == OpenMeteoResult::Ok && offset == -14400);
  const char* kolkata = "{\"utc_offset_seconds\":19800,\"timezone\":\"Asia/Kolkata\"}";
  assert(parseUtcOffset(kolkata, strlen(kolkata), offset) == OpenMeteoResult::Ok && offset == 19800);
  // Failures: the caller keeps the last known offset.
  struct { const char* body; OpenMeteoResult expected; } bad[] = {
    {"{\"reason\":\"Invalid timezone\",\"error\":true}", OpenMeteoResult::ApiError},   // HTTP 400 body.
    {"{\"timezone\":\"Asia/Tokyo\"}", OpenMeteoResult::MissingFields},                  // No offset.
    {"{\"utc_offset_seconds\":\"3600\"}", OpenMeteoResult::MissingFields},              // Not a number.
    {"{\"utc_offset_seconds\":3601}", OpenMeteoResult::MissingFields},                    // Not a real offset.
    {"{\"utc_offset_seconds\":54000}", OpenMeteoResult::MissingFields},                   // +15 h.
    {"{\"utc_offset_seconds\":36.5}", OpenMeteoResult::MissingFields},
    {"[1,2]", OpenMeteoResult::BadJson},
    {"{\"utc_offset_seconds\":", OpenMeteoResult::BadJson},                               // Cut short.
    {"<html>502</html>", OpenMeteoResult::BadJson},
    {"", OpenMeteoResult::BadJson},
  };
  for (const auto& b : bad) {
    offset = 1;
    assert(parseUtcOffset(b.body, strlen(b.body), offset) == b.expected && offset == 0);
  }
  assert(parseUtcOffset(nullptr, 0, offset) == OpenMeteoResult::BadJson);
}

int main() {
  testUrls();
  testForecast();
  testForecastFailures();
  testGeocoding();
  testLocationValidation();
  testSourcePrecedence();
  testSchedule();
  testWeatherCodes();
  testUtcOffset();
  printf("open-meteo tests passed (JSON peak: forecast %u B, geocoding %u B, utc offset %u B)\n",
         unsigned(forecastPeak), unsigned(geocodingPeak), unsigned(offsetPeak));
  return 0;
}
