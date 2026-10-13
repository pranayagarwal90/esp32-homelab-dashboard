#pragma once
#include "LocalClockLogic.h"

// ADD WORLD CLOCK > COMMON CITIES: a short built-in list, so a clock can be
// added on the device without typing. Coordinates and IANA zones as the
// location search (GeoNames) reports them, so the same city found on the
// phone counts as a duplicate. No UTC offsets: like any clock, each city's
// current offset comes from Open-Meteo after it is added. Nothing here is
// added by itself; a fresh device has no clocks.

struct CommonCity {
  const char* name;
  const char* admin1;
  const char* country;
  const char* timezone;
  double latitude;
  double longitude;
};

constexpr CommonCity COMMON_CITIES[] = {
  {"New York", "New York", "United States", "America/New_York", 40.71427, -74.00597},
  {"Los Angeles", "California", "United States", "America/Los_Angeles", 34.05223, -118.24368},
  {"London", "England", "United Kingdom", "Europe/London", 51.50853, -0.12574},
  {"Paris", "", "France", "Europe/Paris", 48.85341, 2.3488},
  {"Dubai", "Dubai", "United Arab Emirates", "Asia/Dubai", 25.07725, 55.30927},
  {"New Delhi", "National Capital Territory of Delhi", "India", "Asia/Kolkata", 28.62137, 77.2148},
  {"Singapore", "", "Singapore", "Asia/Singapore", 1.28967, 103.85007},
  {"Tokyo", "Tokyo", "Japan", "Asia/Tokyo", 35.6895, 139.69171},
  {"Sydney", "New South Wales", "Australia", "Australia/Sydney", -33.86785, 151.20732},
};
constexpr int COMMON_CITY_COUNT = int(sizeof(COMMON_CITIES) / sizeof(COMMON_CITIES[0]));

inline WorldClockLocation commonCityClock(int index) {
  WorldClockLocation c;
  if (index < 0 || index >= COMMON_CITY_COUNT) return c;
  const CommonCity& city = COMMON_CITIES[index];
  snprintf(c.name, sizeof(c.name), "%s", city.name);
  snprintf(c.admin1, sizeof(c.admin1), "%s", city.admin1);
  snprintf(c.country, sizeof(c.country), "%s", city.country);
  snprintf(c.timezone, sizeof(c.timezone), "%s", city.timezone);
  c.latitude = city.latitude;
  c.longitude = city.longitude;
  return c; // offsetKnown false.
}

// --- COMMON CITIES grid ----------------------------------------------------------------------
// Two columns of four per page (145 x 36 px buttons); PREV | CANCEL | NEXT
// in the bottom bar (Pages.h pagerBarAt).
constexpr int CITY_GRID_X0 = 10;
constexpr int CITY_GRID_Y0 = 42;
constexpr int CITY_BUTTON_W = 145;
constexpr int CITY_BUTTON_H = 36;
constexpr int CITY_PITCH_X = 155;
constexpr int CITY_PITCH_Y = 40;
constexpr int CITY_ROWS = 4;
constexpr int CITIES_PER_PAGE = 2 * CITY_ROWS;

inline int cityGridX(int slot) { return CITY_GRID_X0 + (slot % 2) * CITY_PITCH_X; }
inline int cityGridY(int slot) { return CITY_GRID_Y0 + (slot / 2) * CITY_PITCH_Y; }

// The common city under (x, y) on page, or -1.
inline int commonCityAt(int x, int y, int page) {
  if (x < CITY_GRID_X0 || y < CITY_GRID_Y0 || page < 0) return -1;
  int column = (x - CITY_GRID_X0) / CITY_PITCH_X, row = (y - CITY_GRID_Y0) / CITY_PITCH_Y;
  if (column > 1 || row >= CITY_ROWS) return -1;
  if ((x - CITY_GRID_X0) % CITY_PITCH_X >= CITY_BUTTON_W || (y - CITY_GRID_Y0) % CITY_PITCH_Y >= CITY_BUTTON_H) return -1;
  int index = page * CITIES_PER_PAGE + row * 2 + column;
  return index < COMMON_CITY_COUNT ? index : -1;
}
