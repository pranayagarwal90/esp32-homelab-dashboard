#include <Arduino.h>
#include "PostalCodeScreen.h"
#include "DirectWeather.h"
#include "Display.h"
#include "LocationStore.h"
#include "LocationUiLogic.h"
#include "SettingsScreen.h"
#include "SettingsUi.h"
#include "TouchHandler.h"
#include "UiHelpers.h"
#include "UiTheme.h"

static PostalCodeEntry entry;               // Kept while the results are shown (BACK edits it).
// What the results view shows (touches act on what is on screen).
static PostalScreen drawnScreen = PostalScreen::Searching;
static uint32_t shownGeneration = 0;
static int shownCount = 0;
static bool shownRetry = false;
static int resultsPage = 0;

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

static void printCentered(const char* text, int y, uint8_t size, uint16_t color) {
  tft.setTextSize(size);
  tft.setTextColor(color, TFT_BLACK);
  tft.setCursor((320 - tft.textWidth(text)) / 2, y);
  tft.print(text);
}

// --- Keypad ------------------------------------------------------------------------------------

static void drawKey(int row, int column) {
  KeypadHit key = keypadKey(row, column);
  int x = keypadKeyX(column), y = keypadKeyY(row);
  bool enabled = key.key != KeypadKey::Confirm || postalCanSearch(entry);
  uint16_t fill = key.key == KeypadKey::Confirm ? (enabled ? TFT_BLUE : TFT_BLACK) : TFT_DARKGREY;
  uint16_t ink = enabled ? TFT_WHITE : TFT_DARKGREY;
  tft.fillRoundRect(x, y, KEYPAD_KEY_W, KEYPAD_KEY_H, 8, fill);
  tft.drawRoundRect(x, y, KEYPAD_KEY_W, KEYPAD_KEY_H, 8, enabled ? TFT_LIGHTGREY : TFT_DARKGREY);
  int cx = x + KEYPAD_KEY_W / 2, cy = y + KEYPAD_KEY_H / 2;
  if (key.key == KeypadKey::Digit) {
    char label[2] = {key.digit, '\0'};
    tft.setTextSize(2);
    tft.setTextColor(ink, fill);
    tft.setCursor(cx - tft.textWidth(label) / 2, cy - 7);
    tft.print(label);
  } else if (key.key == KeypadKey::Backspace) {
    // A left arrow.
    tft.fillTriangle(cx - 14, cy, cx - 4, cy - 8, cx - 4, cy + 8, ink);
    tft.fillRect(cx - 4, cy - 3, 18, 7, ink);
  } else {
    // A check mark, three pixels thick.
    for (int d = -1; d <= 1; d++) {
      tft.drawLine(cx - 11, cy + d, cx - 4, cy + 7 + d, ink);
      tft.drawLine(cx - 4, cy + 7 + d, cx + 11, cy - 8 + d, ink);
    }
  }
}

// The entered code, left of the keypad.
static void drawPostalField() {
  tft.fillRoundRect(8, 46, 126, 40, 8, TFT_BLACK);
  tft.drawRoundRect(8, 46, 126, 40, 8, TFT_LIGHTGREY);
  if (entry.length == 0) {
    tft.setTextSize(1);
    tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
    tft.setCursor(8 + (126 - tft.textWidth("Enter code")) / 2, 62);
    tft.print("Enter code");
    return;
  }
  tft.setTextSize(2);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setCursor(8 + (126 - tft.textWidth(entry.digits)) / 2, 59);
  tft.print(entry.digits);
}

void openPostalCode() {
  postalClear(entry);
  showSettingsView(SettingsView::PostalCode);
}

void drawPostalCode() {
  drawSettingsFrame("POSTAL CODE", "CANCEL");
  drawPostalField();
  tft.setTextSize(1);
  tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  tft.setCursor(12, 100);
  tft.print("Numbers only");
  tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
  tft.setCursor(12, 114);
  tft.print("3 to 10 digits");
  for (int row = 0; row < 4; row++) {
    for (int column = 0; column < 3; column++) drawKey(row, column);
  }
}

static void searchPostalCode() {
  resultsPage = 0;
  if (requestPostalSearch(entry.digits)) showSettingsView(SettingsView::PostalResults);
}

void handlePostalCodeTouch(int x, int y) {
  if (touchIsRepeat()) return; // One press, one digit.
  KeypadHit hit = keypadKeyAt(x, y);
  bool couldSearch = postalCanSearch(entry);
  switch (hit.key) {
    case KeypadKey::Digit:
      if (!postalAppend(entry, hit.digit)) return;
      break;
    case KeypadKey::Backspace:
      if (!postalBackspace(entry)) return;
      break;
    case KeypadKey::Confirm:
      if (couldSearch) searchPostalCode(); // Too short: ignored (the key is drawn disabled).
      return;
    case KeypadKey::None:
      return;
  }
  drawPostalField();
  if (postalCanSearch(entry) != couldSearch) drawKey(3, 2);
}

// --- Results -------------------------------------------------------------------------------------

static void regionText(const Location& l, char* out, size_t size) {
  snprintf(out, size, "%s%s%s", l.admin1, l.admin1[0] && l.country[0] ? ", " : "", l.country);
}

static void drawOneResult(const Location& l) {
  char text[LOCATION_NAME_MAX], region[2 * LOCATION_REGION_MAX + 4];
  fitText(l.name, text, 25);
  printCentered(text, 56, 2, TFT_WHITE);
  regionText(l, region, sizeof(region));
  fitText(region, text, 50);
  printCentered(text, 82, 1, TFT_LIGHTGREY);
  Location current;
  if (currentLocation(current)) {
    char line[LOCATION_NAME_MAX + 16];
    snprintf(line, sizeof(line), "Replaces %s", current.name);
    fitText(line, text, 45);
    printCentered(text, 108, 1, TFT_DARKGREY);
  }
  drawActionButton(POSTAL_ACTION_X, POSTAL_ACTION_Y, POSTAL_ACTION_W, POSTAL_ACTION_H, "USE LOCATION", true, TFT_BLUE);
}

