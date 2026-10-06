#include <Arduino.h>
#include <WiFi.h>
#include "SettingsScreen.h"
#include "AppState.h"
#include "BluetoothControl.h"
#include "Display.h"
#include "DisplaySettingsScreen.h"
#include "OtaManager.h"
#include "PageRouter.h"
#include "PowerManager.h"
#include "SettingsStore.h"
#include "SettingsUi.h"
#include "UiHelpers.h"
#include "NetworkManager.h"
#include "WifiProvisioning.h"
#include "WifiSettingsScreen.h"

static SettingsView view = SettingsView::Menu;
static ConfirmAction pendingConfirm = ConfirmAction::None;
static SettingsView confirmReturn = SettingsView::Menu;

// --- Shared row widgets -----------------------------------------------------------

static int rowY(int row) {
  return ROW_Y0 + row * ROW_PITCH;
}

void drawSettingsFrame(const char* title, const char* backLabel) {
  tft.fillScreen(TFT_BLACK);
  drawHeader(title);
  drawBackBar(nullptr, backLabel, nullptr);
}

void drawSettingValue(int row, const char* value) {
  int y = rowY(row);
  tft.fillRect(110, y, BUTTON_X - 112, ROW_H, TFT_BLACK);
  tft.setTextSize(2);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setCursor(110, y + 9);
  tft.print(value);
}

void drawSettingRow(int row, const char* label, const char* value) {
  int y = rowY(row);
  tft.setTextSize(1);
  tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  tft.setCursor(14, y + 12);
  tft.print(label);
  if (value) drawSettingValue(row, value);
}

void drawSettingToggle(int row, bool on) {
  int y = rowY(row);
  uint16_t fill = on ? TFT_BLUE : TFT_DARKGREY;
  tft.fillRoundRect(BUTTON_X, y, 95, ROW_H, 8, fill);
  tft.drawRoundRect(BUTTON_X, y, 95, ROW_H, 8, TFT_LIGHTGREY);
  tft.setTextSize(2);
  tft.setTextColor(TFT_WHITE, fill);
  const char* label = on ? "ON" : "OFF";
  tft.setCursor(BUTTON_X + (95 - tft.textWidth(label)) / 2, y + 9);
  tft.print(label);
}

void drawSettingSteppers(int row) {
  int y = rowY(row);
  drawMenuButton(BUTTON_X, y, 45, ROW_H, "-");
  drawMenuButton(BUTTON_SPLIT + 3, y, 44, ROW_H, "+");
}

void drawInfoValue(int y, const char* value) {
  tft.fillRect(110, y, 210, 10, TFT_BLACK);
  tft.setTextSize(1);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setCursor(110, y);
  tft.print(value);
}

void drawInfoLine(int y, const char* label, const char* value) {
  tft.setTextSize(1);
  tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  tft.setCursor(14, y);
  tft.print(label);
  drawInfoValue(y, value);
}

// --- Menu ---------------------------------------------------------------------------

static void drawSettingsMenu() {
  static const char* labels[4][2] = {
    {"WI-FI", "DISPLAY"},
    {"SCREENSAVER", "BLUETOOTH"},
    {"WALLPAPER", "DEVICE INFO"},
    {"RESTART", "SLEEP"},
  };
  tft.fillScreen(TFT_BLACK);
  drawHeader("SETTINGS");
  for (int row = 0; row < 4; row++) {
    for (int col = 0; col < 2; col++) {
      drawMenuButton(MENU_X[col], MENU_Y0 + row * MENU_PITCH, MENU_W, MENU_H, labels[row][col]);
    }
  }
  drawBackBar(nullptr, "BACK", nullptr);
}

