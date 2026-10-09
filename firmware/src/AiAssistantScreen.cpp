#include <Arduino.h>
#include "AiAssistantScreen.h"
#include "AiClient.h"
#include "AlertManager.h"
#include "Display.h"
#include "MenuLayout.h"
#include "PageRouter.h"
#include "SystemAnimation.h"
#include "UiHelpers.h"
#include "UiIcons.h"
#include "UiTheme.h"

enum class Availability : uint8_t { Unknown, Checking, Ready, Unavailable };

static AiSession session;
static AiResponse answer;               // The last answer (kept for the screensaver).
static Availability availability = Availability::Unknown;
static uint32_t availabilityAt = 0;
static uint32_t loadingStartMs = 0;
static int loadingPhaseShown = -1;

static constexpr uint32_t AVAILABILITY_RECHECK_MS = 30000;

// --- Shared drawing -------------------------------------------------------------------

static void printLine(const char* text, const AiLine& line, int x, int y) {
  char buffer[AI_LINE_CHARS + 4];
  int length = line.length < AI_LINE_CHARS ? line.length : AI_LINE_CHARS;
  memcpy(buffer, text + line.start, length);
  if (line.ellipsis) {
    memcpy(buffer + length, "...", 3);
    length += 3;
  }
  buffer[length] = '\0';
  tft.setCursor(x, y);
  tft.print(buffer);
}

// Wrapped size-1 text; returns the y below it.
static int drawWrapped(const char* text, int maxLines, int y, uint16_t color) {
  AiLine lines[AI_TEXT_LINES];
  int count = aiWrap(text, AI_LINE_CHARS, maxLines < AI_TEXT_LINES ? maxLines : AI_TEXT_LINES, lines);
  tft.setTextSize(1);
  tft.setTextColor(color, TFT_BLACK);
  for (int i = 0; i < count; i++) printLine(text, lines[i], 10, y + i * AI_LINE_PITCH);
  return y + count * AI_LINE_PITCH;
}

static void printCentered(const char* text, int y) {
  tft.setCursor((320 - tft.textWidth(text)) / 2, y);
  tft.print(text);
}

void drawAskAiHeaderButton() {
  tft.fillRoundRect(AI_ASK_X, AI_ASK_Y, AI_ASK_W, AI_ASK_H, 6, UiColor::Indigo);
  drawUiIcon(UiIcon::AiAssistant, AI_ASK_X + 10, AI_ASK_Y + 6, 16, UiColor::White, UiColor::Yellow, UiColor::Indigo);
  tft.setTextSize(1);
  tft.setTextColor(TFT_WHITE, UiColor::Indigo);
  tft.setCursor(AI_ASK_X + 34, AI_ASK_Y + 10);
  tft.print("ASK AI");
}

// --- Menu -----------------------------------------------------------------------------

static void drawAvailability() {
  tft.fillRect(200, 8, 120, 20, TFT_DARKGREY);
  const char* text = "";
  uint16_t color = TFT_LIGHTGREY;
  switch (availability) {
    case Availability::Ready: text = "LOCAL AI READY"; color = UiColor::Green; break;
    case Availability::Unavailable: text = "AI OFFLINE"; color = UiColor::Orange; break;
    case Availability::Checking: text = "CHECKING..."; break;
    case Availability::Unknown: break;
  }
  tft.setTextSize(1);
  tft.setTextColor(color, TFT_DARKGREY);
  tft.setCursor(310 - tft.textWidth(text), 14);
  tft.print(text);
}

