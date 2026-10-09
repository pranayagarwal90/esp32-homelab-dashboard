#include "DeviceSettings.h"
#include "SettingsLogic.h"
#include "BluetoothControl.h"
#include "BuildInfo.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static void testDefaults() {
  DeviceSettings s;
  // Defaults reproduce the pre-Settings firmware.
  assert(s.autoBrightness);
  assert(percentToDuty(s.dayBrightness) == 255);
  assert(percentToDuty(s.nightBrightness) == 38);
  assert(percentToDuty(s.boostBrightness) == 153);
  assert(s.screensaverEnabled && s.screensaverTimeoutMin == 3 && s.screensaverRotateSec == 30);
  assert(!s.bluetoothEnabled && s.wallpaper[0] == '\0');
  assert(sanitizeSettings(s));
  // Default settings map to the original fixed backlight behaviour.
  BacklightControl::Levels levels = backlightLevels(s), original;
  assert(levels.automatic && levels.day == original.day && levels.night == original.night &&
         levels.boost == original.boost);
  s.autoBrightness = false;
  s.manualBrightness = 50;
  levels = backlightLevels(s);
  assert(!levels.automatic && levels.manual == 128);
}

static void testCodec() {
  DeviceSettings s;
  s.autoBrightness = false;
  s.manualBrightness = 45;
  s.nightBrightness = 5;
  s.screensaverEnabled = false;
  s.screensaverTimeoutMin = 10;
  s.screensaverRotateSec = 60;
  s.bluetoothEnabled = true;
  strcpy(s.wallpaper, "photo_007.jpg");
  uint8_t blob[SETTINGS_BLOB_MAX];
  size_t length = encodeSettings(s, blob);
  assert(length == SETTINGS_HEADER_SIZE + strlen("photo_007.jpg"));
  DeviceSettings back;
  assert(decodeSettings(blob, length, back));
  assert(sameSettings(s, back));

  // Missing / truncated / wrong magic / future version -> all defaults.
  DeviceSettings d;
  assert(!decodeSettings(nullptr, 0, back) && sameSettings(back, d));
  assert(!decodeSettings(blob, length - 1, back) && sameSettings(back, d));
  uint8_t bad[SETTINGS_BLOB_MAX];
  memcpy(bad, blob, length);
  bad[0] = 'X';
  assert(!decodeSettings(bad, length, back) && sameSettings(back, d));
  memcpy(bad, blob, length);
  bad[1] = SETTINGS_VERSION + 1;
  assert(!decodeSettings(bad, length, back) && sameSettings(back, d));
  memcpy(bad, blob, length);
  bad[2] = 0x80; // Unknown flag bit.
  assert(!decodeSettings(bad, length, back) && sameSettings(back, d));

  // Known version with bad fields: only those fields reset.
  memcpy(bad, blob, length);
  bad[3] = 0;   // manual below minimum
  bad[5] = 7;   // night not a 5% step
  bad[7] = 4;   // timeout not an option
  bad[8] = 0;   // rotation not an option
  bad[10] = '/'; // path separator in wallpaper
  assert(!decodeSettings(bad, length, back));
  assert(back.manualBrightness == d.manualBrightness);
  assert(back.nightBrightness == d.nightBrightness);
  assert(back.screensaverTimeoutMin == d.screensaverTimeoutMin);
  assert(back.screensaverRotateSec == d.screensaverRotateSec);
  assert(back.wallpaper[0] == '\0');
  assert(!back.autoBrightness && back.bluetoothEnabled && !back.screensaverEnabled); // Kept.

  // Same settings encode identically (no rewrite); a change differs.
  DeviceSettings a, b;
  assert(sameSettings(a, b));
  b.dayBrightness = 95;
  assert(!sameSettings(a, b));
}