static void handleMenuTouch(int x, int y) {
  switch (settingsMenuItemAt(x, y)) {
    case SettingsItem::Wifi: showSettingsView(SettingsView::Wifi); break;
    case SettingsItem::Display: showSettingsView(SettingsView::Display); break;
    case SettingsItem::Screensaver: showSettingsView(SettingsView::Screensaver); break;
    case SettingsItem::Bluetooth: showSettingsView(SettingsView::Bluetooth); break;
    case SettingsItem::Wallpaper: showSettingsView(SettingsView::Wallpaper); break;
    case SettingsItem::Info: showSettingsView(SettingsView::Info); break;
    case SettingsItem::Restart: askConfirm(ConfirmAction::Restart, SettingsView::Menu); break;
    case SettingsItem::Sleep: askConfirm(ConfirmAction::Sleep, SettingsView::Menu); break;
    default: break;
  }
}

// --- Device info --------------------------------------------------------------------

static constexpr uint32_t INFO_REFRESH_MS = 2000;
static unsigned long infoLastRefresh = 0;
static const int INFO_Y0 = 46;
static const int INFO_PITCH = 16;

enum InfoLine { INFO_DEVICE, INFO_SSID, INFO_IP, INFO_SIGNAL, INFO_UPTIME, INFO_HEAP, INFO_MIN_HEAP,
                INFO_FLASH, INFO_CHIP, INFO_SDK, INFO_LINES };

static int infoY(int line) {
  return INFO_Y0 + line * INFO_PITCH;
}

// Values that change while the page is open.
static void drawInfoValues() {
  char text[40];
  bool connected = WiFi.status() == WL_CONNECTED;
  drawInfoValue(infoY(INFO_SSID), connected ? WiFi.SSID().c_str() : "(not connected)");
  drawInfoValue(infoY(INFO_IP), connected ? WiFi.localIP().toString().c_str() : "--");
  formatSignal(text, sizeof(text), connected, WiFi.RSSI());
  drawInfoValue(infoY(INFO_SIGNAL), text);
  formatUptime(text, sizeof(text), millis() / 1000);
  drawInfoValue(infoY(INFO_UPTIME), text);
  formatKilobytes(text, sizeof(text), ESP.getFreeHeap());
  drawInfoValue(infoY(INFO_HEAP), text);
  formatKilobytes(text, sizeof(text), ESP.getMinFreeHeap());
  drawInfoValue(infoY(INFO_MIN_HEAP), text);
  infoLastRefresh = millis();
}

static void drawDeviceInfo() {
  static const char* labels[INFO_LINES] = {
    "Device", "Wi-Fi SSID", "IP address", "Signal", "Uptime", "Free heap", "Min free heap",
    "Flash size", "Chip", "SDK"
  };
  drawSettingsFrame("DEVICE INFO");
  for (int i = 0; i < INFO_LINES; i++) drawInfoLine(infoY(i), labels[i], "");
  char text[40];
  drawInfoValue(infoY(INFO_DEVICE), DEVICE_HOSTNAME);
  snprintf(text, sizeof(text), "%u MB", unsigned(ESP.getFlashChipSize() / (1024 * 1024)));
  drawInfoValue(infoY(INFO_FLASH), text);
  snprintf(text, sizeof(text), "%s rev %u, %u MHz", ESP.getChipModel(), unsigned(ESP.getChipRevision()),
           unsigned(ESP.getCpuFreqMHz()));
  drawInfoValue(infoY(INFO_CHIP), text);
  drawInfoValue(infoY(INFO_SDK), ESP.getSdkVersion());
  drawInfoValues();
}

// --- Bluetooth ------------------------------------------------------------------------

static void drawBluetoothStatus() {
  char text[40];
  drawInfoValue(124, bluetoothControllerEnabled() ? "Controller enabled"
                     : bluetoothReservedAtBoot() ? "Controller failed to start" : "Off (not initialized)");
  drawInfoValue(140, bluetoothRestartPending() ? "Restart required to apply" : "Applied");
  formatKilobytes(text, sizeof(text), ESP.getFreeHeap());
  drawInfoValue(156, text);
}

