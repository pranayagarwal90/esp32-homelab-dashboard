#pragma once
#include <stdint.h>
#include "LocationLogic.h"

// The device location in NVS namespace "location" (separate from "wifi",
// "device" (backend) and "settings"; none of them touches another):
//   name, admin1, country, timezone   strings (LocationLogic.h bounds)
//   latitude, longitude               doubles
//   utc_offset                        int32 seconds, once a forecast reported it
//   v                                 1; written last, removed first, so a
//                                     half-written location is never loaded
// Missing or invalid data is simply "no location".
//
// One RAM copy, guarded by a lock: any task may read it; saves come from the
// main task or the setup page task (NVS is thread-safe).

void loadLocation();                       // setup(), before any task starts.
bool locationConfigured();
bool currentLocation(Location& out);       // false (out cleared) when none.
// Increments when a location is saved or removed (not on offset updates).
uint32_t locationRevision();
bool saveLocation(const Location& location);  // Validated; offset not yet known.
bool clearLocation();
// The location's UTC offset, once known (LocalTime, OpenMeteo).
bool locationUtcOffset(int32_t& out);
// Main task, after a forecast: stored only when it changed (no NVS wear).
bool saveLocationOffset(int32_t seconds);
