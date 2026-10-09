#include "SystemAnimationLogic.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

static void testBoot() {
  assert(bootLedsOn(0) == 0);
  assert(bootLedsOn(BOOT_LED_AT[0] - 1) == 0 && bootLedsOn(BOOT_LED_AT[0]) == 1);
  assert(bootLedsOn(BOOT_LED_AT[1]) == 2 && bootLedsOn(BOOT_LED_AT[2]) == 3);
  assert(bootLedsOn(100000) == BOOT_LEDS);
  // LEDs first, then links, then READY, all within the animation.
  assert(bootLinks(BOOT_LED_AT[2]) == 0);
  assert(bootLinks(BOOT_LINK_AT[0]) == 1 && bootLinks(BOOT_LINK_AT[2]) == BOOT_LINKS);
  assert(BOOT_LINK_AT[0] > BOOT_LED_AT[BOOT_LEDS - 1]);
  assert(BOOT_READY_AT > BOOT_LINK_AT[BOOT_LINKS - 1] && BOOT_READY_AT < BOOT_MS);
  // Monotonic progression.
  for (uint32_t t = 1; t < 2000; t++) assert(bootLedsOn(t) >= bootLedsOn(t - 1));
  assert(BOOT_MS >= 1000 && BOOT_MS <= 1500);
}

static void testWake() {
  assert(wakeSunStep(0) == 0 && wakeSunStep(WAKE_SUN_AT - 1) == 0);
  assert(wakeSunStep(WAKE_SUN_AT) == 1);
  assert(wakeSunStep(WAKE_SUN_AT + WAKE_SUN_STEP_MS) == 2);
  assert(wakeSunStep(WAKE_RAYS_AT) == WAKE_SUN_STEPS && wakeSunStep(100000) == WAKE_SUN_STEPS);
  for (uint32_t t = 1; t < 2000; t++) assert(wakeSunStep(t) >= wakeSunStep(t - 1));
  assert(WAKE_MOON_GONE_AT < WAKE_SUN_AT && WAKE_RAYS_AT < WAKE_MS);
  assert(WAKE_MS >= 700 && WAKE_MS <= 1200);
}

static void testWifiArcs() {
  // 0 -> 1 -> 2 -> 3 arcs, then start over.
  const int expected[] = {0, 1, 2, 3, 0, 1, 2, 3, 0};
  for (int i = 0; i < 9; i++) assert(wifiArcCount(i * WIFI_FRAME_MS) == expected[i]);
  assert(wifiArcCount(WIFI_FRAME_MS - 1) == 0);
  assert(wifiPulseRings(0) == 1 && wifiPulseRings(WIFI_PULSE_MS) == 2);
  assert(wifiPulseRings(10000) == WIFI_PULSES);
  assert(WIFI_CONNECTED_MS >= 400 && WIFI_CONNECTED_MS <= 700);
}

static void testRestart() {
  assert(restartLedsOn(0) == BOOT_LEDS);
  assert(restartLedsOn(RESTART_LED_OFF_MS) == BOOT_LEDS - 1);
  assert(restartLedsOn(RESTART_MS) == 0);
  // The gear cycles through its four distinct phases.
  for (int i = 0; i < 12; i++) assert(restartGearStep(i * RESTART_GEAR_MS) == i % GEAR_PHASES);
  assert(RESTART_MS >= 600 && RESTART_MS <= 1000);

  // Sine table: unit circle (x64) and the right quadrants.
  for (int k = 0; k < GEAR_ANGLES; k++) {
    int s = gearSin(k), c = gearCos(k);
    int r2 = s * s + c * c;
    assert(abs(r2 - 64 * 64) <= 64 * 64 / 20); // Within 5%.
  }
  assert(gearSin(0) == 0 && gearSin(8) == 64 && gearSin(16) == 0 && gearSin(24) == -64);
  assert(gearCos(0) == 64 && gearCos(8) == 0 && gearCos(16) == -64);
  assert(gearSin(-8) == -64 && gearSin(40) == 64); // Wraps.
  // Teeth spread evenly; a step rotates every tooth by one angle.
  assert(gearToothAngle(0, 0) == 0 && gearToothAngle(1, 0) == GEAR_PHASES);
  assert(gearToothAngle(GEAR_TEETH - 1, GEAR_PHASES - 1) == GEAR_ANGLES - 1);
  assert(gearToothAngle(3, 1) == gearToothAngle(3, 0) + 1);
}

