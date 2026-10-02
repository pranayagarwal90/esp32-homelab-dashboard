#include "BacklightControl.h"
#include <assert.h>
#include <stdio.h>

static SolarSchedule schedule(uint32_t seconds) {
  SolarSchedule value;
  value.dayStart = 1800000000;
  value.dayEnd = value.dayStart + 86400;
  value.validUntil = value.dayEnd;
  value.sunrise = value.dayStart + 7 * 3600;
  value.sunset = value.dayStart + 19 * 3600;
  value.timestamp = value.dayStart + seconds;
  return value;
}

int main() {
  BacklightControl control;
  assert(control.duty(0) == 255); // Missing solar data.
  control.setSchedule(schedule(12 * 3600), 0);
  assert(control.duty(0) == 255);
  control.acceptedTouch(100);
  assert(control.duty(100) == 255); // Day touch has no effect.
  control.setSchedule(schedule(7 * 3600), 0);
  assert(control.duty(0) == 255); // Sunrise inclusive.
  control.setSchedule(schedule(19 * 3600 - 1), 0);
  assert(control.duty(999) == 255);
  assert(control.duty(1000) == 38); // Sunset inclusive, between status fetches.
  control.setSchedule(schedule(23 * 3600), 0);
  assert(control.duty(0) == 38);
  control.setSchedule(schedule(3600), 0);
  assert(control.duty(0) == 38); // After midnight is still night.
  control.acceptedTouch(100);
  assert(control.duty(100) == 153); // Start.
  assert(control.duty(30099) == 153);
  control.acceptedTouch(30000);
  assert(control.duty(30100) == 153); // Extension past original expiry.
  assert(control.duty(59999) == 153);
  assert(control.duty(60000) == 38); // Exact extended expiry.
  control.setSchedule(schedule(23 * 3600 + 3599), 0);
  assert(control.duty(999) == 38);
  assert(control.duty(1000) == 255); // Expired prior-day schedule is unsafe.
  auto overnight = schedule(23 * 3600 + 3599);
  overnight.validUntil = overnight.dayEnd + 7 * 3600;
  control.setSchedule(overnight, 0);
  assert(control.duty(1000) == 38); // Cached next sunrise covers midnight.
  control.acceptedTouch(500);
  assert(control.duty(1000) == 153); // Boost survives midnight.
  assert(control.duty(30500) == 38);
  assert(control.duty(7 * 3600000 + 1000) == 255); // Next sunrise fallback.
  control.setSchedule(schedule(0), 1000);
  assert(control.duty(1000) == 38); // New day's schedule restores night.
  auto invalid = schedule(3600);
  invalid.sunrise = 0;
  control.setSchedule(invalid, 0);
  assert(control.duty(0) == 255);
  invalid = schedule(3600);
  invalid.sunset = 0;
  control.setSchedule(invalid, 0);
  assert(control.duty(0) == 255);
  invalid = schedule(3600);
  invalid.sunset = invalid.sunrise;
  control.setSchedule(invalid, 0);
  assert(control.duty(0) == 255);
  control.setSchedule(schedule(3600), UINT32_MAX - 10000);
  control.acceptedTouch(UINT32_MAX - 5000);
  assert(control.duty(24998) == 153);
  assert(control.duty(24999) == 38); // Boost expiry across millis rollover.
  control.setSchedule(schedule(19 * 3600), 0);
  control.acceptedTouch(0);
  control.setSchedule(schedule(12 * 3600), 1);
  assert(control.duty(1) == 255); // Day cancels an active boost.
  control.setSchedule(schedule(19 * 3600), 2);
  assert(control.duty(2) == 38);
  puts("Backlight tests passed: day/night, boundaries, midnight, missing/invalid, boost, rollover");
}
