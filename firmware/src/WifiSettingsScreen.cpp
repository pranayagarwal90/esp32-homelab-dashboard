#include <Arduino.h>
#include <WiFi.h>
#include "WifiSettingsScreen.h"
#include "Display.h"
#include "NetworkManager.h"
#include "SettingsScreen.h"
#include "SettingsUi.h"
#include "UiHelpers.h"
#include "WifiProvisioning.h"

static constexpr uint32_t REFRESH_MS = 2000;
static unsigned long lastRefresh = 0;

enum WifiLine { LINE_STATUS, LINE_SSID, LINE_IP, LINE_SIGNAL, LINE_SOURCE, WIFI_LINES };
static const int LINE_Y0 = 50;
static const int LINE_PITCH = 18;
static const int BUTTON_Y = 150;
static const int BUTTON_H = 46;

static int lineY(int line) {
  return LINE_Y0 + line * LINE_PITCH;
}

static void drawWifiValues() {
  bool connected = WiFi.status() == WL_CONNECTED;
  WifiSwitchState switching = wifiSwitchState();
  const char* status = switching == WifiSwitchState::Connecting || switching == WifiSwitchState::Pending
                       ? "Connecting..." : connected ? "Connected" : "Disconnected";
  char signal[24];
  formatSignal(signal, sizeof(signal), connected, WiFi.RSSI());
  drawInfoValue(lineY(LINE_STATUS), status);
  drawInfoValue(lineY(LINE_SSID), connected ? WiFi.SSID().c_str() : "--");
  drawInfoValue(lineY(LINE_IP), connected ? WiFi.localIP().toString().c_str() : "--");
  drawInfoValue(lineY(LINE_SIGNAL), signal);
  drawInfoValue(lineY(LINE_SOURCE), wifiSourceLabel(activeWifiSource()));
  lastRefresh = millis();
}

void drawWifiSettings() {
  static const char* labels[WIFI_LINES] = {"Status", "SSID", "IP address", "Signal", "Network"};
  drawSettingsFrame("WI-FI");
  for (int i = 0; i < WIFI_LINES; i++) drawInfoLine(lineY(i), labels[i], "");
  drawWifiValues();
  drawMenuButton(15, BUTTON_Y, 140, BUTTON_H, "CHANGE WI-FI");
  if (activeWifiSource() == WifiSource::Saved) drawMenuButton(165, BUTTON_Y, 140, BUTTON_H, "USE BUILT-IN");
}

void handleWifiSettingsTouch(int x, int y) {
  if (y < BUTTON_Y || y > BUTTON_Y + BUTTON_H) return;
  if (x >= 15 && x < 155) askConfirm(ConfirmAction::StartWifiSetup, SettingsView::Wifi);
  else if (x >= 165 && x < 305 && activeWifiSource() == WifiSource::Saved) {
    askConfirm(ConfirmAction::ForgetWifi, SettingsView::Wifi);
  }
}

void updateWifiSettings() {
  if (millis() - lastRefresh >= REFRESH_MS) drawWifiValues();
}

// --- Setup hotspot view ----------------------------------------------------------------

static uint32_t drawnRevision = 0;
static const int STATUS_Y = 128;

static void drawSetupStatus() {
  char line[64];
  const char* second = "";
  uint16_t color = TFT_LIGHTGREY;
  const char* ssid = wifiSetupTarget();
  switch (wifiSetupPhase()) {
    case WifiSetupPhase::Waiting:
      snprintf(line, sizeof(line), "Waiting for your phone...");
      break;
    case WifiSetupPhase::Connecting:
      snprintf(line, sizeof(line), "Connecting to %s...", ssid);
      color = TFT_YELLOW;
      break;
    case WifiSetupPhase::Connected:
      snprintf(line, sizeof(line), "Connected to %s. Saved.", ssid);
      second = "Hotspot closes automatically.";
      color = TFT_GREEN;
      break;
    case WifiSetupPhase::Failed:
      snprintf(line, sizeof(line), "Could not join %s.", ssid);
      second = "Previous network restored. Try again.";
      color = TFT_RED;
      break;
    case WifiSetupPhase::Stopping:
      snprintf(line, sizeof(line), "Closing hotspot...");
      break;
    case WifiSetupPhase::StartFailed:
      snprintf(line, sizeof(line), "Could not start the setup hotspot.");
      color = TFT_RED;
      break;
    case WifiSetupPhase::Off:
      snprintf(line, sizeof(line), "Setup stopped.");
      break;
  }
  tft.fillRect(0, STATUS_Y, 320, 36, TFT_BLACK);
  tft.setTextSize(1);
  tft.setTextColor(color, TFT_BLACK);
  tft.setCursor(14, STATUS_Y);
  tft.print(line);
  tft.setCursor(14, STATUS_Y + 16);
  tft.print(second);
  drawnRevision = wifiSetupRevision();
}

void drawWifiSetup() {
  bool failed = wifiSetupPhase() == WifiSetupPhase::StartFailed;
  drawSettingsFrame("WI-FI SETUP", failed ? "BACK" : "STOP SETUP");
  if (!failed) {
    tft.setTextSize(1);
    tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    tft.setCursor(14, 46);
    tft.print("1. On your phone, join this Wi-Fi:");
    tft.setTextSize(2);
    tft.setTextColor(TFT_CYAN, TFT_BLACK);
    tft.setCursor(14, 60);
    tft.print(wifiSetupApSsid());
    tft.setTextSize(1);
    tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    tft.setCursor(14, 86);
    tft.print("Password:");
    tft.setTextSize(2);
    tft.setTextColor(TFT_CYAN, TFT_BLACK);
    tft.setCursor(80, 82);
    tft.print(wifiSetupApPassword());
    tft.setTextSize(1);
    tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    tft.setCursor(14, 108);
    tft.print("2. Open http://");
    tft.print(WiFi.softAPIP().toString());
  }
  drawSetupStatus();
}

void handleWifiSetupTouch(int, int y) {
  if (y < BACK_BAR_Y) return;
  // STOP SETUP closes the hotspot; the view returns to Wi-Fi status once it
  // has closed (updateWifiSetupView).
  if (wifiSetupRunning()) stopWifiSetup();
  else showSettingsView(SettingsView::Wifi);
}

void updateWifiSetupView() {
  if (wifiSetupPhase() == WifiSetupPhase::Off) {
    showSettingsView(SettingsView::Wifi);
    return;
  }
  if (wifiSetupRevision() != drawnRevision) drawSetupStatus();
}
