#include <Arduino.h>
#include "StopwatchScreen.h"
#include "AppState.h"
#include "Display.h"
#include "MenuScreens.h"
#include "PageRouter.h"
#include "Stopwatch.h"
#include "UiHelpers.h"

static constexpr uint32_t REFRESH_MS = 100;
static constexpr int TIME_Y = 46;

static Stopwatch stopwatch;
static char shownTime[12] = "";
static unsigned long lastRefresh = 0;

// Large time, redrawn only when the text changes. Glyph backgrounds overwrite
// the old digits; the line is cleared only when its width changes.
static void drawTime(bool force) {
  char text[12];
  formatStopwatch(stopwatch.elapsed(millis()), text, sizeof(text));
  if (!force && strcmp(text, shownTime) == 0) return;
  tft.setTextSize(4);
  if (force || strlen(text) != strlen(shownTime)) tft.fillRect(0, TIME_Y, 320, 32, TFT_BLACK);
  tft.setTextColor(stopwatch.running ? TFT_WHITE : TFT_LIGHTGREY, TFT_BLACK);
  tft.setCursor((320 - tft.textWidth(text)) / 2, TIME_Y);
  tft.print(text);
  strcpy(shownTime, text);
}

static void drawButtons() {
  const char* primary = stopwatch.running ? "PAUSE" : stopwatch.elapsed(millis()) ? "RESUME" : "START";
  drawMenuButton(STOPWATCH_BUTTON_X[0], STOPWATCH_BUTTON_Y, STOPWATCH_BUTTON_W, STOPWATCH_BUTTON_H, "RESET");
  drawMenuButton(STOPWATCH_BUTTON_X[1], STOPWATCH_BUTTON_Y, STOPWATCH_BUTTON_W, STOPWATCH_BUTTON_H, primary);
  drawMenuButton(STOPWATCH_BUTTON_X[2], STOPWATCH_BUTTON_Y, STOPWATCH_BUTTON_W, STOPWATCH_BUTTON_H,
                 stopwatch.lapCount >= STOPWATCH_MAX_LAPS ? "LAPS FULL" : "LAP");
}

static void drawLapCount() {
  char text[12];
  snprintf(text, sizeof(text), "LAPS %u", (unsigned)stopwatch.lapCount);
  drawTitleBarValue(text);
}

static void drawLap(int index) {
  char time[12], line[20];
  formatStopwatch(stopwatch.lapMs[index], time, sizeof(time));
  snprintf(line, sizeof(line), "%2d  %s", index + 1, time);
  tft.setTextSize(1);
  tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  tft.setCursor(index < 5 ? 40 : 190, STOPWATCH_LAP_Y0 + (index % 5) * STOPWATCH_LAP_PITCH);
  tft.print(line);
}

void drawStopwatchPage() {
  app.currentPage = PAGE_STOPWATCH;
  tft.fillScreen(TFT_BLACK);
  drawTitleBar("STOPWATCH");
  drawLapCount();
  drawTime(true);
  drawButtons();
  for (int i = 0; i < stopwatch.lapCount; i++) drawLap(i);
  drawBackBar(nullptr, "BACK", nullptr);
  lastRefresh = millis();
}

void handleStopwatchTouch(int x, int y) {
  uint32_t now = millis();
  switch (stopwatchHitAt(x, y)) {
    case StopwatchHit::Back:
      showMenuNode(menuParent(MenuNode::Stopwatch));
      return;
    case StopwatchHit::Reset:
      stopwatch.reset();
      drawStopwatchPage();
      return;
    case StopwatchHit::StartPause:
      stopwatch.toggle(now);
      drawTime(true);
      drawButtons();
      return;
    case StopwatchHit::Lap:
      if (stopwatch.lap(now)) {
        drawLap(stopwatch.lapCount - 1);
        drawLapCount();
        if (stopwatch.lapCount == STOPWATCH_MAX_LAPS) drawButtons();
      }
      return;
    case StopwatchHit::None:
      return;
  }
}

void updateStopwatch() {
  if (app.currentPage != PAGE_STOPWATCH || !stopwatch.running) return;
  if (millis() - lastRefresh < REFRESH_MS) return;
  lastRefresh = millis();
  drawTime(false);
}
