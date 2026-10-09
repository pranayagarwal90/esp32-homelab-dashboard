#pragma once
#include <stdint.h>
#include <stdio.h>

// Pure weather model shared with host tests: the one authoritative WMO code
// mapping, day/night, compact forecast storage and text formatting.

enum class WeatherCondition : uint8_t {
  Clear, PartlyCloudy, Cloudy, Fog, Drizzle, Rain, HeavyRain, Thunderstorm, Snow, Unknown
};

// WMO weather interpretation codes (Open-Meteo `weather_code`):
//   0, 1              Clear (clear sky, mainly clear)
//   2                 PartlyCloudy
//   3                 Cloudy (overcast)
//   45, 48            Fog (fog, depositing rime fog)
//   51, 53, 55        Drizzle (light, moderate, dense)
//   56, 57            Drizzle (freezing drizzle)
//   61, 63            Rain (slight, moderate)
//   66                Rain (light freezing rain)
//   80, 81            Rain (slight, moderate showers)
//   65, 67, 82        HeavyRain (heavy rain, heavy freezing rain, violent showers)
//   71, 73, 75, 77    Snow (slight, moderate, heavy, snow grains)
//   85, 86            Snow (snow showers)
//   95, 96, 99        Thunderstorm (with or without hail)
//   anything else     Unknown
inline WeatherCondition weatherConditionFor(int code) {
  switch (code) {
    case 0: case 1: return WeatherCondition::Clear;
    case 2: return WeatherCondition::PartlyCloudy;
    case 3: return WeatherCondition::Cloudy;
    case 45: case 48: return WeatherCondition::Fog;
    case 51: case 53: case 55: case 56: case 57: return WeatherCondition::Drizzle;
    case 61: case 63: case 66: case 80: case 81: return WeatherCondition::Rain;
    case 65: case 67: case 82: return WeatherCondition::HeavyRain;
    case 71: case 73: case 75: case 77: case 85: case 86: return WeatherCondition::Snow;
    case 95: case 96: case 99: return WeatherCondition::Thunderstorm;
    default: return WeatherCondition::Unknown;
  }
}

// --- Compact values -------------------------------------------------------------------

constexpr int16_t WEATHER_NO_VALUE = -32768; // Missing tenths value.
constexpr int WEATHER_HOURLY_MAX = 8;          // Stored (as sent by the backend).
constexpr int WEATHER_HOURLY_SHOWN = 6;        // Displayed.

struct HourlyForecast {     // 6 bytes.
  uint8_t hour = 0;          // Local hour 0..23.
  int8_t precip = -1;        // Probability %, -1 unknown.
  int16_t temp10 = 0;        // Celsius x10.
  int16_t code = -1;         // WMO code, -1 unknown.
};

inline int16_t toTenths(float value) {
  float scaled = value * 10.0f;
  if (!(scaled > -3000.0f && scaled < 3000.0f)) return WEATHER_NO_VALUE;
  return (int16_t)(scaled < 0 ? scaled - 0.5f : scaled + 0.5f);
}

inline bool validHour(int hour) { return hour >= 0 && hour <= 23; }
inline bool validPercent(int percent) { return percent >= 0 && percent <= 100; }

// --- Day / night ----------------------------------------------------------------------

// Device estimate of the current Unix time: the status timestamp plus the
// time since it was applied (rollover-safe). 0 when unknown.
inline uint32_t weatherNowEpoch(uint32_t observedAt, uint32_t observedMs, uint32_t nowMs) {
  if (!observedAt) return 0;
  return observedAt + (nowMs - observedMs) / 1000;
}

// Day between today's sunrise and sunset. Without usable data: day.
inline bool weatherIsDay(uint32_t now, uint32_t sunrise, uint32_t sunset) {
  if (!now || !sunrise || !sunset || sunrise >= sunset) return true;
  return now >= sunrise && now < sunset;
}

// --- Formatting -----------------------------------------------------------------------

// The degree sign in the TFT_eSPI GLCD font (glyph 0xF8; the library shifts
// codes above 175 by one).
#define WEATHER_DEGREE "\xF7"

// "14" + degree, rounded; "--" when missing.
inline void formatDegrees(int16_t temp10, char* out, size_t size) {
  if (temp10 == WEATHER_NO_VALUE) {
    snprintf(out, size, "--");
    return;
  }
  int whole = temp10 >= 0 ? (temp10 + 5) / 10 : -((-temp10 + 5) / 10);
  snprintf(out, size, "%d" WEATHER_DEGREE, whole);
}

// "14.5" + degree + "C" for the big current temperature.
inline void formatDegreesC1(int16_t temp10, char* out, size_t size) {
  if (temp10 == WEATHER_NO_VALUE) {
    snprintf(out, size, "--" WEATHER_DEGREE "C");
    return;
  }
  int magnitude = temp10 < 0 ? -temp10 : temp10;
  snprintf(out, size, "%s%d.%d" WEATHER_DEGREE "C", temp10 < 0 ? "-" : "", magnitude / 10, magnitude % 10);
}

inline void formatPercent(int value, char* out, size_t size) {
  if (validPercent(value)) snprintf(out, size, "%d%%", value);
  else snprintf(out, size, "--");
}

// "5PM", "12AM".
inline void formatHour12(int hour, char* out, size_t size) {
  if (!validHour(hour)) {
    snprintf(out, size, "--");
    return;
  }
  int h = hour % 12 == 0 ? 12 : hour % 12;
  snprintf(out, size, "%d%s", h, hour < 12 ? "AM" : "PM");
}

// Local "6:42 PM" from a Unix time and the local offset in seconds.
inline void formatClock12(uint32_t epoch, int32_t offsetSeconds, char* out, size_t size) {
  int64_t local = (int64_t)epoch + offsetSeconds;
  int minutes = (int)((local / 60) % 1440);
  if (minutes < 0) minutes += 1440;
  int hour = minutes / 60, minute = minutes % 60;
  int h = hour % 12 == 0 ? 12 : hour % 12;
  snprintf(out, size, "%d:%02d %s", h, minute, hour < 12 ? "AM" : "PM");
}

// Next solar event to show: sunrise before it, sunset during the day, and
// after sunset "Sunset" stays (tomorrow's sunrise is not in the payload).
inline bool weatherShowSunrise(uint32_t now, uint32_t sunrise) {
  return now && sunrise && now < sunrise;
}

// Local UTC offset, from the status: the backend's solar_day_start is local
// midnight, so its seconds past a UTC midnight give the offset (DST-aware).
inline int32_t weatherUtcOffset(uint32_t localMidnight) {
  if (!localMidnight) return 0;
  int32_t past = (int32_t)(localMidnight % 86400);
  return past > 43200 ? 86400 - past : -past;
}

// --- Navigation / touch -------------------------------------------------------------

// TIME / WEATHER: the weather block (condition and High/Low lines, plus the
// FORECAST > hint) opens WEATHER. The clock above and the world clocks below
// stay inert.
constexpr int TIME_WEATHER_TOP = 80;
constexpr int TIME_WEATHER_BOTTOM = 118;
inline bool timeWeatherBlockHit(int x, int y) {
  return x >= 0 && x < 320 && y >= TIME_WEATHER_TOP && y <= TIME_WEATHER_BOTTOM;
}

// WEATHER: only the bottom bar is interactive; BACK returns to TIME / WEATHER.
enum class WeatherHit : uint8_t { None, Back };
inline WeatherHit weatherHitAt(int, int y) {
  return y >= 205 ? WeatherHit::Back : WeatherHit::None;
}
