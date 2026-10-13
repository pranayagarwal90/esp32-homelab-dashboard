#include <Arduino.h>
#include <Preferences.h>
#include "LocalClockStore.h"
#include "LocalTime.h"
#include "SettingsStore.h"

static const char* CLOCKS_NAMESPACE = "worldclocks";
static const char* CLOCKS_KEY = "clocks";

static portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;
static LocalClockList clocks;               // Guarded by lock.
static SemaphoreHandle_t writer = nullptr;  // One change (NVS + RAM) at a time.
static volatile uint32_t revision = 0;
static volatile uint32_t displayRevision = 0;
static uint32_t lastSavedUtc = 0;           // Writer only.

// The list being changed and its blob (~1.6 KB): taken from the heap only
// for one change, so neither a static nor the setup page task's 6 KB stack
// holds it. Fixed size; freed at once.
struct Scratch {
  LocalClockList list;
  uint8_t blob[CLOCK_BLOB_MAX];
};

static Scratch* takeScratch() {
  Scratch* s = (Scratch*)malloc(sizeof(Scratch));
  if (s) new (&s->list) LocalClockList();
  else Serial.println("[WORLD CLOCK] Not enough memory to change the clocks");
  return s;
}

static void copyOut(LocalClockList& out) {
  portENTER_CRITICAL(&lock);
  out = clocks;
  portEXIT_CRITICAL(&lock);
}

static void copyIn(const LocalClockList& in) {
  portENTER_CRITICAL(&lock);
  clocks = in;
  portEXIT_CRITICAL(&lock);
}

static bool persist(Scratch& s) {
  size_t length = encodeLocalClocks(s.list, s.blob, sizeof(s.blob));
  if (!length) return false;
  Preferences prefs;
  if (!prefs.begin(CLOCKS_NAMESPACE, false)) return false;
  bool ok = prefs.putBytes(CLOCKS_KEY, s.blob, length) == length; // One write: old or new, never half.
  prefs.end();
  if (!ok) Serial.println("[WORLD CLOCK] NOT saved (NVS write failed)");
  return ok;
}

void loadLocalClocks() {
  if (!writer) writer = xSemaphoreCreateMutex();
  Scratch* s = takeScratch();
  if (!s) return;
  Preferences prefs;
  if (nvsNamespaceExists(CLOCKS_NAMESPACE) && prefs.begin(CLOCKS_NAMESPACE, true)) {
    size_t length = prefs.isKey(CLOCKS_KEY) ? prefs.getBytesLength(CLOCKS_KEY) : 0;
    if (length > 0 && length <= sizeof(s->blob) && prefs.getBytes(CLOCKS_KEY, s->blob, length) == length &&
        !decodeLocalClocks(s->blob, length, s->list)) {
      Serial.println("[WORLD CLOCK] Saved clocks not valid; ignored");
    }
    prefs.end();
  }
  for (int i = 0; i < s->list.count; i++) {
    if (s->list.items[i].offsetUpdatedAt > lastSavedUtc) lastSavedUtc = s->list.items[i].offsetUpdatedAt;
  }
  copyIn(s->list);
  Serial.printf("[WORLD CLOCK] %d local clock(s)\n", s->list.count);
  free(s);
}

int localClockCount() {
  portENTER_CRITICAL(&lock);
  int count = clocks.count;
  portEXIT_CRITICAL(&lock);
  return count;
}

void localClocks(LocalClockList& out) {
  copyOut(out);
}

bool localClockAt(int index, WorldClockLocation& out) {
  bool ok = false;
  portENTER_CRITICAL(&lock);
  if (index >= 0 && index < clocks.count) {
    out = clocks.items[index];
    ok = true;
  }
  portEXIT_CRITICAL(&lock);
  return ok;
}

bool localClockAdded(const WorldClockLocation& place) {
  portENTER_CRITICAL(&lock);
  bool added = findLocalClock(clocks, place) >= 0;
  portEXIT_CRITICAL(&lock);
  return added;
}

ClockAddResult addLocalClockPlace(const WorldClockLocation& place) {
  if (!writer || xSemaphoreTake(writer, portMAX_DELAY) != pdTRUE) return ClockAddResult::SaveFailed;
  ClockAddResult result = ClockAddResult::SaveFailed;
  Scratch* s = takeScratch();
  if (s) {
    copyOut(s->list);
    result = addLocalClock(s->list, place); // Duplicate / full: no NVS write.
    if (result == ClockAddResult::Added && !persist(*s)) result = ClockAddResult::SaveFailed;
    if (result == ClockAddResult::Added) {
      copyIn(s->list);
      revision = revision + 1;
      displayRevision = displayRevision + 1;
    }
    free(s);
  }
  if (result == ClockAddResult::Added) Serial.printf("[WORLD CLOCK] Added: %s\n", place.name);
  else Serial.printf("[WORLD CLOCK] Not added: %s (%s)\n", place.name, clockAddResultText(result));
  xSemaphoreGive(writer);
  return result;
}

bool removeLocalClockAt(int index) {
  if (!writer || xSemaphoreTake(writer, portMAX_DELAY) != pdTRUE) return false;
  bool ok = false;
  char name[LOCATION_NAME_MAX] = "";
  Scratch* s = takeScratch();
  if (s) {
    copyOut(s->list);
    if (index >= 0 && index < s->list.count) memcpy(name, s->list.items[index].name, sizeof(name));
    ok = removeLocalClock(s->list, index) && persist(*s);
    if (ok) {
      copyIn(s->list);
      revision = revision + 1;
      displayRevision = displayRevision + 1;
    }
    free(s);
  }
  if (ok) Serial.printf("[WORLD CLOCK] Removed: %s\n", name);
  xSemaphoreGive(writer);
  return ok;
}

bool applyLocalClockOffset(const WorldClockLocation& place, int32_t offset) {
  if (!writer || xSemaphoreTake(writer, portMAX_DELAY) != pdTRUE) return false;
  bool ok = false;
  Scratch* s = takeScratch();
  if (s) {
    copyOut(s->list);
    uint32_t now = utcNow();
    bool changed = false;
    ok = setLocalClockOffset(s->list, place, offset, now, changed);
    if (ok) {
      // RAM first (the screen uses it at once); NVS only when it matters.
      copyIn(s->list);
      if (changed) displayRevision = displayRevision + 1;
      if (clockOffsetNeedsSave(changed, lastSavedUtc, now) && persist(*s)) lastSavedUtc = now;
    }
    free(s);
  }
  xSemaphoreGive(writer);
  return ok;
}

uint32_t localClocksRevision() {
  return revision;
}

uint32_t localClocksDisplayRevision() {
  return displayRevision;
}
