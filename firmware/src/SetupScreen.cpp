#include <Arduino.h>
#include "SetupScreen.h"
#include "AppState.h"
#include "Display.h"
#include "PageRouter.h"
#include "PowerManager.h"
#include "SettingsLogic.h"
#include "UiHelpers.h"
#include "WifiProvisioning.h"
#include "WifiSettingsScreen.h"

static constexpr uint32_t START_FAILED_RETRY_MS = 60000;
static const int STATUS_Y = 170;

static SetupReason reason = SetupReason::None;
static bool active = false;
static uint32_t drawnRevision = 0;
static unsigned long enteredAt = 0;
static bool restarting = false;

void enterSetupMode(SetupReason why) {
  reason = why;
  active = true;
  enteredAt = millis();
  Serial.printf("Setup Mode: %s\n", setupReasonText(reason));
  startWifiSetup(true);
  showPage(PAGE_SETUP);
}

bool setupModeActive() {
  return active;
}

void drawSetupPage() {
  app.currentPage = PAGE_SETUP;
  tft.fillScreen(TFT_BLACK);
  // Own header: the dashboard's LIVE / OFFLINE does not apply here.
  tft.fillRect(0, 0, 320, 36, TFT_DARKGREY);
  tft.setTextSize(2);
  tft.setTextColor(TFT_WHITE, TFT_DARKGREY);
  tft.setCursor(10, 10);
  tft.print("SETUP REQUIRED");
  tft.setTextSize(1);
  tft.setTextColor(TFT_YELLOW, TFT_BLACK);
  tft.setCursor(14, 44);
  tft.print(setupReasonText(reason));
  if (wifiSetupPhase() != WifiSetupPhase::StartFailed) {
    drawSetupInstructions(62);
    tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
    tft.setCursor(14, 148);
    tft.print("3. Enter Wi-Fi and the dashboard server.");
  }
  drawSetupStatus(STATUS_Y);
  drawnRevision = wifiSetupRevision();
  drawBackBar(nullptr, "RESTART", nullptr);
}

void handleSetupTouch(int, int y) {
  if (y < BACK_BAR_Y || restarting) return;
  restarting = true;
  restartDevice(); // Retry with whatever is saved.
}

void updateSetupMode() {
  updateWifiSetup();
  if (!active || restarting) return;
  if (wifiSetupPhase() == WifiSetupPhase::Restarting) restarting = true; // Saved: it restarts itself.
  if (wifiSetupRevision() != drawnRevision) {
    drawSetupStatus(STATUS_Y);
    drawnRevision = wifiSetupRevision();
  }
  bool idle = wifiSetupPhase() == WifiSetupPhase::Waiting || wifiSetupPhase() == WifiSetupPhase::Failed;
  bool retry = setupShouldRetry(reason, idle, wifiSetupIdleMs()) ||
               (wifiSetupPhase() == WifiSetupPhase::StartFailed && millis() - enteredAt >= START_FAILED_RETRY_MS);
  if (retry && !restarting) {
    Serial.println("Setup Mode: retrying the saved configuration");
    restarting = true;
    restartDevice();
  }
}
