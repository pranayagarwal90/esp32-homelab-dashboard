#include "AlertLogic.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static const uint8_t ALL_SERVICES = (1u << ALERT_SERVICE_COUNT) - 1;

// A healthy report: every service reported online, RAM and three disks low.
static AlertInput healthy() {
  AlertInput input;
  input.memPercent = 40;
  input.diskCount = 3;
  input.diskNames[0] = "C:";
  input.diskNames[1] = "D:";
  input.diskNames[2] = "E:";
  input.diskPercent[0] = 45;
  input.diskPercent[1] = 49;
  input.diskPercent[2] = 5;
  input.servicesReported = ALL_SERVICES;
  input.servicesOnline = ALL_SERVICES;
  return input;
}

static AlertSeverity severityOf(const AlertTable& table, int slot) {
  return table.slots[slot].severity;
}

static int diskSlot(const AlertTable& table, const char* name) {
  for (int i = 0; i < ALERT_MAX_DISKS; i++) {
    if (strcmp(table.slots[ALERT_SLOT_DISK0 + i].subject, name) == 0) return ALERT_SLOT_DISK0 + i;
  }
  return -1;
}

static void testServices() {
  AlertTable table;
  AlertInput input = healthy();
  assert(alertsApplyStatus(table, input, 1000)); // First status changes "NO DATA".
  assert(alertsActiveCount(table) == 0);
  assert(!alertsApplyStatus(table, input, 2000)); // Unchanged snapshot.

  input.servicesOnline &= ~(1u << ALERT_METUBE);
  assert(alertsApplyStatus(table, input, 3000));
  assert(severityOf(table, ALERT_METUBE) == AlertSeverity::Warning);
  assert(table.slots[ALERT_METUBE].sinceMs == 3000);
  assert(alertsActiveCount(table) == 1);

  // Still offline: no duplicate, no change, first-seen time kept.
  assert(!alertsApplyStatus(table, input, 4000));
  assert(alertsActiveCount(table) == 1 && table.slots[ALERT_METUBE].sinceMs == 3000);

  input.servicesOnline = ALL_SERVICES;
  assert(alertsApplyStatus(table, input, 5000));
  assert(alertsActiveCount(table) == 0);

  // Offline again later: a fresh activation.
  input.servicesOnline &= ~(1u << ALERT_METUBE);
  assert(alertsApplyStatus(table, input, 9000));
  assert(table.slots[ALERT_METUBE].sinceMs == 9000);

  // Every monitored service is a WARNING, Cloudflare included.
  input.servicesOnline = 0;
  alertsApplyStatus(table, input, 10000);
  for (int i = 0; i < ALERT_SERVICE_COUNT; i++) assert(severityOf(table, i) == AlertSeverity::Warning);
  assert(alertsCount(table, AlertSeverity::Critical) == 0);
}

static void testMissingServiceKeys() {
  AlertTable table;
  AlertInput input = healthy();
  // Key missing: unknown, never an outage, even though "online" is false.
  input.servicesReported &= ~(1u << ALERT_OLLAMA);
  input.servicesOnline &= ~(1u << ALERT_OLLAMA);
  alertsApplyStatus(table, input, 1000);
  assert(severityOf(table, ALERT_OLLAMA) == AlertSeverity::None);

  // Reported offline, then the key disappears: previous state is kept.
  input.servicesReported = ALL_SERVICES;
  alertsApplyStatus(table, input, 2000);
  assert(severityOf(table, ALERT_OLLAMA) == AlertSeverity::Warning);
  input.servicesReported &= ~(1u << ALERT_OLLAMA);
  input.servicesOnline |= 1u << ALERT_OLLAMA;
  assert(!alertsApplyStatus(table, input, 3000));
  assert(severityOf(table, ALERT_OLLAMA) == AlertSeverity::Warning);

  // No services reported at all (e.g. empty object).
  AlertTable fresh;
  AlertInput none = healthy();
  none.servicesReported = 0;
  none.servicesOnline = 0;
  alertsApplyStatus(fresh, none, 1000);
  assert(alertsActiveCount(fresh) == 0);
}

