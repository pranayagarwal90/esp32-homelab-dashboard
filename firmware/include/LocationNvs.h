#pragma once
#include <string.h>
#include "LocationLogic.h"

// The NVS schema of the "location" namespace, over any Preferences-like store
// (Arduino Preferences on the device, a fake in host tests):
//   name, admin1, country, timezone   strings
//   latitude, longitude               doubles
//   utc_offset                        int32 seconds (only once known)
//   v                                 LOCATION_NVS_VERSION, written last and
//                                     removed first: a save cut short by a
//                                     power loss reads as "no location".

constexpr uint8_t LOCATION_NVS_VERSION = 1;

template <class Store>
bool readLocation(Store& prefs, Location& out) {
  out = Location();
  if (prefs.getUChar("v", 0) != LOCATION_NVS_VERSION || !prefs.isKey("latitude") || !prefs.isKey("longitude")) {
    return false;
  }
  Location l;
  prefs.getString("name", l.name, sizeof(l.name));
  prefs.getString("admin1", l.admin1, sizeof(l.admin1));
  prefs.getString("country", l.country, sizeof(l.country));
  prefs.getString("timezone", l.timezone, sizeof(l.timezone));
  l.latitude = prefs.getDouble("latitude", 1000);
  l.longitude = prefs.getDouble("longitude", 1000);
  l.offsetKnown = prefs.isKey("utc_offset");
  l.utcOffset = l.offsetKnown ? prefs.getInt("utc_offset", 0) : 0;
  if (!validLocation(l)) return false;
  out = l;
  return true;
}

// A new place: its offset is unknown until its first forecast.
template <class Store>
bool writeLocation(Store& prefs, const Location& location) {
  Location l = location;
  l.offsetKnown = false;
  l.utcOffset = 0;
  if (!validLocation(l)) return false;
  // Only keys that exist: removing a missing one is harmless but logs an
  // nvs_erase_key NOT_FOUND error (every first save on a fresh device).
  if (prefs.isKey("v")) prefs.remove("v");
  if (prefs.isKey("utc_offset")) prefs.remove("utc_offset");
  return prefs.putString("name", l.name) == strlen(l.name) &&
         prefs.putString("admin1", l.admin1) == strlen(l.admin1) &&
         prefs.putString("country", l.country) == strlen(l.country) &&
         prefs.putString("timezone", l.timezone) == strlen(l.timezone) &&
         prefs.putDouble("latitude", l.latitude) == sizeof(double) &&
         prefs.putDouble("longitude", l.longitude) == sizeof(double) &&
         prefs.putUChar("v", LOCATION_NVS_VERSION) == 1;
}

template <class Store>
bool writeLocationOffset(Store& prefs, int32_t seconds) {
  return validUtcOffset(seconds) && prefs.getUChar("v", 0) == LOCATION_NVS_VERSION &&
         prefs.putInt("utc_offset", seconds) == sizeof(int32_t);
}