static void drawResultRows(const LocationSearchSnapshot& s) {
  int pages = postalPageCount(s.count);
  if (resultsPage >= pages) resultsPage = pages - 1;
  int first = postalPageFirst(resultsPage);
  for (int row = 0; row < postalPageRows(resultsPage, s.count); row++) {
    const Location& l = s.results[first + row];
    int y = postalRowY(row);
    tft.fillRoundRect(10, y, POSTAL_USE_X - 16, POSTAL_ROW_H, 8, UiColor::Tile);
    char text[LOCATION_NAME_MAX], region[2 * LOCATION_REGION_MAX + 4];
    bool large = strlen(l.name) <= 17;
    fitText(l.name, text, 34);
    tft.setTextSize(large ? 2 : 1);
    tft.setTextColor(TFT_WHITE, UiColor::Tile);
    tft.setCursor(16, y + (large ? 4 : 8));
    tft.print(text);
    regionText(l, region, sizeof(region));
    fitText(region, text, 34);
    tft.setTextSize(1);
    tft.setTextColor(TFT_LIGHTGREY, UiColor::Tile);
    tft.setCursor(16, y + 25);
    tft.print(text);
    drawActionButton(POSTAL_USE_X, y, POSTAL_USE_W, POSTAL_ROW_H, "USE", true, TFT_BLUE);
  }
  if (pages < 2) return;
  drawActionButton(POSTAL_PREV_X, POSTAL_PAGER_Y, POSTAL_PAGER_W, POSTAL_PAGER_H, "PREV", resultsPage > 0,
                   TFT_DARKGREY);
  drawActionButton(POSTAL_NEXT_X, POSTAL_PAGER_Y, POSTAL_PAGER_W, POSTAL_PAGER_H, "NEXT", resultsPage + 1 < pages,
                   TFT_DARKGREY);
  char page[8];
  snprintf(page, sizeof(page), "%d/%d", resultsPage + 1, pages);
  printCentered(page, POSTAL_PAGER_Y + 14, 1, TFT_LIGHTGREY);
}

void drawPostalResults() {
  // A transient copy (main task stack) only while drawing; the results live
  // in DirectWeather.
  LocationSearchSnapshot s;
  locationSearchSnapshot(s);
  if (s.state == LocationSearchState::Idle) { // Search reset elsewhere: back to the keypad.
    showSettingsView(SettingsView::PostalCode);
    return;
  }
  drawnScreen = postalScreenFor(s.state, s.count);
  shownGeneration = s.generation;
  shownCount = s.count;
  shownRetry = false;
  drawSettingsFrame(drawnScreen == PostalScreen::Several ? "RESULTS" : "POSTAL CODE", "BACK");
  char line[LOCATION_QUERY_MAX + 16];
  switch (drawnScreen) {
    case PostalScreen::Searching:
      printCentered("Searching...", 84, 2, TFT_WHITE);
      snprintf(line, sizeof(line), "Postal code %s", s.query);
      printCentered(line, 114, 1, TFT_LIGHTGREY);
      break;
    case PostalScreen::One:
      drawOneResult(s.results[0]);
      break;
    case PostalScreen::Several:
      drawResultRows(s);
      break;
    case PostalScreen::Message: {
      PostalMessage m = postalMessage(s.state, s.error);
      shownRetry = m.retry;
      printCentered(m.title, 70, 2, s.state == LocationSearchState::Done ? TFT_WHITE : TFT_ORANGE);
      printCentered(m.hint, 100, 1, TFT_LIGHTGREY);
      if (m.retry) {
        drawActionButton(POSTAL_ACTION_X, POSTAL_ACTION_Y, POSTAL_ACTION_W, POSTAL_ACTION_H, "TRY AGAIN", true,
                         TFT_DARKGREY);
      }
      break;
    }
  }
}

// Saves only on an explicit USE, and only the result of the search on screen.
static void useResult(int index) {
  if (!chooseLocationResult(shownGeneration, index)) {
    Serial.println("[LOCATION] Not saved (search changed or NVS write failed)");
    drawPostalResults();
    return;
  }
  resetLocationSearch();
  showSettingsView(SettingsView::Location);
}

void handlePostalResultsTouch(int x, int y) {
  if (touchIsRepeat()) return; // The finger that pressed the check mark.
  switch (drawnScreen) {
    case PostalScreen::Searching:
      return;
    case PostalScreen::One:
      if (postalActionAt(x, y)) useResult(0);
      return;
    case PostalScreen::Several: {
      int index = postalUseAt(x, y, resultsPage, shownCount);
      if (index >= 0) {
        useResult(index);
        return;
      }
      PostalPager pager = postalPagerAt(x, y, resultsPage, shownCount);
      if (pager == PostalPager::None) return;
      resultsPage += pager == PostalPager::Next ? 1 : -1;
      drawPostalResults();
      return;
    }
    case PostalScreen::Message:
      if (shownRetry && postalActionAt(x, y)) searchPostalCode();
      return;
  }
}

void updatePostalResults() {
  if (drawnScreen == PostalScreen::Searching && locationSearchState() != LocationSearchState::Searching) {
    drawPostalResults();
  }
}

void cancelPostalSearch() {
  resetLocationSearch();
}