static void testThresholds() {
  typedef AlertSeverity S;
  // Entering.
  assert(thresholdSeverity(S::None, 0) == S::None);
  assert(thresholdSeverity(S::None, 84.9f) == S::None);
  assert(thresholdSeverity(S::None, 85) == S::Warning);
  assert(thresholdSeverity(S::None, 94.9f) == S::Warning);
  assert(thresholdSeverity(S::None, 95) == S::Critical);
  assert(thresholdSeverity(S::Warning, 95) == S::Critical);
  // Warning hysteresis: holds down to 82, clears below it.
  assert(thresholdSeverity(S::Warning, 84) == S::Warning);
  assert(thresholdSeverity(S::Warning, 82) == S::Warning);
  assert(thresholdSeverity(S::Warning, 81.9f) == S::None);
  // Critical hysteresis: holds down to 92, then warning, or none below 82.
  assert(thresholdSeverity(S::Critical, 93) == S::Critical);
  assert(thresholdSeverity(S::Critical, 92) == S::Critical);
  assert(thresholdSeverity(S::Critical, 91.9f) == S::Warning);
  assert(thresholdSeverity(S::Critical, 83) == S::Warning);
  assert(thresholdSeverity(S::Critical, 81) == S::None);
  // From none, values inside a hysteresis band do not enter.
  assert(thresholdSeverity(S::None, 83) == S::None);
  assert(thresholdSeverity(S::None, 93) == S::Warning);
}

static void testRam() {
  AlertTable table;
  AlertInput input = healthy();
  const struct { float percent; AlertSeverity expected; } steps[] = {
    {80, AlertSeverity::None},
    {88, AlertSeverity::Warning},
    {83, AlertSeverity::Warning},  // Hysteresis.
    {96, AlertSeverity::Critical},
    {93, AlertSeverity::Critical}, // Still above critical clear.
    {90, AlertSeverity::Warning},  // Downgrade, never both.
    {82, AlertSeverity::Warning},
    {81.9f, AlertSeverity::None},  // Clear.
  };
  for (const auto& step : steps) {
    input.memPercent = step.percent;
    alertsApplyStatus(table, input, 1000);
    assert(severityOf(table, ALERT_SLOT_RAM) == step.expected);
    assert(alertsActiveCount(table) == (step.expected == AlertSeverity::None ? 0 : 1));
  }

  // Value updates are a visible change; the first-seen time survives a
  // warning -> critical escalation.
  input.memPercent = 88;
  alertsApplyStatus(table, input, 5000);
  assert(table.slots[ALERT_SLOT_RAM].value == 88);
  input.memPercent = 89.4f;
  assert(alertsApplyStatus(table, input, 6000));
  assert(table.slots[ALERT_SLOT_RAM].value == 89);
  assert(!alertsApplyStatus(table, input, 7000));
  input.memPercent = 97;
  alertsApplyStatus(table, input, 8000);
  assert(severityOf(table, ALERT_SLOT_RAM) == AlertSeverity::Critical);
  assert(table.slots[ALERT_SLOT_RAM].sinceMs == 5000);
}

static void testDisks() {
  AlertTable table;
  AlertInput input = healthy();
  input.diskPercent[1] = 88; // D: warning only.
  alertsApplyStatus(table, input, 1000);
  assert(severityOf(table, diskSlot(table, "D:")) == AlertSeverity::Warning);
  assert(diskSlot(table, "C:") < 0 || severityOf(table, diskSlot(table, "C:")) == AlertSeverity::None);
  assert(alertsActiveCount(table) == 1);

  // Independent disks and severities.
  input.diskPercent[0] = 96;
  input.diskPercent[2] = 86;
  alertsApplyStatus(table, input, 2000);
  assert(severityOf(table, diskSlot(table, "C:")) == AlertSeverity::Critical);
  assert(severityOf(table, diskSlot(table, "D:")) == AlertSeverity::Warning);
  assert(severityOf(table, diskSlot(table, "E:")) == AlertSeverity::Warning);
  assert(alertsActiveCount(table) == 3);

  // Disk hysteresis.
  input.diskPercent[0] = 93;
  input.diskPercent[1] = 83;
  input.diskPercent[2] = 81;
  alertsApplyStatus(table, input, 3000);
  assert(severityOf(table, diskSlot(table, "C:")) == AlertSeverity::Critical);
  assert(severityOf(table, diskSlot(table, "D:")) == AlertSeverity::Warning);
  assert(severityOf(table, diskSlot(table, "E:")) == AlertSeverity::None);

  // Report order changes: alerts follow the drive letter, not the index.
  AlertInput reordered = input;
  reordered.diskNames[0] = "D:"; reordered.diskPercent[0] = 83;
  reordered.diskNames[1] = "C:"; reordered.diskPercent[1] = 93;
  assert(!alertsApplyStatus(table, reordered, 4000));
  assert(severityOf(table, diskSlot(table, "C:")) == AlertSeverity::Critical);

  // A disk absent from a valid host report: its alert clears, others stay.
  input.diskCount = 1; // Only C: reported.
  alertsApplyStatus(table, input, 5000);
  assert(severityOf(table, diskSlot(table, "D:")) == AlertSeverity::None);
  assert(severityOf(table, diskSlot(table, "C:")) == AlertSeverity::Critical);

  // A missing disk never creates an alert.
  AlertTable fresh;
  AlertInput empty = healthy();
  empty.diskCount = 0;
  alertsApplyStatus(fresh, empty, 1000);
  assert(alertsActiveCount(fresh) == 0);

  // A cleared slot can be reused by a different disk without disturbing others.
  AlertTable reuse;
  AlertInput first = healthy();
  first.diskCount = 2;
  first.diskNames[1] = "F:";
  first.diskPercent[1] = 90;
  alertsApplyStatus(reuse, first, 1000);
  AlertInput second = healthy();
  second.diskPercent[0] = 90;
  second.diskPercent[1] = 90;
  alertsApplyStatus(reuse, second, 2000);
  assert(severityOf(reuse, diskSlot(reuse, "C:")) == AlertSeverity::Warning);
  assert(severityOf(reuse, diskSlot(reuse, "D:")) == AlertSeverity::Warning);
  assert(diskSlot(reuse, "F:") < 0 || severityOf(reuse, diskSlot(reuse, "F:")) == AlertSeverity::None);
  assert(alertsActiveCount(reuse) == 2);
}

