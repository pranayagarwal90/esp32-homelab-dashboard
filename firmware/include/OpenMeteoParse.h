#pragma once
#include <ArduinoJson.h>
#include <stdint.h>
#include <string.h>
#include "BacklightControl.h"
#include "LocationLogic.h"
#include "WeatherLogic.h"

// Open-Meteo responses -> HomeDeck's own values. Pure (ArduinoJson only),
// host-tested with recorded responses.
//
// Memory: the body is read into a bounded buffer first (OPEN_METEO_BODY_MAX),
// then parsed with a filter, so the document holds only the fields used here
// (a few hundred bytes; measured in test_open_meteo). Every field is checked:
// a missing optional value stays unknown, a missing required one fails the
// whole response, and nothing is half-applied.

constexpr size_t OPEN_METEO_BODY_MAX = 8192; // Forecast ~1.3 KB; 5 places up to ~2.9 KB (measured).

enum class OpenMeteoResult : uint8_t { Ok, BadJson, ApiError, MissingFields };

inline const char* openMeteoResultText(OpenMeteoResult r) {
  switch (r) {
    case OpenMeteoResult::Ok: return "ok";
    case OpenMeteoResult::BadJson: return "malformed response";
    case OpenMeteoResult::ApiError: return "request rejected";
    case OpenMeteoResult::MissingFields: return "incomplete response";
  }
  return "";
}

// One forecast, normalized (the shape /api/status weather has on the device).
struct WeatherReport {
  int32_t utcOffset = 0;            // utc_offset_seconds (the location's current offset).
  uint32_t observedAt = 0;          // current.time (UTC).
  float temperatureC = 0;
  float highC = 0;
  float lowC = 0;
  int16_t weatherCode = -1;
  int16_t feelsLike10 = WEATHER_NO_VALUE;
  int16_t wind10 = WEATHER_NO_VALUE;
  int8_t humidity = -1;
  int8_t precipChance = -1;         // The current hour.
  int8_t precipChanceMax = -1;      // Today.
  uint8_t hourlyCount = 0;
  HourlyForecast hourly[WEATHER_HOURLY_MAX];
  // Today's solar day, UTC; all 0 unless consistent.
  uint32_t sunrise = 0;
  uint32_t sunset = 0;
  uint32_t dayStart = 0;            // Local midnight.
  uint32_t dayEnd = 0;              // Next local midnight.
  uint32_t validUntil = 0;          // Tomorrow's sunrise (covers the night), else dayEnd.
};

namespace openmeteo {

inline ArduinoJson::Allocator* allocatorOr(ArduinoJson::Allocator* allocator) {
  return allocator ? allocator : ArduinoJson::detail::DefaultAllocator::instance();
}

inline bool isNumber(JsonVariantConst v) {
  return (v.is<float>() || v.is<int>() || v.is<long>()) && !v.is<bool>();
}

inline bool epochAt(JsonVariantConst array, int index, uint32_t& out) {
  JsonVariantConst v = array[index];
  if (!v.is<uint32_t>()) return false;
  out = v.as<uint32_t>();
  return validUtcEpoch(out);
}

inline int8_t percentOf(JsonVariantConst v) {
  if (!isNumber(v)) return -1;
  float value = v.as<float>();
  int rounded = int(value + 0.5f);
  return value >= 0 && validPercent(rounded) ? int8_t(rounded) : -1;
}

inline int16_t tenthsOf(JsonVariantConst v) {
  return isNumber(v) ? toTenths(v.as<float>()) : WEATHER_NO_VALUE;
}

// Copies at most size-1 bytes without splitting a UTF-8 character.
inline void copyText(char* out, size_t size, const char* text) {
  if (!size) return;
  size_t length = text ? strlen(text) : 0;
  if (length >= size) {
    length = size - 1;
    while (length > 0 && (((unsigned char)text[length]) & 0xC0) == 0x80) length--;
  }
  if (length) memcpy(out, text, length);
  out[length] = '\0';
}

inline bool apiError(const JsonDocument& doc) {
  return doc["error"].is<bool>() && doc["error"].as<bool>();
}

}  // namespace openmeteo

// --- Forecast -----------------------------------------------------------------------------

