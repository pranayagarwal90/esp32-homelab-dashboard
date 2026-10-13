#include <Arduino.h>
#include <time.h>
#include "LocalTime.h"
#include "AppState.h"
#include "CalendarScreen.h"
#include "DeviceConfig.h"
#include "LocationStore.h"

static constexpr uint32_t CHECK_MS = 500;
static bool started = false;
static bool syncLogged = false;
static unsigned long lastCheck = 0;
static bool forceUpdate = true;

void beginLocalTime() {
  if (started) return;
  configTime(0, 0, "pool.ntp.org", "time.nist.gov"); // UTC; offsets are applied by TimeLogic.
  started = true;
  Serial.println("[TIME] NTP started (UTC)");
}

uint32_t utcNow() {
  time_t now = time(nullptr);
  return now > 0 && validUtcEpoch(uint32_t(now)) ? uint32_t(now) : 0;
}

bool hasUtcTime() {
  return utcNow() != 0;
}

ClockSource clockSource() {
  int32_t offset = 0;
  return clockSourceFor(hasUtcTime(), locationUtcOffset(offset), backendConfigured());
}

void invalidateLocalClock() {
  forceUpdate = true;
}

static bool setShown(const char* clock, const char* date) {
  if (app.time.localTime == clock && app.time.localDate == date) return false;
  app.time.localTime = clock;
  app.time.localDate = date;
  return true;
}

bool updateLocalClock() {
  unsigned long now = millis();
  if (!forceUpdate && now - lastCheck < CHECK_MS) return false;
  lastCheck = now;
  forceUpdate = false;
  uint32_t utc = utcNow();
  if (utc && !syncLogged) {
    syncLogged = true;
    Serial.println("[TIME] NTP synchronized");
  }
  int32_t offset = 0;
  bool offsetKnown = locationUtcOffset(offset);
  switch (clockSourceFor(utc != 0, offsetKnown, backendConfigured())) {
    case ClockSource::Local: {
      CivilTime t = localCivil(utc, offset);
      char clock[16], date[24];
      formatLocalClock(t, clock, sizeof(clock));
      formatLocalDate(t, date, sizeof(date));
      bool dayChanged = app.time.currentDay != t.day || app.time.currentMonth != t.month ||
                        app.time.currentYear != t.year;
      app.time.currentYear = t.year;
      app.time.currentMonth = t.month;
      app.time.currentDay = t.day;
      syncCalendarIfUnset(t.year, t.month);
      return setShown(clock, date) || dayChanged;
    }
    case ClockSource::None:
      // No guessed timezone: ask for a location (or wait for the first forecast).
      return setShown("--:--", locationConfigured() ? "---" : "Set location");
    case ClockSource::Backend:
      return false; // /api/status supplies the strings, as before.
  }
  return false;
}
