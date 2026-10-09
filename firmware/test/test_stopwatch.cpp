#include "Stopwatch.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static void testStartPauseResume() {
  Stopwatch sw;
  assert(!sw.running && sw.elapsed(5000) == 0);
  sw.start(1000);
  assert(sw.running);
  assert(sw.elapsed(1000) == 0 && sw.elapsed(4500) == 3500);
  sw.start(2000); // Already running: no restart.
  assert(sw.elapsed(4500) == 3500);
  sw.pause(4500);
  assert(!sw.running && sw.elapsed(99999) == 3500); // Frozen while paused.
  sw.pause(6000);                                   // Already paused.
  assert(sw.elapsed(6000) == 3500);
  sw.start(10000); // Resume.
  assert(sw.elapsed(12000) == 5500);
  sw.toggle(13000);
  assert(!sw.running && sw.elapsed(20000) == 6500);
  sw.toggle(20000);
  assert(sw.running && sw.elapsed(20100) == 6600);
}

static void testReset() {
  Stopwatch sw;
  sw.start(0);
  sw.lap(1000);
  sw.reset();
  assert(!sw.running && sw.elapsed(5000) == 0 && sw.lapCount == 0 && sw.lastLapAt == 0);
  // Reset while paused too.
  sw.start(0);
  sw.pause(700);
  sw.reset();
  assert(sw.elapsed(800) == 0);
}

static void testLaps() {
  Stopwatch sw;
  assert(!sw.lap(100)); // Not running.
  sw.start(0);
  assert(sw.lap(72400));
  assert(sw.lap(131100));
  assert(sw.lapCount == 2 && sw.lapMs[0] == 72400 && sw.lapMs[1] == 58700);
  // Pausing excludes the paused time from the next lap.
  sw.pause(140000);
  assert(!sw.lap(150000));
  sw.start(200000);
  assert(sw.lap(201000));
  assert(sw.lapMs[2] == 140000 - 131100 + 1000);

  // Capacity: refused once full, existing laps untouched.
  Stopwatch full;
  full.start(0);
  for (int i = 1; i <= STOPWATCH_MAX_LAPS; i++) assert(full.lap(i * 1000));
  assert(full.lapCount == STOPWATCH_MAX_LAPS);
  assert(!full.lap(99000));
  assert(full.lapCount == STOPWATCH_MAX_LAPS && full.lapMs[STOPWATCH_MAX_LAPS - 1] == 1000);
  assert(full.running && full.elapsed(99000) == 99000);
}

static void testRollover() {
  Stopwatch sw;
  sw.start(0xFFFFFC18u); // 1 s before millis() wraps.
  assert(sw.elapsed(0x000003E8u) == 2000);
  assert(sw.lap(0x000003E8u) && sw.lapMs[0] == 2000);
  sw.pause(0x00000BB8u);
  assert(sw.elapsed(0) == 4000);
  // Long runs saturate at the display cap instead of wrapping.
  Stopwatch longRun;
  longRun.accumulatedMs = STOPWATCH_MAX_MS - 10;
  longRun.start(0);
  assert(longRun.elapsed(1000) == STOPWATCH_MAX_MS);
}

static void testFormat() {
  char text[16];
  formatStopwatch(0, text, sizeof(text));
  assert(strcmp(text, "00:00.0") == 0);
  formatStopwatch(222899, text, sizeof(text));
  assert(strcmp(text, "03:42.8") == 0);
  formatStopwatch(3599999, text, sizeof(text));
  assert(strcmp(text, "59:59.9") == 0);
  formatStopwatch(3600000, text, sizeof(text));
  assert(strcmp(text, "01:00:00.0") == 0);
  formatStopwatch(0xFFFFFFFFu, text, sizeof(text));
  assert(strcmp(text, "99:59:59.9") == 0);
}

static void testLayout() {
  assert(stopwatchHitAt(50, 110) == StopwatchHit::Reset);
  assert(stopwatchHitAt(160, 110) == StopwatchHit::StartPause);
  assert(stopwatchHitAt(270, 110) == StopwatchHit::Lap);
  assert(stopwatchHitAt(160, 60) == StopwatchHit::None);  // Time digits.
  assert(stopwatchHitAt(160, 170) == StopwatchHit::None); // Lap list.
  assert(stopwatchHitAt(10, 220) == StopwatchHit::Back);
  // Buttons and lap lines fit between the header and the bottom bar.
  assert(STOPWATCH_BUTTON_X[2] + STOPWATCH_BUTTON_W <= 320);
  assert(STOPWATCH_BUTTON_Y + STOPWATCH_BUTTON_H < STOPWATCH_LAP_Y0);
  assert(STOPWATCH_LAP_Y0 + 4 * STOPWATCH_LAP_PITCH + 8 <= 205);
}

int main() {
  testStartPauseResume();
  testReset();
  testLaps();
  testRollover();
  testFormat();
  testLayout();
  puts("Stopwatch tests passed: start/pause/resume, reset, laps, capacity, rollover, format, layout");
}