inline OpenMeteoResult parseForecast(const char* body, size_t length, WeatherReport& out,
                                     ArduinoJson::Allocator* allocator = nullptr) {
  using namespace openmeteo;
  out = WeatherReport();
  ArduinoJson::Allocator* alloc = allocatorOr(allocator);
  JsonDocument filter(alloc);
  filter["error"] = true;
  filter["utc_offset_seconds"] = true;
  for (const char* key : {"time", "temperature_2m", "apparent_temperature", "relative_humidity_2m",
                          "weather_code", "wind_speed_10m"}) {
    filter["current"][key] = true;
  }
  for (const char* key : {"time", "temperature_2m", "precipitation_probability", "weather_code"}) {
    filter["hourly"][key] = true;
  }
  for (const char* key : {"time", "temperature_2m_max", "temperature_2m_min", "sunrise", "sunset",
                          "precipitation_probability_max"}) {
    filter["daily"][key] = true;
  }
  JsonDocument doc(alloc);
  if (!body || deserializeJson(doc, body, length, DeserializationOption::Filter(filter)) || doc.overflowed()) {
    return OpenMeteoResult::BadJson;
  }
  if (!doc.is<JsonObjectConst>()) return OpenMeteoResult::BadJson;
  if (apiError(doc)) return OpenMeteoResult::ApiError;

  JsonVariantConst offset = doc["utc_offset_seconds"];
  JsonVariantConst current = doc["current"];
  JsonVariantConst daily = doc["daily"];
  if (!offset.is<int32_t>() || !validUtcOffset(offset.as<int32_t>())) return OpenMeteoResult::MissingFields;
  if (!current["time"].is<uint32_t>() || !validUtcEpoch(current["time"].as<uint32_t>()) ||
      !isNumber(current["temperature_2m"]) || !current["weather_code"].is<int>() ||
      !isNumber(daily["temperature_2m_max"][0]) || !isNumber(daily["temperature_2m_min"][0])) {
    return OpenMeteoResult::MissingFields;
  }
  WeatherReport r;
  r.utcOffset = offset.as<int32_t>();
  r.observedAt = current["time"].as<uint32_t>();
  r.temperatureC = current["temperature_2m"].as<float>();
  r.highC = daily["temperature_2m_max"][0].as<float>();
  r.lowC = daily["temperature_2m_min"][0].as<float>();
  int code = current["weather_code"].as<int>();
  r.weatherCode = code >= 0 && code <= 99 ? int16_t(code) : -1;
  r.feelsLike10 = tenthsOf(current["apparent_temperature"]);
  r.wind10 = tenthsOf(current["wind_speed_10m"]);
  r.humidity = percentOf(current["relative_humidity_2m"]);
  r.precipChanceMax = percentOf(daily["precipitation_probability_max"][0]);

  // Hours from the current one on (forecast_hours counts from it). An hour
  // without a time or temperature is skipped.
  JsonVariantConst hourly = doc["hourly"];
  JsonArrayConst times = hourly["time"].as<JsonArrayConst>();
  for (size_t i = 0; i < times.size() && r.hourlyCount < WEATHER_HOURLY_MAX; i++) {
    uint32_t at = 0;
    if (!epochAt(hourly["time"], int(i), at) || !isNumber(hourly["temperature_2m"][i])) continue;
    HourlyForecast& slot = r.hourly[r.hourlyCount++];
    slot.hour = uint8_t(localCivil(at, r.utcOffset).hour);
    slot.temp10 = toTenths(hourly["temperature_2m"][i].as<float>());
    slot.precip = percentOf(hourly["precipitation_probability"][i]);
    JsonVariantConst c = hourly["weather_code"][i];
    slot.code = c.is<int>() && c.as<int>() >= 0 && c.as<int>() <= 99 ? int16_t(c.as<int>()) : -1;
    // The current hour's rain chance (as the backend reports it).
    if (r.precipChance < 0 && at <= r.observedAt && r.observedAt < at + 3600) r.precipChance = slot.precip;
  }

  // Solar day: daily.time[0] is local midnight (timezone=auto). Used only if
  // consistent, so night dimming never runs on a bad schedule.
  uint32_t start = 0, end = 0, rise = 0, set = 0, nextRise = 0;
  if (epochAt(daily["time"], 0, start) && epochAt(daily["sunrise"], 0, rise) && epochAt(daily["sunset"], 0, set)) {
    if (!epochAt(daily["time"], 1, end)) end = start + 86400;
    if (start < rise && rise < set && set < end && end - start >= 82800 && end - start <= 90000) {
      r.dayStart = start;
      r.dayEnd = end;
      r.sunrise = rise;
      r.sunset = set;
      r.validUntil = epochAt(daily["sunrise"], 1, nextRise) && nextRise > end && nextRise - end < 86400
                     ? nextRise : end;
    }
  }
  out = r;
  return OpenMeteoResult::Ok;
}

