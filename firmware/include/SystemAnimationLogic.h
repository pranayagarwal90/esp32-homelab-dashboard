#pragma once
#include <stdint.h>
#include "WalkerLogic.h"

// Pure timelines for the system animations (boot, wake, Wi-Fi, restart,
// sleep, loading), shared with host tests. Every animation is a function of
// the time since it started (now - startedAt, rollover-safe); the drawing
// module redraws only what changed between two evaluations.

enum class SystemAnimationType : uint8_t {
  None, Boot, Wake, WifiConnecting, WifiConnected, WifiFailed, Restart, Sleep, Loading
};

// --- Boot: server rack LEDs light one by one, then links to three nodes -------------

constexpr int BOOT_LEDS = 3;
constexpr uint32_t BOOT_LED_AT[BOOT_LEDS] = {150, 400, 650};
constexpr int BOOT_LINKS = 3;
constexpr uint32_t BOOT_LINK_AT[BOOT_LINKS] = {800, 900, 1000};
constexpr uint32_t BOOT_READY_AT = 1050;   // "HOMELAB READY".
constexpr uint32_t BOOT_MS = 1300;

inline int stepsReached(const uint32_t* at, int count, uint32_t elapsed) {
  int n = 0;
  while (n < count && elapsed >= at[n]) n++;
  return n;
}

inline int bootLedsOn(uint32_t elapsed) { return stepsReached(BOOT_LED_AT, BOOT_LEDS, elapsed); }
inline int bootLinks(uint32_t elapsed) { return stepsReached(BOOT_LINK_AT, BOOT_LINKS, elapsed); }

// --- Wake: the moon sets, the sun rises in steps, rays appear --------------------------

constexpr uint32_t WAKE_MOON_GONE_AT = 200;
constexpr uint32_t WAKE_SUN_AT = 250;
constexpr uint32_t WAKE_SUN_STEP_MS = 100;
constexpr int WAKE_SUN_STEPS = 6;
constexpr uint32_t WAKE_RAYS_AT = WAKE_SUN_AT + WAKE_SUN_STEPS * WAKE_SUN_STEP_MS; // Also the text.
constexpr uint32_t WAKE_MS = 1100;

// 0 = below the horizon, WAKE_SUN_STEPS = fully risen.
inline int wakeSunStep(uint32_t elapsed) {
  if (elapsed < WAKE_SUN_AT) return 0;
  uint32_t step = (elapsed - WAKE_SUN_AT) / WAKE_SUN_STEP_MS + 1;
  return step > (uint32_t)WAKE_SUN_STEPS ? WAKE_SUN_STEPS : (int)step;
}

// --- Wi-Fi: arcs grow 0 -> 1 -> 2 -> 3, then restart; success / failure holds ---------

constexpr uint32_t WIFI_FRAME_MS = 220;
constexpr int WIFI_ARCS = 3;
constexpr uint32_t WIFI_CONNECTED_MS = 600;
constexpr uint32_t WIFI_PULSE_MS = 150;     // One pulse ring per step.
constexpr int WIFI_PULSES = 3;
constexpr uint32_t WIFI_FAILED_MS = 800;

inline int wifiArcCount(uint32_t elapsed) {
  return (int)((elapsed / WIFI_FRAME_MS) % (WIFI_ARCS + 1));
}

inline int wifiPulseRings(uint32_t elapsed) {
  uint32_t rings = elapsed / WIFI_PULSE_MS + 1;
  return rings > (uint32_t)WIFI_PULSES ? WIFI_PULSES : (int)rings;
}

// --- Restart: a gear turns while the rack LEDs switch off in reverse -------------------

constexpr uint32_t RESTART_GEAR_MS = 75;     // ~13 fps rotation.
constexpr uint32_t RESTART_LED_OFF_MS = 250;
constexpr uint32_t RESTART_MS = 900;
constexpr int GEAR_TEETH = 8;
constexpr int GEAR_ANGLES = 32;              // 11.25 degree steps.
constexpr int GEAR_PHASES = GEAR_ANGLES / GEAR_TEETH; // Teeth repeat every 4 steps.

