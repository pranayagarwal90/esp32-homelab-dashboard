#pragma once
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "BackendUrl.h"

// Pure HomeDeck Mini pull-update logic, shared with host tests: versions,
// offer validation, URLs, check cadence, the prompt rule, the post-update
// marker and the text the screens show. Offers come only from signed
// manifests (ManifestSignature.h): the signature authenticates the release,
// including its SHA-256, and the SHA-256 authenticates the downloaded image.

// --- Versions -------------------------------------------------------------------------

// MAJOR.MINOR.PATCH, decimal, no leading zeros, each part 0..65535 (the
// backend applies the same rule). Pre-releases are not supported.
struct SemVer {
  uint16_t major, minor, patch;
};

constexpr size_t UPDATE_VERSION_MAX = 18; // "65535.65535.65535" + terminator.

inline bool parseSemVer(const char* text, SemVer& out) {
  if (!text) return false;
  uint32_t parts[3] = {0, 0, 0};
  int part = 0, digits = 0;
  for (const char* p = text; ; p++) {
    if (*p >= '0' && *p <= '9') {
      if (digits == 1 && parts[part] == 0) return false; // Leading zero.
      if (++digits > 5) return false;
      parts[part] = parts[part] * 10 + uint32_t(*p - '0');
    } else if (*p == '.' || *p == '\0') {
      if (digits == 0 || parts[part] > 65535) return false;
      if (*p == '\0') break;
      if (++part > 2) return false;
      digits = 0;
    } else {
      return false;
    }
  }
  if (part != 2) return false;
  out = {uint16_t(parts[0]), uint16_t(parts[1]), uint16_t(parts[2])};
  return true;
}

inline int compareSemVer(const SemVer& a, const SemVer& b) {
  if (a.major != b.major) return a.major < b.major ? -1 : 1;
  if (a.minor != b.minor) return a.minor < b.minor ? -1 : 1;
  if (a.patch != b.patch) return a.patch < b.patch ? -1 : 1;
  return 0;
}

// Only a strictly newer, valid version is ever an update.
inline bool isNewerVersion(const char* current, const char* offered) {
  SemVer a, b;
  return parseSemVer(current, a) && parseSemVer(offered, b) && compareSemVer(b, a) > 0;
}

// --- Offers -----------------------------------------------------------------------------

constexpr size_t UPDATE_FILE_MAX = 65;   // 64-character file name + terminator.
constexpr size_t UPDATE_NOTES_MAX = 201; // 200 characters + terminator.

struct UpdateOffer {
  char version[UPDATE_VERSION_MAX];
  uint32_t size;
  uint8_t sha256[32];           // Covered by the manifest signature.
  char file[UPDATE_FILE_MAX];   // Under /firmware/ on the configured backend.
  char notes[UPDATE_NOTES_MAX]; // Printable ASCII, exactly as signed.
  char keyId[33];               // The trusted key that signed it (log only).
};

