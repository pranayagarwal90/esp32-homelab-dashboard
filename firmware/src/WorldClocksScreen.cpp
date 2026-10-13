#include <Arduino.h>
#include "WorldClocksScreen.h"
#include "AppState.h"
#include "CommonCities.h"
#include "DeviceConfig.h"
#include "Display.h"
#include "LocalClockStore.h"
#include "LocalTime.h"
#include "SettingsScreen.h"
#include "SettingsUi.h"
#include "UiHelpers.h"
#include "WorldClockUi.h"

static int selected = -1;           // ClockDetail: index into the list.
static int citiesPage = 0;
static uint32_t drawnRevision = 0;  // localClocksDisplayRevision() when drawn.
static uint32_t drawnMinute = 0;    // utcNow() / 60 when drawn.

static void printCentered(const char* text, int y, uint8_t size, uint16_t color) {
  tft.setTextSize(size);
  tft.setTextColor(color, TFT_BLACK);
  tft.setCursor((320 - tft.textWidth(text)) / 2, y);
  tft.print(text);
}

// A copy of text cut to at most chars characters (at a UTF-8 boundary).
static void fitText(const char* text, char* out, size_t chars) {
  size_t length = strlen(text);
  if (length > chars) {
    length = chars;
    while (length > 0 && (((unsigned char)text[length]) & 0xC0) == 0x80) length--;
  }
  memcpy(out, text, length);
  out[length] = '\0';
}

static void markDrawn() {
  drawnRevision = localClocksDisplayRevision();
  drawnMinute = utcNow() / 60;
}

// --- WORLD CLOCKS -------------------------------------------------------------------------------

void drawWorldClockList() {
  LocalClockList list;
  localClocks(list);
  drawSettingsFrame("WORLD CLOCKS");
  drawBackBar(nullptr, "BACK", list.count > 0 ? "ADD CLOCK" : nullptr);
  markDrawn();
  if (list.count == 0) {
    printCentered("No clocks added", 64, 2, TFT_LIGHTGREY);
    printCentered("Add a city to see its time here.", 92, 1, TFT_DARKGREY);
    drawActionButton(CLOCK_EMPTY_ADD_X, CLOCK_EMPTY_ADD_Y, CLOCK_EMPTY_ADD_W, CLOCK_EMPTY_ADD_H, "ADD CLOCK", true,
                     TFT_BLUE);
    // Existing backend clocks keep showing on CLOCKS until a local one is added.
    if (backendConfigured() && app.time.worldClocks.count > 0) {
      printCentered("Backend clocks show until you add one.", 182, 1, TFT_DARKGREY);
    }
    return;
  }
  uint32_t utc = utcNow();
  for (int i = 0; i < list.count; i++) {
    const WorldClockLocation& c = list.items[i];
    int y = SETTINGS_LIST_Y0 + i * SETTINGS_LIST_PITCH;
    tft.fillRoundRect(SETTINGS_LIST_X, y, SETTINGS_LIST_W, SETTINGS_LIST_H, 8, TFT_DARKGREY);
    tft.drawRoundRect(SETTINGS_LIST_X, y, SETTINGS_LIST_W, SETTINGS_LIST_H, 8, TFT_LIGHTGREY);
    char label[LOCATION_NAME_MAX], time[16], date[24];
    clockDisplayLabel(c.name, label, sizeof(label));
    bool large = strlen(label) <= 14;
    if (!large) label[30] = '\0';
    tft.setTextSize(large ? 2 : 1);
    tft.setTextColor(TFT_WHITE, TFT_DARKGREY);
    tft.setCursor(SETTINGS_LIST_X + 10, y + (large ? 11 : 14));
    tft.print(label);
    const char* right = formatWorldClock(c, utc, time, sizeof(time), date, sizeof(date)) ? time : "Syncing...";
    tft.setTextSize(1);
    tft.setTextColor(TFT_LIGHTGREY, TFT_DARKGREY);
    tft.setCursor(SETTINGS_LIST_X + SETTINGS_LIST_W - 26 - tft.textWidth(right), y + 14);
    tft.print(right);
    tft.setCursor(SETTINGS_LIST_X + SETTINGS_LIST_W - 14, y + 14);
    tft.print(">");
  }
}

bool handleWorldClockListTouch(int x, int y) {
  int count = localClockCount();
  switch (clockListBarAt(x, y, count)) {
    case ClockListHit::Add:
      showSettingsView(SettingsView::AddClock);
      return true;
    case ClockListHit::Back:
      return false; // Settings' BACK (to Utilities, or the CLOCKS page).
    case ClockListHit::None:
      break;
  }
  int row = settingsListRowAt(x, y, count);
  if (row < 0) return true;
  selected = row;
  showSettingsView(SettingsView::ClockDetail);
  return true;
}

// --- ADD WORLD CLOCK --------------------------------------------------------------------------

void drawAddClock() {
  int count = localClockCount();
  drawSettingsFrame("ADD WORLD CLOCK", "CANCEL");
  char text[40];
  snprintf(text, sizeof(text), "%d of %d clocks added", count, LOCAL_CLOCKS_MAX);
  if (count >= LOCAL_CLOCKS_MAX) {
    printCentered(text, 80, 2, TFT_WHITE);
    printCentered("Remove a clock to add another.", 112, 1, TFT_LIGHTGREY);
    return;
  }
  printCentered(text, 52, 1, TFT_LIGHTGREY);
  drawActionButton(ADD_CLOCK_X, ADD_CLOCK_COMMON_Y, ADD_CLOCK_W, ADD_CLOCK_H, "COMMON CITIES", true, TFT_DARKGREY);
  drawActionButton(ADD_CLOCK_X, ADD_CLOCK_PHONE_Y, ADD_CLOCK_W, ADD_CLOCK_H, "SEARCH ON PHONE", true, TFT_DARKGREY);
  printCentered("Any town or city: search on your phone.", 188, 1, TFT_DARKGREY);
}

