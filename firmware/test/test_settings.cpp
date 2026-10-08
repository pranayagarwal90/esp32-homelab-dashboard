#include "DeviceSettings.h"
#include "SettingsLogic.h"
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
  assert(settingsMenuItemAt(20, 50) == SettingsItem::Wifi);
  assert(settingsMenuItemAt(300, 50) == SettingsItem::Display);
  assert(settingsMenuItemAt(20, 44 + 41 * 3 + 10) == SettingsItem::Restart);
  assert(settingsMenuItemAt(200, 44 + 41 * 3 + 10) == SettingsItem::Sleep);
  assert(settingsMenuItemAt(160, 50) == SettingsItem::None);       // Gap between columns.
  assert(settingsMenuItemAt(20, 44 + 38) == SettingsItem::None);   // Gap between rows.
  assert(settingsMenuItemAt(20, 230) == SettingsItem::None);       // Back bar.

  RowHit hit = settingsRowAt(220, 50, true);
  assert(hit.row == 0 && hit.control == RowControl::Toggle);
  hit = settingsRowAt(220, 90, false);
  assert(hit.row == 1 && hit.control == RowControl::Minus);
  hit = settingsRowAt(290, 170, false);
  assert(hit.row == 3 && hit.control == RowControl::Plus);
  assert(settingsRowAt(100, 90, false).row == -1); // Label area is inert.
  assert(settingsRowAt(290, 210, false).row == -1); // Back bar.

  assert(settingsBackLeaves(SettingsView::Menu));
  assert(!settingsBackLeaves(SettingsView::Display));
  assert(!settingsBackLeaves(SettingsView::Info));
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
  testConfirm();
  testFormatting();
  testWifiSelection();
  testSleepRelease();
  puts("Settings tests passed: defaults, codec, corrupt fallback, steps, navigation, confirm, formatting, wifi, sleep");
}
