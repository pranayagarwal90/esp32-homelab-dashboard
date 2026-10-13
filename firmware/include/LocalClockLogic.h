#pragma once
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "LocationLogic.h"

// World clocks kept on the device (Phase 8.7): no backend needed. Pure,
// host-tested; stored by LocalClockStore (NVS namespace "worldclocks").
//
// Same idea as the device location (LocalTime.h): the clock is NTP UTC
// (utcNow()) plus each city's current UTC offset, which Open-Meteo reports
// (utc_offset_seconds for the city's timezone). No offset table, no DST
// rules: the offset is refreshed every few hours, so DST changes arrive with
// the next refresh. A city whose offset was never obtained shows "Syncing...",
// never a guess.

constexpr int LOCAL_CLOCKS_MAX = 4;   // Hard limit (screen, NVS, requests).

struct WorldClockLocation {
  char name[LOCATION_NAME_MAX] = "";
  char admin1[LOCATION_REGION_MAX] = "";
  char country[LOCATION_REGION_MAX] = "";
  char timezone[LOCATION_TZ_MAX] = "";   // IANA name; "" = Open-Meteo picks (timezone=auto).
  double latitude = 0;
  double longitude = 0;
  bool offsetKnown = false;
  int32_t utcOffsetSeconds = 0;          // The last offset Open-Meteo reported.
  uint32_t offsetUpdatedAt = 0;          // UTC epoch it was last confirmed (0 = never).
};

struct LocalClockList {
  WorldClockLocation items[LOCAL_CLOCKS_MAX];
  uint8_t count = 0;
};

inline WorldClockLocation clockFromLocation(const Location& l) {
  WorldClockLocation c;
  memcpy(c.name, l.name, sizeof(c.name));
  memcpy(c.admin1, l.admin1, sizeof(c.admin1));
  memcpy(c.country, l.country, sizeof(c.country));
  memcpy(c.timezone, l.timezone, sizeof(c.timezone));
  c.name[sizeof(c.name) - 1] = c.admin1[sizeof(c.admin1) - 1] = '\0';
  c.country[sizeof(c.country) - 1] = c.timezone[sizeof(c.timezone) - 1] = '\0';
  c.latitude = l.latitude;
  c.longitude = l.longitude;
  return c; // Offset unknown: the first refresh supplies it.
}

inline bool validClock(const WorldClockLocation& c) {
  return validLocationText(c.name, sizeof(c.name), true) && validLocationText(c.admin1, sizeof(c.admin1), false) &&
         validLocationText(c.country, sizeof(c.country), false) && validTimezoneName(c.timezone) &&
         strnlen(c.timezone, sizeof(c.timezone)) < sizeof(c.timezone) && validLatitude(c.latitude) &&
         validLongitude(c.longitude) && (!c.offsetKnown || validUtcOffset(c.utcOffsetSeconds));
}

// The same place: same timezone and within about 2 km (the phone search and
// the common-city list may differ in the last decimals).
constexpr double CLOCK_SAME_PLACE_DEGREES = 0.02;

inline bool sameClockPlace(const WorldClockLocation& a, const WorldClockLocation& b) {
  return strcmp(a.timezone, b.timezone) == 0 && fabs(a.latitude - b.latitude) < CLOCK_SAME_PLACE_DEGREES &&
         fabs(a.longitude - b.longitude) < CLOCK_SAME_PLACE_DEGREES;
}

inline int findLocalClock(const LocalClockList& list, const WorldClockLocation& place) {
  for (int i = 0; i < list.count && i < LOCAL_CLOCKS_MAX; i++) {
    if (sameClockPlace(list.items[i], place)) return i;
  }
  return -1;
}

enum class ClockAddResult : uint8_t { Added, Duplicate, Full, Invalid, SaveFailed, Stale };

inline const char* clockAddResultText(ClockAddResult r) {
  switch (r) {
    case ClockAddResult::Added: return "Added";
    case ClockAddResult::Duplicate: return "Already added";
    case ClockAddResult::Full: return "4 of 4 clocks added. Remove a clock to add another.";
    case ClockAddResult::Invalid: return "That place cannot be used.";
    case ClockAddResult::SaveFailed: return "Could not save the clock.";
    case ClockAddResult::Stale: return "That result is no longer available. Search again.";
  }
  return "";
}

