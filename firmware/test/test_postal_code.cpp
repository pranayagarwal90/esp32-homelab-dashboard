// Settings > Location on the device (Phase 8.6.1): the numeric postal-code
// keypad, its result list and navigation (LocationUiLogic.h, SettingsLogic.h),
// and source checks that the keypad search is the phone page's search without
// the hotspot, saves only on USE, and adds no other location provider.
#include "LocationUiLogic.h"
#include "SettingsLogic.h"
#include "SetupPage.h"
#include <assert.h>
#include <fstream>
#include <sstream>
#include <stdio.h>
#include <string.h>
#include <string>

static void testEntry() {
  PostalCodeEntry e;
  assert(e.length == 0 && e.digits[0] == '\0' && !postalCanSearch(e));
  // Digits append; anything else is refused.
  assert(postalAppend(e, '2') && postalAppend(e, '0'));
  assert(!postalAppend(e, 'A') && !postalAppend(e, ' ') && !postalAppend(e, '\0') && !postalAppend(e, '-'));
  assert(strcmp(e.digits, "20") == 0 && e.length == 2);
  // Too short to search (minimum 3).
  assert(!postalCanSearch(e));
  assert(postalAppend(e, '1') && postalCanSearch(e));
  assert(postalAppend(e, '4') && postalAppend(e, '8') && strcmp(e.digits, "20148") == 0 && postalCanSearch(e));
  // Backspace removes the last digit, down to empty, then does nothing.
  assert(postalBackspace(e) && strcmp(e.digits, "2014") == 0);
  while (postalBackspace(e)) {}
  assert(e.length == 0 && e.digits[0] == '\0' && !postalBackspace(e));
  // Maximum 10 digits: the 11th is refused, nothing overflows.
  for (int i = 0; i < POSTAL_CODE_MAX; i++) assert(postalAppend(e, char('0' + i)));
  assert(!postalAppend(e, '9') && e.length == POSTAL_CODE_MAX && strcmp(e.digits, "0123456789") == 0);
  assert(postalCanSearch(e));
  postalClear(e);
  assert(e.length == 0 && e.digits[0] == '\0');
  // Not hard-coded to five digits: 3..10 digits are valid, nothing else.
  for (const char* ok : {"123", "1234", "20148", "110001", "1234567", "0123456789"}) assert(validPostalCode(ok));
  for (const char* bad : {"", "12", "12345678901", "K1A 0B1", "SW1A", "2014a", " 20148", "20-148"}) {
    assert(!validPostalCode(bad));
  }
  assert(!validPostalCode(nullptr));
}

static void testKeypad() {
  // Every key is a digit, backspace or confirm: no letters (no QWERTY).
  int digits = 0, backspace = 0, confirm = 0;
  bool seen[10] = {};
  for (int row = 0; row < 4; row++) {
    for (int column = 0; column < 3; column++) {
      KeypadHit k = keypadKey(row, column);
      if (k.key == KeypadKey::Digit) {
        assert(k.digit >= '0' && k.digit <= '9' && !seen[k.digit - '0']);
        seen[k.digit - '0'] = true;
        digits++;
      }
      backspace += k.key == KeypadKey::Backspace;
      confirm += k.key == KeypadKey::Confirm;
      // The centre of each key hits that key.
      KeypadHit hit = keypadKeyAt(keypadKeyX(column) + KEYPAD_KEY_W / 2, keypadKeyY(row) + KEYPAD_KEY_H / 2);
      assert(hit.key == k.key && hit.digit == k.digit);
    }
  }
  assert(digits == 10 && backspace == 1 && confirm == 1);
  assert(keypadKey(0, 0).digit == '1' && keypadKey(2, 2).digit == '9' && keypadKey(3, 1).digit == '0');
  assert(keypadKey(3, 0).key == KeypadKey::Backspace && keypadKey(3, 2).key == KeypadKey::Confirm);
  // Large keys, on screen, above the bottom bar.
  assert(KEYPAD_KEY_W >= 48 && KEYPAD_KEY_H >= 36);
  assert(keypadKeyX(2) + KEYPAD_KEY_W <= 320 && keypadKeyY(3) + KEYPAD_KEY_H <= BACK_BAR_Y);
  // The code display, gaps and the bottom bar are not keys.
  for (int x : {10, 100, KEYPAD_X0 - 1, keypadKeyX(0) + KEYPAD_KEY_W + 1, 319}) {
    assert(keypadKeyAt(x, keypadKeyY(0) + 5).key == KeypadKey::None);
  }
  assert(keypadKeyAt(keypadKeyX(1) + 5, keypadKeyY(1) + KEYPAD_KEY_H + 1).key == KeypadKey::None);
  assert(keypadKeyAt(keypadKeyX(1) + 5, 220).key == KeypadKey::None && keypadKeyAt(200, 20).key == KeypadKey::None);
}