static void drawBluetoothSettings() {
  drawSettingsFrame("BLUETOOTH");
  if (!BLUETOOTH_SUPPORTED) {
    tft.setTextSize(1);
    tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    tft.setCursor(14, 60);
    tft.print("Bluetooth support is disabled in this build.");
    tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
    tft.setCursor(14, 80);
    tft.print("Rebuild with -D DASHBOARD_BLUETOOTH=1 to enable.");
    return;
  }
  drawSettingRow(0, "BLUETOOTH", nullptr);
  drawSettingToggle(0, settings().bluetoothEnabled);
  tft.setTextSize(1);
  tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
  tft.setCursor(14, 88);
  tft.print("Applies after restart. No Bluetooth services yet;");
  tft.setCursor(14, 100);
  tft.print("ON reserves controller RAM for future features.");
  drawInfoLine(124, "Status", "");
  drawInfoLine(140, "Setting", "");
  drawInfoLine(156, "Free heap", "");
  drawBluetoothStatus();
}

static void handleBluetoothTouch(int x, int y) {
  if (!BLUETOOTH_SUPPORTED) return;
  RowHit hit = settingsRowAt(x, y, true);
  if (hit.row != 0) return;
  editSettings().bluetoothEnabled = !settings().bluetoothEnabled;
  settingsChanged();
  flushSettings(); // The next boot must see it, even after an unplanned reset.
  if (bluetoothRestartPending()) askConfirm(ConfirmAction::ApplyBluetooth, SettingsView::Bluetooth);
  else drawBluetoothSettings();
}

// --- Sleeping -------------------------------------------------------------------------

static void drawSleeping() {
  tft.fillScreen(TFT_BLACK);
  tft.setTextSize(2);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setCursor((320 - tft.textWidth("SLEEPING")) / 2, 80);
  tft.print("SLEEPING");
  tft.setTextSize(1);
  tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  const char* lines[] = {"Lift your finger to continue.", "Wake: tap screen if supported, or press RST."};
  for (int i = 0; i < 2; i++) {
    tft.setCursor((320 - tft.textWidth(lines[i])) / 2, 120 + i * 16);
    tft.print(lines[i]);
  }
}

// --- Confirmation -------------------------------------------------------------------

static void drawConfirm() {
  ConfirmText text = confirmText(pendingConfirm);
  tft.fillScreen(TFT_BLACK);
  tft.setTextSize(2);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setCursor((320 - tft.textWidth(text.title)) / 2, 50);
  tft.print(text.title);
  tft.setTextSize(1);
  tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  tft.setCursor((320 - tft.textWidth(text.line1)) / 2, 90);
  tft.print(text.line1);
  tft.setCursor((320 - tft.textWidth(text.line2)) / 2, 106);
  tft.print(text.line2);
  drawMenuButton(15, 150, 140, 50, "CANCEL");
  tft.fillRoundRect(165, 150, 140, 50, 8, TFT_RED);
  tft.drawRoundRect(165, 150, 140, 50, 8, TFT_LIGHTGREY);
  tft.setTextColor(TFT_WHITE, TFT_RED);
  tft.setCursor(165 + (140 - tft.textWidth(text.confirmLabel)) / 2, 172);
  tft.print(text.confirmLabel);
}

static void runConfirmed(ConfirmAction action) {
  switch (action) {
    case ConfirmAction::Restart:
    case ConfirmAction::ApplyBluetooth:
      restartDevice();
      break;
    case ConfirmAction::Sleep:
      showSettingsView(SettingsView::Sleeping);
      beginSleep();
      break;
    case ConfirmAction::StartWifiSetup:
      startWifiSetup();
      showSettingsView(SettingsView::WifiSetup); // Shows a failure message if it did not start.
      break;
    case ConfirmAction::ForgetWifi: {
      WifiCredentials builtIn;
      builtInWifiCredentials(builtIn);
      requestWifiSwitch(builtIn, WifiSource::BuiltIn);
      memset(&builtIn, 0, sizeof(builtIn));
      showSettingsView(SettingsView::Wifi);
      break;
    }
    default:
      showSettingsView(confirmReturn);
      break;
  }
}

