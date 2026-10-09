// Needs ArduinoJson (header-only) for the response parser:
//   -Ifirmware/.pio/libdeps/esp32dev/ArduinoJson/src
#include "AiResponse.h"
#include "MenuLayout.h"
#include "UiIconShapes.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static void testModesAndMenu() {
  const char* keys[] = {"status", "alerts", "attention", "action", "summary"};
  const UiIcon icons[] = {UiIcon::AiStatus, UiIcon::AiAlerts, UiIcon::AiAttention, UiIcon::AiAction, UiIcon::AiSummary};
  for (int i = 0; i < AI_MODE_COUNT; i++) {
    assert(strcmp(aiModeInfo((AiMode)i).key, keys[i]) == 0);
    assert(AI_MODES[i].icon == icons[i]);
    assert(46 + strlen(AI_MODES[i].label) * 12 <= 284);  // Label clear of ">" at size 2.
    int y = AI_MENU_Y0 + i * AI_MENU_PITCH;
    assert(aiMenuRowAt(160, y + AI_MENU_ROW_H / 2) == i);
  }
  assert(AI_MENU_Y0 + (AI_MODE_COUNT - 1) * AI_MENU_PITCH + AI_MENU_ROW_H < 205);  // Above BACK.
  assert(aiMenuRowAt(160, 20) == -1 && aiMenuRowAt(160, 210) == -1);
  assert(aiMenuRowAt(160, AI_MENU_Y0 + AI_MENU_ROW_H) == 0 && aiMenuRowAt(160, AI_MENU_Y0 + AI_MENU_PITCH - 1) == 1);  // Gaps belong to rows.
  assert(strcmp(aiModeInfo(AiMode::Count).key, "status") == 0);  // Out of range: safe.
  // Default modes from context.
  assert(aiDefaultModeFor(PAGE_ALERTS) == AiMode::Alerts);
  assert(aiDefaultModeFor(PAGE_HOMESERVER) == AiMode::Attention);
  assert(aiDefaultModeFor(PAGE_AI) == AiMode::Status);
  // MORE has the assistant; it is an app (MORE tab).
  bool found = false;
  for (int i = 0; i < MORE_APP_COUNT; i++) {
    if (MORE_APPS[i].page == PAGE_AI) found = MORE_APPS[i].icon == UiIcon::AiAssistant;
  }
  assert(found);
  assert(navTabForPage(PAGE_AI) == NavTab::More && navTabForPage(PAGE_AI_RESULT) == NavTab::More);
}

static void testBackAndTouch() {
  assert(appBackTarget(PAGE_AI, PAGE_HOME) == PAGE_MORE);                // Menu -> MORE.
  assert(appBackTarget(PAGE_AI_RESULT, PAGE_AI) == PAGE_AI);             // Result -> menu.
  assert(appBackTarget(PAGE_AI_RESULT, PAGE_ALERTS) == PAGE_ALERTS);     // -> ALERTS.
  assert(appBackTarget(PAGE_AI_RESULT, PAGE_HOMESERVER) == PAGE_HOMESERVER);
  assert(appBackTarget(PAGE_AI_RESULT, PAGE_SCREENSAVER) == PAGE_AI);    // Unknown: menu.
  assert(aiResultHitAt(50, 220) == AiResultHit::Again);
  assert(aiResultHitAt(160, 220) == AiResultHit::Back);
  assert(aiResultHitAt(300, 220) == AiResultHit::None && aiResultHitAt(50, 100) == AiResultHit::None);
  // ASK AI: ALERTS title bar right (clear of "ALERTS 10" and of PREV/BACK/NEXT).
  assert(aiAskHeaderHit(260, 18) && !aiAskHeaderHit(100, 18) && !aiAskHeaderHit(260, 40));
  assert(10 + strlen("ALERTS 10") * 12 < AI_ASK_X);
  assert(AI_ASK_X + AI_ASK_W <= 320 && AI_ASK_Y + AI_ASK_H <= 36 && AI_ASK_H >= 28);
  // HOMESERVER: bottom bar right third; BACK keeps the middle.
  assert(aiAskBarHit(250, 220) && !aiAskBarHit(160, 220) && !aiAskBarHit(250, 150));
}

