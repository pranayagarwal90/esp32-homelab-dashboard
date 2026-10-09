#pragma once
#include <stdint.h>
#include <stdio.h>

// Pure stopwatch, shared with host tests. Time comes from the caller
// (millis()); all arithmetic is unsigned subtraction, so it is rollover-safe.
// Laps are a fixed array: once full, further laps are refused.

constexpr int STOPWATCH_MAX_LAPS = 10;
// Display cap: 99:59:59.9.
constexpr uint32_t STOPWATCH_MAX_MS = 99UL * 3600000UL + 59UL * 60000UL + 59999UL;

struct Stopwatch {
  bool running = false;
  uint32_t accumulatedMs = 0; // Time from finished running spans.
  uint32_t startedAt = 0;     // millis() of the current span, when running.
  uint32_t lapMs[STOPWATCH_MAX_LAPS] = {}; // Duration of each lap.
  uint8_t lapCount = 0;
  uint32_t lastLapAt = 0;     // Elapsed time at the previous lap.

  uint32_t elapsed(uint32_t now) const {
    uint32_t total = accumulatedMs + (running ? now - startedAt : 0);
    return total > STOPWATCH_MAX_MS ? STOPWATCH_MAX_MS : total;
  }

  // Start or resume.
  void start(uint32_t now) {
    if (running) return;
    running = true;
    startedAt = now;
  }

  void pause(uint32_t now) {
    if (!running) return;
    accumulatedMs = elapsed(now);
    running = false;
  }

  void toggle(uint32_t now) {
    if (running) pause(now);
    else start(now);
  }

  // Stops and clears everything, including laps.
  void reset() {
    *this = Stopwatch();
  }

  // Records the time since the previous lap. Only while running; refused
  // (false) when the lap list is full.
  bool lap(uint32_t now) {
    if (!running || lapCount >= STOPWATCH_MAX_LAPS) return false;
    uint32_t total = elapsed(now);
    lapMs[lapCount++] = total - lastLapAt;
    lastLapAt = total;
    return true;
  }
};

// "MM:SS.d" under an hour, otherwise "HH:MM:SS.d" (tenths truncated).
inline void formatStopwatch(uint32_t ms, char* out, size_t size) {
  if (ms > STOPWATCH_MAX_MS) ms = STOPWATCH_MAX_MS;
  unsigned tenths = (ms / 100) % 10;
  unsigned seconds = (ms / 1000) % 60;
  unsigned minutes = (ms / 60000) % 60;
  unsigned hours = ms / 3600000;
  if (hours) snprintf(out, size, "%02u:%02u:%02u.%u", hours, minutes, seconds, tenths);
  else snprintf(out, size, "%02u:%02u.%u", minutes, seconds, tenths);
}

// --- Stopwatch page layout ----------------------------------------------------------

// Three 96x44 buttons under the time: RESET | START/PAUSE/RESUME | LAP.
constexpr int STOPWATCH_BUTTON_Y = 90;
constexpr int STOPWATCH_BUTTON_H = 44;
constexpr int STOPWATCH_BUTTON_W = 96;
constexpr int STOPWATCH_BUTTON_X[3] = {10, 112, 214};
// Laps: two columns of five lines (laps 1-5 and 6-10).
constexpr int STOPWATCH_LAP_Y0 = 148;
constexpr int STOPWATCH_LAP_PITCH = 11;

enum class StopwatchHit : uint8_t { None, Reset, StartPause, Lap, Back };

inline StopwatchHit stopwatchHitAt(int x, int y) {
  if (y >= 205) return StopwatchHit::Back;
  // The touch band extends a little beyond the drawn buttons.
  if (y < STOPWATCH_BUTTON_Y - 6 || y > STOPWATCH_BUTTON_Y + STOPWATCH_BUTTON_H + 6) return StopwatchHit::None;
  if (x < 107) return StopwatchHit::Reset;
  if (x < 213) return StopwatchHit::StartPause;
  return StopwatchHit::Lap;
}