// Appends (offset unknown until the first refresh); nothing changes unless Added.
inline ClockAddResult addLocalClock(LocalClockList& list, const WorldClockLocation& input) {
  WorldClockLocation c = input;
  c.offsetKnown = false;
  c.utcOffsetSeconds = 0;
  c.offsetUpdatedAt = 0;
  if (!validClock(c)) return ClockAddResult::Invalid;
  if (findLocalClock(list, c) >= 0) return ClockAddResult::Duplicate;
  if (list.count >= LOCAL_CLOCKS_MAX) return ClockAddResult::Full;
  list.items[list.count++] = c;
  return ClockAddResult::Added;
}

inline bool removeLocalClock(LocalClockList& list, int index) {
  if (index < 0 || index >= list.count) return false;
  for (int i = index; i + 1 < list.count; i++) list.items[i] = list.items[i + 1];
  list.items[--list.count] = WorldClockLocation();
  return true;
}

// A refresh result for place (matched by identity: the list may have changed
// meanwhile). False if it no longer exists or the offset is not real.
inline bool setLocalClockOffset(LocalClockList& list, const WorldClockLocation& place, int32_t offset,
                                uint32_t utcNow, bool& changed) {
  changed = false;
  int i = findLocalClock(list, place);
  if (i < 0 || !validUtcOffset(offset)) return false;
  WorldClockLocation& c = list.items[i];
  changed = !c.offsetKnown || c.utcOffsetSeconds != offset;
  c.offsetKnown = true;
  c.utcOffsetSeconds = offset;
  if (validUtcEpoch(utcNow)) c.offsetUpdatedAt = utcNow;
  return true;
}

// NVS is written when the offset is new or changed (DST), otherwise at most
// daily (offsetUpdatedAt), never on every refresh.
constexpr uint32_t CLOCK_SAVE_UNCHANGED_SECONDS = 24UL * 3600UL;

inline bool clockOffsetNeedsSave(bool changed, uint32_t lastSavedUtc, uint32_t utcNow) {
  return changed || !validUtcEpoch(lastSavedUtc) || utcNow - lastSavedUtc >= CLOCK_SAVE_UNCHANGED_SECONDS;
}

// --- Display ---------------------------------------------------------------------------------
// The city's own time and date from NTP UTC: its date can differ from
// HomeDeck's (Tokyo is already tomorrow, Los Angeles may still be yesterday).
// False ("Syncing...") without NTP time or a known offset.
inline bool formatWorldClock(const WorldClockLocation& c, uint32_t utcNow, char* time, size_t timeSize,
                             char* date, size_t dateSize) {
  if (timeSize) time[0] = '\0';
  if (dateSize) date[0] = '\0';
  if (!validUtcEpoch(utcNow) || !c.offsetKnown || !validUtcOffset(c.utcOffsetSeconds)) return false;
  CivilTime t = localCivil(utcNow, c.utcOffsetSeconds);
  formatLocalClock(t, time, timeSize);
  formatLocalDate(t, date, dateSize);
  return true;
}

// The screen's label: printable ASCII in capitals (the display font has no
// accents: others become '?'), cut to fit.
inline void clockDisplayLabel(const char* name, char* out, size_t size) {
  size_t n = 0;
  for (const unsigned char* p = (const unsigned char*)(name ? name : ""); *p && n + 1 < size; p++) {
    if (*p >= 0x80) {
      while ((p[1] & 0xC0) == 0x80) p++; // One '?' per UTF-8 character.
      out[n++] = '?';
    } else {
      out[n++] = (*p >= 'a' && *p <= 'z') ? char(*p - 32) : (*p >= ' ' && *p <= '~') ? char(*p) : '?';
    }
  }
  if (size) out[n] = '\0';
}

// --- Which world clocks the CLOCKS page shows -------------------------------------------------
// 1. Clocks added on the device (no backend needed).
// 2. Otherwise a backend's world_clocks, as before (no forced migration).
// 3. Neither: "No clocks added".
enum class WorldClockSource : uint8_t { None, Local, Backend };

inline WorldClockSource worldClockSourceFor(int localCount, bool backendConfigured, int backendCount) {
  if (localCount > 0) return WorldClockSource::Local;
  return backendConfigured && backendCount > 0 ? WorldClockSource::Backend : WorldClockSource::None;
}

