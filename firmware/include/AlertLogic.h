#pragma once
#include <stdint.h>
#include <stdio.h>
#include <string.h>

// Pure homelab alert evaluation, shared with host tests. Active alerts only:
// a fixed table of slots, one per monitored condition, so each condition has
// a stable identity and can never be duplicated. No heap, no Strings.

enum class AlertSeverity : uint8_t { None, Info, Warning, Critical };
enum class AlertKind : uint8_t { ServiceOffline, RamHigh, DiskHigh, StatusStale };

// Monitored services; also the bit positions of the reported/online masks.
enum AlertService : uint8_t {
  ALERT_JELLYFIN,
  ALERT_NAVIDROME,
  ALERT_METUBE,
  ALERT_OLLAMA,
  ALERT_CLOUDFLARE,
  ALERT_SERVICE_COUNT
};

constexpr int ALERT_MAX_DISKS = 4;
constexpr int ALERT_SLOT_RAM = ALERT_SERVICE_COUNT;
constexpr int ALERT_SLOT_DISK0 = ALERT_SLOT_RAM + 1;
constexpr int ALERT_SLOT_STALE = ALERT_SLOT_DISK0 + ALERT_MAX_DISKS;
constexpr int ALERT_SLOT_COUNT = ALERT_SLOT_STALE + 1;
constexpr int ALERT_SUBJECT_MAX = 6; // Disk name such as "C:", NUL included.

// RAM and each disk, in percent. Enter at >=, clear below the clear value.
constexpr float ALERT_WARN_ENTER = 85.0f;
constexpr float ALERT_WARN_CLEAR = 82.0f;
constexpr float ALERT_CRIT_ENTER = 95.0f;
constexpr float ALERT_CRIT_CLEAR = 92.0f;

// Stale status: both conditions, counted only over real fetch attempts.
constexpr uint8_t ALERT_STALE_FAILURES = 3;
constexpr uint32_t ALERT_STALE_MS = 60000;

struct Alert {
  AlertSeverity severity = AlertSeverity::None; // None = inactive slot.
  uint8_t value = 0;                             // Percent, RAM/disk only.
  uint32_t sinceMs = 0;                          // When it became active.
  char subject[ALERT_SUBJECT_MAX] = {};          // Disk name, disk slots only.
};

struct AlertTable {
  Alert slots[ALERT_SLOT_COUNT];
  bool haveStatus = false;      // At least one successful status evaluated.
  uint8_t failures = 0;         // Consecutive failed fetches.
  uint32_t lastSuccessMs = 0;   // Anchor of the stale clock (boot = 0).
};

// One successful /api/status, reduced to what alerts need.
struct AlertInput {
  bool hostAvailable = true;
  float memPercent = 0;
  int diskCount = 0;
  const char* diskNames[ALERT_MAX_DISKS] = {};
  float diskPercent[ALERT_MAX_DISKS] = {};
  uint8_t servicesReported = 0; // Bit per AlertService: key present as a bool.
  uint8_t servicesOnline = 0;   // Bit per AlertService: reported healthy.
};

inline AlertKind alertKindOf(int slot) {
  if (slot < ALERT_SLOT_RAM) return AlertKind::ServiceOffline;
  if (slot == ALERT_SLOT_RAM) return AlertKind::RamHigh;
  if (slot < ALERT_SLOT_STALE) return AlertKind::DiskHigh;
  return AlertKind::StatusStale;
}

// One severity per metric, with hysteresis:
//   none -> warning at >= 85, warning -> none below 82,
//   any  -> critical at >= 95, critical stays until below 92, then warning
//   (or none if also below 82).
inline AlertSeverity thresholdSeverity(AlertSeverity current, float percent) {
  if (percent >= ALERT_CRIT_ENTER) return AlertSeverity::Critical;
  if (current == AlertSeverity::Critical && percent >= ALERT_CRIT_CLEAR) return AlertSeverity::Critical;
  if (percent >= ALERT_WARN_ENTER) return AlertSeverity::Warning;
  if (current != AlertSeverity::None && percent >= ALERT_WARN_CLEAR) return AlertSeverity::Warning;
  return AlertSeverity::None;
}

inline uint8_t alertPercentValue(float percent) {
  if (!(percent > 0)) return 0;
  if (percent >= 100) return 100;
  return (uint8_t)(percent + 0.5f);
}

// Returns true if anything visible changed. A new activation records nowMs;
// a severity change of an already active alert keeps its first-seen time.
inline bool setAlert(Alert& alert, AlertSeverity severity, uint8_t value, uint32_t nowMs) {
  if (severity == AlertSeverity::None) value = 0;
  if (alert.severity == severity && alert.value == value) return false;
  if (alert.severity == AlertSeverity::None) alert.sinceMs = nowMs;
  alert.severity = severity;
  alert.value = value;
  return true;
}