static void drawMenuRow(int row) {
  const AiModeInfo& mode = AI_MODES[row];
  int y = AI_MENU_Y0 + row * AI_MENU_PITCH;
  tft.fillRoundRect(10, y, 300, AI_MENU_ROW_H, 8, TFT_DARKGREY);
  tft.drawRoundRect(10, y, 300, AI_MENU_ROW_H, 8, TFT_LIGHTGREY);
  int tileX = 14, tileY = y + 3;
  tft.fillRoundRect(tileX, tileY, 25, 25, 6, mode.tile);
  drawUiIcon(mode.icon, tileX + 3, tileY + 3, 19, glyphColorOn(mode.tile), UiColor::Yellow, mode.tile);
  tft.setTextSize(2);
  tft.setTextColor(TFT_WHITE, TFT_DARKGREY);
  tft.setCursor(48, y + 8);
  tft.print(mode.label);
  tft.setTextColor(TFT_LIGHTGREY, TFT_DARKGREY);
  tft.setCursor(288, y + 8);
  tft.print(">");
}

void drawAiMenuPage() {
  app.currentPage = PAGE_AI;
  tft.fillScreen(TFT_BLACK);
  drawTitleBar("AI ASSISTANT");
  drawAvailability();
  for (int row = 0; row < AI_MODE_COUNT; row++) drawMenuRow(row);
  drawBackBar(nullptr, "BACK", nullptr);
  // A quick availability check (no generation); it also warms the model.
  if (availability != Availability::Checking &&
      (availability == Availability::Unknown || millis() - availabilityAt >= AVAILABILITY_RECHECK_MS) &&
      !session.slot.busy()) {
    AiJob job = {};
    job.type = AiJobType::Status;
    job.generation = session.slot.begin();
    if (submitAiJob(job)) {
      availability = Availability::Checking;
    } else {
      session.complete(job.generation);
      availability = Availability::Unavailable;
    }
    drawAvailability();
  }
}

void handleAiMenuTouch(int x, int y) {
  if (y >= 205) {
    showPage(appBackTarget(PAGE_AI, PAGE_MORE));
    return;
  }
  int row = aiMenuRowAt(x, y);
  if (row >= 0) openAiResult((AiMode)row, PAGE_AI);
}

// --- Result ---------------------------------------------------------------------------

static void drawResultBar() {
  drawBackBar(session.state == AiState::Loading ? nullptr : "AGAIN", "BACK", nullptr);
}

static void drawLoading() {
  tft.setTextSize(2);
  tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  printCentered("Thinking...", 92);
  tft.setTextSize(1);
  tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
  printCentered(aiModeInfo(session.mode).label, 70);
  printCentered("Local AI on the HomeServer", 160);
  loadingPhaseShown = -1;
}

static void drawError() {
  tft.setTextSize(2);
  tft.setTextColor(UiColor::Orange, TFT_BLACK);
  printCentered(aiErrorHeading(answer.error), 80);
  tft.setTextSize(1);
  tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  printCentered(answer.error, 112);
  tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
  printCentered("Dashboard data is not affected.", 136);
  printCentered("Try AGAIN in a moment.", 150);
}

static void drawAnswer() {
  drawUiIcon(UiIcon::AiAssistant, 10, 42, 16, UiColor::Indigo, UiColor::Yellow, TFT_BLACK);
  bool large = strlen(answer.title) * 12 <= 280;
  tft.setTextSize(large ? 2 : 1);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setCursor(32, large ? 42 : 46);
  tft.print(answer.title);

  bool hasAction = answer.action[0] != '\0';
  AiLine probe[AI_ACTION_LINES + 1];
  int actionLines = hasAction ? aiWrap(answer.action, AI_LINE_CHARS, AI_ACTION_LINES, probe) : 0;
  int y = drawWrapped(answer.summary, aiSummaryLineBudget(hasAction, actionLines), AI_TEXT_TOP, TFT_WHITE);
  if (hasAction) {
    y += 4;
    tft.setTextSize(1);
    tft.setTextColor(UiColor::Cyan, TFT_BLACK);
    tft.setCursor(10, y);
    tft.print("SUGGESTED CHECK");
    drawWrapped(answer.action, AI_ACTION_LINES, y + AI_LINE_PITCH, TFT_LIGHTGREY);
  }

  // Never presented as a person: says what produced the text.
  tft.setTextSize(1);
  tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
  char footer[32];
  aiFooterLabel(answer, footer, sizeof(footer));
  tft.setCursor(10, 196);
  tft.print(footer);
  const char* label = aiModeInfo(session.mode).label;
  tft.setCursor(310 - tft.textWidth(label), 196);
  tft.print(label);
}

