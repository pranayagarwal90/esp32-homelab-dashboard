#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "BacklightControl.h"

// User-adjustable device settings. Pure (no Arduino dependencies) so the
// defaults, option steps and NVS encoding are covered by host tests.
// Defaults reproduce the pre-Settings firmware exactly.

constexpr size_t WALLPAPER_NAME_MAX = 64; // Including the terminator.

struct DeviceSettings {
  bool autoBrightness = true;      // Sunrise/sunset schedule vs. fixed level.
  uint8_t manualBrightness = 100;  // Percent, used when autoBrightness is off.
  uint8_t dayBrightness = 100;     // Percent; 100% = duty 255.
  uint8_t nightBrightness = 15;    // Percent; 15% = duty 38.
  uint8_t boostBrightness = 60;    // Percent; 60% = duty 153.

  bool screensaverEnabled = true;
  uint8_t screensaverTimeoutMin = 3;
  uint8_t screensaverRotateSec = 30;

  bool bluetoothEnabled = false;   // Boot-time preference; see Bluetooth notes.

  // Photo shown by the screensaver instead of rotating; "" = rotate all.
  char wallpaper[WALLPAPER_NAME_MAX] = "";
};

// --- Option steps -----------------------------------------------------------

constexpr uint8_t BRIGHTNESS_STEP = 5;
constexpr uint8_t MANUAL_MIN = 10;   // Never let the screen go fully dark.
constexpr uint8_t DAY_MIN = 10;
constexpr uint8_t NIGHT_MIN = 5;
constexpr uint8_t BOOST_MIN = 10;
constexpr uint8_t BRIGHTNESS_MAX = 100;

constexpr uint8_t SCREENSAVER_TIMEOUTS_MIN[] = {1, 3, 5, 10};
constexpr uint8_t SCREENSAVER_ROTATIONS_SEC[] = {15, 30, 60};

inline uint8_t percentToDuty(uint8_t percent) {
  if (percent > 100) percent = 100;
  return uint8_t((uint16_t(percent) * 255 + 50) / 100);
}

// Steps a percentage by +/-BRIGHTNESS_STEP within [minimum, BRIGHTNESS_MAX].
inline uint8_t stepBrightness(uint8_t value, int direction, uint8_t minimum) {
  int next = int(value) + (direction > 0 ? BRIGHTNESS_STEP : -int(BRIGHTNESS_STEP));
  if (next < minimum) next = minimum;
  if (next > BRIGHTNESS_MAX) next = BRIGHTNESS_MAX;
  return uint8_t(next);
}

// Moves to the neighbouring option, clamping at the ends. An unknown current
// value restarts from the first option.
template <size_t N>
inline uint8_t stepOption(const uint8_t (&options)[N], uint8_t value, int direction) {
  size_t index = N;
  for (size_t i = 0; i < N; i++) if (options[i] == value) index = i;
  if (index == N) return options[0];
  if (direction > 0 && index + 1 < N) index++;
  if (direction < 0 && index > 0) index--;
  return options[index];
}

template <size_t N>
inline bool isOption(const uint8_t (&options)[N], uint8_t value) {
  for (size_t i = 0; i < N; i++) if (options[i] == value) return true;
  return false;
}

// --- Validation ---------------------------------------------------------------

inline bool validBrightness(uint8_t value, uint8_t minimum) {
  return value >= minimum && value <= BRIGHTNESS_MAX && value % BRIGHTNESS_STEP == 0;
}

// A plain photo file name: printable ASCII, no path separators, terminated.
inline bool validWallpaperName(const char* name, size_t capacity) {
  size_t length = strnlen(name, capacity);
  if (length == capacity) return false;
  for (size_t i = 0; i < length; i++) {
    char c = name[i];
    if (c < 0x21 || c > 0x7e || c == '/' || c == '\\') return false;
  }
  return true;
}

inline void resetIfInvalid(bool valid, uint8_t& field, uint8_t fallback, bool& ok) {
  if (!valid) {
    field = fallback;
    ok = false;
  }
}

// Replaces every out-of-range field with its default. Returns true if all
// fields were already valid.
inline bool sanitizeSettings(DeviceSettings& s) {
  const DeviceSettings d;
  bool ok = true;
  resetIfInvalid(validBrightness(s.manualBrightness, MANUAL_MIN), s.manualBrightness, d.manualBrightness, ok);
  resetIfInvalid(validBrightness(s.dayBrightness, DAY_MIN), s.dayBrightness, d.dayBrightness, ok);
  resetIfInvalid(validBrightness(s.nightBrightness, NIGHT_MIN), s.nightBrightness, d.nightBrightness, ok);
  resetIfInvalid(validBrightness(s.boostBrightness, BOOST_MIN), s.boostBrightness, d.boostBrightness, ok);
  resetIfInvalid(isOption(SCREENSAVER_TIMEOUTS_MIN, s.screensaverTimeoutMin),
                 s.screensaverTimeoutMin, d.screensaverTimeoutMin, ok);
  resetIfInvalid(isOption(SCREENSAVER_ROTATIONS_SEC, s.screensaverRotateSec),
                 s.screensaverRotateSec, d.screensaverRotateSec, ok);
  if (!validWallpaperName(s.wallpaper, sizeof(s.wallpaper))) {
    s.wallpaper[0] = '\0';
    ok = false;
  }
  return ok;
}