inline void copyAlertSubject(char* out, const char* name) {
  snprintf(out, ALERT_SUBJECT_MAX, "%s", name ? name : "");
}

// Slot for a disk name: the slot already keyed to it, else a free one not
// claimed by another disk in this report, else -1.
inline int alertDiskSlot(AlertTable& table, const char* subject, const bool claimed[ALERT_MAX_DISKS]) {
  for (int i = 0; i < ALERT_MAX_DISKS; i++) {
    if (strcmp(table.slots[ALERT_SLOT_DISK0 + i].subject, subject) == 0) return ALERT_SLOT_DISK0 + i;
  }
  for (int i = 0; i < ALERT_MAX_DISKS; i++) {
    Alert& alert = table.slots[ALERT_SLOT_DISK0 + i];
    if (!claimed[i] && alert.severity == AlertSeverity::None) {
      memcpy(alert.subject, subject, ALERT_SUBJECT_MAX);
      return ALERT_SLOT_DISK0 + i;
    }
  }
  return -1;
}

// A successful status fetch. Services are evaluated only when reported;
// RAM/disks only when the host metrics are valid, otherwise they keep their
// previous state. Clears the stale alert. Returns true if anything changed.
inline bool alertsApplyStatus(AlertTable& table, const AlertInput& input, uint32_t nowMs) {
  bool changed = !table.haveStatus;
  table.haveStatus = true;
  table.failures = 0;
  table.lastSuccessMs = nowMs;
  changed |= setAlert(table.slots[ALERT_SLOT_STALE], AlertSeverity::None, 0, nowMs);

  for (int i = 0; i < ALERT_SERVICE_COUNT; i++) {
    if (!(input.servicesReported & (1u << i))) continue; // Unknown, not offline.
    bool online = input.servicesOnline & (1u << i);
    changed |= setAlert(table.slots[i], online ? AlertSeverity::None : AlertSeverity::Warning, 0, nowMs);
  }

  if (!input.hostAvailable) return changed;

  Alert& ram = table.slots[ALERT_SLOT_RAM];
  changed |= setAlert(ram, thresholdSeverity(ram.severity, input.memPercent),
                      alertPercentValue(input.memPercent), nowMs);

  bool seen[ALERT_MAX_DISKS] = {};
  int diskCount = input.diskCount < ALERT_MAX_DISKS ? input.diskCount : ALERT_MAX_DISKS;
  for (int i = 0; i < diskCount; i++) {
    char subject[ALERT_SUBJECT_MAX];
    copyAlertSubject(subject, input.diskNames[i]);
    if (subject[0] == '\0') continue;
    int slot = alertDiskSlot(table, subject, seen);
    if (slot < 0) continue;
    seen[slot - ALERT_SLOT_DISK0] = true;
    Alert& disk = table.slots[slot];
    changed |= setAlert(disk, thresholdSeverity(disk.severity, input.diskPercent[i]),
                        alertPercentValue(input.diskPercent[i]), nowMs);
  }
  // A disk missing from a valid host report has nothing left to alert on.
  for (int i = 0; i < ALERT_MAX_DISKS; i++) {
    if (!seen[i]) changed |= setAlert(table.slots[ALERT_SLOT_DISK0 + i], AlertSeverity::None, 0, nowMs);
  }
  return changed;
}

// A real fetch attempt failed. Everything else keeps its state; the stale
// alert is raised only, never cleared, here.
inline bool alertsFetchFailed(AlertTable& table, uint32_t nowMs) {
  if (table.failures < 255) table.failures++;
  if (table.failures < ALERT_STALE_FAILURES || nowMs - table.lastSuccessMs < ALERT_STALE_MS) return false;
  return setAlert(table.slots[ALERT_SLOT_STALE], AlertSeverity::Warning, 0, nowMs);
}

// Fetching was intentionally suspended (games, photos) for pausedMs. That time
// must not count toward staleness, so the stale clock is moved forward by it.
inline void alertsExcludePause(AlertTable& table, uint32_t pausedMs, uint32_t nowMs) {
  uint32_t elapsed = nowMs - table.lastSuccessMs;
  table.lastSuccessMs += pausedMs < elapsed ? pausedMs : elapsed;
}

// --- Queries ----------------------------------------------------------------------

inline int alertsCount(const AlertTable& table, AlertSeverity severity) {
  int count = 0;
  for (const Alert& alert : table.slots) count += alert.severity == severity;
  return count;
}

inline int alertsActiveCount(const AlertTable& table) {
  int count = 0;
  for (const Alert& alert : table.slots) count += alert.severity != AlertSeverity::None;
  return count;
}

inline AlertSeverity alertsHighest(const AlertTable& table) {
  AlertSeverity highest = AlertSeverity::None;
  for (const Alert& alert : table.slots) {
    if (alert.severity > highest) highest = alert.severity;
  }
  return highest;
}