static void testSteps() {
  assert(stepBrightness(100, +1, DAY_MIN) == 100);
  assert(stepBrightness(100, -1, DAY_MIN) == 95);
  assert(stepBrightness(10, -1, MANUAL_MIN) == 10); // Never fully dark.
  assert(stepBrightness(5, -1, NIGHT_MIN) == 5);
  assert(stepBrightness(15, -1, NIGHT_MIN) == 10);
  assert(percentToDuty(0) == 0 && percentToDuty(50) == 128 && percentToDuty(200) == 255);

  assert(stepOption(SCREENSAVER_TIMEOUTS_MIN, 3, +1) == 5);
  assert(stepOption(SCREENSAVER_TIMEOUTS_MIN, 10, +1) == 10);
  assert(stepOption(SCREENSAVER_TIMEOUTS_MIN, 1, -1) == 1);
  assert(stepOption(SCREENSAVER_TIMEOUTS_MIN, 7, +1) == 1); // Unknown -> first.
  assert(stepOption(SCREENSAVER_ROTATIONS_SEC, 30, -1) == 15);
  assert(stepOption(SCREENSAVER_ROTATIONS_SEC, 30, +1) == 60);
}

static void testScreensaver() {
  DeviceSettings s;
  assert(screensaverTimeoutMs(s) == 180000 && screensaverRotateMs(s) == 30000); // Original timings.
  assert(!shouldStartScreensaver(s, 179999, false));
  assert(shouldStartScreensaver(s, 180000, false));
  assert(!shouldStartScreensaver(s, 999999, true)); // Setup hotspot / dialog visible.
  s.screensaverTimeoutMin = 1;
  assert(shouldStartScreensaver(s, 60000, false));
  s.screensaverEnabled = false; // Disabled: never starts, timeout ignored.
  assert(screensaverTimeoutMs(s) == 0);
  assert(!shouldStartScreensaver(s, 0xFFFFFFFFu, false));
  s.screensaverRotateSec = 15;
  assert(screensaverRotateMs(s) == 15000);
}

static void testNavigation() {

  RowHit hit = settingsRowAt(220, 50, true);
  assert(hit.row == 0 && hit.control == RowControl::Toggle);
  hit = settingsRowAt(220, 90, false);
  assert(hit.row == 1 && hit.control == RowControl::Minus);
  hit = settingsRowAt(290, 170, false);
  assert(hit.row == 3 && hit.control == RowControl::Plus);
  assert(settingsRowAt(100, 90, false).row == -1); // Label area is inert.
  assert(settingsRowAt(290, 210, false).row == -1); // Back bar.

}

// Taps row `row` of a list view's centre and returns the entry it opens.
static const SettingsMenuEntry& tapRow(SettingsView view, int row) {
  SettingsMenu menu;
  assert(settingsMenuFor(view, menu));
  int y = SETTINGS_LIST_Y0 + row * SETTINGS_LIST_PITCH + SETTINGS_LIST_H / 2;
  int hit = settingsListRowAt(160, y, menu.count);
  assert(hit == row);
  return menu.entries[hit];
}