static AlertTable tableWith() {
  AlertTable table;
  AlertInput input;
  input.memPercent = 96;
  input.diskCount = 2;
  const char* names[] = {"C:", "D:"};
  input.diskNames[0] = names[0];
  input.diskNames[1] = names[1];
  input.diskPercent[0] = 40;
  input.diskPercent[1] = 88;
  input.servicesReported = 0x1F;
  input.servicesOnline = 0x1F & ~(1u << ALERT_METUBE);
  alertsApplyStatus(table, input, 1000);
  return table;
}

static void testRequestBody() {
  char body[AI_REQUEST_MAX];
  AlertTable none;
  assert(aiRequestBody(AiMode::Status, false, none, body, sizeof(body)));
  assert(strcmp(body, "{\"mode\":\"status\"}") == 0);  // No status yet: alerts unknown.

  AlertTable healthy;
  AlertInput ok;
  ok.servicesReported = 0x1F;
  ok.servicesOnline = 0x1F;
  alertsApplyStatus(healthy, ok, 0);
  aiRequestBody(AiMode::Action, true, healthy, body, sizeof(body));
  assert(strcmp(body, "{\"mode\":\"action\",\"refresh\":true,\"alerts\":[]}") == 0);

  AlertTable table = tableWith();
  size_t n = aiRequestBody(AiMode::Alerts, false, table, body, sizeof(body));
  assert(n == strlen(body));
  // Critical first (the dashboard's own order), codes only, no metrics.
  assert(strcmp(body, "{\"mode\":\"alerts\",\"alerts\":[{\"id\":\"ram\",\"level\":\"critical\"},"
                      "{\"id\":\"metube\",\"level\":\"warning\"},"
                      "{\"id\":\"disk\",\"level\":\"warning\",\"subject\":\"D:\"}]}") == 0);
  assert(!strstr(body, "96") && !strstr(body, "88"));

  // Every slot active still fits.
  AlertTable full = tableWith();
  for (int i = 0; i < ALERT_SLOT_COUNT; i++) full.slots[i].severity = AlertSeverity::Critical;
  for (int i = 0; i < ALERT_MAX_DISKS; i++) snprintf(full.slots[ALERT_SLOT_DISK0 + i].subject, ALERT_SUBJECT_MAX, "%c:", 'C' + i);
  assert(aiRequestBody(AiMode::Summary, true, full, body, sizeof(body)) > 0);
  // Too small a buffer fails cleanly; bad disk names are skipped.
  char tiny[24];
  memset(tiny, 'x', sizeof(tiny));
  assert(aiRequestBody(AiMode::Summary, true, full, tiny, sizeof(tiny)) == 0 && tiny[0] == '\0');
  char shortBuf[60];  // Mode fits, alerts do not.
  assert(aiRequestBody(AiMode::Alerts, false, full, shortBuf, sizeof(shortBuf)) == 0 && shortBuf[0] == '\0');
  AlertTable odd = tableWith();
  snprintf(odd.slots[ALERT_SLOT_DISK0 + 1].subject, ALERT_SUBJECT_MAX, "a\"b");
  aiRequestBody(AiMode::Alerts, false, odd, body, sizeof(body));
  assert(!strstr(body, "\"disk\""));
  assert(aiSubjectValid("C:") && aiSubjectValid("E") && !aiSubjectValid("") && !aiSubjectValid(":C"));
}

