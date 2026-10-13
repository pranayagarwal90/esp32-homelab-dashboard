#pragma once
#include <stdint.h>
#include "TimeLogic.h"

// The device clock: UTC from NTP, local time from the saved location's UTC
// offset (LocationStore, from Open-Meteo). The one source for the Home clock,
// the screensaver, the Time page and the calendar's "today"; independent of
// the backend. Without a location the clock shows "--:--" (never a guessed
// timezone); a backend without a location keeps supplying the clock as
// before (TimeLogic.h clockSourceFor).

// After Wi-Fi connects: starts SNTP (pool.ntp.org, time.nist.gov). The system
// clock stays UTC; SNTP re-syncs it periodically by itself.
void beginLocalTime();
bool hasUtcTime();
uint32_t utcNow();            // 0 until NTP has set the clock.
ClockSource clockSource();
// Main task, every loop: keeps app.time in step with the local clock. True
// when what the screens show (minute or date) changed.
bool updateLocalClock();
// After a change of location or offset: re-evaluate at the next update.
void invalidateLocalClock();
