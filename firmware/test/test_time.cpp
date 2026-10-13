// Local time from a UTC epoch + offset (TimeLogic.h): never the host's
// timezone. Offsets, rollovers, DST via a new offset, clock source, calendar.
#include "TimeLogic.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <string>

static std::string clockText(uint32_t utc, int32_t offset) {
  char text[16];
  formatLocalClock(localCivil(utc, offset), text, sizeof(text));
  return text;
}

static std::string dateText(uint32_t utc, int32_t offset) {
  char text[24];
  formatLocalDate(localCivil(utc, offset), text, sizeof(text));
  return text;
}

static void expect(const CivilTime& t, int year, int month, int day, int hour, int minute, int weekday) {
  assert(t.year == year && t.month == month && t.day == day && t.hour == hour && t.minute == minute &&
         t.weekday == weekday);
}

static void testOffsets() {
  const uint32_t noonUtc = 1791806400u;   // 2026-10-12 12:00:00 UTC, a Monday.
  expect(localCivil(noonUtc, 0), 2026, 10, 12, 12, 0, 1);
  expect(localCivil(noonUtc, -5 * 3600), 2026, 10, 12, 7, 0, 1);           // UTC-5.
  expect(localCivil(noonUtc, 5 * 3600 + 1800), 2026, 10, 12, 17, 30, 1);  // UTC+5:30 (India).
  expect(localCivil(noonUtc, 5 * 3600 + 2700), 2026, 10, 12, 17, 45, 1);  // UTC+5:45 (Nepal).
  expect(localCivil(noonUtc, -(3 * 3600 + 1800)), 2026, 10, 12, 8, 30, 1); // UTC-3:30.
  assert(clockText(noonUtc, 0) == "12:00 PM" && clockText(noonUtc, -5 * 3600) == "7:00 AM");
  assert(clockText(noonUtc, 12 * 3600) == "12:00 AM");
  assert(dateText(noonUtc, 0) == "Mon, Oct 12");
  // The epoch itself, and the seconds.
  expect(localCivil(0, 0), 1970, 1, 1, 0, 0, 4);
  assert(localCivil(noonUtc + 59, 0).second == 59);
}

static void testRollovers() {
  // Day: 2026-10-12 23:30 UTC is the 13th in India, still the 12th in New York.
  const uint32_t lateUtc = 1791847800u;
  expect(localCivil(lateUtc, 19800), 2026, 10, 13, 5, 0, 2);
  expect(localCivil(lateUtc, -4 * 3600), 2026, 10, 12, 19, 30, 1);
  // Month: 2026-10-31 22:00 UTC -> Nov 1 at UTC+5:45.
  expect(localCivil(1793484000u, 20700), 2026, 11, 1, 3, 45, 0);
  // Year: 2026-12-31 20:00 UTC -> 2027-01-01 at UTC+5; still 2026 at UTC-5.
  expect(localCivil(1798747200u, 5 * 3600), 2027, 1, 1, 1, 0, 5);
  expect(localCivil(1798747200u, -5 * 3600), 2026, 12, 31, 15, 0, 4);
  // Leap day 2028-02-29 and the day after.
  expect(localCivil(1835395200u, 0), 2028, 2, 29, 0, 0, 2);
  expect(localCivil(1835395200u + 86400, 0), 2028, 3, 1, 0, 0, 3);
  // Far future stays correct (uint32 epochs reach 2106).
  expect(localCivil(4102444800u, 0), 2100, 1, 1, 0, 0, 5);
}

// DST: the next forecast reports a new utc_offset_seconds; the same UTC
// instant then shows the new local time at once, no restart.
static void testDstOffsetChange() {
  const uint32_t utc = 1793512800u;            // 2026-11-01 06:00 UTC (US fall back day).
  int32_t offset = -4 * 3600;                   // EDT, saved from the last forecast.
  assert(clockText(utc, offset) == "2:00 AM");
  offset = -5 * 3600;                           // The next refresh reports EST.
  assert(clockText(utc, offset) == "1:00 AM");
  assert(clockText(utc + 60, offset) == "1:01 AM");
  // Europe spring forward (+1 -> +2).
  assert(clockText(1774746000u, 3600) == "2:00 AM" && clockText(1774746000u, 7200) == "3:00 AM");
}

static void testValidation() {
  assert(!validUtcEpoch(0) && !validUtcEpoch(86400 * 365) && validUtcEpoch(MIN_VALID_EPOCH));
  assert(validUtcOffset(0) && validUtcOffset(20700) && validUtcOffset(-12 * 3600) && validUtcOffset(14 * 3600));
  assert(!validUtcOffset(14 * 3600 + 900) && !validUtcOffset(-12 * 3600 - 900) && !validUtcOffset(61));
  char text[16];
  formatUtcOffset(-4 * 3600, text, sizeof(text));
  assert(strcmp(text, "UTC-04:00") == 0);
  formatUtcOffset(20700, text, sizeof(text));
  assert(strcmp(text, "UTC+05:45") == 0);
  formatUtcOffset(0, text, sizeof(text));
  assert(strcmp(text, "UTC+00:00") == 0);
}

static void testClockSource() {
  // Local time wins whenever it exists: the backend's strings are not used.
  assert(clockSourceFor(true, true, true) == ClockSource::Local);
  assert(clockSourceFor(true, true, false) == ClockSource::Local);
  // No location (offset unknown): never guess a timezone.
  assert(clockSourceFor(true, false, false) == ClockSource::None);
  assert(clockSourceFor(true, false, true) == ClockSource::Backend);    // Full Mode as before.
  // NTP not synced yet.
  assert(clockSourceFor(false, true, false) == ClockSource::None);
  assert(clockSourceFor(false, true, true) == ClockSource::Backend);
}

static void testCalendar() {
  CalendarCursor c;                    // Default before any date: Jan 2026.
  assert(c.year == 2026 && c.month == 1 && !c.set);
  calendarSyncToday(c, 2027, 1);       // A real January is applied too.
  assert(c.year == 2027 && c.month == 1 && c.set);
  calendarSyncToday(c, 2027, 3);       // Already synced: the shown month stays.
  assert(c.month == 1);
  CalendarCursor d;
  calendarStep(d, -1);                 // The user moved it first: kept.
  assert(d.year == 2025 && d.month == 12 && d.set);
  calendarSyncToday(d, 2026, 10);
  assert(d.year == 2025 && d.month == 12);
  calendarStep(d, 14);
  assert(d.year == 2027 && d.month == 2);
  CalendarCursor e;
  calendarSyncToday(e, 2026, 13);      // Nonsense ignored.
  calendarSyncToday(e, 1970, 1);
  assert(!e.set);
  // Local date selection: the calendar month is the location's date.
  CivilTime india = localCivil(1798747200u, 5 * 3600 + 1800); // 2027-01-01 01:30 IST.
  CalendarCursor f;
  calendarSyncToday(f, india.year, india.month);
  assert(f.year == 2027 && f.month == 1);
}

int main() {
  testOffsets();
  testRollovers();
  testDstOffsetChange();
  testValidation();
  testClockSource();
  testCalendar();
  puts("time tests passed");
  return 0;
}
