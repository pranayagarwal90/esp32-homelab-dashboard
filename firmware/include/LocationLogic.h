#pragma once
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "TimeLogic.h"

// The device's location (for local time and direct Open-Meteo weather), the
// weather-source precedence, request URLs and the refresh schedule. Pure,
// host-tested. Stored by LocationStore (NVS namespace "location").
//
// Nothing here is personal: a location exists only once the user picks one.

constexpr size_t LOCATION_NAME_MAX = 48;     // Including the terminator.
constexpr size_t LOCATION_REGION_MAX = 40;   // admin1 / country.
constexpr size_t LOCATION_TZ_MAX = 40;       // IANA name, e.g. "America/Argentina/Buenos_Aires".
constexpr size_t LOCATION_QUERY_MAX = 64;    // Typed search, including the terminator.
constexpr int LOCATION_RESULTS_MAX = 5;      // Geocoding results asked for and kept.

struct Location {
  char name[LOCATION_NAME_MAX] = "";
  char admin1[LOCATION_REGION_MAX] = "";
  char country[LOCATION_REGION_MAX] = "";
  char timezone[LOCATION_TZ_MAX] = "";
  double latitude = 0;
  double longitude = 0;
  bool offsetKnown = false;  // Set by the first successful forecast.
  int32_t utcOffset = 0;     // Seconds; the last one Open-Meteo reported.
};

inline bool validLatitude(double value) {
  return value == value && value >= -90.0 && value <= 90.0;
}

inline bool validLongitude(double value) {
  return value == value && value >= -180.0 && value <= 180.0;
}

// Printable text (no control characters, no quotes or angle brackets), not
// blank, shorter than size. UTF-8 bytes (names such as "Zürich") are allowed.
inline bool validLocationText(const char* text, size_t size, bool required) {
  if (!text) return !required;
  size_t length = strnlen(text, size);
  if (length >= size) return false;
  if (length == 0) return !required;
  bool visible = false;
  for (size_t i = 0; i < length; i++) {
    unsigned char c = (unsigned char)text[i];
    if (c < 0x20 || c == 0x7F || c == '"' || c == '<' || c == '>' || c == '\\') return false;
    visible |= c != ' ';
  }
  return visible;
}

// IANA names: letters, digits, '/', '_', '-', '+' ("Etc/GMT+5"). Optional.
inline bool validTimezoneName(const char* text) {
  if (!text) return true;
  size_t length = strnlen(text, LOCATION_TZ_MAX);
  if (length >= LOCATION_TZ_MAX) return false;
  for (size_t i = 0; i < length; i++) {
    char c = text[i];
    bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
              c == '/' || c == '_' || c == '-' || c == '+';
    if (!ok) return false;
  }
  return true;
}

inline bool validLocation(const Location& l) {
  return validLocationText(l.name, sizeof(l.name), true) && validLocationText(l.admin1, sizeof(l.admin1), false) &&
         validLocationText(l.country, sizeof(l.country), false) && validTimezoneName(l.timezone) &&
         validLatitude(l.latitude) && validLongitude(l.longitude) &&
         (!l.offsetKnown || validUtcOffset(l.utcOffset));
}

// "Springfield, Illinois, United States" (parts that exist, without repeats).
inline void formatLocationLabel(const Location& l, char* out, size_t size) {
  snprintf(out, size, "%s", l.name);
  const char* parts[] = {l.admin1, l.country};
  for (const char* part : parts) {
    if (!part[0] || strcmp(part, l.name) == 0) continue;
    size_t used = strlen(out);
    if (used + 2 < size) snprintf(out + used, size - used, ", %s", part);
  }
}

// Typed search text: trimmed, 2..63 bytes, printable (Open-Meteo returns
// nothing for fewer than 2 characters). Copies the trimmed text into out.
inline bool normalizeLocationQuery(const char* input, char* out, size_t size) {
  if (!out || size == 0) return false;
  out[0] = '\0';
  if (!input) return false;
  while (*input == ' ' || *input == '\t') input++;
  size_t length = strlen(input);
  while (length > 0 && (input[length - 1] == ' ' || input[length - 1] == '\t' || input[length - 1] == '\r' ||
                        input[length - 1] == '\n')) length--;
  if (length < 2 || length >= LOCATION_QUERY_MAX || length >= size) return false;
  for (size_t i = 0; i < length; i++) {
    unsigned char c = (unsigned char)input[i];
    if (c < 0x20 || c == 0x7F) return false;
  }
  memcpy(out, input, length);
  out[length] = '\0';
  return true;
}