static void testParsing() {
  AiResponse r;
  const char* ok = "{\"available\":true,\"mode\":\"alerts\",\"title\":\"2 items need attention\","
                   "\"summary\":\"MeTube is offline and RAM usage is 87%.\",\"action\":\"Check MeTube logs.\","
                   "\"source\":\"ai\",\"generated_at\":1791565000,\"cached\":true}";
  aiParseResponse(ok, strlen(ok), 200, r);
  assert(r.available && r.cached && !r.rules);
  assert(strcmp(r.title, "2 items need attention") == 0 && strcmp(r.action, "Check MeTube logs.") == 0);
  assert(r.engine == AiEngine::Unknown);  // No engine field (older backend): still fine.
  char footer[32];
  aiFooterLabel(r, footer, sizeof(footer));
  assert(strcmp(footer, "AI - LOCAL (CACHED)") == 0);
  const char* gpu = "{\"available\":true,\"title\":\"t\",\"summary\":\"s\",\"source\":\"ai\",\"engine\":\"primary\"}";
  aiParseResponse(gpu, strlen(gpu), 200, r);
  aiFooterLabel(r, footer, sizeof(footer));
  assert(r.engine == AiEngine::Primary && strcmp(footer, "AI - GPU") == 0);
  const char* cpu = "{\"available\":true,\"title\":\"t\",\"summary\":\"s\",\"engine\":\"fallback\",\"cached\":true}";
  aiParseResponse(cpu, strlen(cpu), 200, r);
  aiFooterLabel(r, footer, sizeof(footer));
  assert(r.engine == AiEngine::Fallback && strcmp(footer, "AI - CPU (CACHED)") == 0);
  const char* odd = "{\"available\":true,\"title\":\"t\",\"summary\":\"s\",\"engine\":\"10.10.10.1\"}";
  aiParseResponse(odd, strlen(odd), 200, r);
  assert(r.engine == AiEngine::Unknown);

  const char* rules = "{\"available\":true,\"title\":\"All systems healthy\",\"summary\":\"No active alerts.\","
                      "\"action\":\"\",\"source\":\"rules\"}";
  aiParseResponse(rules, strlen(rules), 200, r);
  assert(r.available && r.rules && r.action[0] == '\0');
  aiFooterLabel(r, footer, sizeof(footer));
  assert(strcmp(footer, "RULES - LOCAL") == 0);

  const char* down = "{\"available\":false,\"mode\":\"status\",\"error\":\"AI response timed out\"}";
  aiParseResponse(down, strlen(down), 200, r);
  assert(!r.available && strcmp(r.error, "AI response timed out") == 0);
  assert(strcmp(aiErrorHeading(r.error), "AI TIMED OUT") == 0);
  assert(strcmp(aiErrorHeading("AI service is not responding"), "AI UNAVAILABLE") == 0);

  const char* malformed[] = {"not json", "[1,2]", "{\"available\":true}", "{\"available\":true,\"title\":\"x\"}", ""};
  for (const char* body : malformed) {
    aiParseResponse(body, strlen(body), 200, r);
    assert(!r.available && r.error[0]);
  }
  const char* rejected = "{\"available\":false,\"error\":\"invalid request: unsupported mode\"}";
  aiParseResponse(rejected, strlen(rejected), 400, r);
  assert(!r.available && strstr(r.error, "invalid request"));
  const char* serverError = "{\"available\":true,\"title\":\"t\",\"summary\":\"s\"}";
  aiParseResponse(serverError, strlen(serverError), 500, r);
  assert(!r.available && strcmp(r.error, "AI request failed") == 0);  // Non-200 is never an answer.

  const char* ready = "{\"available\":true,\"provider\":\"ollama\",\"model\":\"llama3.2:3b\"}";
  aiParseAvailability(ready, strlen(ready), r);
  assert(r.available);
  const char* missing = "{\"available\":false,\"error\":\"AI model is not installed\"}";
  aiParseAvailability(missing, strlen(missing), r);
  assert(!r.available && strcmp(r.error, "AI model is not installed") == 0);
}

static void testTextLimits() {
  char out[16];
  aiCopyText(out, sizeof(out), "  caf\xC3\xA9   \n ok\t ");
  assert(strcmp(out, "caf ok") == 0);                 // UTF-8 dropped, spaces collapsed.
  aiCopyText(out, sizeof(out), "abcdefghijklmnopqrstuvwxyz");
  assert(strlen(out) == 15 && strcmp(out + 12, "...") == 0);  // Cut with an ellipsis.
  aiCopyText(out, sizeof(out), nullptr);
  assert(out[0] == '\0');

  // Over-long answer: every buffer stays terminated and bounded.
  char longText[2000];
  memset(longText, 'w', sizeof(longText) - 1);
  longText[sizeof(longText) - 1] = '\0';
  char json[2300];
  snprintf(json, sizeof(json), "{\"available\":true,\"title\":\"%s\",\"summary\":\"%s\",\"action\":\"x\"}",
           "A very long title that keeps going and going past forty", longText);
  AiResponse r;
  aiParseResponse(json, strlen(json), 200, r);
  assert(r.available && strlen(r.title) < AI_TITLE_MAX && strlen(r.summary) < AI_SUMMARY_MAX);
}

