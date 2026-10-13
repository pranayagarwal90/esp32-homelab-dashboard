#pragma once
#include <stdint.h>
#include <stdio.h>

// Local time from a UTC epoch and a UTC offset. Pure, host-tested; never uses
// the host's or the device's libc timezone.
//
// The device keeps UTC (NTP, LocalTime.cpp). The offset comes from Open-Meteo
// (`utc_offset_seconds` for the saved location, timezone=auto), refreshed with
// the weather and saved in NVS, so DST changes arrive with the next refresh.

// Before this, the clock has not been set by NTP (2025-01-01T00:00:00Z).
constexpr uint32_t MIN_VALID_EPOCH = 1735689600UL;
constexpr int32_t UTC_OFFSET_MIN = -12 * 3600;  // Baker Island.
constexpr int32_t UTC_OFFSET_MAX = 14 * 3600;   // Line Islands.

inline bool validUtcEpoch(uint32_t epoch) {
  return epoch >= MIN_VALID_EPOCH;
}

// Real offsets are whole quarter hours (+5:45, +12:45) within -12..+14 h.
inline bool validUtcOffset(int32_t seconds) {
  return seconds >= UTC_OFFSET_MIN && seconds <= UTC_OFFSET_MAX && seconds % 900 == 0;
}

struct CivilTime {
  int year = 1970;
  int month = 1;     // 1..12
  int day = 1;       // 1..31
  int hour = 0;
  int minute = 0;
  int second = 0;
  int weekday = 4;   // 0 = Sunday (1970-01-01 was a Thursday).
};

// Days since 1970-01-01 -> proleptic Gregorian date (H. Hinnant's algorithm).
inline void civilFromDays(int64_t days, int& year, int& month, int& day) {
  days += 719468;
  int64_t era = (days >= 0 ? days : days - 146096) / 146097;
  int64_t doe = days - era * 146097;
  int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  int64_t mp = (5 * doy + 2) / 153;
  day = int(doy - (153 * mp + 2) / 5 + 1);
  month = int(mp < 10 ? mp + 3 : mp - 9);
  year = int(yoe + era * 400 + (month <= 2 ? 1 : 0));
}

inline CivilTime localCivil(uint32_t utcEpoch, int32_t offsetSeconds) {
  int64_t local = int64_t(utcEpoch) + offsetSeconds;
  int64_t days = local >= 0 ? local / 86400 : (local - 86399) / 86400;
  int64_t secs = local - days * 86400;
  CivilTime t;
  civilFromDays(days, t.year, t.month, t.day);
  t.hour = int(secs / 3600);
  t.minute = int(secs / 60 % 60);
  t.second = int(secs % 60);
  t.weekday = int(((days % 7) + 11) % 7); // 1970-01-01 = Thursday (4).
  return t;
}

// Same formats as the backend's /api/status: "9:05 AM" and "Mon, Oct 12".
inline void formatLocalClock(const CivilTime& t, char* out, size_t size) {
  int h = t.hour % 12 == 0 ? 12 : t.hour % 12;
  snprintf(out, size, "%d:%02d %s", h, t.minute, t.hour < 12 ? "AM" : "PM");
}

inline void formatLocalDate(const CivilTime& t, char* out, size_t size) {
  static const char* days[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
  static const char* months[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                 "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
  snprintf(out, size, "%s, %s %d", days[t.weekday % 7], months[(t.month + 11) % 12], t.day);
}

// "UTC-04:00", "UTC+05:45".
inline void formatUtcOffset(int32_t seconds, char* out, size_t size) {
  int32_t magnitude = seconds < 0 ? -seconds : seconds;
  snprintf(out, size, "UTC%c%02d:%02d", seconds < 0 ? '-' : '+', int(magnitude / 3600), int(magnitude / 60 % 60));
}

// --- Which clock the screens show ------------------------------------------------------
// Local: NTP time + the saved location's offset (wins whenever available).
// Backend: the /api/status strings (Full Mode without a location, as before).
// None: "--:--" and "Set location" (no guessing a timezone).
enum class ClockSource : uint8_t { None, Local, Backend };

inline ClockSource clockSourceFor(bool utcValid, bool offsetKnown, bool backendConfigured) {
  if (utcValid && offsetKnown) return ClockSource::Local;
  return backendConfigured ? ClockSource::Backend : ClockSource::None;
}

// --- Calendar ----------------------------------------------------------------------------
// The month the calendar shows: today's month once a real date is known,
// unless the user already moved it.
struct CalendarCursor {
  int year = 2026;
  int month = 1;
  bool set = false; // Synced from a real date, or moved by the user.
};

inline void calendarSyncToday(CalendarCursor& c, int year, int month) {
  if (c.set || month < 1 || month > 12 || year < 2000) return;
  c.year = year;
  c.month = month;
  c.set = true;
}

inline void calendarStep(CalendarCursor& c, int delta) {
  c.month += delta;
  while (c.month < 1) { c.month += 12; c.year--; }
  while (c.month > 12) { c.month -= 12; c.year++; }
  c.set = true;
}