// Active slots, most severe first, then in stable slot order. Returns count.
inline int alertsSorted(const AlertTable& table, uint8_t out[ALERT_SLOT_COUNT]) {
  int count = 0;
  for (int severity = (int)AlertSeverity::Critical; severity > (int)AlertSeverity::None; severity--) {
    for (int slot = 0; slot < ALERT_SLOT_COUNT; slot++) {
      if ((int)table.slots[slot].severity == severity) out[count++] = (uint8_t)slot;
    }
  }
  return count;
}

inline const char* alertSeverityLabel(AlertSeverity severity) {
  switch (severity) {
    case AlertSeverity::Critical: return "CRITICAL";
    case AlertSeverity::Warning: return "WARNING";
    case AlertSeverity::Info: return "INFO";
    default: return "";
  }
}

inline void alertTitle(const AlertTable& table, int slot, char* out, size_t size) {
  static const char* const SERVICE_NAMES[ALERT_SERVICE_COUNT] = {
    "Jellyfin", "Navidrome", "MeTube", "Ollama", "Cloudflare"
  };
  switch (alertKindOf(slot)) {
    case AlertKind::ServiceOffline: snprintf(out, size, "%s", SERVICE_NAMES[slot]); break;
    case AlertKind::RamHigh: snprintf(out, size, "RAM"); break;
    case AlertKind::DiskHigh: snprintf(out, size, "Disk %s", table.slots[slot].subject); break;
    case AlertKind::StatusStale: snprintf(out, size, "Status data"); break;
  }
}

// Short right-hand value: "OFFLINE", "89%", "STALE".
inline void alertValueText(const AlertTable& table, int slot, char* out, size_t size) {
  switch (alertKindOf(slot)) {
    case AlertKind::ServiceOffline: snprintf(out, size, "OFFLINE"); break;
    case AlertKind::StatusStale: snprintf(out, size, "STALE"); break;
    default: snprintf(out, size, "%u%%", (unsigned)table.slots[slot].value); break;
  }
}

// How long an alert has been active: "<1m", "12m", "3h 5m", "2d 4h".
inline void formatAlertAge(uint32_t ms, char* out, size_t size) {
  uint32_t minutes = ms / 60000;
  if (minutes < 1) snprintf(out, size, "<1m");
  else if (minutes < 60) snprintf(out, size, "%um", (unsigned)minutes);
  else if (minutes < 1440) snprintf(out, size, "%uh %um", (unsigned)(minutes / 60), (unsigned)(minutes % 60));
  else snprintf(out, size, "%ud %uh", (unsigned)(minutes / 1440), (unsigned)(minutes % 1440 / 60));
}

// HOME indicator text; the colour follows alertsHighest(). "NO DATA" until
// the first status unless an alert (stale) is already active.
inline void alertSummary(const AlertTable& table, char* out, size_t size) {
  int critical = alertsCount(table, AlertSeverity::Critical);
  int warning = alertsCount(table, AlertSeverity::Warning);
  int info = alertsCount(table, AlertSeverity::Info);
  int lead = critical ? critical : warning ? warning : info;
  int rest = critical ? warning + info : warning ? info : 0;
  const char* label = critical ? "CRITICAL" : warning ? (warning == 1 ? "WARNING" : "WARNINGS") : "INFO";
  if (lead == 0) snprintf(out, size, "%s", table.haveStatus ? "ALL GOOD" : "NO DATA");
  else if (rest) snprintf(out, size, "%d %s +%d", lead, label, rest);
  else snprintf(out, size, "%d %s", lead, label);
}

// --- ALERTS page layout -------------------------------------------------------------

constexpr int ALERTS_PER_PAGE = 3;
constexpr int ALERT_ROW_Y0 = 42;
constexpr int ALERT_ROW_PITCH = 52;
constexpr int ALERT_ROW_H = 48;

inline int alertPageCount(int count) {
  return count <= ALERTS_PER_PAGE ? 1 : (count + ALERTS_PER_PAGE - 1) / ALERTS_PER_PAGE;
}

inline int clampAlertPage(int page, int count) {
  int last = alertPageCount(count) - 1;
  return page < 0 ? 0 : page > last ? last : page;
}

// Bottom bar: PREV | BACK | NEXT, matching the navigation bar thirds.
enum class AlertsBarHit : uint8_t { None, Prev, Back, Next };
inline AlertsBarHit alertsBarAt(int x, int y) {
  if (y < 205) return AlertsBarHit::None;
  if (x < 107) return AlertsBarHit::Prev;
  if (x >= 214) return AlertsBarHit::Next;
  return AlertsBarHit::Back;
}

// HOME indicator: drawn right-aligned on the "RAM x / y GB" line; the tap
// area is the empty band around it, clear of the RAM bar and disk rows.
constexpr int ALERT_BADGE_RIGHT = 306;
constexpr int ALERT_BADGE_Y = 75;
inline bool alertBadgeHit(int x, int y) {
  return x >= 160 && y >= 68 && y <= 108;
}