// --- NVS blob encoding ----------------------------------------------------------
// Explicit byte layout (independent of struct padding):
//   0 magic 'S', 1 version, 2 flags (bit0 auto, bit1 screensaver, bit2 BT),
//   3 manual, 4 day, 5 night, 6 boost, 7 timeout min, 8 rotate sec,
//   9 wallpaper length n, 10.. wallpaper bytes (no terminator).

constexpr uint8_t SETTINGS_MAGIC = 'S';
constexpr uint8_t SETTINGS_VERSION = 1;
constexpr size_t SETTINGS_HEADER_SIZE = 10;
constexpr size_t SETTINGS_BLOB_MAX = SETTINGS_HEADER_SIZE + WALLPAPER_NAME_MAX - 1;

inline size_t encodeSettings(const DeviceSettings& s, uint8_t (&out)[SETTINGS_BLOB_MAX]) {
  size_t wallpaperLength = strnlen(s.wallpaper, sizeof(s.wallpaper) - 1);
  out[0] = SETTINGS_MAGIC;
  out[1] = SETTINGS_VERSION;
  out[2] = (s.autoBrightness ? 1 : 0) | (s.screensaverEnabled ? 2 : 0) | (s.bluetoothEnabled ? 4 : 0);
  out[3] = s.manualBrightness;
  out[4] = s.dayBrightness;
  out[5] = s.nightBrightness;
  out[6] = s.boostBrightness;
  out[7] = s.screensaverTimeoutMin;
  out[8] = s.screensaverRotateSec;
  out[9] = uint8_t(wallpaperLength);
  memcpy(out + SETTINGS_HEADER_SIZE, s.wallpaper, wallpaperLength);
  return SETTINGS_HEADER_SIZE + wallpaperLength;
}

// Decodes a stored blob into out. Unknown/corrupt blobs yield defaults; known
// blobs with bad fields keep the valid fields. Returns true if the blob was
// fully valid.
inline bool decodeSettings(const uint8_t* data, size_t length, DeviceSettings& out) {
  out = DeviceSettings();
  if (!data || length < SETTINGS_HEADER_SIZE || data[0] != SETTINGS_MAGIC ||
      data[1] != SETTINGS_VERSION || length != SETTINGS_HEADER_SIZE + data[9] ||
      data[9] >= WALLPAPER_NAME_MAX || (data[2] & ~0x07)) {
    return false;
  }
  out.autoBrightness = data[2] & 1;
  out.screensaverEnabled = data[2] & 2;
  out.bluetoothEnabled = data[2] & 4;
  out.manualBrightness = data[3];
  out.dayBrightness = data[4];
  out.nightBrightness = data[5];
  out.boostBrightness = data[6];
  out.screensaverTimeoutMin = data[7];
  out.screensaverRotateSec = data[8];
  memcpy(out.wallpaper, data + SETTINGS_HEADER_SIZE, data[9]);
  out.wallpaper[data[9]] = '\0';
  return sanitizeSettings(out);
}

// Idle time before the screensaver starts; 0 = screensaver disabled.
inline uint32_t screensaverTimeoutMs(const DeviceSettings& s) {
  return s.screensaverEnabled ? uint32_t(s.screensaverTimeoutMin) * 60000UL : 0;
}

inline uint32_t screensaverRotateMs(const DeviceSettings& s) {
  return uint32_t(s.screensaverRotateSec) * 1000UL;
}

// blocked: the current view must stay visible (setup hotspot, dialogs).
inline bool shouldStartScreensaver(const DeviceSettings& s, uint32_t idleMs, bool blocked) {
  uint32_t timeout = screensaverTimeoutMs(s);
  return timeout != 0 && !blocked && idleMs >= timeout;
}

inline BacklightControl::Levels backlightLevels(const DeviceSettings& s) {
  BacklightControl::Levels levels;
  levels.automatic = s.autoBrightness;
  levels.manual = percentToDuty(s.manualBrightness);
  levels.day = percentToDuty(s.dayBrightness);
  levels.night = percentToDuty(s.nightBrightness);
  levels.boost = percentToDuty(s.boostBrightness);
  return levels;
}

inline bool sameSettings(const DeviceSettings& a, const DeviceSettings& b) {
  uint8_t ea[SETTINGS_BLOB_MAX], eb[SETTINGS_BLOB_MAX];
  size_t la = encodeSettings(a, ea), lb = encodeSettings(b, eb);
  return la == lb && memcmp(ea, eb, la) == 0;
}