static void testHierarchy() {
  SettingsMenu menu;
  // Root: SYSTEM / CONNECTIVITY / DISPLAY / UTILITIES.
  assert(settingsMenuFor(SettingsView::Root, menu) && menu.count == 4);
  assert(strcmp(menu.title, "SETTINGS") == 0);
  assert(tapRow(SettingsView::Root, 0).target == SettingsView::System);
  assert(tapRow(SettingsView::Root, 1).target == SettingsView::Connectivity);
  assert(tapRow(SettingsView::Root, 2).target == SettingsView::DisplayMenu);
  assert(tapRow(SettingsView::Root, 3).target == SettingsView::Utilities);
  // Four root rows still fit above the bottom navigation.
  assert(menu.count <= SETTINGS_LIST_MAX_ROWS);

  // UTILITIES: Stopwatch (its own page; BACK returns to UTILITIES).
  assert(settingsMenuFor(SettingsView::Utilities, menu) && menu.count == 1);
  assert(strcmp(menu.title, "UTILITIES") == 0);
  assert(tapRow(SettingsView::Utilities, 0).target == SettingsView::Stopwatch);
  assert(tapRow(SettingsView::Utilities, 0).icon == UiIcon::Stopwatch);
  assert(!settingsMenuFor(SettingsView::Stopwatch, menu));

  // List hit-testing: gaps, margins, missing rows and the BACK bar are inert.
  assert(settingsListRowAt(160, SETTINGS_LIST_Y0 + SETTINGS_LIST_H + 2, 3) == -1); // Gap.
  assert(settingsListRowAt(5, 60, 3) == -1);                                       // Left margin.
  assert(settingsListRowAt(315, 60, 3) == -1);                                     // Right margin.
  assert(settingsListRowAt(160, SETTINGS_LIST_Y0 + 3 * SETTINGS_LIST_PITCH + 10, 3) == -1); // No 4th row.
  assert(settingsListRowAt(160, 30, 3) == -1);                                     // Header.
  assert(settingsListRowAt(160, 220, 4) == -1);                                    // BACK bar.
  assert(SETTINGS_LIST_Y0 + (SETTINGS_LIST_MAX_ROWS - 1) * SETTINGS_LIST_PITCH + SETTINGS_LIST_H < BACK_BAR_Y);

  // SYSTEM: Device Info, Firmware / Build, Restart (confirm), Sleep (confirm).
  assert(settingsMenuFor(SettingsView::System, menu) && menu.count == 4);
  assert(tapRow(SettingsView::System, 0).target == SettingsView::Info);
  assert(tapRow(SettingsView::System, 1).target == SettingsView::Build);
  assert(tapRow(SettingsView::System, 2).confirm == ConfirmAction::Restart);
  assert(tapRow(SettingsView::System, 3).confirm == ConfirmAction::Sleep);

  // CONNECTIVITY: Wi-Fi, Bluetooth.
  assert(settingsMenuFor(SettingsView::Connectivity, menu) && menu.count == 2);
  assert(tapRow(SettingsView::Connectivity, 0).target == SettingsView::Wifi);
  assert(tapRow(SettingsView::Connectivity, 1).target == SettingsView::Bluetooth);

  // DISPLAY: Brightness, Screensaver, Photos & Wallpaper.
  assert(settingsMenuFor(SettingsView::DisplayMenu, menu) && menu.count == 3);
  assert(tapRow(SettingsView::DisplayMenu, 0).target == SettingsView::Brightness);
  assert(tapRow(SettingsView::DisplayMenu, 1).target == SettingsView::Screensaver);
  assert(tapRow(SettingsView::DisplayMenu, 2).target == SettingsView::Wallpaper);
  assert(strcmp(tapRow(SettingsView::DisplayMenu, 2).label, "PHOTOS & WALLPAPER") == 0);

  // Feature pages are not list menus.
  assert(!settingsMenuFor(SettingsView::Wifi, menu));
  assert(!settingsMenuFor(SettingsView::Build, menu));

  // BACK: feature -> category -> root; the root has no BACK (SETTINGS is a
  // bottom-nav tab, its bottom bar is the navigation).
  assert(settingsParent(SettingsView::Info) == SettingsView::System);
  assert(settingsParent(SettingsView::Build) == SettingsView::System);
  assert(settingsParent(SettingsView::Wifi) == SettingsView::Connectivity);
  assert(settingsParent(SettingsView::WifiSetup) == SettingsView::Connectivity);
  assert(settingsParent(SettingsView::Bluetooth) == SettingsView::Connectivity);
  assert(settingsParent(SettingsView::Brightness) == SettingsView::DisplayMenu);
  assert(settingsParent(SettingsView::Screensaver) == SettingsView::DisplayMenu);
  assert(settingsParent(SettingsView::Wallpaper) == SettingsView::DisplayMenu);
  assert(settingsParent(SettingsView::System) == SettingsView::Root);
  assert(settingsParent(SettingsView::Connectivity) == SettingsView::Root);
  assert(settingsParent(SettingsView::DisplayMenu) == SettingsView::Root);
  assert(settingsParent(SettingsView::Utilities) == SettingsView::Root);
  assert(settingsParent(SettingsView::Stopwatch) == SettingsView::Utilities);
  assert(settingsBottomIsNav(SettingsView::Root));
  const SettingsView others[] = {SettingsView::System, SettingsView::Connectivity, SettingsView::DisplayMenu,
                                 SettingsView::Utilities, SettingsView::Brightness, SettingsView::Build,
                                 SettingsView::Wifi, SettingsView::Info};
  for (SettingsView v : others) assert(!settingsBottomIsNav(v));
  SettingsView view = SettingsView::Wallpaper;
  int levels = 0;
  while (!settingsBottomIsNav(view)) {
    view = settingsParent(view);
    levels++;
  }
  assert(levels == 2); // Wallpaper -> DISPLAY -> SETTINGS (root; no further BACK).
  view = SettingsView::Stopwatch;
  for (levels = 0; !settingsBottomIsNav(view); levels++) view = settingsParent(view);
  assert(levels == 2); // Stopwatch -> UTILITIES -> SETTINGS.

  // Every list entry has an icon and a tile colour.
  const SettingsView lists[] = {SettingsView::Root, SettingsView::System, SettingsView::Connectivity,
                                SettingsView::DisplayMenu, SettingsView::Utilities};
  for (SettingsView v : lists) {
    assert(settingsMenuFor(v, menu));
    for (int i = 0; i < menu.count; i++) {
      assert(menu.entries[i].icon < UiIcon::Count && menu.entries[i].tile != 0);
    }
  }
  assert(tapRow(SettingsView::Root, 0).icon == UiIcon::System);
  assert(tapRow(SettingsView::Root, 1).icon == UiIcon::Connectivity);
  assert(tapRow(SettingsView::Root, 2).icon == UiIcon::Display);
  assert(tapRow(SettingsView::Root, 3).icon == UiIcon::Utilities);
  assert(tapRow(SettingsView::System, 0).icon == UiIcon::DeviceInfo);
  assert(tapRow(SettingsView::System, 1).icon == UiIcon::Firmware);
  assert(tapRow(SettingsView::System, 2).icon == UiIcon::Restart);
  assert(tapRow(SettingsView::System, 3).icon == UiIcon::Sleep);
  assert(tapRow(SettingsView::Connectivity, 0).icon == UiIcon::Wifi);
  assert(tapRow(SettingsView::Connectivity, 1).icon == UiIcon::Bluetooth);
  assert(tapRow(SettingsView::DisplayMenu, 0).icon == UiIcon::Brightness);
  assert(tapRow(SettingsView::DisplayMenu, 1).icon == UiIcon::Screensaver);
  assert(tapRow(SettingsView::DisplayMenu, 2).icon == UiIcon::Photos);

  // Bluetooth is compiled out by default; the page shows a message instead.
  assert(!BLUETOOTH_SUPPORTED);
}