static void testHostUnavailable() {
  AlertTable table;
  AlertInput input = healthy();
  input.memPercent = 90;
  input.diskPercent[1] = 96;
  alertsApplyStatus(table, input, 1000);
  assert(alertsActiveCount(table) == 2);

  // Cached or zero values with host_available false: RAM/disks untouched,
  // services still evaluated.
  AlertInput down = healthy();
  down.hostAvailable = false;
  down.memPercent = 0;
  down.diskCount = 0;
  down.servicesOnline &= ~(1u << ALERT_JELLYFIN);
  assert(alertsApplyStatus(table, down, 2000));
  assert(severityOf(table, ALERT_SLOT_RAM) == AlertSeverity::Warning);
  assert(table.slots[ALERT_SLOT_RAM].value == 90);
  assert(severityOf(table, diskSlot(table, "D:")) == AlertSeverity::Critical);
  assert(severityOf(table, ALERT_JELLYFIN) == AlertSeverity::Warning);
  // Unavailable host never raises RAM/disk alerts either.
  AlertTable fresh;
  down.memPercent = 99;
  alertsApplyStatus(fresh, down, 1000);
  assert(severityOf(fresh, ALERT_SLOT_RAM) == AlertSeverity::None);

  // Valid metrics resume: normal evaluation.
  input = healthy();
  alertsApplyStatus(table, input, 3000);
  assert(alertsActiveCount(table) == 0);
}

static void testStale() {
  AlertTable table;
  alertsApplyStatus(table, healthy(), 1000);
  // A single or occasional failure never goes stale.
  assert(!alertsFetchFailed(table, 11000));
  assert(!alertsFetchFailed(table, 21000));
  alertsApplyStatus(table, healthy(), 31000);
  assert(table.failures == 0);
  // Three failures but under 60 s since the last success: not yet.
  assert(!alertsFetchFailed(table, 41000));
  assert(!alertsFetchFailed(table, 51000));
  assert(!alertsFetchFailed(table, 61000));
  assert(severityOf(table, ALERT_SLOT_STALE) == AlertSeverity::None);
  // 60 s reached with >= 3 failures: stale warning.
  assert(alertsFetchFailed(table, 91000));
  assert(severityOf(table, ALERT_SLOT_STALE) == AlertSeverity::Warning);
  assert(table.slots[ALERT_SLOT_STALE].sinceMs == 91000);
  assert(!alertsFetchFailed(table, 101000)); // No duplicate.
  // Long ago (>= 60 s) but only 2 failures: not stale.
  AlertTable two;
  alertsApplyStatus(two, healthy(), 1000);
  assert(!alertsFetchFailed(two, 200000));
  assert(!alertsFetchFailed(two, 210000));
  assert(severityOf(two, ALERT_SLOT_STALE) == AlertSeverity::None);

  // Other alerts are preserved while failing.
  AlertTable kept;
  AlertInput input = healthy();
  input.servicesOnline &= ~(1u << ALERT_CLOUDFLARE);
  alertsApplyStatus(kept, input, 1000);
  for (uint32_t t = 11000; t <= 81000; t += 10000) alertsFetchFailed(kept, t);
  assert(severityOf(kept, ALERT_CLOUDFLARE) == AlertSeverity::Warning);
  assert(severityOf(kept, ALERT_SLOT_STALE) == AlertSeverity::Warning);
  assert(alertsActiveCount(kept) == 2);

  // A successful fetch clears stale immediately and resets the count.
  assert(alertsApplyStatus(table, healthy(), 111000));
  assert(severityOf(table, ALERT_SLOT_STALE) == AlertSeverity::None && table.failures == 0);

  // Before any success (boot): the clock starts at 0.
  AlertTable boot;
  assert(!alertsFetchFailed(boot, 20000));
  assert(!alertsFetchFailed(boot, 40000));
  assert(alertsFetchFailed(boot, 60000));
  char text[24];
  alertSummary(boot, text, sizeof(text));
  assert(strcmp(text, "1 WARNING") == 0);
}

