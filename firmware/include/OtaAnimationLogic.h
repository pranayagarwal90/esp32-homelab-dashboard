#pragma once
#include <stdint.h>

// Pure OTA screen logic, shared with host tests: progress mapping, the
// walking-man frame tables and the millis()-driven frame/error timing.
// ArduinoOTA runs the whole upload inside handle(), so frames are advanced
// from onProgress (called after every received chunk), never from loop().

// --- Walk cycle -------------------------------------------------------------------

struct WalkerPoint {
  int8_t x, y;
};

// Coordinates relative to the point on the ground under the hips (y up is
// negative). Head, shoulders, hips, elbows, hands and knees move with `bob`;
// feet are on the ground and do not. "Left" is the far side (drawn dimmer).
struct WalkerFrame {
  int8_t bob; // Body offset, positive = lower.
  WalkerPoint leftElbow, leftHand, rightElbow, rightHand;
  WalkerPoint leftKnee, rightKnee;
  WalkerPoint leftFoot, rightFoot;
};

constexpr WalkerPoint WALKER_HIP = {0, -26};
constexpr WalkerPoint WALKER_SHOULDER = {2, -45}; // Slight forward lean.
constexpr WalkerPoint WALKER_HEAD = {4, -54};
constexpr int WALKER_HEAD_R = 7;

// Six frames: contact, down, passing, then the same with the sides swapped.
// Each arm swings opposite to the leg on its side.
constexpr int WALKER_FRAMES = 6;
static const WalkerFrame WALK_CYCLE[WALKER_FRAMES] = {
  // Contact: right leg forward, left arm forward.
  {0, {6, -35}, {11, -26}, {-2, -35}, {-8, -27}, {-5, -13}, {6, -14}, {-11, -2}, {11, 0}},
  // Down: weight on the right leg, left foot lifting.
  {1, {5, -35}, {8, -26}, {-1, -35}, {-5, -26}, {-3, -12}, {5, -13}, {-9, -4}, {7, 0}},
  // Passing: left leg swings through, arms close to the body.
  {-1, {3, -35}, {4, -25}, {0, -35}, {-1, -25}, {4, -13}, {2, -13}, {-1, -5}, {1, 0}},
  // Mirror: left leg forward, right arm forward.
  {0, {-2, -35}, {-8, -27}, {6, -35}, {11, -26}, {6, -14}, {-5, -13}, {11, 0}, {-11, -2}},
  {1, {-1, -35}, {-5, -26}, {5, -35}, {8, -26}, {5, -13}, {-3, -12}, {7, 0}, {-9, -4}},
  {-1, {0, -35}, {-1, -25}, {3, -35}, {4, -25}, {2, -13}, {4, -13}, {1, 0}, {-1, -5}},
};

// Arrived: standing with the near arm raised.
static const WalkerFrame WALKER_STANDING = {
  0, {1, -35}, {1, -25}, {10, -47}, {14, -56}, {-1, -13}, {2, -13}, {-3, 0}, {4, 0}
};

// Every pose lies inside this box (local coordinates), so erasing it is safe.
constexpr int WALKER_MIN_X = -14;
constexpr int WALKER_MAX_X = 15; // Lines are drawn 2 px wide (x and x + 1).
constexpr int WALKER_MIN_Y = -62;
constexpr int WALKER_MAX_Y = 0;

// --- Screen layout ------------------------------------------------------------------

constexpr int OTA_GROUND_Y = 142;     // Feet; the ground line is just below.
constexpr int OTA_WALK_START_X = 24;  // Walker position at 0 %.
constexpr int OTA_WALK_END_X = 250;   // At 100 %: next to the house.
constexpr int OTA_HOME_X = 270;       // House (roof) left edge.
constexpr int OTA_BAR_X = 16;
constexpr int OTA_BAR_Y = 158;
constexpr int OTA_BAR_W = 230;
constexpr int OTA_BAR_H = 18;

constexpr uint32_t OTA_FRAME_MS = 140;        // About 7 frames per second.
constexpr uint32_t OTA_ERROR_HOLD_MS = 10000; // Error screen before returning.

// Percent of the upload, 0..100, safe for total == 0 and progress > total.
inline uint8_t otaPercent(uint32_t progress, uint32_t total) {
  if (total == 0) return 0;
  if (progress >= total) return 100;
  return (uint8_t)((uint64_t)progress * 100 / total);
}

inline int otaWalkerX(uint8_t percent) {
  if (percent > 100) percent = 100;
  return OTA_WALK_START_X + (OTA_WALK_END_X - OTA_WALK_START_X) * percent / 100;
}

// Filled width of the progress bar interior.
inline int otaBarFill(uint8_t percent) {
  if (percent > 100) percent = 100;
  return (OTA_BAR_W - 4) * percent / 100;
}

// --- State ----------------------------------------------------------------------------

enum class OtaScreen : uint8_t { Idle, Running, Complete, Error };

struct OtaAnimation {
  OtaScreen screen = OtaScreen::Idle;
  uint8_t percent = 0;
  uint8_t frame = 0;
  uint32_t frameAt = 0;
  uint32_t errorAt = 0;
  int errorCode = -1;

  void begin(uint32_t now) {
    *this = OtaAnimation();
    screen = OtaScreen::Running;
    frameAt = now;
  }

  // True when the percentage changed (redraw the bar and text).
  bool setProgress(uint32_t progress, uint32_t total) {
    uint8_t next = otaPercent(progress, total);
    if (screen != OtaScreen::Running || next == percent) return false;
    percent = next;
    return true;
  }

  // True when the walk cycle advanced a frame (redraw the walker).
  bool tick(uint32_t now) {
    if (screen != OtaScreen::Running || now - frameAt < OTA_FRAME_MS) return false;
    frameAt = now;
    frame = (frame + 1) % WALKER_FRAMES;
    return true;
  }

  void complete() {
    screen = OtaScreen::Complete;
    percent = 100;
  }

  // The first error of an attempt wins (ArduinoOTA can report a connect error
  // followed by an end error). True when the error screen should be drawn.
  bool fail(int code, uint32_t now) {
    if (screen == OtaScreen::Error) return false;
    screen = OtaScreen::Error;
    errorCode = code;
    errorAt = now;
    return true;
  }

  // While true, the error screen owns the display. Returns false (and goes
  // idle) once the hold time has passed.
  bool holdError(uint32_t now) {
    if (screen != OtaScreen::Error) return false;
    if (now - errorAt < OTA_ERROR_HOLD_MS) return true;
    screen = OtaScreen::Idle;
    return false;
  }
};

// ArduinoOTA's ota_error_t values.
inline const char* otaErrorText(int code) {
  switch (code) {
    case 0: return "Authentication failed";
    case 1: return "Could not start update";
    case 2: return "Could not connect";
    case 3: return "Receive failed";
    case 4: return "Could not finish update";
    default: return "Update failed";
  }
}
