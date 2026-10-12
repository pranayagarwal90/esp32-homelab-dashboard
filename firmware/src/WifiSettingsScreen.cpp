#include <Arduino.h>
#include <WiFi.h>
#include "WifiSettingsScreen.h"
#include "AppState.h"
#include "DeviceConfig.h"
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

static bool canUseBuiltIn() {
  return hasBuiltInWifi() && activeWifiSource() == WifiSource::Saved;
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
  // Developer builds only: switch back to the built-in network.
  if (canUseBuiltIn()) drawMenuButton(165, BUTTON_Y, 140, BUTTON_H, "USE BUILT-IN");
}

void handleWifiSettingsTouch(int x, int y) {
  if (y < BUTTON_Y || y > BUTTON_Y + BUTTON_H) return;
  if (x >= 15 && x < 155) askConfirm(ConfirmAction::StartWifiSetup, SettingsView::Wifi);
  else if (x >= 165 && x < 305 && canUseBuiltIn()) askConfirm(ConfirmAction::ForgetWifi, SettingsView::Wifi);
}

void updateWifiSettings() {
  if (millis() - lastRefresh >= REFRESH_MS) drawWifiValues();
}

// --- HomeDeck Backend (optional) ------------------------------------------------------

enum ServerLine { SERVER_STATUS, SERVER_ADDRESS, SERVER_WIFI, SERVER_LINES };
static bool drawnServerOnline = false;
static bool drawnWifiConnected = false;

// Set up (none) or CHANGE + REMOVE (one is stored, even an invalid one).
static bool backendStored() {
  return backendConfig() != BackendConfig::NotConfigured;
}

static void drawServerValues() {
  bool connected = WiFi.status() == WL_CONNECTED;
  drawInfoValue(lineY(SERVER_STATUS), backendStatusText(backendConfig(), app.serverOnline, connected));
  drawInfoValue(lineY(SERVER_WIFI), connected ? "Connected" : "Disconnected");
  drawnServerOnline = app.serverOnline;
  drawnWifiConnected = connected;
  lastRefresh = millis();
}

void drawServerSettings() {
  static const char* labels[SERVER_LINES] = {"Status", "Address", "Wi-Fi"};
  drawSettingsFrame("HOMEDECK BACKEND");
  for (int i = 0; i < SERVER_LINES; i++) drawInfoLine(lineY(i), labels[i], "");
  char address[36];
  if (backendConfigured()) formatBackendForDisplay(backendUrl(), address, sizeof(address), 34);
  else snprintf(address, sizeof(address), "--");
  drawInfoValue(lineY(SERVER_ADDRESS), address);
  drawServerValues();
  tft.setTextSize(1);
  tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
  tft.setCursor(14, 112);
  tft.print(backendStored() ? "Change it from your phone (setup hotspot)," : "Optional: adds services, photos, AI, weather");
  tft.setCursor(14, 124);
  tft.print(backendStored() ? "or remove it to use HomeDeck on its own." : "and updates. Set up from your phone.");
  drawMenuButton(15, BUTTON_Y, 140, BUTTON_H, backendStored() ? "CHANGE" : "SET UP");
  if (backendStored()) drawMenuButton(165, BUTTON_Y, 140, BUTTON_H, "REMOVE");
}

void handleServerSettingsTouch(int x, int y) {
  if (y < BUTTON_Y || y > BUTTON_Y + BUTTON_H) return;
  if (x >= 15 && x < 155) askConfirm(ConfirmAction::StartWifiSetup, SettingsView::Server);
  else if (x >= 165 && x < 305 && backendStored()) askConfirm(ConfirmAction::RemoveBackend, SettingsView::Server);
}

void updateServerSettings() {
  bool changed = app.serverOnline != drawnServerOnline || (WiFi.status() == WL_CONNECTED) != drawnWifiConnected;
  if (changed || millis() - lastRefresh >= REFRESH_MS) drawServerValues();
}

// --- Setup hotspot view ----------------------------------------------------------------

static uint32_t drawnRevision = 0;
static const int STATUS_Y = 128;

void drawSetupStatus(int statusY) {
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
      second = "Nothing was saved. Try again.";
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
    case WifiSetupPhase::Restarting:
      snprintf(line, sizeof(line), "Saved. Restarting...");
      color = TFT_GREEN;
      second = wifiSetupServerCheck() == ServerCheck::Reachable ? "HomeDeck Backend answered."
               : wifiSetupServerCheck() == ServerCheck::NotReachable
               ? "HomeDeck Backend did not answer (saved anyway)." : "";
      if (wifiSetupServerCheck() == ServerCheck::NotReachable) color = TFT_YELLOW;
      break;
    case WifiSetupPhase::NoChange:
      snprintf(line, sizeof(line), "Nothing to change.");
      break;
    case WifiSetupPhase::SaveFailed:
      snprintf(line, sizeof(line), "Could not save the settings.");
      color = TFT_RED;
      break;
  }
  tft.fillRect(0, statusY, 320, 36, TFT_BLACK);
  tft.setTextSize(1);
  tft.setTextColor(color, TFT_BLACK);
  tft.setCursor(14, statusY);
  tft.print(line);
  tft.setCursor(14, statusY + 16);
  tft.print(second);
  drawnRevision = wifiSetupRevision();
}

void drawSetupInstructions(int y) {
  tft.setTextSize(1);
  tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  tft.setCursor(14, y);
  tft.print("1. On your phone, join this Wi-Fi:");
  tft.setTextSize(2);
  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  tft.setCursor(14, y + 14);
  tft.print(wifiSetupApSsid());
  tft.setTextSize(1);
  tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  tft.setCursor(14, y + 40);
  tft.print("Password:");
  tft.setTextSize(2);
  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  tft.setCursor(80, y + 36);
  tft.print(wifiSetupApPassword());
  tft.setTextSize(1);
  tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  tft.setCursor(14, y + 62);
  tft.print("2. Open http://");
  tft.print(WiFi.softAPIP().toString());
}

void drawWifiSetup() {
  bool failed = wifiSetupPhase() == WifiSetupPhase::StartFailed;
  drawSettingsFrame("SETUP HOTSPOT", failed ? "BACK" : "STOP SETUP");
  if (!failed) drawSetupInstructions(46);
  drawSetupStatus(STATUS_Y);
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
  if (wifiSetupRevision() != drawnRevision) drawSetupStatus(STATUS_Y);
}