inline const char* worldClockSourceName(WorldClockSource s) {
  return s == WorldClockSource::Local ? "local" : s == WorldClockSource::Backend ? "backend" : "none";
}

// --- Offset request --------------------------------------------------------------------------
// The smallest forecast request: no weather variables at all, only the
// metadata (about 190 bytes: utc_offset_seconds, timezone, ...). The city's
// own IANA zone when known, else timezone=auto from the coordinates.
inline bool clockOffsetUrl(char* out, size_t size, const WorldClockLocation& c) {
  char zone[LOCATION_TZ_MAX * 3] = "auto";
  if (!validLatitude(c.latitude) || !validLongitude(c.longitude) || !validTimezoneName(c.timezone) ||
      (c.timezone[0] && !urlEncode(c.timezone, zone, sizeof(zone)))) {
    if (size) out[0] = '\0';
    return false;
  }
  int written = snprintf(out, size, "%s?latitude=%.4f&longitude=%.4f&timezone=%s", OPEN_METEO_FORECAST_URL,
                         c.latitude, c.longitude, zone);
  if (written < 0 || size_t(written) >= size) {
    if (size) out[0] = '\0';
    return false;
  }
  return true;
}

// --- Refresh schedule ------------------------------------------------------------------------
// Each clock: once after boot (offsets saved earlier are shown meanwhile),
// then every 6 hours. One batch at a time, sequential; after a failed batch
// the failed clocks retry after 2, 5, 10, 20, then 30 minutes. A failure
// keeps the last known offset.
constexpr uint32_t CLOCK_OFFSET_REFRESH_MS = 6UL * 3600UL * 1000UL;

inline bool clockOffsetDue(bool refreshedThisBoot, uint32_t refreshedMs, uint32_t nowMs) {
  return !refreshedThisBoot || nowMs - refreshedMs >= CLOCK_OFFSET_REFRESH_MS;
}

struct ClockOffsetSchedule {
  uint32_t lastBatchMs = 0;
  uint32_t waitMs = 0;      // Before the next batch may start.
  uint8_t failures = 0;     // Consecutive failed batches.
};

inline bool clockBatchAllowed(const ClockOffsetSchedule& s, uint32_t nowMs) {
  return s.waitMs == 0 || nowMs - s.lastBatchMs >= s.waitMs;
}

inline void clockBatchFinished(ClockOffsetSchedule& s, bool allOk, uint32_t nowMs) {
  s.lastBatchMs = nowMs;
  if (allOk) {
    s.failures = 0;
    s.waitMs = 0;   // Each clock's own 6-hour due time decides.
  } else {
    if (s.failures < 255) s.failures++;
    s.waitMs = weatherRetryDelayMs(s.failures);
  }
}

// A clock was added: try it now, whatever failed before.
inline void clockBatchReset(ClockOffsetSchedule& s) {
  s = ClockOffsetSchedule();
}

// --- NVS blob ("worldclocks"/"clocks") ---------------------------------------------------------
// One blob, so the list is saved in a single NVS write (NVS keeps the old
// value until the new one is complete) and a CRC-32 covers it, so a cut or
// corrupted blob reads as "no clocks", never as bogus ones:
//   'W' 'C' version count | count x entry | CRC-32 (little-endian)
//   entry: name[48] admin1[40] country[40] timezone[40] (NUL-padded),
//          latitude, longitude (IEEE-754 double, little-endian),
//          offsetKnown (u8), utcOffsetSeconds (i32), offsetUpdatedAt (u32)
constexpr uint8_t LOCAL_CLOCKS_NVS_VERSION = 1;
constexpr size_t CLOCK_TEXT_BYTES = LOCATION_NAME_MAX + 2 * LOCATION_REGION_MAX + LOCATION_TZ_MAX;
constexpr size_t CLOCK_ENTRY_BYTES = CLOCK_TEXT_BYTES + 8 + 8 + 1 + 4 + 4;
constexpr size_t CLOCK_BLOB_MAX = 4 + LOCAL_CLOCKS_MAX * CLOCK_ENTRY_BYTES + 4;

