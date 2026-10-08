#pragma once
#include <stdint.h>
#include <stdio.h>

// Pure Settings navigation, hit-testing and formatting, shared with host tests.

enum class SettingsView : uint8_t {
  Menu,
  Wifi,
  WifiSetup,
  Display,
  Screensaver,
  Bluetooth,
  Wallpaper,
  Info,
  Confirm,
  Sleeping
};

// Settings menu: 2 columns x 4 rows of 140x36 buttons, BACK bar below.
enum class SettingsItem : uint8_t {
  None, Wifi, Display, Screensaver, Bluetooth, Wallpaper, Info, Restart, Sleep
};

constexpr int MENU_X[2] = {15, 165};
constexpr int MENU_W = 140;
constexpr int MENU_Y0 = 44;
constexpr int MENU_H = 36;
constexpr int MENU_PITCH = 41;
constexpr int BACK_BAR_Y = 205; // Same threshold as every other page.

inline SettingsItem settingsMenuItemAt(int x, int y) {
  static const SettingsItem grid[4][2] = {
    {SettingsItem::Wifi, SettingsItem::Display},
    {SettingsItem::Screensaver, SettingsItem::Bluetooth},
    {SettingsItem::Wallpaper, SettingsItem::Info},
    {SettingsItem::Restart, SettingsItem::Sleep},
  };
  if (y < MENU_Y0) return SettingsItem::None;
  int row = (y - MENU_Y0) / MENU_PITCH;
  if (row > 3 || (y - MENU_Y0) % MENU_PITCH >= MENU_H) return SettingsItem::None;
  for (int col = 0; col < 2; col++) {
    if (x >= MENU_X[col] && x < MENU_X[col] + MENU_W) return grid[row][col];
  }
  return SettingsItem::None;
}

// Sub-pages: up to 4 rows, 40 px apart, each with buttons on the right.
constexpr int ROW_Y0 = 44;
constexpr int ROW_PITCH = 40;
constexpr int ROW_H = 32;
constexpr int ROWS = 4;
constexpr int BUTTON_X = 205;      // Toggle: 205..300; minus: 205..250; plus: 256..300.
constexpr int BUTTON_SPLIT = 253;

enum class RowControl : uint8_t { None, Toggle, Minus, Plus };

struct RowHit {
  int row;            // -1 if none
  RowControl control;
};

inline RowHit settingsRowAt(int x, int y, bool toggleRow) {
  RowHit none = {-1, RowControl::None};
  if (y < ROW_Y0 || x < BUTTON_X - 5) return none;
  int row = (y - ROW_Y0) / ROW_PITCH;
  if (row >= ROWS || (y - ROW_Y0) % ROW_PITCH >= ROW_H + 4) return none;
  if (toggleRow) return {row, RowControl::Toggle};
  return {row, x < BUTTON_SPLIT ? RowControl::Minus : RowControl::Plus};
}

// BACK always goes one level up: sub-page -> Settings menu -> MORE page.
// Returns true when BACK leaves Settings entirely.
inline bool settingsBackLeaves(SettingsView view) {
  return view == SettingsView::Menu;
}

// --- Confirmation dialogs -------------------------------------------------------

enum class ConfirmAction : uint8_t { None, Restart, Sleep, StartWifiSetup, ForgetWifi, ApplyBluetooth };

struct ConfirmText {
  const char* title;
  const char* line1;
  const char* line2;
  const char* confirmLabel;
};

inline ConfirmText confirmText(ConfirmAction action) {
  switch (action) {
    case ConfirmAction::Restart:
      return {"RESTART DEVICE?", "The dashboard will reboot.", "", "RESTART"};
    case ConfirmAction::Sleep:
      return {"SLEEP DEVICE?", "Screen and Wi-Fi turn off.", "Wake: tap screen if supported, or press RST.", "SLEEP"};
    case ConfirmAction::StartWifiSetup:
      return {"START WI-FI SETUP?", "Opens a setup hotspot.", "Dashboard stays connected.", "START"};
    case ConfirmAction::ForgetWifi:
      return {"USE BUILT-IN WI-FI?", "Forgets the saved network", "and reconnects (brief outage).", "SWITCH"};
    case ConfirmAction::ApplyBluetooth:
      return {"RESTART TO APPLY?", "Bluetooth changes apply", "after a restart.", "RESTART"};
    case ConfirmAction::None:
      break;
  }
  return {"", "", "", "OK"};
}

