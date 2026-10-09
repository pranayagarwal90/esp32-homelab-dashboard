#pragma once
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "AlertLogic.h"
#include "Pages.h"
#include "UiIcons.h"
#include "UiTheme.h"

// Pure AI ASSISTANT logic shared with host tests: modes, menu layout, BACK
// origins, the request body (fixed modes plus the dashboard's alert codes,
// never free text), the request slot and text wrapping. Read-only: the
// assistant only explains; it never runs anything.

// --- Modes ----------------------------------------------------------------------------

enum class AiMode : uint8_t { Status, Alerts, Attention, Action, Summary, Count };

struct AiModeInfo {
  const char* key;    // Backend mode value.
  const char* label;  // Menu row.
  UiIcon icon;
  uint16_t tile;
};

static const AiModeInfo AI_MODES[] = {
  {"status", "EXPLAIN STATUS", UiIcon::AiStatus, UiColor::Blue},
  {"alerts", "EXPLAIN ALERTS", UiIcon::AiAlerts, UiColor::Orange},
  {"attention", "NEEDS ATTENTION", UiIcon::AiAttention, UiColor::Red},
  {"action", "SUGGEST ACTION", UiIcon::AiAction, UiColor::Yellow},
  {"summary", "SERVER SUMMARY", UiIcon::AiSummary, UiColor::Grey},
};
constexpr int AI_MODE_COUNT = (int)AiMode::Count;

inline const AiModeInfo& aiModeInfo(AiMode mode) {
  int index = (int)mode;
  return AI_MODES[index >= 0 && index < AI_MODE_COUNT ? index : 0];
}

// Mode when opened from another page: ALERTS asks about the alerts,
// HOMESERVER about what needs attention.
inline AiMode aiDefaultModeFor(Page origin) {
  return origin == PAGE_ALERTS ? AiMode::Alerts : origin == PAGE_HOMESERVER ? AiMode::Attention : AiMode::Status;
}

// --- Menu layout (320x240): title bar, five rows, BACK bar ---------------------------

constexpr int AI_MENU_Y0 = 40;
constexpr int AI_MENU_PITCH = 33;
constexpr int AI_MENU_ROW_H = 31;

// Row under a touch, or -1. Rows own the 2 px gaps; the bars are excluded.
inline int aiMenuRowAt(int x, int y) {
  if (x < 0 || x >= 320 || y < AI_MENU_Y0 - 1 || y >= 205) return -1;
  int row = (y - (AI_MENU_Y0 - 1)) / AI_MENU_PITCH;
  return row < AI_MODE_COUNT ? row : -1;
}

// Result page bottom bar: AGAIN (left third), BACK (middle third).
enum class AiResultHit : uint8_t { None, Again, Back };
inline AiResultHit aiResultHitAt(int x, int y) {
  if (y < 205) return AiResultHit::None;
  if (x < 107) return AiResultHit::Again;
  return x < 214 ? AiResultHit::Back : AiResultHit::None;
}

// ASK AI on ALERTS: a button in the title bar's right part (in place of
// LIVE / OFFLINE there). On HOMESERVER: the bottom bar's right third.
constexpr int AI_ASK_X = 222;
constexpr int AI_ASK_Y = 4;
constexpr int AI_ASK_W = 92;
constexpr int AI_ASK_H = 28;
inline bool aiAskHeaderHit(int x, int y) { return y >= 0 && y < 36 && x >= 214 && x < 320; }
inline bool aiAskBarHit(int x, int y) { return y >= 205 && x >= 214 && x < 320; }

// --- Response -------------------------------------------------------------------------

constexpr size_t AI_TITLE_MAX = 48;    // Backend sends <= 40.
constexpr size_t AI_SUMMARY_MAX = 256; // Backend sends <= 220.
constexpr size_t AI_ACTION_MAX = 192;  // Backend sends <= 140.
constexpr size_t AI_ERROR_MAX = 96;
constexpr size_t AI_RESPONSE_MAX = 1536; // Whole HTTP body.
constexpr size_t AI_REQUEST_MAX = 640;   // POST body (mode + up to 10 alert codes).

