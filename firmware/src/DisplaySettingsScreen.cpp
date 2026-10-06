#include <Arduino.h>
#include "DisplaySettingsScreen.h"
#include "BacklightPwm.h"
#include "Display.h"
#include "PhotoScreen.h"
#include "UiHelpers.h"
#include "SettingsLogic.h"
#include "SettingsStore.h"
#include "SettingsUi.h"

static void percentText(char* out, size_t size, uint8_t value) {
  snprintf(out, size, "%u%%", unsigned(value));
}

// --- Brightness -----------------------------------------------------------------
// Auto ON:  AUTO (SUNRISE) | DAY | NIGHT | TOUCH BOOST
// Auto OFF: AUTO (SUNRISE) | BRIGHTNESS

struct BrightnessRow {
  const char* label;
  uint8_t DeviceSettings::*field;
  uint8_t minimum;
};

static const BrightnessRow AUTO_ROWS[] = {
  {"DAY", &DeviceSettings::dayBrightness, DAY_MIN},
  {"NIGHT", &DeviceSettings::nightBrightness, NIGHT_MIN},
  {"TOUCH BOOST", &DeviceSettings::boostBrightness, BOOST_MIN},
};
static const BrightnessRow MANUAL_ROWS[] = {
  {"BRIGHTNESS", &DeviceSettings::manualBrightness, MANUAL_MIN},
};

static const BrightnessRow* brightnessRows(int& count) {
  if (settings().autoBrightness) {
    count = 3;
    return AUTO_ROWS;
  }
  count = 1;
  return MANUAL_ROWS;
}

void drawDisplaySettings() {
  drawSettingsFrame("DISPLAY");
  drawSettingRow(0, "AUTO (SUNRISE)", nullptr);
  drawSettingToggle(0, settings().autoBrightness);
  int count;
  const BrightnessRow* rows = brightnessRows(count);
  char value[8];
  for (int i = 0; i < count; i++) {
    percentText(value, sizeof(value), settings().*rows[i].field);
    drawSettingRow(i + 1, rows[i].label, value);
    drawSettingSteppers(i + 1);
  }
}

void handleDisplaySettingsTouch(int x, int y) {
  RowHit hit = settingsRowAt(x, y, false);
  if (hit.row < 0) return;
  DeviceSettings& edit = editSettings();
  if (hit.row == 0) {
    edit.autoBrightness = !edit.autoBrightness;
    settingsChanged();
    backlightApplySettings();
    drawDisplaySettings(); // Rows change with the mode.
    return;
  }
  int count;
  const BrightnessRow* rows = brightnessRows(count);
  if (hit.row > count) return;
  const BrightnessRow& row = rows[hit.row - 1];
  uint8_t next = stepBrightness(edit.*row.field, hit.control == RowControl::Plus ? 1 : -1, row.minimum);
  if (next == edit.*row.field) return;
  edit.*row.field = next;
  settingsChanged();
  backlightApplySettings();
  char value[8];
  percentText(value, sizeof(value), next);
  drawSettingValue(hit.row, value);
}

// --- Screensaver ------------------------------------------------------------------
// SCREENSAVER on/off | TIMEOUT | PHOTO / CLOCK rotation

static void timeoutText(char* out, size_t size) {
  snprintf(out, size, "%u min", unsigned(settings().screensaverTimeoutMin));
}

static void rotationText(char* out, size_t size) {
  snprintf(out, size, "%u s", unsigned(settings().screensaverRotateSec));
}

void drawScreensaverSettings() {
  drawSettingsFrame("SCREENSAVER");
  char value[12];
  drawSettingRow(0, "SCREENSAVER", nullptr);
  drawSettingToggle(0, settings().screensaverEnabled);
  timeoutText(value, sizeof(value));
  drawSettingRow(1, "START AFTER", value);
  drawSettingSteppers(1);
  rotationText(value, sizeof(value));
  drawSettingRow(2, "PHOTO / CLOCK", value);
  drawSettingSteppers(2);
}