// Location search (Settings > Location: the phone page or the postal-code keypad).
enum class LocationSearchState : uint8_t { Idle, Searching, Done, Failed };
enum class LocationSearchError : uint8_t { None, NoWifi, NoTime, Network, Rejected };

// --- Weather source -----------------------------------------------------------------------
// 1. A location on the device: direct Open-Meteo (even with a backend).
// 2. Otherwise a backend: its /api/status weather, as before.
// 3. Neither: "Set location".
enum class WeatherSource : uint8_t { None, Direct, Backend };

inline WeatherSource weatherSourceFor(bool locationConfigured, bool backendConfigured) {
  if (locationConfigured) return WeatherSource::Direct;
  return backendConfigured ? WeatherSource::Backend : WeatherSource::None;
}

// What the weather areas show.
enum class WeatherView : uint8_t {
  SetLocation,   // No source.
  Loading,       // Direct: no result yet, a fetch is pending.
  Unavailable,   // No data (direct: every fetch so far failed; backend: none sent).
  Data           // Current or last-known values (weatherAvailable says which).
};

inline WeatherView weatherViewFor(WeatherSource source, bool haveData, bool directFailed) {
  if (source == WeatherSource::None) return WeatherView::SetLocation;
  if (haveData) return WeatherView::Data;
  if (source == WeatherSource::Direct && !directFailed) return WeatherView::Loading;
  return WeatherView::Unavailable;
}

// --- Requests ------------------------------------------------------------------------------

constexpr const char* OPEN_METEO_FORECAST_URL = "https://api.open-meteo.com/v1/forecast";
constexpr const char* OPEN_METEO_GEOCODING_URL = "https://geocoding-api.open-meteo.com/v1/search";
constexpr int WEATHER_FORECAST_HOURS = 8;  // WEATHER_HOURLY_MAX: what the UI stores.
constexpr size_t OPEN_METEO_URL_MAX = 480;

enum class TemperatureUnit : uint8_t { Celsius, Fahrenheit };

