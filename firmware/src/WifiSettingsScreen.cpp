#include <Arduino.h>
#include <WiFi.h>
#include "WifiSettingsScreen.h"
#include "AppState.h"
#include "DeviceConfig.h"
#include "DirectWeather.h"
#include "LocalTime.h"
#include "LocationStore.h"
#include "LocationUiLogic.h"
#include "PostalCodeScreen.h"
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

// --- Location (optional) ------------------------------------------------------------------

enum LocationLine { LOC_PLACE, LOC_REGION, LOC_TIME, LOC_WEATHER, LOC_LINES };
static uint32_t drawnLocationRevision = 0;
static constexpr size_t VALUE_CHARS = 35; // x 110..320 at size 1.

// Values on one line: cut (at a UTF-8 boundary) to the value column.
static void drawLocationValue(int line, const char* value) {
  char text[VALUE_CHARS + 1];
  size_t length = strlen(value);
  if (length > VALUE_CHARS) {
    length = VALUE_CHARS;
    while (length > 0 && (((unsigned char)value[length]) & 0xC0) == 0x80) length--;
  }
  memcpy(text, value, length);
  text[length] = '\0';
  drawInfoValue(lineY(line), text);
}

static void drawLocationValues() {
  Location l;
  bool configured = currentLocation(l);
  char text[48];
  if (!configured) {
    drawLocationValue(LOC_TIME, hasUtcTime() ? "Needs a location" : "Waiting for network time");
    drawLocationValue(LOC_WEATHER, weatherSource() == WeatherSource::Backend ? "From the HomeDeck Backend"
                                                                              : "Needs a location");
  } else {
    if (l.offsetKnown) {
      formatUtcOffset(l.utcOffset, text, sizeof(text));
      if (l.timezone[0]) snprintf(text + strlen(text), sizeof(text) - strlen(text), " %s", l.timezone);
    } else {
      snprintf(text, sizeof(text), "Getting local time...");
    }
    drawLocationValue(LOC_TIME, text);
    DirectWeatherInfo info;
    directWeatherInfo(info);
    if (info.haveData) snprintf(text, sizeof(text), "Updated %u min ago%s", unsigned(info.ageMs / 60000),
                                info.failed ? " (retrying)" : "");
    else snprintf(text, sizeof(text), "%s", info.failed ? "Unavailable, retrying" : "Loading...");
    drawLocationValue(LOC_WEATHER, text);
  }
  lastRefresh = millis();
}

void drawLocationSettings() {
  static const char* labels[LOC_LINES] = {"Location", "Region", "Time", "Weather"};
  drawSettingsFrame("LOCATION");
  for (int i = 0; i < LOC_LINES; i++) drawInfoLine(lineY(i), labels[i], "");
  Location l;
  bool configured = currentLocation(l);
  drawnLocationRevision = locationRevision();
  char region[2 * LOCATION_REGION_MAX + 4] = "--";
  if (configured && (l.admin1[0] || l.country[0])) {
    snprintf(region, sizeof(region), "%s%s%s", l.admin1, l.admin1[0] && l.country[0] ? ", " : "", l.country);
  }
  drawLocationValue(LOC_PLACE, configured ? l.name : "Not configured");
  drawLocationValue(LOC_REGION, region);
  drawLocationValues();
  // POSTAL CODE: the keypad on the device. SEARCH ON PHONE: the setup
  // hotspot's location page (towns, letters in postal codes).
  drawMenuButton(LOCATION_LEFT_X, LOCATION_BUTTON_Y, LOCATION_BUTTON_W, LOCATION_BUTTON_H, "POSTAL CODE");
  drawMenuButton(LOCATION_RIGHT_X, LOCATION_BUTTON_Y, LOCATION_BUTTON_W, LOCATION_BUTTON_H, "SEARCH ON PHONE");
  if (configured) {
    drawMenuButton(LOCATION_RIGHT_X, LOCATION_REMOVE_Y, LOCATION_BUTTON_W, LOCATION_REMOVE_H, "REMOVE");
  } else {
    tft.setTextSize(1);
    tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
    tft.setCursor(14, 178);
    tft.print("Set your location for local time and weather.");
  }
}

void handleLocationSettingsTouch(int x, int y) {
  switch (locationButtonAt(x, y, locationConfigured())) {
    case LocationButton::PostalCode:
      openPostalCode(); // On the device: no hotspot.
      break;
    case LocationButton::SearchOnPhone:
      askConfirm(ConfirmAction::StartLocationSetup, SettingsView::Location);
      break;
    case LocationButton::Remove:
      askConfirm(ConfirmAction::RemoveLocation, SettingsView::Location);
      break;
    case LocationButton::None:
      break;
  }
}

void updateLocationSettings() {
  if (locationRevision() != drawnLocationRevision) drawLocationSettings();
  else if (millis() - lastRefresh >= REFRESH_MS) drawLocationValues();
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
    case WifiSetupPhase::LocationSaved:
      snprintf(line, sizeof(line), "Location saved.");
      second = "Hotspot closes automatically.";
      color = TFT_GREEN;
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

// Where the hotspot view returns: the page that opened it.
static SettingsView setupReturnView() {
  return wifiSetupForLocation() ? SettingsView::Location : SettingsView::Wifi;
}

void handleWifiSetupTouch(int, int y) {
  if (y < BACK_BAR_Y) return;
  // STOP SETUP closes the hotspot; the view returns to the page that opened
  // it once it has closed (updateWifiSetupView).
  if (wifiSetupRunning()) stopWifiSetup();
  else showSettingsView(setupReturnView());
}

void updateWifiSetupView() {
  if (wifiSetupPhase() == WifiSetupPhase::Off) {
    showSettingsView(setupReturnView());
    return;
  }
  if (wifiSetupRevision() != drawnRevision) drawSetupStatus(STATUS_Y);
}
