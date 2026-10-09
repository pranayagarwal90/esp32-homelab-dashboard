#include <Arduino.h>
#include "games/SimonGame.h"
#include "games/SimonLogic.h"
#include "AppState.h"
#include "Display.h"
#include "MenuScreens.h"
#include "PageRouter.h"
#include "UiHelpers.h"

static constexpr uint32_t FLASH_MS = 180; // Feedback for the player's press.
static const uint16_t PAD_DIM[SIMON_PADS] = {TFT_DARKGREEN, TFT_MAROON, TFT_OLIVE, TFT_NAVY};
static const uint16_t PAD_LIT[SIMON_PADS] = {TFT_GREEN, TFT_RED, TFT_YELLOW, TFT_BLUE};

static SimonGame simon;
static int flashPad = -1;
static unsigned long flashAt = 0;

static void drawPad(int pad, bool lit) {
  int x = SIMON_PAD_X[pad % 2], y = SIMON_PAD_Y[pad / 2];
  uint16_t fill = lit ? PAD_LIT[pad] : PAD_DIM[pad];
  tft.fillRoundRect(x, y, SIMON_PAD_W, SIMON_PAD_H, 10, fill);
  tft.drawRoundRect(x, y, SIMON_PAD_W, SIMON_PAD_H, 10, lit ? TFT_WHITE : TFT_DARKGREY);
  char label[2] = {(char)('1' + pad), '\0'};
  tft.setTextSize(3);
  tft.setTextColor(lit ? TFT_BLACK : TFT_LIGHTGREY, fill);
  tft.setCursor(x + (SIMON_PAD_W - tft.textWidth(label)) / 2, y + (SIMON_PAD_H - 24) / 2);
  tft.print(label);
}

static void drawRound() {
  char text[12];
  snprintf(text, sizeof(text), "ROUND %u", (unsigned)(simon.length ? simon.length : 1));
  drawTitleBarValue(text);
}

static void drawBar() {
  const char* status = nullptr;
  switch (simon.phase) {
    case SimonPhase::ShowPad:
    case SimonPhase::ShowGap: status = "WATCH"; break;
    case SimonPhase::PlayerInput: status = "YOUR TURN"; break;
    default: break;
  }
  drawBackBar(simon.phase == SimonPhase::Idle ? "START" : "RESTART", "BACK", status);
}

static void drawMessage(const char* title, uint16_t color, const char* line) {
  tft.fillRoundRect(50, 84, 220, 72, 8, TFT_BLACK);
  tft.drawRoundRect(50, 84, 220, 72, 8, color);
  tft.setTextSize(2);
  tft.setTextColor(color, TFT_BLACK);
  tft.setCursor(160 - tft.textWidth(title) / 2, 98);
  tft.print(title);
  tft.setTextSize(1);
  tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  tft.setCursor(160 - tft.textWidth(line) / 2, 128);
  tft.print(line);
}

static void drawEndMessage() {
  char line[32];
  snprintf(line, sizeof(line), "Score %d - tap a pad to play", simonScore(simon));
  if (simon.phase == SimonPhase::Won) drawMessage("YOU WIN!", TFT_GREEN, line);
  else drawMessage("GAME OVER", TFT_RED, line);
}

void drawSimonPage() {
  app.currentPage = PAGE_SIMON;
  simonSuspend(simon, millis()); // Replays an interrupted round from its start.
  flashPad = -1;
  tft.fillScreen(TFT_BLACK);
  drawTitleBar("SIMON");
  drawRound();
  for (int pad = 0; pad < SIMON_PADS; pad++) drawPad(pad, false);
  if (simon.phase == SimonPhase::Idle) drawMessage("SIMON SAYS", TFT_WHITE, "Tap a pad to start");
  if (simon.phase == SimonPhase::GameOver || simon.phase == SimonPhase::Won) drawEndMessage();
  drawBar();
}

static void newGame() {
  simonStart(simon, esp_random(), millis());
  drawSimonPage();
}

void handleSimonTouch(int x, int y) {
  if (y >= 205) {
    if (x < 107) newGame();
    else if (x < 214) showMenuNode(menuParent(MenuNode::Simon));
    return;
  }
  int pad = simonPadAt(x, y);
  if (pad < 0) return;
  if (simon.phase == SimonPhase::Idle || simon.phase == SimonPhase::GameOver || simon.phase == SimonPhase::Won) {
    newGame();
    return;
  }
  switch (simonPress(simon, (uint8_t)pad, millis())) {
    case SimonPress::Correct:
    case SimonPress::RoundComplete:
      if (flashPad >= 0) drawPad(flashPad, false);
      drawPad(pad, true);
      flashPad = pad;
      flashAt = millis();
      if (simon.phase != SimonPhase::PlayerInput) {
        drawRound();
        drawBar();
      }
      break;
    case SimonPress::Wrong:
    case SimonPress::Won:
      if (flashPad >= 0) drawPad(flashPad, false);
      flashPad = -1;
      drawEndMessage();
      drawBar();
      break;
    case SimonPress::Ignored:
      break;
  }
}

void updateSimonGame() {
  if (app.currentPage != PAGE_SIMON) return;
  if (flashPad >= 0 && millis() - flashAt >= FLASH_MS) {
    drawPad(flashPad, false);
    flashPad = -1;
  }
  uint8_t pad = 0;
  switch (simonUpdate(simon, millis(), pad)) {
    case SimonEvent::PadOn: drawPad(pad, true); break;
    case SimonEvent::PadOff: drawPad(pad, false); break;
    case SimonEvent::InputReady: drawBar(); break;
    case SimonEvent::None: break;
  }
}