static void testWrap() {
  AiLine lines[AI_TEXT_LINES];
  const char* text = "MeTube is offline and RAM usage is elevated at 87%. Other monitored services are responding.";
  int n = aiWrap(text, 50, 10, lines);
  assert(n == 2);
  for (int i = 0; i < n; i++) {
    assert(lines[i].length <= 50 && !lines[i].ellipsis);
    assert(text[lines[i].start] != ' ');
  }
  assert(lines[0].length == 46 && strncmp(text + lines[0].start, "MeTube", 6) == 0);  // Broke at a space.
  // Too long for the lines allowed: last line cut with room for "...".
  char words[400] = "";
  for (int i = 0; i < 40; i++) strcat(words, "word12345 ");
  n = aiWrap(words, 50, 3, lines);
  assert(n == 3 && lines[2].ellipsis && lines[2].length + 3 <= 50);
  // A word longer than a line is split, never overflowing.
  const char* longWord = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
  n = aiWrap(longWord, 50, 10, lines);
  assert(n == 2 && lines[0].length == 50 && lines[1].length == 28);
  assert(aiWrap("", 50, 10, lines) == 0 && aiWrap("   ", 50, 10, lines) == 0 && aiWrap(nullptr, 50, 10, lines) == 0);
  // Layout: summary and action never reach the footer / buttons.
  assert(AI_TEXT_TOP + AI_TEXT_LINES * AI_LINE_PITCH <= 196);
  assert(aiSummaryLineBudget(false, 0) == AI_TEXT_LINES);
  assert(aiSummaryLineBudget(true, 3) == AI_TEXT_LINES - 4 && aiSummaryLineBudget(true, 9) == AI_TEXT_LINES - 4);
  // A 220-character summary fits its budget next to a 3-line action.
  assert(AI_LINE_CHARS * aiSummaryLineBudget(true, 3) >= 220 + 50);
  // Action of 140 characters fits 3 lines.
  assert(AI_LINE_CHARS * AI_ACTION_LINES >= 140);
}

static void testSession() {
  AiSession s;
  assert(s.state == AiState::Idle && !s.wantsSubmit());
  assert(s.ask(AiMode::Alerts, PAGE_ALERTS, false));
  assert(s.state == AiState::Loading && s.wantsSubmit());
  uint32_t g = s.submitted();
  assert(!s.wantsSubmit() && s.slot.busy());                     // One request at a time.
  assert(!s.ask(AiMode::Alerts, PAGE_ALERTS, true));             // AGAIN while loading: ignored.
  s.pageChanged(PAGE_AI_RESULT);
  s.pageChanged(PAGE_SCREENSAVER);                                // Screensaver keeps the question.
  assert(s.state == AiState::Loading);
  assert(s.complete(g));
  s.finished(true);
  assert(s.state == AiState::Success);
  assert(s.ask(s.mode, s.origin, true) && s.refresh);            // AGAIN after an answer.
  uint32_t g2 = s.submitted();
  s.pageChanged(PAGE_ALERTS);                                     // BACK while loading.
  assert(s.state == AiState::Idle);
  assert(!s.complete(g2));                                        // Late answer discarded.
  assert(!s.slot.busy());
  // Leaving while the request waits for the slot: nothing is sent later.
  AiSession t;
  t.slot.begin();                                                 // e.g. an availability check.
  t.ask(AiMode::Summary, PAGE_AI, false);
  assert(!t.wantsSubmit());
  t.pageChanged(PAGE_MORE);
  assert(!t.pending && t.state == AiState::Idle);
}

static void testIcons() {
  const UiIcon icons[] = {UiIcon::AiAssistant, UiIcon::AiStatus, UiIcon::AiAlerts, UiIcon::AiAttention,
                          UiIcon::AiAction, UiIcon::AiSummary};
  for (UiIcon icon : icons) {
    IconShape shape = uiIconShape(icon);
    assert(shape.steps && shape.count > 0);
    for (UiIcon other : icons) assert(other == icon || uiIconShape(other).steps != shape.steps);
  }
}

int main() {
  testModesAndMenu();
  testBackAndTouch();
  testRequestBody();
  testParsing();
  testTextLimits();
  testWrap();
  testSession();
  testIcons();
  puts("AI logic tests passed: modes/menu, back/touch, request body, parsing, text limits, wrap, session, icons");
}