static void testLocationButtons() {
  int y = LOCATION_BUTTON_Y + 5, removeY = LOCATION_REMOVE_Y + 5;
  for (bool configured : {false, true}) {
    assert(locationButtonAt(LOCATION_LEFT_X + 5, y, configured) == LocationButton::PostalCode);
    assert(locationButtonAt(LOCATION_RIGHT_X + 5, y, configured) == LocationButton::SearchOnPhone);
    assert(locationButtonAt(160, y, configured) == LocationButton::None);
  }
  // REMOVE only when a location is set.
  assert(locationButtonAt(LOCATION_RIGHT_X + 5, removeY, true) == LocationButton::Remove);
  assert(locationButtonAt(LOCATION_RIGHT_X + 5, removeY, false) == LocationButton::None);
  assert(locationButtonAt(LOCATION_LEFT_X + 5, removeY, true) == LocationButton::None);
  assert(LOCATION_REMOVE_Y + LOCATION_REMOVE_H <= BACK_BAR_Y && LOCATION_BUTTON_Y + LOCATION_BUTTON_H <= LOCATION_REMOVE_Y);
}

static void testResults() {
  // Bounded by the search's own maximum (5): two pages of three.
  assert(LOCATION_RESULTS_MAX == 5);
  assert(postalPageCount(0) == 1 && postalPageCount(1) == 1 && postalPageCount(3) == 1);
  assert(postalPageCount(4) == 2 && postalPageCount(5) == 2);
  assert(postalPageRows(0, 5) == 3 && postalPageRows(1, 5) == 2 && postalPageRows(2, 5) == 0);
  assert(postalRowY(POSTAL_PER_PAGE - 1) + POSTAL_ROW_H <= POSTAL_PAGER_Y);
  assert(POSTAL_PAGER_Y + POSTAL_PAGER_H <= BACK_BAR_Y && POSTAL_ROW_H >= 36 && POSTAL_PAGER_H >= 36);
  int useX = POSTAL_USE_X + 5;
  // USE buttons map to the result on this page.
  assert(postalUseAt(useX, postalRowY(0) + 5, 0, 5) == 0);
  assert(postalUseAt(useX, postalRowY(2) + 5, 0, 5) == 2);
  assert(postalUseAt(useX, postalRowY(0) + 5, 1, 5) == 3);
  assert(postalUseAt(useX, postalRowY(1) + 5, 1, 5) == 4);
  assert(postalUseAt(useX, postalRowY(2) + 5, 1, 5) == -1);          // No sixth result.
  assert(postalUseAt(useX, postalRowY(1) + 5, 0, 2) == 1 && postalUseAt(useX, postalRowY(2) + 5, 0, 2) == -1);
  // The place names are not buttons; gaps and other pages are inert.
  assert(postalUseAt(100, postalRowY(0) + 5, 0, 5) == -1);
  assert(postalUseAt(useX, postalRowY(0) + POSTAL_ROW_H + 1, 0, 5) == -1);
  assert(postalUseAt(useX, postalRowY(0) + 5, 2, 5) == -1 && postalUseAt(useX, postalRowY(0) + 5, -1, 5) == -1);
  // One result: the card's USE LOCATION, never a row button; none or too many: nothing.
  assert(postalUseAt(useX, postalRowY(0) + 5, 0, 1) == -1 && postalUseAt(useX, postalRowY(0) + 5, 0, 0) == -1);
  assert(postalUseAt(useX, postalRowY(0) + 5, 0, 6) == -1);
  assert(postalActionAt(POSTAL_ACTION_X + 10, POSTAL_ACTION_Y + 10) && !postalActionAt(10, 10));
  assert(POSTAL_ACTION_Y + POSTAL_ACTION_H <= BACK_BAR_Y);
  // PREV / NEXT only where there is a page to go to.
  int pagerY = POSTAL_PAGER_Y + 5;
  assert(postalPagerAt(POSTAL_NEXT_X + 5, pagerY, 0, 5) == PostalPager::Next);
  assert(postalPagerAt(POSTAL_PREV_X + 5, pagerY, 0, 5) == PostalPager::None);
  assert(postalPagerAt(POSTAL_PREV_X + 5, pagerY, 1, 5) == PostalPager::Prev);
  assert(postalPagerAt(POSTAL_NEXT_X + 5, pagerY, 1, 5) == PostalPager::None);
  assert(postalPagerAt(POSTAL_NEXT_X + 5, pagerY, 0, 3) == PostalPager::None);
  // What the results view shows.
  typedef LocationSearchState S;
  assert(postalScreenFor(S::Searching, 0) == PostalScreen::Searching);
  assert(postalScreenFor(S::Done, 1) == PostalScreen::One);
  assert(postalScreenFor(S::Done, 4) == PostalScreen::Several);
  assert(postalScreenFor(S::Done, 0) == PostalScreen::Message);
  assert(postalScreenFor(S::Failed, 0) == PostalScreen::Message);
  // Friendly words, no technical terms; retry only where it can help.
  PostalMessage none = postalMessage(S::Done, LocationSearchError::None);
  assert(strcmp(none.title, "No places found") == 0 && !none.retry);
  assert(postalMessage(S::Failed, LocationSearchError::NoTime).retry &&
         strstr(postalMessage(S::Failed, LocationSearchError::NoTime).hint, "Still setting the clock. Try again shortly."));
  assert(postalMessage(S::Failed, LocationSearchError::NoWifi).retry);
  assert(postalMessage(S::Failed, LocationSearchError::Network).retry &&
         strstr(postalMessage(S::Failed, LocationSearchError::Network).hint, "Can't reach location search"));
  assert(!postalMessage(S::Failed, LocationSearchError::Rejected).retry);
  for (LocationSearchError e : {LocationSearchError::NoWifi, LocationSearchError::NoTime, LocationSearchError::Network,
                                LocationSearchError::Rejected, LocationSearchError::None}) {
    PostalMessage m = postalMessage(S::Failed, e);
    for (const char* text : {m.title, m.hint}) {
      assert(strlen(text) > 0 && strlen(text) <= 50);
      for (const char* word : {"Open-Meteo", "API", "TLS", "HTTP", "DNS", "backend", "latitude", "ZIP"}) {
        assert(!strstr(text, word));
      }
    }
    assert(strlen(m.title) * 12 <= 300); // Fits the screen at text size 2.
  }
}

