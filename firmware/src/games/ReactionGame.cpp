#include <Arduino.h>
#include "games/ReactionGame.h"
#include "AppState.h"
#include "Display.h"
#include "PageRouter.h"
#include "UiHelpers.h"

enum ReactionState {
  REACTION_IDLE,
  REACTION_WAITING,
  REACTION_GO,
  REACTION_RESULT,
  REACTION_TOO_SOON
};

static ReactionState reactionState = REACTION_IDLE;
static unsigned long reactionTargetTime = 0;
static unsigned long reactionStartTime = 0;
static unsigned long reactionResultMs = 0;

static void drawReactionBackButton() {
  drawBackBar(nullptr, "BACK", nullptr);
}

static void startReactionGame() {
  reactionState = REACTION_WAITING;
  reactionTargetTime = millis() + random(1500, 4500);
  tft.fillScreen(TFT_BLACK);
  tft.setTextSize(3);
  tft.setTextColor(TFT_YELLOW, TFT_BLACK);
  tft.setCursor(75, 70);
  tft.print("WAIT...");
  tft.setTextSize(1);
  tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  tft.setCursor(67, 120);
  tft.print("Tap when screen turns GREEN");
  drawReactionBackButton();
}

void drawReactionPage() {
  app.currentPage = PAGE_REACTION;
  reactionState = REACTION_IDLE;
  tft.fillScreen(TFT_BLACK);
  tft.setTextSize(2);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setCursor(82, 35);
  tft.print("REACTION TAP");
  tft.setTextSize(1);
  tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  tft.setCursor(58, 75);
  tft.print("How fast can you react?");
  tft.fillRoundRect(65, 105, 190, 55, 8, TFT_BLUE);
  tft.setTextSize(2);
  tft.setTextColor(TFT_WHITE, TFT_BLUE);
  tft.setCursor(128, 123);
  tft.print("START");
  drawReactionBackButton();
}

void updateReactionGame() {
  if (app.currentPage != PAGE_REACTION) return;
  if (reactionState == REACTION_WAITING && millis() >= reactionTargetTime) {
    reactionState = REACTION_GO;
    reactionStartTime = millis();
    tft.fillRect(0, 0, 320, 208, TFT_GREEN);
    tft.setTextSize(4);
    tft.setTextColor(TFT_BLACK, TFT_GREEN);
    tft.setCursor(92, 85);
    tft.print("TAP!");
    drawReactionBackButton();
  }
}

void handleReactionTouch(int x, int y) {
  if (y >= 205) {
    reactionState = REACTION_IDLE;
    showPage(PAGE_GAMES);
    return;
  }

  if (reactionState == REACTION_IDLE) {
    if (x >= 55 && x <= 265 && y >= 95 && y <= 170) {
      startReactionGame();
      return;
    }
  } else if (reactionState == REACTION_WAITING) {
    reactionState = REACTION_TOO_SOON;
    tft.fillRect(0, 0, 320, 208, TFT_RED);
    tft.setTextSize(3);
    tft.setTextColor(TFT_WHITE, TFT_RED);
    tft.setCursor(60, 65);
    tft.print("TOO SOON!");
    tft.setTextSize(1);
    tft.setCursor(91, 120);
    tft.print("Tap to try again");
    drawReactionBackButton();
    return;
  } else if (reactionState == REACTION_GO) {
    reactionResultMs = millis() - reactionStartTime;
    reactionState = REACTION_RESULT;
    tft.fillRect(0, 0, 320, 208, TFT_BLACK);
    tft.setTextSize(2);
    tft.setTextColor(TFT_GREEN, TFT_BLACK);
    tft.setCursor(75, 55);
    tft.print("REACTION");
    tft.setTextSize(4);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    String result = String(reactionResultMs) + " ms";
    int width = tft.textWidth(result);
    tft.setCursor((320 - width) / 2, 95);
    tft.print(result);
    tft.setTextSize(1);
    tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    tft.setCursor(90, 160);
    tft.print("Tap to play again");
    drawReactionBackButton();
    return;
  } else {
    startReactionGame();
    return;
  }
}