void handleScreensaverSettingsTouch(int x, int y) {
  RowHit hit = settingsRowAt(x, y, false);
  if (hit.row < 0 || hit.row > 2) return;
  DeviceSettings& edit = editSettings();
  int direction = hit.control == RowControl::Plus ? 1 : -1;
  char value[12];
  if (hit.row == 0) {
    edit.screensaverEnabled = !edit.screensaverEnabled;
    settingsChanged();
    drawSettingToggle(0, edit.screensaverEnabled);
  } else if (hit.row == 1) {
    uint8_t next = stepOption(SCREENSAVER_TIMEOUTS_MIN, edit.screensaverTimeoutMin, direction);
    if (next == edit.screensaverTimeoutMin) return;
    edit.screensaverTimeoutMin = next;
    settingsChanged();
    timeoutText(value, sizeof(value));
    drawSettingValue(1, value);
  } else {
    uint8_t next = stepOption(SCREENSAVER_ROTATIONS_SEC, edit.screensaverRotateSec, direction);
    if (next == edit.screensaverRotateSec) return;
    edit.screensaverRotateSec = next;
    settingsChanged();
    rotationText(value, sizeof(value));
    drawSettingValue(2, value);
  }
}

// --- Wallpaper --------------------------------------------------------------------
// "Wallpaper" = the photo the screensaver shows. Row 0 of the list is "rotate
// all photos" (the original behaviour); the rest are the server's photos.
// Only file names are listed; nothing is downloaded here.

static const int WALL_Y0 = 42;
static const int WALL_PITCH = 32;
static const int WALL_ROWS = 5;
static int wallpaperPage = 0;
static bool wallpaperListWasReady = false;

static int wallpaperItemCount() {
  return 1 + photoListCount();
}

static int wallpaperPages() {
  return (wallpaperItemCount() + WALL_ROWS - 1) / WALL_ROWS;
}

static bool wallpaperItemSelected(int item) {
  const char* current = settings().wallpaper;
  if (item == 0) return current[0] == '\0';
  return strcmp(current, photoListName(item - 1)) == 0;
}

void drawWallpaperSettings() {
  bool ready = photoListReady();
  wallpaperListWasReady = ready;
  if (!ready) requestPhotoList(); // Asynchronous; the page redraws when it arrives.
  if (wallpaperPage >= wallpaperPages()) wallpaperPage = 0;

  tft.fillScreen(TFT_BLACK);
  drawHeader("WALLPAPER");
  for (int row = 0; row < WALL_ROWS; row++) {
    int item = wallpaperPage * WALL_ROWS + row;
    if (item >= wallpaperItemCount()) break;
    int y = WALL_Y0 + row * WALL_PITCH;
    bool selected = wallpaperItemSelected(item);
    uint16_t fill = selected ? TFT_BLUE : TFT_DARKGREY;
    tft.fillRoundRect(10, y, 300, WALL_PITCH - 4, 6, fill);
    tft.setTextSize(1);
    tft.setTextColor(TFT_WHITE, fill);
    tft.setCursor(20, y + 10);
    if (item == 0) {
      tft.print("ALL PHOTOS (ROTATE)");
    } else {
      String name = photoListName(item - 1);
      if (name.length() > 44) name = name.substring(0, 41) + "...";
      tft.print(name);
    }
  }
  if (!ready) {
    tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    tft.setCursor(20, WALL_Y0 + WALL_PITCH + 10);
    tft.print("Loading photo list from HomeServer...");
  }
  bool paged = wallpaperPages() > 1;
  drawBackBar(paged ? "< PREV" : nullptr, "BACK", paged ? "NEXT >" : nullptr);
}

bool handleWallpaperSettingsTouch(int x, int y) {
  if (y >= BACK_BAR_Y) {
    if (wallpaperPages() <= 1 || (x >= 106 && x < 214)) return false; // BACK.
    int pages = wallpaperPages();
    wallpaperPage = (wallpaperPage + (x < 106 ? pages - 1 : 1)) % pages;
    drawWallpaperSettings();
    return true;
  }
  if (y < WALL_Y0) return true;
  int row = (y - WALL_Y0) / WALL_PITCH;
  int item = wallpaperPage * WALL_ROWS + row;
  if (row >= WALL_ROWS || item >= wallpaperItemCount()) return true;
  DeviceSettings& edit = editSettings();
  const char* name = item == 0 ? "" : photoListName(item - 1);
  if (strcmp(edit.wallpaper, name) == 0) return true;
  if (strlen(name) >= sizeof(edit.wallpaper) || !validWallpaperName(name, sizeof(edit.wallpaper))) return true;
  strcpy(edit.wallpaper, name);
  settingsChanged();
  drawWallpaperSettings();
  return true;
}

void updateWallpaperSettings() {
  if (photoListReady() != wallpaperListWasReady) drawWallpaperSettings();
}