static void testStalePause() {
  // Games/photos skip fetches: no failures are recorded, and the paused time
  // is excluded from the stale clock.
  AlertTable table;
  alertsApplyStatus(table, healthy(), 1000);
  assert(!alertsFetchFailed(table, 11000));
  assert(!alertsFetchFailed(table, 21000));
  // Ten minutes in games, then fetching resumes: 590 s paused.
  alertsExcludePause(table, 590000, 621000);
  assert(!alertsFetchFailed(table, 631000)); // 3 failures but only ~40 s of fetching.
  assert(severityOf(table, ALERT_SLOT_STALE) == AlertSeverity::None);
  assert(alertsFetchFailed(table, 651000));  // 60 s of real failing.

  // Already stale before the pause: unchanged by the pause itself.
  alertsExcludePause(table, 600000, 1251000);
  assert(severityOf(table, ALERT_SLOT_STALE) == AlertSeverity::Warning);
  assert(!alertsFetchFailed(table, 1261000));
  assert(severityOf(table, ALERT_SLOT_STALE) == AlertSeverity::Warning);

  // The excluded time never moves the anchor past now.
  AlertTable capped;
  capped.lastSuccessMs = 5000;
  alertsExcludePause(capped, 100000, 10000);
  assert(capped.lastSuccessMs == 10000);
  // millis() rollover.
  AlertTable wrap;
  alertsApplyStatus(wrap, healthy(), 0xFFFFF000u);
  assert(!alertsFetchFailed(wrap, 0x00001000u));
  assert(!alertsFetchFailed(wrap, 0x00002000u));
  assert(!alertsFetchFailed(wrap, 0x00003000u)); // Only ~16 s elapsed.
  assert(alertsFetchFailed(wrap, 0x00010000u));
}