inline uint32_t clockCrc32(const uint8_t* data, size_t length) {
  uint32_t crc = 0xFFFFFFFFu;
  for (size_t i = 0; i < length; i++) {
    crc ^= data[i];
    for (int b = 0; b < 8; b++) crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
  }
  return ~crc;
}

namespace clockblob {
inline void putU32(uint8_t* p, uint32_t v) {
  for (int i = 0; i < 4; i++) p[i] = uint8_t(v >> (8 * i));
}
inline uint32_t getU32(const uint8_t* p) {
  return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
}
inline void putDouble(uint8_t* p, double v) {
  uint64_t bits;
  memcpy(&bits, &v, 8);
  for (int i = 0; i < 8; i++) p[i] = uint8_t(bits >> (8 * i));
}
inline double getDouble(const uint8_t* p) {
  uint64_t bits = 0;
  for (int i = 0; i < 8; i++) bits |= uint64_t(p[i]) << (8 * i);
  double v;
  memcpy(&v, &bits, 8);
  return v;
}
inline void putText(uint8_t*& p, const char* text, size_t size) {
  memset(p, 0, size);
  memcpy(p, text, strnlen(text, size - 1));
  p += size;
}
inline bool getText(const uint8_t*& p, char* out, size_t size) {
  memcpy(out, p, size);
  p += size;
  return memchr(out, '\0', size) != nullptr;
}
}  // namespace clockblob

// Returns the blob length (0 if the list is not valid).
inline size_t encodeLocalClocks(const LocalClockList& list, uint8_t* out, size_t size) {
  using namespace clockblob;
  if (list.count > LOCAL_CLOCKS_MAX) return 0;
  size_t length = 4 + size_t(list.count) * CLOCK_ENTRY_BYTES + 4;
  if (size < length) return 0;
  uint8_t* p = out;
  *p++ = 'W';
  *p++ = 'C';
  *p++ = LOCAL_CLOCKS_NVS_VERSION;
  *p++ = list.count;
  for (int i = 0; i < list.count; i++) {
    const WorldClockLocation& c = list.items[i];
    if (!validClock(c)) return 0;
    putText(p, c.name, sizeof(c.name));
    putText(p, c.admin1, sizeof(c.admin1));
    putText(p, c.country, sizeof(c.country));
    putText(p, c.timezone, sizeof(c.timezone));
    putDouble(p, c.latitude);
    p += 8;
    putDouble(p, c.longitude);
    p += 8;
    *p++ = c.offsetKnown ? 1 : 0;
    putU32(p, uint32_t(c.utcOffsetSeconds));
    p += 4;
    putU32(p, c.offsetUpdatedAt);
    p += 4;
  }
  putU32(p, clockCrc32(out, size_t(p - out)));
  return length;
}

// All or nothing: a bad length, CRC, version, value or duplicate -> empty list.
inline bool decodeLocalClocks(const uint8_t* data, size_t length, LocalClockList& out) {
  using namespace clockblob;
  out = LocalClockList();
  if (!data || length < 8 || data[0] != 'W' || data[1] != 'C' || data[2] != LOCAL_CLOCKS_NVS_VERSION) return false;
  uint8_t count = data[3];
  if (count > LOCAL_CLOCKS_MAX || length != 4 + size_t(count) * CLOCK_ENTRY_BYTES + 4) return false;
  if (clockCrc32(data, length - 4) != getU32(data + length - 4)) return false;
  LocalClockList list;
  const uint8_t* p = data + 4;
  for (int i = 0; i < count; i++) {
    WorldClockLocation c;
    if (!getText(p, c.name, sizeof(c.name)) || !getText(p, c.admin1, sizeof(c.admin1)) ||
        !getText(p, c.country, sizeof(c.country)) || !getText(p, c.timezone, sizeof(c.timezone))) {
      return false;
    }
    c.latitude = getDouble(p);
    p += 8;
    c.longitude = getDouble(p);
    p += 8;
    uint8_t known = *p++;
    c.utcOffsetSeconds = int32_t(getU32(p));
    p += 4;
    c.offsetUpdatedAt = getU32(p);
    p += 4;
    if (known > 1) return false;
    c.offsetKnown = known == 1;
    if (!c.offsetKnown) c.utcOffsetSeconds = 0;
    if (!validClock(c) || findLocalClock(list, c) >= 0) return false;
    list.items[list.count++] = c;
  }
  out = list;
  return true;
}