enum class AiState : uint8_t { Idle, Loading, Success, Error };
// Which HomeServer-side Ollama answered: the GPU laptop (primary) or the
// HomeServer's CPU (fallback). Older backends send none.
enum class AiEngine : uint8_t { Unknown, Primary, Fallback };

struct AiResponse {
  bool available = false;
  bool cached = false;
  bool rules = false;           // Text from the backend's rules, not the model.
  AiEngine engine = AiEngine::Unknown;
  char title[AI_TITLE_MAX] = {};
  char summary[AI_SUMMARY_MAX] = {};
  char action[AI_ACTION_MAX] = {};
  char error[AI_ERROR_MAX] = {};
};

// Copies text for the GLCD font: printable ASCII only, whitespace collapsed,
// always terminated, cut with "..." when it does not fit.
inline void aiCopyText(char* out, size_t size, const char* text) {
  if (!out || size == 0) return;
  size_t n = 0;
  bool space = true; // Drops leading spaces.
  bool cut = false;
  for (const char* p = text ? text : ""; *p; p++) {
    unsigned char c = (unsigned char)*p;
    if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
      if (space) continue;
      c = ' ';
      space = true;
    } else if (c < 32 || c > 126) {
      continue; // UTF-8 / control bytes: not in the font.
    } else {
      space = false;
    }
    if (n + 1 >= size) {
      cut = true;
      break;
    }
    out[n++] = (char)c;
  }
  while (n > 0 && out[n - 1] == ' ') n--;
  if (cut && size >= 4) {
    n = n < size - 4 ? n : size - 4;
    out[n++] = '.';
    out[n++] = '.';
    out[n++] = '.';
  }
  out[n] = '\0';
}

// Error shown for an unusable or missing answer.
inline void aiSetError(AiResponse& response, const char* error) {
  response = AiResponse();
  aiCopyText(response.error, sizeof(response.error), error && error[0] ? error : "AI unavailable");
}

// Validates a parsed response (fields copied by the caller): a usable answer
// needs a title and a summary; anything else becomes an error.
inline void aiFinishResponse(AiResponse& response) {
  if (!response.available) {
    if (!response.error[0]) aiCopyText(response.error, sizeof(response.error), "AI unavailable");
    return;
  }
  if (!response.title[0] || !response.summary[0]) aiSetError(response, "AI returned an unusable answer");
}

// Error page heading: "AI TIMED OUT" for timeouts, otherwise "AI UNAVAILABLE".
inline const char* aiErrorHeading(const char* error) {
  return error && strstr(error, "timed out") ? "AI TIMED OUT" : "AI UNAVAILABLE";
}

inline AiEngine aiEngineFrom(const char* engine) {
  if (!engine) return AiEngine::Unknown;
  if (strcmp(engine, "primary") == 0) return AiEngine::Primary;
  if (strcmp(engine, "fallback") == 0) return AiEngine::Fallback;
  return AiEngine::Unknown;
}

// Answer footer: "AI - GPU", "AI - CPU (CACHED)", "RULES - LOCAL"; never a
// model name or address.
inline void aiFooterLabel(const AiResponse& response, char* out, size_t size) {
  const char* where = response.engine == AiEngine::Primary ? "GPU"
                      : response.engine == AiEngine::Fallback ? "CPU" : "LOCAL";
  if (response.rules) where = "LOCAL";
  snprintf(out, size, "%s - %s%s", response.rules ? "RULES" : "AI", where, response.cached ? " (CACHED)" : "");
}

// --- Request body ---------------------------------------------------------------------