// Percent-encodes everything but RFC 3986 unreserved characters.
inline bool urlEncode(const char* text, char* out, size_t size) {
  static const char hex[] = "0123456789ABCDEF";
  size_t n = 0;
  for (const unsigned char* p = (const unsigned char*)text; *p; p++) {
    bool plain = (*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || (*p >= '0' && *p <= '9') ||
                 *p == '-' || *p == '_' || *p == '.' || *p == '~';
    if (n + (plain ? 1 : 3) >= size) {
      if (size) out[0] = '\0';
      return false;
    }
    if (plain) {
      out[n++] = char(*p);
    } else {
      out[n++] = '%';
      out[n++] = hex[*p >> 4];
      out[n++] = hex[*p & 15];
    }
  }
  out[n] = '\0';
  return true;
}

// One combined request with only what the screens use. The UI is in Celsius
// and km/h; timeformat=unixtime gives UTC epochs (sunrise/sunset included),
// timezone=auto the location's own day and its current utc_offset_seconds.
inline bool forecastUrl(char* out, size_t size, double latitude, double longitude,
                        TemperatureUnit unit = TemperatureUnit::Celsius) {
  if (!validLatitude(latitude) || !validLongitude(longitude)) {
    if (size) out[0] = '\0';
    return false;
  }
  int written = snprintf(out, size,
      "%s?latitude=%.4f&longitude=%.4f"
      "&current=temperature_2m,apparent_temperature,relative_humidity_2m,weather_code,wind_speed_10m"
      "&hourly=temperature_2m,precipitation_probability,weather_code"
      "&daily=temperature_2m_max,temperature_2m_min,sunrise,sunset,precipitation_probability_max"
      "&timezone=auto&timeformat=unixtime&forecast_days=2&forecast_hours=%d"
      "&temperature_unit=%s&wind_speed_unit=kmh",
      OPEN_METEO_FORECAST_URL, latitude, longitude, WEATHER_FORECAST_HOURS,
      unit == TemperatureUnit::Fahrenheit ? "fahrenheit" : "celsius");
  if (written < 0 || size_t(written) >= size) {
    if (size) out[0] = '\0';
    return false;
  }
  return true;
}

inline bool geocodingUrl(char* out, size_t size, const char* query) {
  char normalized[LOCATION_QUERY_MAX];
  char encoded[LOCATION_QUERY_MAX * 3];
  if (!normalizeLocationQuery(query, normalized, sizeof(normalized)) ||
      !urlEncode(normalized, encoded, sizeof(encoded))) {
    if (size) out[0] = '\0';
    return false;
  }
  int written = snprintf(out, size, "%s?name=%s&count=%d&language=en&format=json", OPEN_METEO_GEOCODING_URL,
                         encoded, LOCATION_RESULTS_MAX);
  if (written < 0 || size_t(written) >= size) {
    out[0] = '\0';
    return false;
  }
  return true;
}

// --- Refresh schedule ------------------------------------------------------------------------
// Every 30 minutes after a success (also picks up a DST change); after a
// failure 2, 5, 10, 20 then every 30 minutes. Never more often.
constexpr uint32_t WEATHER_REFRESH_MS = 30UL * 60UL * 1000UL;
// Older than this (three missed refreshes), shown as last known.
constexpr uint32_t WEATHER_STALE_MS = 95UL * 60UL * 1000UL;

inline uint32_t weatherRetryDelayMs(uint8_t failures) {
  static const uint32_t minutes[] = {2, 5, 10, 20, 30};
  uint8_t i = failures == 0 ? 0 : failures - 1;
  if (i > 4) i = 4;
  return minutes[i] * 60UL * 1000UL;
}

struct WeatherSchedule {
  bool started = false;     // A first fetch has been requested.
  uint32_t lastAttemptMs = 0;
  uint32_t waitMs = 0;      // Until the next attempt.
  uint8_t failures = 0;     // Consecutive.
};

// A configured location with Wi-Fi and a valid clock (TLS needs the date):
// the first fetch at once, then by the schedule.
inline bool weatherFetchDue(const WeatherSchedule& s, uint32_t nowMs) {
  return !s.started || nowMs - s.lastAttemptMs >= s.waitMs;
}

inline void weatherFetchStarted(WeatherSchedule& s, uint32_t nowMs) {
  s.started = true;
  s.lastAttemptMs = nowMs;
  s.waitMs = WEATHER_REFRESH_MS; // Until the result says otherwise.
}

inline void weatherFetchFinished(WeatherSchedule& s, bool ok) {
  if (ok) {
    s.failures = 0;
    s.waitMs = WEATHER_REFRESH_MS;
  } else {
    if (s.failures < 255) s.failures++;
    s.waitMs = weatherRetryDelayMs(s.failures);
  }
}

// --- Condition text ----------------------------------------------------------------------------
// Same words as the backend (server.py weather_description) for the codes it
// knows; the category mapping itself is WeatherLogic.h's weatherConditionFor.
inline const char* weatherDescription(int code) {
  switch (code) {
    case 0: return "Clear";
    case 1: return "Mostly clear";
    case 2: return "Partly cloudy";
    case 3: return "Cloudy";
    case 45: case 48: return "Fog";
    case 51: return "Light drizzle";
    case 53: return "Drizzle";
    case 55: return "Heavy drizzle";
    case 56: case 57: return "Freezing drizzle";
    case 61: return "Light rain";
    case 63: return "Rain";
    case 65: return "Heavy rain";
    case 66: case 67: return "Freezing rain";
    case 71: return "Light snow";
    case 73: return "Snow";
    case 75: return "Heavy snow";
    case 77: return "Snow grains";
    case 80: case 81: return "Rain showers";
    case 82: return "Heavy showers";
    case 85: case 86: return "Snow showers";
    case 95: case 96: case 97: case 99: return "Thunderstorm";
    default: return "Unknown";
  }
}