inline int restartGearStep(uint32_t elapsed) {
  return (int)((elapsed / RESTART_GEAR_MS) % GEAR_PHASES);
}

inline int restartLedsOn(uint32_t elapsed) {
  uint32_t off = elapsed / RESTART_LED_OFF_MS;
  return off >= (uint32_t)BOOT_LEDS ? 0 : BOOT_LEDS - (int)off;
}

// sin(k * 11.25 degrees) * 64, k = 0..7 (first quadrant plus the axis).
inline int gearSin(int k) {
  static const int8_t QUARTER[9] = {0, 12, 24, 36, 45, 53, 59, 63, 64};
  k = ((k % GEAR_ANGLES) + GEAR_ANGLES) % GEAR_ANGLES;
  if (k <= 8) return QUARTER[k];
  if (k <= 16) return QUARTER[16 - k];
  if (k <= 24) return -QUARTER[k - 16];
  return -QUARTER[32 - k];
}

inline int gearCos(int k) {
  return gearSin(k + GEAR_ANGLES / 4);
}

// Angle index of a tooth at a rotation step.
inline int gearToothAngle(int tooth, int step) {
  return (tooth * GEAR_PHASES + step) % GEAR_ANGLES;
}

// --- Sleep: walk to the bed, lie down, moon and stars, GOOD NIGHT ---------------------

constexpr uint32_t SLEEP_STEP_MS = 120;      // ~8 fps walk.
constexpr int SLEEP_WALK_STEPS = 10;
constexpr int SLEEP_WALK_FROM_X = 96;
constexpr int SLEEP_WALK_TO_X = 204;         // Beside the bed.
constexpr uint32_t SLEEP_LIE_AT = SLEEP_WALK_STEPS * SLEEP_STEP_MS; // 1200.
constexpr uint32_t SLEEP_MOON_AT = 1450;     // Also "GOOD NIGHT".
constexpr int SLEEP_STARS = 3;
constexpr uint32_t SLEEP_STAR_AT[SLEEP_STARS] = {1600, 1700, 1800};
constexpr uint32_t SLEEP_MS = 2100;

enum class SleepPhase : uint8_t { Walk, Lying, Night };

inline SleepPhase sleepPhase(uint32_t elapsed) {
  if (elapsed < SLEEP_LIE_AT) return SleepPhase::Walk;
  if (elapsed < SLEEP_MOON_AT) return SleepPhase::Lying;
  return SleepPhase::Night;
}

inline int sleepWalkStep(uint32_t elapsed) {
  uint32_t step = elapsed / SLEEP_STEP_MS;
  return step >= (uint32_t)SLEEP_WALK_STEPS ? SLEEP_WALK_STEPS - 1 : (int)step;
}

inline int sleepWalkerX(int step) {
  if (step < 0) step = 0;
  if (step > SLEEP_WALK_STEPS - 1) step = SLEEP_WALK_STEPS - 1;
  return SLEEP_WALK_FROM_X + (SLEEP_WALK_TO_X - SLEEP_WALK_FROM_X) * step / (SLEEP_WALK_STEPS - 1);
}

inline int sleepStars(uint32_t elapsed) { return stepsReached(SLEEP_STAR_AT, SLEEP_STARS, elapsed); }

// --- Loading: three dots bounce in turn (reusable helper) ------------------------------

constexpr uint32_t LOADING_FRAME_MS = 150;
constexpr int LOADING_DOTS = 3;
constexpr int LOADING_PHASES = 6;  // Each dot rises then falls; then a pause.

inline int loadingPhase(uint32_t elapsed) {
  return (int)((elapsed / LOADING_FRAME_MS) % LOADING_PHASES);
}

// Height (px) of a dot above its rest line in a phase.
inline int loadingDotLift(int phase, int dot) {
  if (phase == dot * 2 || phase == dot * 2 + 1) return phase % 2 == 0 ? 6 : 3;
  return 0;
}

// --- Durations and ownership ----------------------------------------------------------

