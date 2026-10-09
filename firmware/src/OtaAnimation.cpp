#include <Arduino.h>
#include "OtaAnimation.h"
#include "OtaAnimationLogic.h"
#include "AppState.h"
#include "BacklightPwm.h"
#include "Display.h"
#include "PageRouter.h"
#include "UiHelpers.h"
#include "WalkerDraw.h"

static OtaAnimation ota;
// Last walker drawn, erased by redrawing it in black (exact pixels, no
// rectangle clears, so no flicker and no damage to the ground or house).
static const WalkerFrame* drawnPose = nullptr;
static int drawnX = 0;

static void showWalker(int x, const WalkerFrame& pose) {
  if (drawnPose) drawWalker(drawnX, OTA_GROUND_Y, *drawnPose, true);
  drawWalker(x, OTA_GROUND_Y, pose, false);
  drawnPose = &pose;
  drawnX = x;
}

static void drawHome() {
  int x = OTA_HOME_X, ground = OTA_GROUND_Y;
  tft.fillTriangle(x, ground - 25, x + 18, ground - 43, x + 36, ground - 25, TFT_ORANGE);
  tft.fillRect(x + 4, ground - 25, 28, 26, TFT_DARKGREY);
  tft.drawRect(x + 4, ground - 25, 28, 26, TFT_LIGHTGREY);
  tft.fillRect(x + 8, ground - 19, 6, 6, TFT_YELLOW);
  tft.fillRect(x + 18, ground - 13, 8, 14, TFT_BROWN);
}

static void drawBar(uint8_t percent) {
  int fill = otaBarFill(percent);
  tft.fillRect(OTA_BAR_X + 2, OTA_BAR_Y + 2, fill, OTA_BAR_H - 4, TFT_GREEN);
  tft.fillRect(OTA_BAR_X + 2 + fill, OTA_BAR_Y + 2, OTA_BAR_W - 4 - fill, OTA_BAR_H - 4, TFT_DARKGREY);
  char text[6];
  snprintf(text, sizeof(text), "%3u%%", (unsigned)percent);
  tft.setTextSize(2);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setCursor(OTA_BAR_X + OTA_BAR_W + 10, OTA_BAR_Y + 2);
  tft.print(text);
}

static void printCentered(const char* text, int y) {
  tft.setCursor((320 - tft.textWidth(text)) / 2, y);
  tft.print(text);
}

void otaAnimationBegin() {
  ota.begin(millis());
  drawnPose = nullptr;
  // Keep the screen readable at night: the existing touch boost, which
  // changes no saved setting and ends normally once loop() resumes.
  backlightAcceptedTouch(millis());
  updateBacklight();

  tft.fillScreen(TFT_BLACK);
  drawTitleBar("UPDATING FIRMWARE");
  tft.drawFastHLine(8, OTA_GROUND_Y + 2, 304, TFT_DARKGREY);
  drawHome();
  tft.drawRoundRect(OTA_BAR_X, OTA_BAR_Y, OTA_BAR_W, OTA_BAR_H, 4, TFT_DARKGREY);
  drawBar(0);
  tft.setTextSize(2);
  tft.setTextColor(TFT_YELLOW, TFT_BLACK);
  printCentered("Do not power off", 192);
  showWalker(otaWalkerX(0), WALK_CYCLE[0]);
}

void otaAnimationProgress(uint32_t progress, uint32_t total) {
  bool moved = ota.setProgress(progress, total);
  if (moved) drawBar(ota.percent);
  // The walker advances with progress but only on frame ticks, so the
  // stride (limbs) and the position change together at ~7 fps.
  if (ota.tick(millis())) showWalker(otaWalkerX(ota.percent), WALK_CYCLE[ota.frame]);
}

void otaAnimationComplete() {
  ota.complete();
  drawBar(100);
  showWalker(otaWalkerX(100), WALKER_STANDING);
  tft.fillRect(0, 186, 320, 54, TFT_BLACK);
  tft.setTextSize(2);
  tft.setTextColor(TFT_GREEN, TFT_BLACK);
  printCentered("UPDATE COMPLETE", 192);
  tft.setTextSize(1);
  tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  printCentered("Restarting...", 218);
}

void otaAnimationError(int errorCode, const char* detail) {
  uint8_t reached = ota.percent;
  bool running = ota.screen == OtaScreen::Running;
  if (!ota.fail(errorCode, millis())) return;
  drawnPose = nullptr;

  tft.fillScreen(TFT_BLACK);
  tft.fillRect(0, 0, 320, 36, TFT_RED);
  tft.setTextSize(2);
  tft.setTextColor(TFT_WHITE, TFT_RED);
  tft.setCursor(10, 10);
  tft.print("UPDATE FAILED");

  tft.setTextColor(TFT_RED, TFT_BLACK);
  printCentered(otaErrorText(errorCode), 62);

  char line[64];
  tft.setTextSize(1);
  tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  if (running) snprintf(line, sizeof(line), "OTA error %d at %u%%", errorCode, (unsigned)reached);
  else snprintf(line, sizeof(line), "OTA error %d", errorCode);
  printCentered(line, 96);
  if (detail && detail[0]) {
    snprintf(line, sizeof(line), "%.50s", detail);
    printCentered(line, 112);
  }
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  printCentered("The current firmware is still installed.", 140);
  printCentered("Retry the upload from the computer.", 156);
  tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
  printCentered("Returning to the dashboard in 10 s", 200);
}

bool otaAnimationService() {
  if (ota.screen != OtaScreen::Error) return false;
  if (ota.holdError(millis())) return true;
  // Hold over: give the display back and avoid an instant screensaver.
  app.lastInteraction = millis();
  drawCurrentPage();
  return false;
}
