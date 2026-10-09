#pragma once
#include <stdint.h>
#include <stdio.h>

// Pure glanceable HOME: health from the Alerts counts, summary text and the
// tappable areas. HOME never evaluates alerts itself.

enum class HomeHealth : uint8_t { NoData, Healthy, Attention, Critical };

// Any critical -> CRITICAL; any warning -> ATTENTION; nothing active ->
// HEALTHY once a status arrived, NO DATA before (an active alert, such as
// stale data, still counts before the first status).
inline HomeHealth homeHealth(bool haveStatus, int critical, int warnings) {
  if (critical > 0) return HomeHealth::Critical;
  if (warnings > 0) return HomeHealth::Attention;
  return haveStatus ? HomeHealth::Healthy : HomeHealth::NoData;
}

inline const char* homeHealthLabel(HomeHealth health) {
  switch (health) {
    case HomeHealth::Healthy: return "HEALTHY";
    case HomeHealth::Attention: return "ATTENTION";
    case HomeHealth::Critical: return "CRITICAL";
    default: return "NO DATA";
  }
}

// "No active alerts", "2 warnings", "1 critical, 2 warnings".
inline void formatAlertCounts(bool haveStatus, int critical, int warnings, char* out, size_t size) {
  if (critical == 0 && warnings == 0) {
    snprintf(out, size, "%s", haveStatus ? "No active alerts" : "Waiting for status");
  } else if (critical == 0) {
    snprintf(out, size, "%d warning%s", warnings, warnings == 1 ? "" : "s");
  } else if (warnings == 0) {
    snprintf(out, size, "%d critical", critical);
  } else {
    snprintf(out, size, "%d critical, %d warning%s", critical, warnings, warnings == 1 ? "" : "s");
  }
}

// "CPU 18%   RAM 72%", or dashes when the host metrics are not current.
inline void formatCpuRam(bool available, float cpu, float ram, char* out, size_t size) {
  if (!available) {
    snprintf(out, size, "CPU --   RAM --");
    return;
  }
  int c = (int)(cpu + 0.5f), r = (int)(ram + 0.5f);
  if (c < 0) c = 0;
  if (c > 100) c = 100;
  if (r < 0) r = 0;
  if (r > 100) r = 100;
  snprintf(out, size, "CPU %d%%   RAM %d%%", c, r);
}

// Layout bands (y) and what tapping them opens.
constexpr int HOME_TIME_Y = 30;
constexpr int HOME_WEATHER_TOP = 80;
constexpr int HOME_WEATHER_BOTTOM = 126;
constexpr int HOME_HEALTH_TOP = 130;
constexpr int HOME_HEALTH_BOTTOM = 172;
constexpr int HOME_METRICS_TOP = 174;
constexpr int HOME_METRICS_BOTTOM = 204;

enum class HomeHit : uint8_t { None, Weather, Alerts, HomeServer };
inline HomeHit homeHitAt(int x, int y) {
  if (x < 0 || x >= 320) return HomeHit::None;
  if (y >= HOME_WEATHER_TOP && y <= HOME_WEATHER_BOTTOM) return HomeHit::Weather;
  if (y >= HOME_HEALTH_TOP && y <= HOME_HEALTH_BOTTOM) return HomeHit::Alerts;
  if (y >= HOME_METRICS_TOP && y <= HOME_METRICS_BOTTOM) return HomeHit::HomeServer;
  return HomeHit::None;
}