// 0: loops until replaced or cancelled.
inline uint32_t systemAnimationDuration(SystemAnimationType type) {
  switch (type) {
    case SystemAnimationType::Boot: return BOOT_MS;
    case SystemAnimationType::Wake: return WAKE_MS;
    case SystemAnimationType::WifiConnected: return WIFI_CONNECTED_MS;
    case SystemAnimationType::WifiFailed: return WIFI_FAILED_MS;
    case SystemAnimationType::Restart: return RESTART_MS;
    case SystemAnimationType::Sleep: return SLEEP_MS;
    default: return 0;
  }
}

struct SystemAnimationState {
  SystemAnimationType type = SystemAnimationType::None;
  uint32_t startedAt = 0;

  void start(SystemAnimationType next, uint32_t now) {
    type = next;
    startedAt = now;
  }
  void cancel() { type = SystemAnimationType::None; }
  bool active() const { return type != SystemAnimationType::None; }
  uint32_t elapsed(uint32_t now) const { return now - startedAt; }
  // A timed animation has played to its end (looping ones never finish).
  bool finished(uint32_t now) const {
    uint32_t duration = systemAnimationDuration(type);
    return active() && duration && elapsed(now) >= duration;
  }
};

// --- Startup sequence: intro (boot or wake), then Wi-Fi ---------------------------------

// esp_sleep_get_wakeup_cause(): 0 (UNDEFINED) on power-on, RST and software
// restart; any other cause means the chip woke from deep sleep.
inline SystemAnimationType startupIntroFor(int wakeCause) {
  return wakeCause == 0 ? SystemAnimationType::Boot : SystemAnimationType::Wake;
}

enum class StartupPhase : uint8_t { Intro, Connecting, Connected, Failed, Done };

// The intro always plays to the end while Wi-Fi connects underneath; then the
// arcs show until connected, a short CONNECTED, and done. A failed attempt
// (the boot loop moving to the other network) shows WI-FI FAILED briefly.
struct StartupSequence {
  SystemAnimationType intro = SystemAnimationType::Boot;
  StartupPhase phase = StartupPhase::Done;
  uint32_t phaseAt = 0;

  void begin(SystemAnimationType introType, uint32_t now) {
    intro = introType;
    phase = StartupPhase::Intro;
    phaseAt = now;
  }

  void enter(StartupPhase next, uint32_t now) {
    phase = next;
    phaseAt = now;
  }

  // True when the phase changed (start the matching animation).
  bool update(uint32_t now, bool connected) {
    uint32_t elapsed = now - phaseAt;
    switch (phase) {
      case StartupPhase::Intro:
        if (elapsed < systemAnimationDuration(intro)) return false;
        enter(connected ? StartupPhase::Connected : StartupPhase::Connecting, now);
        return true;
      case StartupPhase::Connecting:
        if (!connected) return false;
        enter(StartupPhase::Connected, now);
        return true;
      case StartupPhase::Failed:
        if (elapsed < WIFI_FAILED_MS) return false;
        enter(connected ? StartupPhase::Connected : StartupPhase::Connecting, now);
        return true;
      case StartupPhase::Connected:
        if (elapsed < WIFI_CONNECTED_MS) return false;
        enter(StartupPhase::Done, now);
        return true;
      case StartupPhase::Done:
        return false;
    }
    return false;
  }

  // A connection attempt timed out. Shown only once the intro is over.
  bool attemptFailed(uint32_t now) {
    if (phase != StartupPhase::Connecting) return false;
    enter(StartupPhase::Failed, now);
    return true;
  }

  bool done() const { return phase == StartupPhase::Done; }

  SystemAnimationType animation() const {
    switch (phase) {
      case StartupPhase::Intro: return intro;
      case StartupPhase::Connecting: return SystemAnimationType::WifiConnecting;
      case StartupPhase::Connected: return SystemAnimationType::WifiConnected;
      case StartupPhase::Failed: return SystemAnimationType::WifiFailed;
      default: return SystemAnimationType::None;
    }
  }
};