// The backlight's night schedule from a report, as of nowUtc (NTP). Invalid
// (no dimming) when the report has no consistent solar day.
inline SolarSchedule solarScheduleFor(const WeatherReport& r, uint32_t nowUtc) {
  SolarSchedule s;
  if (!r.dayStart) return s;
  s.sunrise = r.sunrise;
  s.sunset = r.sunset;
  s.dayStart = r.dayStart;
  s.dayEnd = r.dayEnd;
  s.validUntil = r.validUntil;
  s.timestamp = nowUtc;
  return s;
}

// --- Geocoding ------------------------------------------------------------------------------

// Up to max results (LOCATION_RESULTS_MAX); entries without a name or valid
// coordinates are skipped, overlong names cut at a character boundary. No
// match is Ok with count 0 (Open-Meteo then omits "results").
inline OpenMeteoResult parseGeocoding(const char* body, size_t length, Location* out, int max, int& count,
                                      ArduinoJson::Allocator* allocator = nullptr) {
  using namespace openmeteo;
  count = 0;
  ArduinoJson::Allocator* alloc = allocatorOr(allocator);
  JsonDocument filter(alloc);
  filter["error"] = true;
  for (const char* key : {"name", "latitude", "longitude", "timezone", "country", "admin1"}) {
    filter["results"][0][key] = true;
  }
  JsonDocument doc(alloc);
  if (!body || deserializeJson(doc, body, length, DeserializationOption::Filter(filter)) || doc.overflowed()) {
    return OpenMeteoResult::BadJson;
  }
  if (!doc.is<JsonObjectConst>()) return OpenMeteoResult::BadJson;
  if (apiError(doc)) return OpenMeteoResult::ApiError;
  JsonVariantConst results = doc["results"];
  if (results.isNull()) return OpenMeteoResult::Ok;
  if (!results.is<JsonArrayConst>()) return OpenMeteoResult::MissingFields;
  for (JsonVariantConst item : results.as<JsonArrayConst>()) {
    if (count >= max || count >= LOCATION_RESULTS_MAX) break;
    if (!item["name"].is<const char*>() || !isNumber(item["latitude"]) || !isNumber(item["longitude"])) continue;
    Location l;
    copyText(l.name, sizeof(l.name), item["name"].as<const char*>());
    copyText(l.admin1, sizeof(l.admin1), item["admin1"].is<const char*>() ? item["admin1"].as<const char*>() : "");
    copyText(l.country, sizeof(l.country), item["country"].is<const char*>() ? item["country"].as<const char*>() : "");
    const char* zone = item["timezone"].is<const char*>() ? item["timezone"].as<const char*>() : "";
    if (strlen(zone) < sizeof(l.timezone) && validTimezoneName(zone)) copyText(l.timezone, sizeof(l.timezone), zone);
    l.latitude = item["latitude"].as<double>();
    l.longitude = item["longitude"].as<double>();
    if (!validLocation(l)) continue;
    out[count++] = l; // offsetKnown false: the first forecast supplies it.
  }
  return OpenMeteoResult::Ok;
}

// --- UTC offset (world clocks) ---------------------------------------------------------------

// The metadata-only forecast response (LocalClockLogic.h clockOffsetUrl):
// only utc_offset_seconds is used. Missing or not a real offset fails.
inline OpenMeteoResult parseUtcOffset(const char* body, size_t length, int32_t& out,
                                      ArduinoJson::Allocator* allocator = nullptr) {
  using namespace openmeteo;
  out = 0;
  ArduinoJson::Allocator* alloc = allocatorOr(allocator);
  JsonDocument filter(alloc);
  filter["error"] = true;
  filter["utc_offset_seconds"] = true;
  JsonDocument doc(alloc);
  if (!body || deserializeJson(doc, body, length, DeserializationOption::Filter(filter)) || doc.overflowed()) {
    return OpenMeteoResult::BadJson;
  }
  if (!doc.is<JsonObjectConst>()) return OpenMeteoResult::BadJson;
  if (apiError(doc)) return OpenMeteoResult::ApiError;
  JsonVariantConst offset = doc["utc_offset_seconds"];
  if (!offset.is<int32_t>() || !validUtcOffset(offset.as<int32_t>())) return OpenMeteoResult::MissingFields;
  out = offset.as<int32_t>();
  return OpenMeteoResult::Ok;
}