static void testSleep() {
  assert(sleepPhase(0) == SleepPhase::Walk);
  assert(sleepPhase(SLEEP_LIE_AT - 1) == SleepPhase::Walk && sleepPhase(SLEEP_LIE_AT) == SleepPhase::Lying);
  assert(sleepPhase(SLEEP_MOON_AT) == SleepPhase::Night && sleepPhase(SLEEP_MS) == SleepPhase::Night);
  // The walker advances every step, from the start to beside the bed.
  assert(sleepWalkStep(0) == 0 && sleepWalkStep(SLEEP_STEP_MS) == 1);
  assert(sleepWalkStep(SLEEP_LIE_AT + 5000) == SLEEP_WALK_STEPS - 1);
  assert(sleepWalkerX(0) == SLEEP_WALK_FROM_X && sleepWalkerX(SLEEP_WALK_STEPS - 1) == SLEEP_WALK_TO_X);
  for (int s = 1; s < SLEEP_WALK_STEPS; s++) {
    int dx = sleepWalkerX(s) - sleepWalkerX(s - 1);
    assert(dx >= 8 && dx <= 16); // A stride per frame, not a slide or a jump.
    // A real walk cycle: the pose changes every step.
    assert(s % WALKER_FRAMES != (s - 1) % WALKER_FRAMES);
  }
  assert(sleepWalkerX(-3) == SLEEP_WALK_FROM_X && sleepWalkerX(99) == SLEEP_WALK_TO_X);
  // Stop short of the bed (bed at x 222, walker extends 15 px right).
  assert(SLEEP_WALK_TO_X + WALKER_MAX_X < 222);
  assert(sleepStars(SLEEP_MOON_AT) == 0 && sleepStars(SLEEP_STAR_AT[0]) == 1);
  assert(sleepStars(SLEEP_MS) == SLEEP_STARS);
  assert(SLEEP_STAR_AT[SLEEP_STARS - 1] < SLEEP_MS);
  assert(SLEEP_MS >= 1500 && SLEEP_MS <= 2500);
}

static void testLoading() {
  for (int phase = 0; phase < LOADING_PHASES; phase++) {
    int lifted = 0;
    for (int dot = 0; dot < LOADING_DOTS; dot++) lifted += loadingDotLift(phase, dot) > 0;
    assert(lifted == 1); // One dot in the air at a time.
  }
  assert(loadingDotLift(0, 0) > loadingDotLift(1, 0)); // Up, then coming down.
  assert(loadingDotLift(2, 1) > 0 && loadingDotLift(4, 2) > 0);
  assert(loadingPhase(0) == 0 && loadingPhase(LOADING_FRAME_MS) == 1);
  assert(loadingPhase(LOADING_PHASES * LOADING_FRAME_MS) == 0);
}

static void testStateAndRollover() {
  SystemAnimationState anim;
  assert(!anim.active() && !anim.finished(5000));
  anim.start(SystemAnimationType::Restart, 0xFFFFFF00u); // 256 ms before wrap.
  assert(anim.active());
  assert(anim.elapsed(0x00000100u) == 512);
  assert(!anim.finished(0x00000100u));
  assert(anim.finished(0xFFFFFF00u + RESTART_MS));
  anim.cancel();
  assert(!anim.active() && !anim.finished(0xFFFFFF00u + RESTART_MS));
  // Looping animations never finish on their own.
  anim.start(SystemAnimationType::WifiConnecting, 0);
  assert(!anim.finished(1000000));
  anim.start(SystemAnimationType::Loading, 0);
  assert(!anim.finished(1000000));
  for (int t = (int)SystemAnimationType::None; t <= (int)SystemAnimationType::Loading; t++) {
    uint32_t d = systemAnimationDuration((SystemAnimationType)t);
    assert(d <= 2500);
  }
}

