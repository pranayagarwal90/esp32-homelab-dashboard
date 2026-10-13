#pragma once
#include <stdint.h>
#include "LocationLogic.h"
#include "OpenMeteoParse.h"

// Direct Open-Meteo weather for the saved location, and the location search
// behind Settings > Location. Network I/O runs on a short-lived worker task
// (HTTPS with the pinned roots in OpenMeteoCerts.h); the main task applies
// results to the same WeatherData / SolarSchedule the backend fills, so every
// weather screen, animation and night dimming work unchanged.
//
// Source precedence (LocationLogic.h): a location on the device -> direct;
// otherwise a backend -> /api/status weather as before; otherwise none.

// Main task, every loop: refresh schedule, apply results, redraw.
void serviceDirectWeather();
WeatherSource weatherSource();
// What the weather areas of Home, Weather, Time show.
WeatherView weatherView();

// Settings > Location.
struct DirectWeatherInfo {
  bool fetching = false;
  bool haveData = false;
  bool failed = false;        // The last attempt failed.
  uint32_t ageMs = 0;         // Since the last success (haveData).
};
void directWeatherInfo(DirectWeatherInfo& out);

// --- Location search (setup page task or main task) ---------------------------------------
// One search at a time, shared by the phone page (Settings > Location >
// SEARCH ON PHONE) and the device keypad (POSTAL CODE); a new one replaces
// the last. Results are kept here only (no copy elsewhere).

struct LocationSearchSnapshot {
  LocationSearchState state = LocationSearchState::Idle;
  LocationSearchError error = LocationSearchError::None;
  uint32_t generation = 0;                  // Identifies this result list.
  int count = 0;
  Location results[LOCATION_RESULTS_MAX];
  char query[LOCATION_QUERY_MAX] = "";
};

// False if the text is not a usable search (LocationLogic.h normalizeLocationQuery).
bool requestLocationSearch(const char* query);
// Main task: the device keypad. Digits only (LocationUiLogic.h validPostalCode);
// the same geocoding request as the phone page, over the normal Wi-Fi.
bool requestPostalSearch(const char* digits);
LocationSearchState locationSearchState();  // Cheap: for polling.
void locationSearchSnapshot(LocationSearchSnapshot& out);
// Saves results[index] of that generation as the device location.
bool chooseLocationResult(uint32_t generation, int index);
// Forgets the last search (a new hotspot session starts clean).
void resetLocationSearch();