inline int hexValue(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

inline bool parseSha256Hex(const char* hex, uint8_t out[32]) {
  if (!hex || strlen(hex) != 64) return false;
  for (int i = 0; i < 32; i++) {
    int high = hexValue(hex[2 * i]), low = hexValue(hex[2 * i + 1]);
    if (high < 0 || low < 0) return false;
    out[i] = uint8_t(high << 4 | low);
  }
  return true;
}

// A plain .bin name, as the backend allows: [A-Za-z0-9][A-Za-z0-9._-]* .bin,
// at most 64 characters, no "..", so it can never name another path.
inline bool validFirmwareFileName(const char* name) {
  if (!name) return false;
  size_t length = strlen(name);
  if (length < 5 || length >= UPDATE_FILE_MAX || strcmp(name + length - 4, ".bin") != 0) return false;
  if (strstr(name, "..")) return false;
  for (size_t i = 0; i < length; i++) {
    char c = name[i];
    bool alnum = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
    if (!alnum && (i == 0 || (c != '.' && c != '_' && c != '-'))) return false;
  }
  return true;
}

// The offer's "url" must be "/firmware/<file>" on the same backend: absolute
// URLs are refused, so a device never downloads from anywhere else.
inline bool firmwareFileFromUrl(const char* url, char* out, size_t size) {
  size_t prefix = strlen(FIRMWARE_FILE_PATH);
  if (!url || strncmp(url, FIRMWARE_FILE_PATH, prefix) != 0 || !validFirmwareFileName(url + prefix)) return false;
  if (strlen(url + prefix) >= size) return false;
  strcpy(out, url + prefix);
  return true;
}

// Release notes are signed, so they are taken exactly or not at all:
// printable ASCII (the size-1 font), at most 200 characters.
inline bool validUpdateNotes(const char* notes) {
  if (!notes) return true;
  size_t n = 0;
  for (const char* p = notes; *p; p++, n++) {
    if (*p < ' ' || *p > '~') return false;
  }
  return n < UPDATE_NOTES_MAX;
}

enum class OfferError : uint8_t { None, Version, NotNewer, Size, TooLarge, Sha, Url, Notes };

// Validates an "available" answer. maxSize is the device's OTA slot size.
inline OfferError validateUpdateOffer(const char* current, const char* version, uint32_t size, const char* sha,
                                      const char* url, const char* notes, uint32_t maxSize, UpdateOffer& out) {
  memset(&out, 0, sizeof(out));
  SemVer parsed;
  if (!version || !parseSemVer(version, parsed)) return OfferError::Version;
  if (!isNewerVersion(current, version)) return OfferError::NotNewer;
  if (size == 0) return OfferError::Size;
  if (size > maxSize) return OfferError::TooLarge;
  if (!parseSha256Hex(sha, out.sha256)) return OfferError::Sha;
  if (!firmwareFileFromUrl(url, out.file, sizeof(out.file))) return OfferError::Url;
  if (!validUpdateNotes(notes)) return OfferError::Notes;
  strcpy(out.version, version); // parseSemVer bounds it to 17 characters.
  out.size = size;
  snprintf(out.notes, sizeof(out.notes), "%s", notes ? notes : "");
  return OfferError::None;
}

// --- URLs (always from the configured backend) ------------------------------------------

inline bool updateCheckUrl(char* out, size_t size, const char* base, const char* product, const char* current) {
  SemVer parsed;
  if (out && size) out[0] = '\0';
  if (!product || !product[0] || !parseSemVer(current, parsed)) return false;
  for (const char* p = product; *p; p++) {
    if (!((*p >= 'a' && *p <= 'z') || (*p >= '0' && *p <= '9') || *p == '-')) return false;
  }
  char query[64];
  int written = snprintf(query, sizeof(query), "?product=%s&current=%s", product, current);
  if (written < 0 || size_t(written) >= sizeof(query)) return false;
  return joinBackendUrl(out, size, base, API_FIRMWARE_UPDATE_PATH, query);
}

inline bool firmwareDownloadUrl(char* out, size_t size, const char* base, const char* file) {
  if (out && size) out[0] = '\0';
  if (!validFirmwareFileName(file)) return false;
  return joinBackendUrl(out, size, base, FIRMWARE_FILE_PATH, file);
}

// --- Check cadence -------------------------------------------------------------------------

// One check shortly after the dashboard starts, then at most every 6 hours
// (a manual CHECK also restarts the interval).
constexpr uint32_t UPDATE_FIRST_CHECK_MS = 30000;
constexpr uint32_t UPDATE_CHECK_INTERVAL_MS = 6UL * 60 * 60 * 1000;

struct UpdateSchedule {
  uint32_t since = 0;   // Dashboard start, then the last check.
  bool checked = false;
};

inline void updateScheduleBegin(UpdateSchedule& s, uint32_t now) {
  s = UpdateSchedule();
  s.since = now;
}

inline bool updateCheckDue(const UpdateSchedule& s, uint32_t now) {
  return now - s.since >= (s.checked ? UPDATE_CHECK_INTERVAL_MS : UPDATE_FIRST_CHECK_MS);
}

inline void updateCheckStarted(UpdateSchedule& s, uint32_t now) {
  s.checked = true;
  s.since = now;
}

// --- State ---------------------------------------------------------------------------------

enum class UpdateState : uint8_t {
  Idle,         // Not checked yet this boot.
  Checking,
  UpToDate,
  Available,
  Unavailable,  // The backend has no update service (404) or none is set.
  CheckFailed,
  Downloading,  // Streaming into the OTA slot.
  Installing,   // Verified; finalizing the slot.
  Failed,       // Install failed; the running firmware is untouched.
  Restarting    // Installed; restarting into it.
};

inline bool updateBusy(UpdateState s) {
  return s == UpdateState::Checking || s == UpdateState::Downloading || s == UpdateState::Installing ||
         s == UpdateState::Restarting;
}

// Flash writing has begun or finished: no navigation, no cancel.
inline bool updateInstalling(UpdateState s) {
  return s == UpdateState::Downloading || s == UpdateState::Installing || s == UpdateState::Restarting;
}

// Checks run only on the normal dashboard (never in Setup Mode), with Wi-Fi
// and a backend, and never during another check or an install.
inline bool updateCheckAllowed(bool setupMode, bool wifiConnected, bool hasBackend, UpdateState s) {
  return !setupMode && wifiConnected && hasBackend && !updateBusy(s);
}

inline uint8_t updatePercent(uint32_t done, uint32_t total) {
  if (total == 0) return 0;
  if (done >= total) return 100;
  return uint8_t(uint64_t(done) * 100 / total);
}

// --- Errors --------------------------------------------------------------------------------

enum class UpdateError : uint8_t {
  None, NoWifi, NoServer, Unreachable, ServerError, BadResponse, TooLarge, NoUpdate,
  FileMissing, HttpError, SizeMismatch, Interrupted, Timeout, HashMismatch,
  BeginFailed, WriteFailed, EndFailed, Busy, VerificationFailed
};

inline const char* updateErrorText(UpdateError e) {
  switch (e) {
    case UpdateError::None: return "";
    case UpdateError::NoWifi: return "Wi-Fi is not connected";
    case UpdateError::NoServer: return "No dashboard server is set";
    case UpdateError::Unreachable: return "Update server not reachable";
    case UpdateError::ServerError: return "Update server error";
    case UpdateError::BadResponse: return "Invalid update information";
    case UpdateError::TooLarge: return "Update too large for this device";
    case UpdateError::NoUpdate: return "No update to install";
    case UpdateError::FileMissing: return "Firmware file not found";
    case UpdateError::HttpError: return "Download failed (server error)";
    case UpdateError::SizeMismatch: return "Download size does not match";
    case UpdateError::Interrupted: return "Download was interrupted";
    case UpdateError::Timeout: return "Download timed out";
    case UpdateError::HashMismatch: return "Download failed verification";
    case UpdateError::BeginFailed: return "Could not prepare the update";
    case UpdateError::WriteFailed: return "Could not write the update";
    case UpdateError::EndFailed: return "Could not finish the update";
    case UpdateError::Busy: return "Another update task is running";
    case UpdateError::VerificationFailed: return "Update verification failed";
  }
  return "Update failed";
}

// HTTP status of GET /api/firmware/update -> what it means. 404: an older or
// update-less backend (or updates disabled): updates are unavailable, not an
// error.
inline UpdateState checkStateForHttp(int code, UpdateError& error) {
  error = UpdateError::None;
  if (code == 200) return UpdateState::Checking; // Body decides.
  if (code == 404) return UpdateState::Unavailable;
  error = code <= 0 ? UpdateError::Unreachable : UpdateError::ServerError;
  return UpdateState::CheckFailed;
}

// --- Prompt ----------------------------------------------------------------------------------

// The automatic prompt appears once per offered version per boot (LATER, or
// a failed INSTALL NOW, dismisses it until the next restart); the Firmware
// page still shows the update. Only on calm pages (HOME, MORE).
inline bool updatePromptDue(UpdateState s, const char* offered, const char* prompted, bool calmPage) {
  return s == UpdateState::Available && calmPage && offered && offered[0] &&
         strcmp(offered, prompted ? prompted : "") != 0;
}

// --- Post-update marker (NVS update/pending) ------------------------------------------------

enum class PendingUpdate : uint8_t { None, Succeeded, NotRunning };

// Saved just before restarting into a new image. At the next normal boot it
// is compared with the running version and cleared either way, so the
// success screen shows at most once. NotRunning: the device did not boot the
// new image (e.g. the bootloader rolled it back).
inline PendingUpdate pendingUpdateOutcome(bool stored, const char* pending, const char* running) {
  if (!stored) return PendingUpdate::None;
  return pending && running && pending[0] && strcmp(pending, running) == 0 ? PendingUpdate::Succeeded
                                                                          : PendingUpdate::NotRunning;
}

// --- Text ---------------------------------------------------------------------------------------

inline void formatUpdateStatus(char* out, size_t size, UpdateState s, const char* offered, UpdateError e) {
  switch (s) {
    case UpdateState::Idle: snprintf(out, size, "Not checked yet"); return;
    case UpdateState::Checking: snprintf(out, size, "Checking..."); return;
    case UpdateState::UpToDate: snprintf(out, size, "Up to date"); return;
    case UpdateState::Available: snprintf(out, size, "Update available: %s", offered); return;
    case UpdateState::Unavailable: snprintf(out, size, "Updates unavailable"); return;
    case UpdateState::CheckFailed:
      // An unsigned or untrusted release: never "available", and no crypto detail.
      if (e == UpdateError::VerificationFailed) snprintf(out, size, "%s", updateErrorText(e));
      else snprintf(out, size, "Check failed: %s", updateErrorText(e));
      return;
    case UpdateState::Downloading:
    case UpdateState::Installing: snprintf(out, size, "Installing %s...", offered); return;
    case UpdateState::Failed: snprintf(out, size, "Install failed: %s", updateErrorText(e)); return;
    case UpdateState::Restarting: snprintf(out, size, "Restarting..."); return;
  }
  snprintf(out, size, "--");
}

// --- Layout (Settings > Firmware page and the update screens) -------------------------------------

constexpr int FW_BUTTON_Y = 156;
constexpr int FW_BUTTON_H = 40;
constexpr int FW_CHECK_X = 14;
constexpr int FW_INSTALL_X = 166;
constexpr int FW_BUTTON_W = 140;

enum class FirmwareButton : uint8_t { None, Check, Install };

inline FirmwareButton firmwareButtonAt(int x, int y) {
  if (y < FW_BUTTON_Y || y >= FW_BUTTON_Y + FW_BUTTON_H) return FirmwareButton::None;
  if (x >= FW_CHECK_X && x < FW_CHECK_X + FW_BUTTON_W) return FirmwareButton::Check;
  if (x >= FW_INSTALL_X && x < FW_INSTALL_X + FW_BUTTON_W) return FirmwareButton::Install;
  return FirmwareButton::None;
}

// Prompt: INSTALL NOW 15..155, LATER 165..305, y 156..200.
constexpr int PROMPT_BUTTON_Y = 156;
constexpr int PROMPT_BUTTON_H = 44;

enum class PromptChoice : uint8_t { None, Install, Later };

inline PromptChoice promptChoiceAt(int x, int y) {
  if (y < PROMPT_BUTTON_Y || y >= PROMPT_BUTTON_Y + PROMPT_BUTTON_H) return PromptChoice::None;
  if (x >= 15 && x < 155) return PromptChoice::Install;
  if (x >= 165 && x < 305) return PromptChoice::Later;
  return PromptChoice::None;
}