static void testBuildInfo() {
  // The single version source is semantic MAJOR.MINOR.PATCH.
  assert(isSemanticVersion(DASHBOARD_FIRMWARE_VERSION));
  assert(isSemanticVersion("1.0.0") && isSemanticVersion("12.3.45"));
  assert(!isSemanticVersion("1.0") && !isSemanticVersion("1.0.0.1") && !isSemanticVersion("v1.0.0"));
  assert(!isSemanticVersion("1..0") && !isSemanticVersion(""));

  char out[32];
  formatBuildTimestamp("Oct  8 2026", "15:10:42", out, sizeof(out));
  assert(strcmp(out, "2026-10-08 15:10") == 0);
  formatBuildTimestamp("Jan 31 2027", "00:05:00", out, sizeof(out));
  assert(strcmp(out, "2027-01-31 00:05") == 0);
  formatBuildTimestamp("Foo  8 2026", "15:10:42", out, sizeof(out)); // Unknown month.
  assert(strcmp(out, "Foo  8 2026 15:10:42") == 0);
  formatBuildTimestamp("Oct  8 2026", "bad", out, sizeof(out));
  assert(strcmp(out, "Oct  8 2026 bad") == 0);
  // The real compiler macros always parse.
  formatBuildTimestamp(__DATE__, __TIME__, out, sizeof(out));
  assert(strlen(out) == 16 && out[4] == '-' && out[10] == ' ');

  // Missing injected metadata falls back to "unknown".
  assert(strcmp(buildValueOr(""), "unknown") == 0);
  assert(strcmp(buildValueOr(nullptr), "unknown") == 0);
  assert(strcmp(buildValueOr("a1b2c3d"), "a1b2c3d") == 0);
}