// Confirm dialog buttons: CANCEL 15..155, CONFIRM 165..305, y 150..200.
enum class ConfirmChoice : uint8_t { None, Cancel, Confirm };

inline ConfirmChoice confirmChoiceAt(int x, int y) {
  if (y < 150 || y > 200) return ConfirmChoice::None;
  if (x >= 15 && x < 155) return ConfirmChoice::Cancel;
  if (x >= 165 && x < 305) return ConfirmChoice::Confirm;
  return ConfirmChoice::None;
}

// --- Formatting -------------------------------------------------------------------

// Common linear dBm -> quality mapping: <= -100 dBm is 0%, >= -50 dBm is 100%.
inline int rssiToPercent(int rssi) {
  if (rssi <= -100) return 0;
  if (rssi >= -50) return 100;
  return 2 * (rssi + 100);
}

inline void formatSignal(char* out, size_t size, bool connected, int rssi) {
  if (!connected) snprintf(out, size, "--");
  else snprintf(out, size, "%d dBm (%d%%)", rssi, rssiToPercent(rssi));
}

inline void formatUptime(char* out, size_t size, uint32_t seconds) {
  uint32_t days = seconds / 86400, hours = seconds / 3600 % 24, minutes = seconds / 60 % 60;
  if (days > 0) snprintf(out, size, "%ud %uh %um", unsigned(days), unsigned(hours), unsigned(minutes));
  else if (hours > 0) snprintf(out, size, "%uh %um", unsigned(hours), unsigned(minutes));
  else snprintf(out, size, "%um %us", unsigned(minutes), unsigned(seconds % 60));
}

inline void formatKilobytes(char* out, size_t size, uint32_t bytes) {
  snprintf(out, size, "%u.%u KB", unsigned(bytes / 1024), unsigned(bytes % 1024 * 10 / 1024));
}

// --- Wi-Fi network selection -----------------------------------------------------------

enum class WifiSource : uint8_t { BuiltIn, Saved };

// Reconnect policy: stay on the active network; after RECONNECT_SWITCH_AFTER
// consecutive failures switch to the other one (if a saved network exists) and
// start counting again. Returns the source to try next.
constexpr uint8_t RECONNECT_SWITCH_AFTER = 2;

inline WifiSource reconnectSource(WifiSource active, bool hasSaved, uint8_t& failures) {
  if (!hasSaved) {
    failures = 0;
    return WifiSource::BuiltIn;
  }
  if (failures >= RECONNECT_SWITCH_AFTER) {
    failures = 0;
    return active == WifiSource::Saved ? WifiSource::BuiltIn : WifiSource::Saved;
  }
  return active;
}

inline const char* wifiSourceLabel(WifiSource source) {
  return source == WifiSource::Saved ? "Saved (setup)" : "Built-in";
}

// Credentials accepted by the setup page (WPA2-PSK passphrase or open network).
inline bool validWifiInput(const char* ssid, size_t ssidLength, size_t passwordLength) {
  if (!ssid || ssidLength == 0 || ssidLength > 32) return false;
  return passwordLength == 0 || (passwordLength >= 8 && passwordLength <= 63);
}

// --- Sleep sequencing ----------------------------------------------------------------------

// Deep sleep starts only after the touch has been released continuously for
// SLEEP_RELEASE_MS, so the finger that pressed SLEEP cannot wake it again.
constexpr uint32_t SLEEP_RELEASE_MS = 500;

inline bool sleepReady(bool pressed, uint32_t now, bool& releasedSeen, uint32_t& releasedSince) {
  if (pressed) {
    releasedSeen = false;
    return false;
  }
  if (!releasedSeen) {
    releasedSeen = true;
    releasedSince = now;
  }
  return uint32_t(now - releasedSince) >= SLEEP_RELEASE_MS;
}
