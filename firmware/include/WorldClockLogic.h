#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string.h>

// Pure world-clock list (host-tested): /api/status `world_clocks` is
// [{label, timezone, time}] from the backend's DASHBOARD_WORLD_CLOCKS, in
// its order. The firmware only shows the label and the backend-formatted
// time; the device's own local time and calendar still come from
// timezones.local. Fixed-size storage, no heap.

constexpr int MAX_WORLD_CLOCKS = 8;     // The backend accepts at most 8 too.
constexpr size_t CLOCK_LABEL_SIZE = 21; // 20 characters.
constexpr size_t CLOCK_TIME_SIZE = 10;  // "12:59 PM".

struct WorldClock {
  char label[CLOCK_LABEL_SIZE];
  char time[CLOCK_TIME_SIZE];
};

struct WorldClockState {
  WorldClock items[MAX_WORLD_CLOCKS];
  uint8_t count = 0;
};

// Printable ASCII only (others become '?'), cut to fit.
inline void copyClockText(char* out, size_t size, const char* text) {
  size_t n = 0;
  for (const char* p = text ? text : ""; *p && n + 1 < size; p++) out[n++] = (*p >= ' ' && *p <= '~') ? *p : '?';
  out[n] = '\0';
}

// Adds one clock; false (ignored) without a label, or once the list is full.
// A missing or overlong time shows as "--:--".
inline bool addWorldClock(WorldClockState& state, const char* label, const char* time) {
  if (state.count >= MAX_WORLD_CLOCKS || !label || !label[0]) return false;
  WorldClock& clock = state.items[state.count++];
  copyClockText(clock.label, sizeof(clock.label), label);
  bool timeOk = time && time[0] && strlen(time) < CLOCK_TIME_SIZE;
  copyClockText(clock.time, sizeof(clock.time), timeOk ? time : "--:--");
  return true;
}

// CLOCKS page: three rows under the local time and weather.
constexpr int CLOCKS_PER_PAGE = 3;
constexpr int CLOCKS_TITLE_Y = 126;
constexpr int CLOCKS_ROW_Y0 = 144;
constexpr int CLOCKS_ROW_PITCH = 20;