inline const char* aiAlertId(const AlertTable& table, int slot) {
  static const char* const SERVICES[ALERT_SERVICE_COUNT] = {"jellyfin", "navidrome", "metube", "ollama", "cloudflare"};
  (void)table;
  switch (alertKindOf(slot)) {
    case AlertKind::ServiceOffline: return SERVICES[slot];
    case AlertKind::RamHigh: return "ram";
    case AlertKind::DiskHigh: return "disk";
    case AlertKind::StatusStale: return "stale";
  }
  return "";
}

inline bool aiSubjectValid(const char* subject) {
  size_t length = strlen(subject);
  if (length == 0 || length > 4) return false;
  for (size_t i = 0; i < length; i++) {
    char c = subject[i];
    bool ok = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
              (c == ':' && i == length - 1);
    if (!ok) return false;
  }
  return true;
}

// {"mode":"alerts","refresh":true,"alerts":[{"id":"metube","level":"warning"},
//  {"id":"disk","level":"critical","subject":"C:"}]}
// The dashboard's own alert verdicts (its rules, hysteresis and stale state)
// as fixed codes; no metrics. Returns the length, or 0 if it did not fit.
inline size_t aiRequestBody(AiMode mode, bool refresh, const AlertTable& table, char* out, size_t size) {
  if (!out || size == 0) return 0;
  int n = snprintf(out, size, "{\"mode\":\"%s\"%s", aiModeInfo(mode).key, refresh ? ",\"refresh\":true" : "");
  if (n < 0 || (size_t)n >= size) {
    out[0] = '\0';
    return 0;
  }
  if (table.haveStatus || alertsActiveCount(table) > 0) {
    n += snprintf(out + n, size - n, ",\"alerts\":[");
    bool first = true;
    uint8_t order[ALERT_SLOT_COUNT];
    int count = alertsSorted(table, order);
    for (int i = 0; i < count && (size_t)n < size; i++) {  // Stops once full.
      const Alert& alert = table.slots[order[i]];
      const char* level = alert.severity == AlertSeverity::Critical ? "critical"
                          : alert.severity == AlertSeverity::Warning ? "warning" : nullptr;
      if (!level) continue;
      bool disk = alertKindOf(order[i]) == AlertKind::DiskHigh;
      if (disk && !aiSubjectValid(alert.subject)) continue;
      n += snprintf(out + n, size - n, "%s{\"id\":\"%s\",\"level\":\"%s\"", first ? "" : ",",
                    aiAlertId(table, order[i]), level);
      if (disk && (size_t)n < size) n += snprintf(out + n, size - n, ",\"subject\":\"%s\"", alert.subject);
      if ((size_t)n < size) n += snprintf(out + n, size - n, "}");
      first = false;
    }
    if ((size_t)n < size) n += snprintf(out + n, size - n, "]");
  }
  if ((size_t)n < size) n += snprintf(out + n, size - n, "}");
  if ((size_t)n >= size) {
    out[0] = '\0';
    return 0;
  }
  return (size_t)n;
}

// --- Request slot ---------------------------------------------------------------------
// One AI request at a time. Leaving the AI result page (not for the
// screensaver) invalidates it: its answer is discarded, never drawn.

class AiRequestSlot {
 public:
  bool busy() const { return inFlight; }
  uint32_t begin() {
    inFlight = true;
    return generation;
  }
  void invalidate() { ++generation; }
  bool complete(uint32_t resultGeneration) {
    inFlight = false;
    return resultGeneration == generation;
  }

 private:
  uint32_t generation = 1;
  bool inFlight = false;
};

// The assistant's request state, shared by the menu (availability check)
// and the result page. One request at a time; AGAIN while loading is ignored.
struct AiSession {
  AiState state = AiState::Idle;
  AiMode mode = AiMode::Status;
  Page origin = PAGE_AI;
  bool refresh = false;
  bool pending = false;     // Asked for, waiting for the slot.
  AiRequestSlot slot;