static void handleConfirmTouch(int x, int y) {
  ConfirmChoice choice = confirmChoiceAt(x, y);
  if (choice == ConfirmChoice::None) return;
  ConfirmAction action = pendingConfirm;
  pendingConfirm = ConfirmAction::None;
  if (choice == ConfirmChoice::Cancel) showSettingsView(confirmReturn);
  else runConfirmed(action);
}

void askConfirm(ConfirmAction action, SettingsView returnTo) {
  pendingConfirm = action;
  confirmReturn = returnTo;
  showSettingsView(SettingsView::Confirm);
}

// --- Routing ------------------------------------------------------------------------

void openSettings() {
  view = SettingsView::Menu;
  showPage(PAGE_SETTINGS);
}

void showSettingsView(SettingsView next) {
  view = next;
  if (app.currentPage == PAGE_SETTINGS) drawSettingsPage();
}

void drawSettingsPage() {
  app.currentPage = PAGE_SETTINGS;
  switch (view) {
    case SettingsView::Confirm: drawConfirm(); break;
    case SettingsView::Display: drawDisplaySettings(); break;
    case SettingsView::Screensaver: drawScreensaverSettings(); break;
    case SettingsView::Info: drawDeviceInfo(); break;
    case SettingsView::Wifi: drawWifiSettings(); break;
    case SettingsView::Bluetooth: drawBluetoothSettings(); break;
    case SettingsView::Sleeping: drawSleeping(); break;
    case SettingsView::Wallpaper: drawWallpaperSettings(); break;
    case SettingsView::WifiSetup: drawWifiSetup(); break;
    default: view = SettingsView::Menu; drawSettingsMenu(); break;
  }
}

static void goBack() {
  flushSettings();
  if (settingsBackLeaves(view)) showPage(PAGE_MORE);
  else showSettingsView(SettingsView::Menu);
}

void handleSettingsTouch(int x, int y) {
  switch (view) {
    case SettingsView::Confirm: handleConfirmTouch(x, y); return;
    case SettingsView::WifiSetup: handleWifiSetupTouch(x, y); return;
    case SettingsView::Sleeping: return; // Touches only delay sleep until release.
    case SettingsView::Wallpaper:
      if (handleWallpaperSettingsTouch(x, y)) return;
      break;
    default: break;
  }
  if (y >= BACK_BAR_Y) {
    goBack();
    return;
  }
  switch (view) {
    case SettingsView::Menu: handleMenuTouch(x, y); break;
    case SettingsView::Display: handleDisplaySettingsTouch(x, y); break;
    case SettingsView::Screensaver: handleScreensaverSettingsTouch(x, y); break;
    case SettingsView::Wifi: handleWifiSettingsTouch(x, y); break;
    case SettingsView::Bluetooth: handleBluetoothTouch(x, y); break;
    default: break;
  }
}

void updateSettings() {
  serviceSettingsStore();
  updateWifiSwitch();
  updateWifiSetup();
  updatePower();
  if (app.currentPage != PAGE_SETTINGS) return;
  if (view == SettingsView::Info && millis() - infoLastRefresh >= INFO_REFRESH_MS) drawInfoValues();
  if (view == SettingsView::Wifi) updateWifiSettings();
  if (view == SettingsView::WifiSetup) updateWifiSetupView();
  if (view == SettingsView::Wallpaper) updateWallpaperSettings();
}

bool settingsBlocksScreensaver() {
  if (app.currentPage != PAGE_SETTINGS) return false;
  return view == SettingsView::Confirm || view == SettingsView::WifiSetup || view == SettingsView::Sleeping;
}
