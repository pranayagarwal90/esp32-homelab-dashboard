#pragma once
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "WeatherLogic.h"

// Pure WEATHER RADAR logic shared with host tests: metadata validation, the
// frame player, request generations and labels. The backend composes the
// frames; the device only fetches one small JPEG at a time and draws it.

// --- Layout (320x240) ------------------------------------------------------------------
// Title bar 0..35 (frame time on the right), image 300x156 at (10, 38),
// footer line at y 196, bottom bar from 208 (PLAY/PAUSE | BACK).
constexpr int RADAR_X = 10;
constexpr int RADAR_Y = 38;
constexpr int RADAR_W = 300;
constexpr int RADAR_H = 156;
constexpr int RADAR_FOOTER_Y = 197;

// --- Timing and limits ------------------------------------------------------------------
constexpr int RADAR_MAX_FRAMES = 6;
constexpr size_t RADAR_PATH_MAX = 32;              // "/radar/1791559800.jpg" is 21.
constexpr uint32_t RADAR_FRAME_MS = 750;            // Per frame.
constexpr uint32_t RADAR_LAST_FRAME_MS = 1500;      // Hold on the newest frame.
constexpr uint32_t RADAR_META_REFRESH_MS = 300000;  // Backend refreshes every 5 min.
constexpr uint32_t RADAR_META_UPDATING_MS = 3000;   // Backend still generating.
constexpr uint32_t RADAR_META_RETRY_MS = 30000;     // Unavailable or failed.
constexpr uint32_t RADAR_FRAME_RETRY_MS = 2000;     // After a failed frame download.
constexpr int RADAR_FRAME_FAILURES_FOR_META = 3;    // Then re-read the frame list.
constexpr uint32_t RADAR_META_MAX_BYTES = 4096;
constexpr uint32_t RADAR_JPEG_MAX_BYTES = 49152;

// --- Metadata ---------------------------------------------------------------------------

struct RadarFrame {
  uint32_t time;              // Unix time of the radar scan.
  char path[RADAR_PATH_MAX];  // HomeServer-local path.
};

struct RadarMeta {
  bool available = false;
  bool stale = false;
  bool updating = false;
  int32_t utcOffset = 0;
  int count = 0;
  RadarFrame frames[RADAR_MAX_FRAMES];
};

// Only short HomeServer paths under /radar/: no scheme, host, query or "..".
inline bool radarPathValid(const char* path) {
  if (!path || strncmp(path, "/radar/", 7) != 0) return false;
  size_t length = strlen(path);
  if (length <= 7 || length >= RADAR_PATH_MAX || strstr(path, "..")) return false;
  for (size_t i = 7; i < length; i++) {
    char c = path[i];
    bool ok = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              c == '.' || c == '_' || c == '-';
    if (!ok) return false;
  }
  return true;
}

// With more frames than fit, the newest are kept: index of the first to use.
inline int radarFirstFrame(int total) {
  return total > RADAR_MAX_FRAMES ? total - RADAR_MAX_FRAMES : 0;
}

// Appends a frame (oldest first). Rejects invalid paths, missing times, times
// out of order and frames beyond the maximum.
inline bool radarAddFrame(RadarMeta& meta, uint32_t time, const char* path) {
  if (meta.count >= RADAR_MAX_FRAMES || time == 0 || !radarPathValid(path)) return false;
  if (meta.count > 0 && time <= meta.frames[meta.count - 1].time) return false;
  RadarFrame& frame = meta.frames[meta.count++];
  frame.time = time;
  strncpy(frame.path, path, RADAR_PATH_MAX - 1);
  frame.path[RADAR_PATH_MAX - 1] = '\0';
  return true;
}

// Available only with at least one usable frame.
inline void radarFinishMeta(RadarMeta& meta) {
  if (meta.count == 0) meta.available = false;
}

inline int radarIndexOfTime(const RadarMeta& meta, uint32_t time) {
  for (int i = 0; i < meta.count; i++) {
    if (meta.frames[i].time == time) return i;
  }
  return -1;
}

inline bool radarSameFrames(const RadarMeta& a, const RadarMeta& b) {
  if (a.count != b.count) return false;
  for (int i = 0; i < a.count; i++) {
    if (a.frames[i].time != b.frames[i].time || strcmp(a.frames[i].path, b.frames[i].path) != 0) return false;
  }
  return true;
}