static void testConfirm() {
  assert(confirmChoiceAt(50, 170) == ConfirmChoice::Cancel);
  assert(confirmChoiceAt(250, 170) == ConfirmChoice::Confirm);
  assert(confirmChoiceAt(160, 170) == ConfirmChoice::None);  // Gap.
  assert(confirmChoiceAt(250, 100) == ConfirmChoice::None);  // Message area.
  assert(strcmp(confirmText(ConfirmAction::Restart).confirmLabel, "RESTART") == 0);
  assert(strcmp(confirmText(ConfirmAction::Sleep).confirmLabel, "SLEEP") == 0);
  // Sleep never claims to power off.
  assert(strstr(confirmText(ConfirmAction::Sleep).title, "POWER") == nullptr);
  // RST is the guaranteed wake method; touch wake is not promised.
  assert(strcmp(confirmText(ConfirmAction::Sleep).line2, "Wake: tap screen if supported, or press RST.") == 0);
}

static void testFormatting() {
  char out[32];
  assert(rssiToPercent(-40) == 100 && rssiToPercent(-75) == 50 && rssiToPercent(-110) == 0);
  formatSignal(out, sizeof(out), true, -58);
  assert(strcmp(out, "-58 dBm (84%)") == 0);
  formatSignal(out, sizeof(out), false, -58);
  assert(strcmp(out, "--") == 0);
  formatUptime(out, sizeof(out), 59);
  assert(strcmp(out, "0m 59s") == 0);
  formatUptime(out, sizeof(out), 3 * 3600 + 5 * 60);
  assert(strcmp(out, "3h 5m") == 0);
  formatUptime(out, sizeof(out), 2 * 86400 + 3600 + 60);
  assert(strcmp(out, "2d 1h 1m") == 0);
  formatKilobytes(out, sizeof(out), 150 * 1024 + 512);
  assert(strcmp(out, "150.5 KB") == 0);
}

static void testWifiSelection() {
  uint8_t failures = 0;
  // No saved network: always the built-in one (original behaviour).
  failures = 9;
  assert(reconnectSource(WifiSource::BuiltIn, false, failures) == WifiSource::BuiltIn && failures == 0);
  // Saved network: stay on the active one until two consecutive failures.
  failures = 1;
  assert(reconnectSource(WifiSource::Saved, true, failures) == WifiSource::Saved && failures == 1);
  failures = 2;
  assert(reconnectSource(WifiSource::Saved, true, failures) == WifiSource::BuiltIn && failures == 0);
  failures = 2;
  assert(reconnectSource(WifiSource::BuiltIn, true, failures) == WifiSource::Saved && failures == 0);

  assert(validWifiInput("Home", 4, 0));     // Open network.
  assert(validWifiInput("Home", 4, 8));
  assert(validWifiInput("Home", 4, 63));
  assert(!validWifiInput("Home", 4, 7));    // WPA2 minimum.
  assert(!validWifiInput("Home", 4, 64));
  assert(!validWifiInput("", 0, 8));
  assert(!validWifiInput("x", 33, 8));
  assert(strcmp(wifiSourceLabel(WifiSource::BuiltIn), "Built-in") == 0);
}

static void testSleepRelease() {
  bool seen = false;
  uint32_t since = 0;
  assert(!sleepReady(true, 0, seen, since));      // Finger still on SLEEP.
  assert(!sleepReady(false, 100, seen, since));   // Released, timer starts.
  assert(!sleepReady(false, 599, seen, since));
  assert(!sleepReady(true, 550, seen, since));    // Touched again: restart.
  assert(!sleepReady(false, 700, seen, since));
  assert(!sleepReady(false, 1199, seen, since));
  assert(sleepReady(false, 1200, seen, since));
  seen = false;
  assert(!sleepReady(false, 0xFFFFFF00u, seen, since));
  assert(sleepReady(false, 0x00000200u, seen, since)); // millis() rollover.
}

int main() {
  testDefaults();
  testCodec();
  testSteps();
  testScreensaver();
  testNavigation();
  testHierarchy();
  testBuildInfo();
  testConfirm();
  testFormatting();
  testWifiSelection();
  testSleepRelease();
  puts("Settings tests passed: defaults, codec, corrupt fallback, steps, navigation, confirm, formatting, wifi, sleep, hierarchy, build info");
}
