#include <Arduino.h>
#include "UpdateScreen.h"
#include "AiLogic.h"
#include "AppState.h"
#include "Display.h"
#include "FirmwareUpdate.h"
#include "PageRouter.h"
#include "PowerManager.h"
#include "ProductIdentity.h"
#include "SettingsLogic.h"
#include "SettingsScreen.h"
#include "UiHelpers.h"

static constexpr uint32_t RESTART_DELAY_MS = 1500; // "Restarting..." stays readable.
static constexpr int BAR_X = 30;
static constexpr int BAR_Y = 150;
static constexpr int BAR_W = 260;
static constexpr int BAR_H = 14;
static constexpr int NOTES_Y = 106;
static constexpr int NOTES_LINES = 4;

enum class UpdateView : uint8_t { Prompt, Progress, Failed, Success };

static UpdateView view = UpdateView::Prompt;
static Page returnPage = PAGE_HOME;
static bool returnToFirmware = false;
static char prompted[UPDATE_VERSION_MAX] = "";   // Auto-prompted (or dismissed) this boot.
static char installedVersion[UPDATE_VERSION_MAX] = "";
static uint32_t drawnRevision = 0;
static UpdateState drawnState = UpdateState::Idle;
static uint8_t drawnPercent = 255;
static unsigned long restartAt = 0;
static bool restartScheduled = false;
static bool restartIssued = false;

static void printCenteredText(const char* text, int y, uint8_t size, uint16_t color) {
  tft.setTextSize(size);
  tft.setTextColor(color, TFT_BLACK);
  tft.setCursor((320 - tft.textWidth(text)) / 2, y);
  tft.print(text);
}

static void drawFrame() {
  tft.fillScreen(TFT_BLACK);
  drawTitleBar(PRODUCT_TITLE);
}

static void drawButton(int x, const char* label, uint16_t fill) {
  tft.fillRoundRect(x, PROMPT_BUTTON_Y, 140, PROMPT_BUTTON_H, 8, fill);
  tft.drawRoundRect(x, PROMPT_BUTTON_Y, 140, PROMPT_BUTTON_H, 8, TFT_LIGHTGREY);
  tft.setTextSize(2);
  tft.setTextColor(TFT_WHITE, fill);
  tft.setCursor(x + (140 - tft.textWidth(label)) / 2, PROMPT_BUTTON_Y + 14);
  tft.print(label);
}

static void drawPrompt(const UpdateStatus& s) {
  drawFrame();
  printCenteredText("UPDATE AVAILABLE", 50, 2, TFT_WHITE);
  char text[32];
  snprintf(text, sizeof(text), "Version %s", s.offer.version);
  printCenteredText(text, 76, 2, TFT_CYAN);
  const char* notes = s.offer.notes[0] ? s.offer.notes : "A new HomeDeck Mini firmware is ready.";
  AiLine lines[NOTES_LINES];
  int count = aiWrap(notes, AI_LINE_CHARS, NOTES_LINES, lines);
  tft.setTextSize(1);
  tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  for (int i = 0; i < count; i++) {
    char line[AI_LINE_CHARS + 4];
    snprintf(line, sizeof(line), "%.*s%s", lines[i].length, notes + lines[i].start, lines[i].ellipsis ? "..." : "");
    tft.setCursor((320 - tft.textWidth(line)) / 2, NOTES_Y + i * 12);
    tft.print(line);
  }
  drawButton(15, "INSTALL NOW", TFT_BLUE);
  drawButton(165, "LATER", TFT_DARKGREY);
}

static const char* progressText(UpdateState state) {
  switch (state) {
    case UpdateState::Installing: return "Verifying and installing...";
    case UpdateState::Restarting: return "Installed. Restarting...";
    default: return "Downloading...";
  }
}

static void drawProgressValues(const UpdateStatus& s) {
  if (s.state != drawnState) {
    tft.fillRect(0, 92, 320, 10, TFT_BLACK);
    printCenteredText(progressText(s.state), 92, 1, TFT_LIGHTGREY);
    drawnState = s.state;
  }
  if (s.percent != drawnPercent) {
    char text[8];
    snprintf(text, sizeof(text), "%u%%", unsigned(s.percent));
    tft.fillRect(100, 112, 120, 24, TFT_BLACK);
    printCenteredText(text, 112, 3, TFT_WHITE);
    int fill = (BAR_W - 4) * s.percent / 100;
    tft.fillRect(BAR_X + 2, BAR_Y + 2, fill, BAR_H - 4, TFT_CYAN);
    drawnPercent = s.percent;
  }
}

static void drawProgress(const UpdateStatus& s) {
  drawFrame();
  printCenteredText("UPDATING", 56, 2, TFT_WHITE);
  tft.drawRect(BAR_X, BAR_Y, BAR_W, BAR_H, TFT_LIGHTGREY);
  printCenteredText("Keep HomeDeck Mini plugged in.", 184, 1, TFT_DARKGREY);
  drawnState = UpdateState::Idle;
  drawnPercent = 255;
  drawProgressValues(s);
}