// Wait after `failures` consecutive failed frame downloads: quick retries,
// then slow ones once the streak suggests the server or frame is gone.
inline uint32_t radarFrameRetryMs(int failures) {
  return failures >= RADAR_FRAME_FAILURES_FOR_META ? RADAR_META_RETRY_MS : RADAR_FRAME_RETRY_MS;
}

// How long to wait before asking the backend for metadata again.
inline uint32_t radarMetaInterval(bool haveMeta, bool lastFailed, const RadarMeta& meta) {
  if (lastFailed) return RADAR_META_RETRY_MS;
  if (!haveMeta) return 0;
  if (meta.available) return RADAR_META_REFRESH_MS;
  return meta.updating ? RADAR_META_UPDATING_MS : RADAR_META_RETRY_MS;
}

// --- Player -----------------------------------------------------------------------------
// Loops oldest -> newest, starting on the newest frame so the current radar
// appears first. `shown` survives leaving the page (screensaver), so the same
// frame is fetched again on return.

struct RadarPlayer {
  int count = 0;
  int shown = -1;          // Frame index logically on display.
  bool onScreen = false;   // The TFT shows that frame right now.
  bool playing = true;
  uint32_t shownAt = 0;

  // The frame to fetch next, or -1 (nothing to load).
  int wanted() const {
    if (count <= 0) return -1;
    if (!onScreen) return shown >= 0 && shown < count ? shown : count - 1;
    if (!playing) return -1;
    return shown >= 0 && shown < count ? (shown + 1) % count : count - 1;
  }

  uint32_t dwellMs() const {
    return shown == count - 1 ? RADAR_LAST_FRAME_MS : RADAR_FRAME_MS;
  }

  // Whether a fetched frame may replace the one on screen yet.
  bool readyToShow(uint32_t now) const {
    return !onScreen || shown < 0 || now - shownAt >= dwellMs();
  }

  void showed(int index, uint32_t now) {
    shown = index;
    onScreen = true;
    shownAt = now;
  }

  // The page was left or redrawn: the image must be fetched again.
  void offScreen() { onScreen = false; }

  // New frame list: keep showing the same scan if it is still listed.
  void setFrames(int frames, int indexOfShown) {
    count = frames;
    shown = indexOfShown;
  }
};

// --- Requests ---------------------------------------------------------------------------
// One Radar job outstanding at a time. Leaving the page invalidates it: its
// result (when it arrives) is freed and never drawn.

class RadarRequestSlot {
 public:
  bool busy() const { return inFlight; }
  uint32_t begin() {
    inFlight = true;
    return generation;
  }
  void invalidate() { ++generation; }
  // Frees the slot. True if the result still belongs to the current page visit.
  bool complete(uint32_t resultGeneration) {
    inFlight = false;
    return resultGeneration == generation;
  }

 private:
  uint32_t generation = 1;
  bool inFlight = false;
};

// --- Labels and messages ----------------------------------------------------------------

// "10:40 AM" in the HomeServer's local time (offset from /api/radar).
inline void radarTimeLabel(uint32_t time, int32_t utcOffset, char* out, size_t size) {
  if (!time) snprintf(out, size, "--");
  else formatClock12(time, utcOffset, out, size);
}

// "3/6", or "" with nothing shown.
inline void radarPositionLabel(int shown, int count, char* out, size_t size) {
  if (shown < 0 || shown >= count) snprintf(out, size, "%s", "");
  else snprintf(out, size, "%d/%d", shown + 1, count);
}

enum class RadarMessage : uint8_t { None, Loading, Unavailable };

// What the image area says while no frame is on screen.
inline RadarMessage radarMessage(bool onScreen, bool haveMeta, bool metaFailed, const RadarMeta& meta,
                                 int frameFailures) {
  if (onScreen) return RadarMessage::None;
  if (!haveMeta) return metaFailed ? RadarMessage::Unavailable : RadarMessage::Loading;
  if (!meta.available) return meta.updating ? RadarMessage::Loading : RadarMessage::Unavailable;
  return frameFailures >= RADAR_FRAME_FAILURES_FOR_META ? RadarMessage::Unavailable : RadarMessage::Loading;
}

// Bottom bar: PLAY/PAUSE (left third), BACK (middle third).
enum class RadarHit : uint8_t { None, PlayPause, Back };
inline RadarHit radarHitAt(int x, int y) {
  if (y < 205) return RadarHit::None;
  if (x < 107) return RadarHit::PlayPause;
  return x < 214 ? RadarHit::Back : RadarHit::None;
}