static void testStartupSelection() {
  assert(startupIntroFor(0) == SystemAnimationType::Boot); // Power-on / RST / restart.
  assert(startupIntroFor(2) == SystemAnimationType::Wake); // EXT0 (touch).
  assert(startupIntroFor(4) == SystemAnimationType::Wake); // Any other wake cause.
}

static void testStartupSequence() {
  // Wi-Fi connects during the intro: intro plays out, then CONNECTED, done.
  StartupSequence fast;
  fast.begin(SystemAnimationType::Boot, 1000);
  assert(fast.animation() == SystemAnimationType::Boot);
  assert(!fast.update(1500, true));
  assert(fast.update(1000 + BOOT_MS, true));
  assert(fast.phase == StartupPhase::Connected && fast.animation() == SystemAnimationType::WifiConnected);
  assert(!fast.update(1000 + BOOT_MS + WIFI_CONNECTED_MS - 1, true));
  assert(fast.update(1000 + BOOT_MS + WIFI_CONNECTED_MS, true) && fast.done());
  assert(fast.animation() == SystemAnimationType::None);
  assert(!fast.update(99999, true));

  // Slow Wi-Fi: intro, arcs until connected, CONNECTED.
  StartupSequence slow;
  slow.begin(SystemAnimationType::Wake, 0);
  assert(slow.update(WAKE_MS, false) && slow.phase == StartupPhase::Connecting);
  assert(slow.animation() == SystemAnimationType::WifiConnecting);
  assert(!slow.update(5000, false));
  assert(slow.update(5020, true) && slow.phase == StartupPhase::Connected);

  // A failed attempt: WI-FI FAILED briefly, then back to connecting.
  StartupSequence failing;
  failing.begin(SystemAnimationType::Boot, 0);
  assert(!failing.attemptFailed(500)); // Not during the intro.
  failing.update(BOOT_MS, false);
  assert(failing.attemptFailed(16000) && failing.animation() == SystemAnimationType::WifiFailed);
  assert(!failing.attemptFailed(16100)); // Already showing.
  assert(!failing.update(16000 + WIFI_FAILED_MS - 1, false));
  assert(failing.update(16000 + WIFI_FAILED_MS, false) && failing.phase == StartupPhase::Connecting);
  // Connected while the failure shows: straight to CONNECTED afterwards.
  failing.attemptFailed(32000);
  assert(!failing.update(32100, true));
  assert(failing.update(32000 + WIFI_FAILED_MS, true) && failing.phase == StartupPhase::Connected);

  // Bounded startup overhead when Wi-Fi is instant: intro + CONNECTED.
  assert(BOOT_MS + WIFI_CONNECTED_MS <= 2000 && WAKE_MS + WIFI_CONNECTED_MS <= 2000);

  // Rollover during startup.
  StartupSequence wrap;
  wrap.begin(SystemAnimationType::Boot, 0xFFFFFF00u);
  assert(!wrap.update(0x00000100u, true));
  assert(wrap.update(0xFFFFFF00u + BOOT_MS, true));
}

int main() {
  testBoot();
  testWake();
  testWifiArcs();
  testRestart();
  testSleep();
  testLoading();
  testStateAndRollover();
  testStartupSelection();
  testStartupSequence();
  puts("System animation tests passed: boot, wake, wifi arcs, restart, sleep, loading, state/rollover, "
       "startup selection, startup sequence");
}