static void testGeneral() {
  AlertTable table;
  char text[32];
  alertSummary(table, text, sizeof(text));
  assert(strcmp(text, "NO DATA") == 0);
  assert(alertsHighest(table) == AlertSeverity::None);

  alertsApplyStatus(table, healthy(), 1000);
  alertSummary(table, text, sizeof(text));
  assert(strcmp(text, "ALL GOOD") == 0);

  AlertInput input = healthy();
  input.servicesOnline &= ~((1u << ALERT_JELLYFIN) | (1u << ALERT_CLOUDFLARE));
  input.memPercent = 97;
  input.diskPercent[1] = 88;
  alertsApplyStatus(table, input, 2000);
  assert(alertsActiveCount(table) == 4);
  assert(alertsCount(table, AlertSeverity::Critical) == 1);
  assert(alertsCount(table, AlertSeverity::Warning) == 3);
  assert(alertsHighest(table) == AlertSeverity::Critical);
  alertSummary(table, text, sizeof(text));
  assert(strcmp(text, "1 CRITICAL +3") == 0);

  // Most severe first, then stable slot order.
  uint8_t order[ALERT_SLOT_COUNT];
  assert(alertsSorted(table, order) == 4);
  assert(order[0] == ALERT_SLOT_RAM);
  assert(order[1] == ALERT_JELLYFIN && order[2] == ALERT_CLOUDFLARE);
  assert(order[3] == diskSlot(table, "D:"));

  alertTitle(table, order[0], text, sizeof(text));
  assert(strcmp(text, "RAM") == 0);
  alertValueText(table, order[0], text, sizeof(text));
  assert(strcmp(text, "97%") == 0);
  alertTitle(table, order[2], text, sizeof(text));
  assert(strcmp(text, "Cloudflare") == 0);
  alertValueText(table, order[2], text, sizeof(text));
  assert(strcmp(text, "OFFLINE") == 0);
  alertTitle(table, order[3], text, sizeof(text));
  assert(strcmp(text, "Disk D:") == 0);
  alertTitle(table, ALERT_METUBE, text, sizeof(text));
  assert(strcmp(text, "MeTube") == 0);
  alertTitle(table, ALERT_SLOT_STALE, text, sizeof(text));
  assert(strcmp(text, "Status data") == 0);
  alertValueText(table, ALERT_SLOT_STALE, text, sizeof(text));
  assert(strcmp(text, "STALE") == 0);
  assert(strcmp(alertSeverityLabel(AlertSeverity::Critical), "CRITICAL") == 0);
  assert(strcmp(alertSeverityLabel(AlertSeverity::Warning), "WARNING") == 0);

  input.memPercent = 40;
  alertsApplyStatus(table, input, 3000);
  alertSummary(table, text, sizeof(text));
  assert(strcmp(text, "3 WARNINGS") == 0);
  assert(alertsHighest(table) == AlertSeverity::Warning);

  // Every slot active at once still fits the fixed table.
  AlertTable full;
  AlertInput worst = healthy();
  worst.servicesOnline = 0;
  worst.memPercent = 99;
  worst.diskCount = 4;
  worst.diskNames[3] = "F:";
  for (int i = 0; i < 4; i++) worst.diskPercent[i] = 99;
  alertsApplyStatus(full, worst, 1000);
  for (uint32_t t = 11000; t <= 71000; t += 20000) alertsFetchFailed(full, t);
  assert(alertsActiveCount(full) == ALERT_SLOT_COUNT);
  assert(alertsSorted(full, order) == ALERT_SLOT_COUNT);

  // Ages.
  formatAlertAge(59999, text, sizeof(text));
  assert(strcmp(text, "<1m") == 0);
  formatAlertAge(12 * 60000, text, sizeof(text));
  assert(strcmp(text, "12m") == 0);
  formatAlertAge(185 * 60000u, text, sizeof(text));
  assert(strcmp(text, "3h 5m") == 0);
  formatAlertAge((2 * 1440 + 4 * 60 + 9) * 60000u, text, sizeof(text));
  assert(strcmp(text, "2d 4h") == 0);

  assert(alertPercentValue(-5) == 0 && alertPercentValue(150) == 100);
  assert(alertPercentValue(88.5f) == 89 && alertPercentValue(88.4f) == 88);
}

static void testPaging() {
  assert(alertPageCount(0) == 1);
  assert(alertPageCount(1) == 1 && alertPageCount(3) == 1);
  assert(alertPageCount(4) == 2 && alertPageCount(6) == 2);
  assert(alertPageCount(7) == 3 && alertPageCount(ALERT_SLOT_COUNT) == 4);
  assert(clampAlertPage(-1, 5) == 0);
  assert(clampAlertPage(1, 5) == 1);
  assert(clampAlertPage(3, 5) == 1); // Alerts cleared while on a later page.
  assert(clampAlertPage(2, 0) == 0);
  // Rows fit between the header and the bottom bar.
  assert(ALERT_ROW_Y0 >= 36);
  assert(ALERT_ROW_Y0 + (ALERTS_PER_PAGE - 1) * ALERT_ROW_PITCH + ALERT_ROW_H <= 205);

  assert(alertsBarAt(50, 150) == AlertsBarHit::None);
  assert(alertsBarAt(50, 220) == AlertsBarHit::Prev);
  assert(alertsBarAt(160, 220) == AlertsBarHit::Back);
  assert(alertsBarAt(300, 220) == AlertsBarHit::Next);

  // HOME badge: the empty band right of the RAM line, clear of the RAM bar
  // (y <= 66), the disk rows (y >= 111) and the nav bar.
  assert(alertBadgeHit(250, ALERT_BADGE_Y));
  assert(alertBadgeHit(160, 68) && alertBadgeHit(319, 108));
  assert(!alertBadgeHit(100, 75));
  assert(!alertBadgeHit(250, 60) && !alertBadgeHit(250, 111) && !alertBadgeHit(250, 220));
}

int main() {
  testServices();
  testMissingServiceKeys();
  testThresholds();
  testRam();
  testDisks();
  testHostUnavailable();
  testStale();
  testStalePause();
  testGeneral();
  testPaging();
  puts("Alert tests passed: services, missing keys, thresholds, ram, disks, host unavailable, stale, stale pause, general, paging");
}