static void drawResultBody() {
  tft.fillRect(0, 36, 320, 172, TFT_BLACK);
  switch (session.state) {
    case AiState::Loading: drawLoading(); break;
    case AiState::Success: drawAnswer(); break;
    case AiState::Error: drawError(); break;
    case AiState::Idle: break;
  }
  drawResultBar();
}

void drawAiResultPage() {
  app.currentPage = PAGE_AI_RESULT;
  // Reached without a question (should not happen): ask the default one.
  if (session.state == AiState::Idle) session.ask(session.mode, session.origin, false);
  tft.fillScreen(TFT_BLACK);
  drawTitleBar("AI ASSISTANT");
  drawResultBody();
}

void openAiResult(AiMode mode, Page origin) {
  if (session.state == AiState::Loading && session.mode == mode && session.origin == origin) {
    showPage(PAGE_AI_RESULT); // Already asking exactly this.
    return;
  }
  session.pageChanged(PAGE_AI); // Drop any older question first.
  session.ask(mode, origin, false);
  loadingStartMs = millis();
  showPage(PAGE_AI_RESULT);
}

void handleAiResultTouch(int x, int y) {
  switch (aiResultHitAt(x, y)) {
    case AiResultHit::Again:
      if (session.ask(session.mode, session.origin, true)) {  // Ignored while loading.
        loadingStartMs = millis();
        drawResultBody();
      }
      break;
    case AiResultHit::Back: {
      Page target = appBackTarget(PAGE_AI_RESULT, session.origin);
      session.pageChanged(target); // Abandon: a late answer is discarded.
      showPage(target);
      break;
    }
    case AiResultHit::None:
      break;
  }
}

// --- Loop -----------------------------------------------------------------------------

static void submitQuestion() {
  AiJob job = {};
  job.type = AiJobType::Explain;
  if (!aiRequestBody(session.mode, session.refresh, alerts(), job.body, sizeof(job.body))) {
    aiRequestBody(session.mode, session.refresh, AlertTable(), job.body, sizeof(job.body));
  }
  job.generation = session.submitted();
  if (!submitAiJob(job)) {
    session.complete(job.generation);
    aiSetError(answer, "AI is not available on this device");
    session.finished(false);
    if (app.currentPage == PAGE_AI_RESULT) drawResultBody();
  }
}

void updateAiAssistant() {
  session.pageChanged(app.currentPage);

  AiResult result;
  if (receiveAiResult(result)) {
    bool current = session.complete(result.generation);
    if (current && result.type == AiJobType::Status) {
      availability = result.response.available ? Availability::Ready : Availability::Unavailable;
      availabilityAt = millis();
      if (app.currentPage == PAGE_AI) drawAvailability();
    } else if (current && result.type == AiJobType::Explain && session.state == AiState::Loading) {
      answer = result.response;
      session.finished(answer.available);
      // Under the screensaver the answer is kept and drawn on wake.
      if (app.currentPage == PAGE_AI_RESULT) drawResultBody();
    } else if (result.type == AiJobType::Status) {
      availability = Availability::Unknown; // Superseded check: ask again next time.
    }
  }

  if (session.wantsSubmit()) submitQuestion();

  if (app.currentPage == PAGE_AI_RESULT && session.state == AiState::Loading) {
    int phase = loadingPhase(millis() - loadingStartMs);
    if (phase != loadingPhaseShown) {
      loadingPhaseShown = phase;
      drawLoadingDots(160, 132, phase, UiColor::Indigo);
    }
  }
}