static void testNavigation() {
  // CANCEL on the keypad -> Location; BACK on the results -> the keypad;
  // BACK on Location -> Connectivity, as before.
  assert(settingsParent(SettingsView::PostalCode) == SettingsView::Location);
  assert(settingsParent(SettingsView::PostalResults) == SettingsView::PostalCode);
  assert(settingsParent(SettingsView::Location) == SettingsView::Connectivity);
  assert(!settingsBottomIsNav(SettingsView::PostalCode) && !settingsBottomIsNav(SettingsView::PostalResults));
  SettingsMenu menu;
  assert(!settingsMenuFor(SettingsView::PostalCode, menu) && !settingsMenuFor(SettingsView::PostalResults, menu));
  // The phone search keeps its confirmation, now named for the button.
  ConfirmText text = confirmText(ConfirmAction::StartLocationSetup);
  assert(strcmp(text.title, "SEARCH ON PHONE?") == 0 && strcmp(text.confirmLabel, "START") == 0);
}

static void testPortalPagesUnchanged() {
  // First run is still Wi-Fi only; the phone's Location page is still the search.
  SetupFormView setup;
  LocationPageView location;
  location.wifiConnected = true;
  std::string wifi, phone;
  renderPortalRoot(wifi, PortalMode::FirstRunWifi, setup, location);
  assert(wifi.find("<select name=net>") != std::string::npos && wifi.find("name=server") == std::string::npos &&
         wifi.find("postal") == std::string::npos && wifi.find("/location/") == std::string::npos);
  renderPortalRoot(phone, PortalMode::Location, setup, location);
  for (const char* text : {"Town, city or postal code", "<button>Search</button>", "action=/location/search"}) {
    assert(phone.find(text) != std::string::npos);
  }
  assert(phone.find("name=pass") == std::string::npos);
}