  // A new question (or AGAIN). False if one is already loading.
  bool ask(AiMode newMode, Page newOrigin, bool again) {
    if (state == AiState::Loading) return false;
    mode = newMode;
    origin = newOrigin;
    refresh = again;
    state = AiState::Loading;
    pending = true;
    return true;
  }
  bool wantsSubmit() const { return pending && !slot.busy(); }
  uint32_t submitted() {
    pending = false;
    return slot.begin();
  }
  // Called every loop with the current page: leaving the result for anything
  // but the screensaver abandons the question and discards its answer.
  void pageChanged(Page page);
  // A result arrived. True if it is still wanted.
  bool complete(uint32_t generation) { return slot.complete(generation); }
  void finished(bool ok) { state = ok ? AiState::Success : AiState::Error; }
};

// Whether leaving for `page` abandons the AI result. The screensaver keeps it
// (the answer is stored and shown on wake); any other page abandons it.
inline bool aiKeepsResultOn(Page page) {
  return page == PAGE_AI_RESULT || page == PAGE_SCREENSAVER;
}

// --- Text wrapping --------------------------------------------------------------------

constexpr int AI_LINE_CHARS = 50;   // 300 px of size-1 text.
constexpr int AI_LINE_PITCH = 12;
constexpr int AI_TEXT_TOP = 66;
constexpr int AI_TEXT_BOTTOM = 192; // Footer at 196, bar at 208.
constexpr int AI_TEXT_LINES = (AI_TEXT_BOTTOM - AI_TEXT_TOP) / AI_LINE_PITCH;  // 10
constexpr int AI_ACTION_LINES = 3;

struct AiLine {
  uint16_t start;
  uint8_t length;
  bool ellipsis;  // Draw "..." after this (last) line: the text was cut.
};

// Greedy word wrap into at most maxLines lines of at most width characters.
// Long words are split. When the text does not fit, the last line is
// shortened so "..." fits after it. Returns the line count.
inline int aiWrap(const char* text, int width, int maxLines, AiLine* lines) {
  if (!text || width < 4 || maxLines <= 0) return 0;
  int count = 0;
  int length = (int)strlen(text);
  int pos = 0;
  while (pos < length && text[pos] == ' ') pos++;
  while (pos < length) {
    if (count == maxLines) {
      AiLine& last = lines[count - 1];
      int keep = last.length;
      if (keep > width - 3) keep = width - 3;
      // Prefer ending at a word.
      int cut = keep;
      while (cut > 0 && text[last.start + cut] != ' ') cut--;
      if (cut > keep / 2) keep = cut;
      while (keep > 0 && text[last.start + keep - 1] == ' ') keep--;
      last.length = (uint8_t)keep;
      last.ellipsis = true;
      return count;
    }
    int end = pos + width;
    if (end >= length) {
      end = length;
    } else {
      int space = end;
      while (space > pos && text[space] != ' ') space--;
      if (space > pos) end = space; // Else a long word: hard split.
    }
    int lineEnd = end;
    while (lineEnd > pos && text[lineEnd - 1] == ' ') lineEnd--;
    lines[count].start = (uint16_t)pos;
    lines[count].length = (uint8_t)(lineEnd - pos);
    lines[count].ellipsis = false;
    count++;
    pos = end;
    while (pos < length && text[pos] == ' ') pos++;
  }
  return count;
}

// Lines for the summary when an action (label + up to AI_ACTION_LINES) is
// also shown: the action keeps its room, the summary gets the rest.
inline int aiSummaryLineBudget(bool hasAction, int actionLines) {
  if (!hasAction) return AI_TEXT_LINES;
  if (actionLines > AI_ACTION_LINES) actionLines = AI_ACTION_LINES;
  return AI_TEXT_LINES - 1 - actionLines; // Minus "SUGGESTED CHECK" label.
}

inline void AiSession::pageChanged(Page page) {
  if (aiKeepsResultOn(page) || state == AiState::Idle) return;
  if (slot.busy()) slot.invalidate();
  state = AiState::Idle;
  pending = false;
}