static void drawFailed(const UpdateStatus& s) {
  drawFrame();
  printCenteredText("UPDATE FAILED", 64, 2, TFT_RED);
  printCenteredText(updateErrorText(s.error), 104, 1, TFT_WHITE);
  printCenteredText("Your current firmware is unchanged.", 124, 1, TFT_LIGHTGREY);
  drawBackBar(nullptr, "BACK", nullptr);
}

static void drawSuccess() {
  drawFrame();
  printCenteredText("UPDATED SUCCESSFULLY", 70, 2, TFT_GREEN);
  char text[32];
  snprintf(text, sizeof(text), "Version %s", installedVersion);
  printCenteredText(text, 104, 2, TFT_WHITE);
  drawBackBar(nullptr, "CONTINUE", nullptr);
}

void drawUpdatePage() {
  app.currentPage = PAGE_UPDATE;
  UpdateStatus s;
  firmwareUpdateStatus(s);
  drawnRevision = s.revision;
  switch (view) {
    case UpdateView::Prompt: drawPrompt(s); break;
    case UpdateView::Progress: drawProgress(s); break;
    case UpdateView::Failed: drawFailed(s); break;
    case UpdateView::Success: drawSuccess(); break;
  }
}

static void leave() {
  app.lastInteraction = millis();
  if (returnToFirmware) openSettingsAt(SettingsView::Build);
  else showPage(returnPage);
}

static void openPrompt(Page from, bool fromFirmware, const char* version) {
  snprintf(prompted, sizeof(prompted), "%s", version);
  returnPage = from;
  returnToFirmware = fromFirmware;
  view = UpdateView::Prompt;
  app.lastInteraction = millis(); // A full screensaver timeout to notice it.
  showPage(PAGE_UPDATE);
}

void openUpdatePromptFromSettings() {
  UpdateStatus s;
  firmwareUpdateStatus(s);
  if (!s.hasOffer || updateBusy(s.state)) return;
  openPrompt(PAGE_SETTINGS, true, s.offer.version);
}

void showUpdateSuccess(const char* version) {
  snprintf(installedVersion, sizeof(installedVersion), "%s", version);
  returnPage = PAGE_HOME;
  returnToFirmware = false;
  view = UpdateView::Success;
  showPage(PAGE_UPDATE);
}

void handleUpdateTouch(int x, int y) {
  switch (view) {
    case UpdateView::Prompt:
      switch (promptChoiceAt(x, y)) {
        case PromptChoice::Install:
          if (requestUpdateInstall()) {
            view = UpdateView::Progress;
            drawUpdatePage();
          } else {
            leave(); // The offer is gone or something is running.
          }
          break;
        case PromptChoice::Later:
          Serial.println("[UPDATE] Later: not asking again until restart");
          leave();
          break;
        case PromptChoice::None:
          break;
      }
      break;
    case UpdateView::Failed:
    case UpdateView::Success:
      if (y >= BACK_BAR_Y) leave();
      break;
    case UpdateView::Progress:
      break; // No way out while installing.
  }
}

bool updateUpdateScreen() {
  serviceFirmwareUpdates();
  UpdateStatus s;
  firmwareUpdateStatus(s);

  if (updateInstalling(s.state)) {
    if (app.currentPage != PAGE_UPDATE || view != UpdateView::Progress) {
      view = UpdateView::Progress;
      showPage(PAGE_UPDATE);
    } else if (s.revision != drawnRevision) {
      drawnRevision = s.revision;
      drawProgressValues(s);
    }
    if (s.state == UpdateState::Restarting && !restartScheduled) {
      restartScheduled = true;
      restartAt = millis();
    }
    if (restartScheduled && !restartIssued && millis() - restartAt >= RESTART_DELAY_MS) {
      restartIssued = true;
      restartDevice();
    }
    return true;
  }

  if (app.currentPage == PAGE_UPDATE && view == UpdateView::Progress) {
    // The install ended without restarting: it failed.
    view = UpdateView::Failed;
    app.lastInteraction = millis();
    drawUpdatePage();
    return false;
  }
  if (app.currentPage == PAGE_UPDATE && view == UpdateView::Prompt && !s.hasOffer) {
    leave(); // A later check withdrew the offer.
    return false;
  }

  bool calm = app.currentPage == PAGE_HOME || app.currentPage == PAGE_MORE;
  if (updatePromptDue(s.state, s.offer.version, prompted, calm)) {
    Serial.printf("[UPDATE] Showing the update prompt for %s\n", s.offer.version);
    openPrompt(app.currentPage, false, s.offer.version);
  }
  return false;
}