// --- Source checks ------------------------------------------------------------------------------

static std::string readSource(const char* name) {
  for (const char* prefix : {"firmware/", "", "../"}) {
    std::ifstream file(std::string(prefix) + name);
    if (file) {
      std::stringstream text;
      text << file.rdbuf();
      return text.str();
    }
  }
  fprintf(stderr, "cannot find %s (run from the repository root)\n", name);
  assert(false);
  return "";
}

static bool has(const std::string& text, const char* needle) {
  return text.find(needle) != std::string::npos;
}

static size_t occurrences(const std::string& text, const char* needle) {
  size_t count = 0;
  for (size_t at = text.find(needle); at != std::string::npos; at = text.find(needle, at + 1)) count++;
  return count;
}

static std::string functionBody(const std::string& source, const char* signature) {
  size_t start = source.find(signature);
  assert(start != std::string::npos);
  size_t end = source.find("\n}\n", start);
  assert(end != std::string::npos);
  return source.substr(start, end - start);
}

static void testSources() {
  std::string screen = readSource("src/PostalCodeScreen.cpp");
  std::string location = readSource("src/WifiSettingsScreen.cpp");
  std::string weather = readSource("src/DirectWeather.cpp");
  std::string settings = readSource("src/SettingsScreen.cpp");

  // POSTAL CODE opens the keypad, never the hotspot; SEARCH ON PHONE keeps
  // the existing confirmation -> startLocationSetup().
  std::string touch = functionBody(location, "void handleLocationSettingsTouch(int x, int y) {");
  size_t postal = touch.find("case LocationButton::PostalCode:");
  size_t phone = touch.find("case LocationButton::SearchOnPhone:");
  assert(postal != std::string::npos && phone != std::string::npos);
  std::string postalCase = touch.substr(postal, touch.find("break;", postal) - postal);
  std::string phoneCase = touch.substr(phone, touch.find("break;", phone) - phone);
  assert(has(postalCase, "openPostalCode();") && !has(postalCase, "askConfirm") && !has(postalCase, "Setup"));
  assert(has(phoneCase, "askConfirm(ConfirmAction::StartLocationSetup, SettingsView::Location);"));
  std::string run = functionBody(settings, "static void runConfirmed(");
  size_t at = run.find("case ConfirmAction::StartLocationSetup:");
  assert(has(run.substr(at, run.find("break;", at) - at), "startLocationSetup();"));
  assert(has(location, "\"POSTAL CODE\"") && has(location, "\"SEARCH ON PHONE\"") && has(location, "\"REMOVE\""));

  // The keypad screen starts no hotspot and touches no Wi-Fi or backend setting.
  for (const char* text : {"startLocationSetup", "startWifiSetup", "WifiProvisioning", "softAP", "WiFi.mode",
                           "saveLocation(", "clearLocation(", "Preferences", "saveBackend", "requestLocationSearch("}) {
    if (has(screen, text)) fprintf(stderr, "PostalCodeScreen.cpp contains %s\n", text);
    assert(!has(screen, text));
  }
  // It searches with the shared search (requestPostalSearch), and saves only
  // through chooseLocationResult() in useResult(), reached only from a USE button.
  assert(occurrences(screen, "requestPostalSearch(") == 1 && occurrences(screen, "chooseLocationResult(") == 1);
  assert(has(functionBody(screen, "static void useResult(int index) {"), "chooseLocationResult(shownGeneration, index)"));
  assert(occurrences(screen, "useResult(") == 3); // Definition + USE LOCATION + a row's USE.
  std::string results = functionBody(screen, "void handlePostalResultsTouch(int x, int y) {");
  assert(has(results, "if (postalActionAt(x, y)) useResult(0);"));
  assert(has(results, "int index = postalUseAt(x, y, resultsPage, shownCount);"));
  assert(has(results, "if (touchIsRepeat()) return;"));
  assert(has(functionBody(screen, "void handlePostalCodeTouch(int x, int y) {"), "if (touchIsRepeat()) return;"));
  // CANCEL / BACK / a failed search leave the location alone.
  assert(has(functionBody(screen, "void cancelPostalSearch() {"), "resetLocationSearch();"));
  assert(!has(functionBody(screen, "void cancelPostalSearch() {"), "Location("));
  std::string back = functionBody(settings, "static void goBack() {");
  assert(has(back, "if (view == SettingsView::PostalResults) cancelPostalSearch();") && !has(back, "saveLocation"));

  // One search for both: the same worker, request builder and parser.
  std::string postalSearch = functionBody(weather, "bool requestPostalSearch(const char* digits) {");
  assert(has(postalSearch, "if (!validPostalCode(digits)) return false;"));
  assert(has(postalSearch, "return startSearch(digits, \"Postal search\");"));
  assert(has(functionBody(weather, "bool requestLocationSearch(const char* text) {"), "return startSearch(text, \"Search\");"));
  assert(occurrences(weather, "geocodingUrl(") == 1 && occurrences(weather, "parseGeocoding(") == 1);
  assert(occurrences(weather, "xTaskCreate(") == 1 && occurrences(weather, "LocationSearchSnapshot search;") == 1);
  assert(has(weather, "Serial.printf(\"[LOCATION] Postal search: \\\"%s\\\"\\n\", digits);"));
  assert(has(weather, "Serial.printf(\"[LOCATION] %s: %d result(s)\\n\", label, count);"));
  // The search never saves: only chooseLocationResult() does.
  assert(!has(functionBody(weather, "static void runSearch("), "saveLocation"));
  assert(occurrences(weather, "saveLocation(") == 1);
  // No second Open-Meteo parser.
  for (const char* file : {"src/PostalCodeScreen.cpp", "include/LocationUiLogic.h", "include/PostalCodeScreen.h"}) {
    std::string source = readSource(file);
    for (const char* text : {"JsonDocument", "deserializeJson", "ArduinoJson", "parseGeocoding", "https://", "HTTPClient"}) {
      if (has(source, text)) fprintf(stderr, "%s contains %s\n", file, text);
      assert(!has(source, text));
    }
  }

  // No automatic location, other provider or full keyboard; no personal place.
  for (const char* file : {"src/PostalCodeScreen.cpp", "include/LocationUiLogic.h", "src/DirectWeather.cpp",
                           "include/LocationLogic.h", "src/WifiSettingsScreen.cpp", "include/SetupPage.h",
                           "src/WifiProvisioning.cpp", "include/SettingsLogic.h", "src/SettingsScreen.cpp"}) {
    std::string source = readSource(file);
    for (const char* text : {"geolocation", "ip-api", "ipapi", "ipinfo", "geoip", "freegeoip", "ipgeolocation",
                             "AUTO DETECT", "AutoDetect", "QWERTY", "qwerty", "GPS", "20148", "Ashburn", "Virginia"}) {
      if (has(source, text)) fprintf(stderr, "%s contains %s\n", file, text);
      assert(!has(source, text));
    }
  }
  // The keypad is called a postal code everywhere, not a ZIP code.
  for (const char* file : {"src/PostalCodeScreen.cpp", "include/LocationUiLogic.h", "src/WifiSettingsScreen.cpp"}) {
    assert(!has(readSource(file), "ZIP") && !has(readSource(file), "Zip"));
  }
}

int main() {
  testEntry();
  testKeypad();
  testLocationButtons();
  testResults();
  testNavigation();
  testPortalPagesUnchanged();
  testSources();
  puts("postal code tests passed");
  return 0;
}
