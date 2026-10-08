#pragma once
#include <stddef.h>
#include <stdio.h>
#include <string.h>

// Firmware build metadata. The release version below is the single source of
// truth: bump it manually for each release (it never changes by itself).
#define DASHBOARD_FIRMWARE_VERSION "1.0.0"

// Read-only values, compiled into BuildInfo.cpp only. The git SHA and
// PlatformIO environment come from scripts/build_info.py and fall back to
// "unknown" (e.g. building outside a git checkout).
const char* firmwareVersion();
const char* firmwareBuildTime();   // "YYYY-MM-DD HH:MM" (build machine local time)
const char* firmwareGitSha();      // Short SHA, "+dirty" for uncommitted changes
const char* firmwareBuildEnv();    // e.g. "esp32dev-ota"
const char* firmwareBuildBoard();  // PlatformIO board, e.g. "esp32dev"

// --- Pure helpers (host-tested) -----------------------------------------------------

// Defines injected by the build script may be missing or empty.
inline const char* buildValueOr(const char* value, const char* fallback = "unknown") {
  return value && value[0] ? value : fallback;
}

// "MAJOR.MINOR.PATCH" with decimal parts.
inline bool isSemanticVersion(const char* version) {
  int parts = 0, digits = 0;
  for (const char* p = version; ; p++) {
    if (*p >= '0' && *p <= '9') {
      digits++;
    } else if (*p == '.' || *p == '\0') {
      if (digits == 0) return false;
      parts++;
      digits = 0;
      if (*p == '\0') break;
    } else {
      return false;
    }
  }
  return parts == 3;
}

// Parses an unsigned decimal field of up to `width` characters, skipping
// leading spaces (__DATE__ pads single-digit days with a space). -1 if none.
inline int parseDecimalField(const char* text, int width) {
  int value = -1;
  for (int i = 0; i < width && text[i]; i++) {
    if (text[i] == ' ' && value < 0) continue;
    if (text[i] < '0' || text[i] > '9') return -1;
    value = (value < 0 ? 0 : value * 10) + (text[i] - '0');
  }
  return value;
}

// Compiler __DATE__ ("Oct  8 2026") + __TIME__ ("15:10:42") -> "2026-10-08 15:10".
// Unparseable input is copied through unchanged. (No sscanf: it would link
// ~17 KB of scanf code.)
inline void formatBuildTimestamp(const char* date, const char* time, char* out, size_t size) {
  static const char months[] = "JanFebMarAprMayJunJulAugSepOctNovDec";
  int month = 0;
  bool shaped = date && strlen(date) == 11 && date[3] == ' ' && date[6] == ' ' &&
                time && strlen(time) >= 5 && time[2] == ':';
  for (int i = 0; shaped && i < 12; i++) {
    if (strncmp(date, months + i * 3, 3) == 0) month = i + 1;
  }
  int day = shaped ? parseDecimalField(date + 4, 2) : -1;
  int year = shaped ? parseDecimalField(date + 7, 4) : -1;
  int hour = shaped ? parseDecimalField(time, 2) : -1;
  int minute = shaped ? parseDecimalField(time + 3, 2) : -1;
  if (month && day >= 1 && day <= 31 && year >= 2000 && hour >= 0 && hour < 24 && minute >= 0 && minute < 60) {
    snprintf(out, size, "%04d-%02d-%02d %02d:%02d", year, month, day, hour, minute);
  } else {
    snprintf(out, size, "%s %s", date ? date : "", time ? time : "");
  }
}
