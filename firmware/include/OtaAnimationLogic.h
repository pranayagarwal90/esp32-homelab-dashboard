#pragma once
#include <stdint.h>
#include "WalkerLogic.h"

// Pure OTA screen logic, shared with host tests: progress mapping, the
// walking-man frame tables and the millis()-driven frame/error timing.
// ArduinoOTA runs the whole upload inside handle(), so frames are advanced
// from onProgress (called after every received chunk), never from loop().

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