void handleAddClockTouch(int x, int y) {
  switch (addClockAt(x, y, localClockCount())) {
    case AddClockHit::CommonCities:
      citiesPage = 0;
      showSettingsView(SettingsView::CommonCities);
      break;
    case AddClockHit::SearchOnPhone:
      askConfirm(ConfirmAction::StartClockSetup, SettingsView::AddClock);
      break;
    case AddClockHit::None:
      break;
  }
}

// --- COMMON CITIES ----------------------------------------------------------------------------

static int cityPages() {
  return pageCountFor(COMMON_CITY_COUNT, CITIES_PER_PAGE);
}

// A short note in the header's right side (e.g. "Already added").
static void headerNote(const char* text) {
  tft.fillRect(176, 8, 144, 20, TFT_DARKGREY);
  tft.setTextSize(1);
  tft.setTextColor(TFT_YELLOW, TFT_DARKGREY);
  tft.setCursor(312 - tft.textWidth(text), 14);
  tft.print(text);
}

void drawCommonCities() {
  int pages = cityPages();
  citiesPage = clampPageIndex(citiesPage, COMMON_CITY_COUNT, CITIES_PER_PAGE);
  drawSettingsFrame("COMMON CITIES", nullptr);
  drawBackBar(pages > 1 && citiesPage > 0 ? "PREV" : nullptr, "CANCEL",
              pages > 1 && citiesPage < pages - 1 ? "NEXT" : nullptr);
  for (int slot = 0; slot < CITIES_PER_PAGE; slot++) {
    int index = citiesPage * CITIES_PER_PAGE + slot;
    if (index >= COMMON_CITY_COUNT) break;
    bool added = localClockAdded(commonCityClock(index));
    drawActionButton(cityGridX(slot), cityGridY(slot), CITY_BUTTON_W, CITY_BUTTON_H, COMMON_CITIES[index].name, !added,
                     TFT_DARKGREY);
  }
}

bool handleCommonCitiesTouch(int x, int y) {
  if (y >= NAV_Y) {
    switch (pagerBarAt(x, y, cityPages())) {
      case PagerHit::Prev:
        if (citiesPage > 0) citiesPage--;
        drawCommonCities();
        return true;
      case PagerHit::Next:
        if (citiesPage < cityPages() - 1) citiesPage++;
        drawCommonCities();
        return true;
      default:
        return false; // CANCEL: Settings' BACK (to ADD WORLD CLOCK).
    }
  }
  int index = commonCityAt(x, y, citiesPage);
  if (index < 0) return true;
  ClockAddResult result = addLocalClockPlace(commonCityClock(index));
  if (result == ClockAddResult::Added) showSettingsView(SettingsView::WorldClocks);
  else headerNote(result == ClockAddResult::Full ? "4 of 4 clocks added" : clockAddResultText(result));
  return true;
}

// --- One clock ------------------------------------------------------------------------------

static void drawDetailValue(int y, const char* value) {
  char text[36];
  fitText(value, text, 35);
  drawInfoValue(y, text);
}

void drawClockDetail() {
  WorldClockLocation c;
  if (!localClockAt(selected, c)) {
    showSettingsView(SettingsView::WorldClocks);
    return;
  }
  drawSettingsFrame("WORLD CLOCK");
  markDrawn();
  static const char* labels[] = {"City", "Region", "Time zone", "Offset", "Local time"};
  for (int i = 0; i < 5; i++) drawInfoLine(50 + i * 18, labels[i], "");
  char text[2 * LOCATION_REGION_MAX + 4];
  drawDetailValue(50, c.name);
  snprintf(text, sizeof(text), "%s%s%s", c.admin1, c.admin1[0] && c.country[0] ? ", " : "", c.country);
  drawDetailValue(68, text[0] ? text : "--");
  drawDetailValue(86, c.timezone[0] ? c.timezone : "Automatic");
  if (c.offsetKnown) formatUtcOffset(c.utcOffsetSeconds, text, sizeof(text));
  drawDetailValue(104, c.offsetKnown ? text : "Syncing...");
  char time[16], date[24];
  if (formatWorldClock(c, utcNow(), time, sizeof(time), date, sizeof(date))) {
    snprintf(text, sizeof(text), "%s  %s", date, time);
    drawDetailValue(122, text);
  } else {
    drawDetailValue(122, "Syncing...");
  }
  drawMenuButton(CLOCK_REMOVE_X, CLOCK_REMOVE_Y, CLOCK_REMOVE_W, CLOCK_REMOVE_H, "REMOVE");
}

void handleClockDetailTouch(int x, int y) {
  if (clockRemoveHit(x, y)) askConfirm(ConfirmAction::RemoveClock, SettingsView::ClockDetail);
}

void removeSelectedClock() {
  if (!removeLocalClockAt(selected)) Serial.println("[WORLD CLOCK] Could not remove (NVS write failed)");
  selected = -1;
  showSettingsView(SettingsView::WorldClocks);
}

void updateWorldClockViews(SettingsView view) {
  if (view != SettingsView::WorldClocks && view != SettingsView::ClockDetail) return;
  if (localClocksDisplayRevision() == drawnRevision && utcNow() / 60 == drawnMinute) return;
  if (view == SettingsView::WorldClocks) drawWorldClockList();
  else drawClockDetail();
}
