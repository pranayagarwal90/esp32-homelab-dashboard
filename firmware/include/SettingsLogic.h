#pragma once
#include <stdint.h>
#include <stdio.h>
#include "UiIcons.h"
#include "UiTheme.h"

// Pure Settings navigation, hit-testing and formatting, shared with host tests.

// Settings is one global page (PAGE_SETTINGS, a root tab); these are its
// internal views. Root -> category list -> feature page, like iPhone Settings.
enum class SettingsView : uint8_t {
  Root,
  System,
  Connectivity,
  DisplayMenu,
  Wifi,
  WifiSetup,
  Brightness,
  Screensaver,
  Bluetooth,
  Wallpaper,
  Info,
  Build,
  Confirm,
  Sleeping,
  Utilities,
  Stopwatch   // Not drawn here: opens PAGE_STOPWATCH; BACK returns to Utilities.
};

constexpr int BACK_BAR_Y = 205; // Same threshold as every other page.

// --- List menus (root and categories) ------------------------------------------------
// Full-width rows: x 10..310, 36 px tall, 41 px pitch, up to 4 rows above BACK.

constexpr int SETTINGS_LIST_X = 10;
constexpr int SETTINGS_LIST_W = 300;
constexpr int SETTINGS_LIST_Y0 = 44;
constexpr int SETTINGS_LIST_H = 36;
constexpr int SETTINGS_LIST_PITCH = 41;
constexpr int SETTINGS_LIST_MAX_ROWS = 4;

enum class ConfirmAction : uint8_t { None, Restart, Sleep, StartWifiSetup, ForgetWifi, ApplyBluetooth };

struct SettingsMenuEntry {
  const char* label;
  SettingsView target;   // Opened when tapped, unless confirm is set.
  ConfirmAction confirm; // Asks first (restart/sleep), returning to this menu.
  UiIcon icon;
  uint16_t tile;         // Icon tile colour.
};

struct SettingsMenu {
  const char* title;
  const SettingsMenuEntry* entries;
  int count;
};

// Fills out the menu for a list view; false for feature pages.
inline bool settingsMenuFor(SettingsView view, SettingsMenu& out) {
  typedef ConfirmAction A;
  static const SettingsMenuEntry root[] = {
    {"SYSTEM", SettingsView::System, A::None, UiIcon::System, UiColor::Grey},
    {"CONNECTIVITY", SettingsView::Connectivity, A::None, UiIcon::Connectivity, UiColor::Blue},
    {"DISPLAY", SettingsView::DisplayMenu, A::None, UiIcon::Display, UiColor::Yellow},
    {"UTILITIES", SettingsView::Utilities, A::None, UiIcon::Utilities, UiColor::Orange},
  };
  static const SettingsMenuEntry system[] = {
    {"DEVICE INFO", SettingsView::Info, A::None, UiIcon::DeviceInfo, UiColor::Grey},
    {"FIRMWARE / BUILD", SettingsView::Build, A::None, UiIcon::Firmware, UiColor::Grey},
    {"RESTART", SettingsView::System, A::Restart, UiIcon::Restart, UiColor::Orange},
    {"SLEEP", SettingsView::System, A::Sleep, UiIcon::Sleep, UiColor::Purple},
  };
  static const SettingsMenuEntry connectivity[] = {
    {"WI-FI", SettingsView::Wifi, A::None, UiIcon::Wifi, UiColor::Blue},
    {"BLUETOOTH", SettingsView::Bluetooth, A::None, UiIcon::Bluetooth, UiColor::Blue},
  };
  static const SettingsMenuEntry display[] = {
    {"BRIGHTNESS", SettingsView::Brightness, A::None, UiIcon::Brightness, UiColor::Yellow},
    {"SCREENSAVER", SettingsView::Screensaver, A::None, UiIcon::Screensaver, UiColor::Teal},
    {"PHOTOS & WALLPAPER", SettingsView::Wallpaper, A::None, UiIcon::Photos, UiColor::Purple},
  };
  static const SettingsMenuEntry utilities[] = {
    {"STOPWATCH", SettingsView::Stopwatch, A::None, UiIcon::Stopwatch, UiColor::Orange},
  };
  switch (view) {
    case SettingsView::Root: out = {"SETTINGS", root, 4}; return true;
    case SettingsView::System: out = {"SYSTEM", system, 4}; return true;
    case SettingsView::Connectivity: out = {"CONNECTIVITY", connectivity, 2}; return true;
    case SettingsView::DisplayMenu: out = {"DISPLAY", display, 3}; return true;
    case SettingsView::Utilities: out = {"UTILITIES", utilities, 1}; return true;
    default: return false;
  }
}

// Row index under (x, y) in a list of count rows, or -1 (gaps are inert).
inline int settingsListRowAt(int x, int y, int count) {
  if (x < SETTINGS_LIST_X || x >= SETTINGS_LIST_X + SETTINGS_LIST_W || y < SETTINGS_LIST_Y0) return -1;
  int row = (y - SETTINGS_LIST_Y0) / SETTINGS_LIST_PITCH;
  if (row >= count || row >= SETTINGS_LIST_MAX_ROWS || (y - SETTINGS_LIST_Y0) % SETTINGS_LIST_PITCH >= SETTINGS_LIST_H) return -1;
  return row;
}

// BACK goes one level up: feature page -> its category -> Settings root. The
// root has no BACK: SETTINGS is a root tab and its bottom bar is the
// navigation (settingsBottomIsNav).
inline SettingsView settingsParent(SettingsView view) {
  switch (view) {
    case SettingsView::Info:
    case SettingsView::Build:
      return SettingsView::System;
    case SettingsView::Wifi:
    case SettingsView::WifiSetup:
    case SettingsView::Bluetooth:
      return SettingsView::Connectivity;
    case SettingsView::Brightness:
    case SettingsView::Screensaver:
    case SettingsView::Wallpaper:
      return SettingsView::DisplayMenu;
    case SettingsView::Stopwatch:
      return SettingsView::Utilities;
    default:
      return SettingsView::Root;
  }
}

inline bool settingsBottomIsNav(SettingsView view) {
  return view == SettingsView::Root;
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

// --- Confirmation dialogs -------------------------------------------------------

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
