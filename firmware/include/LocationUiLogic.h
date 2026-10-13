#pragma once
#include <stdint.h>
#include <string.h>
#include "LocationLogic.h"

// Settings > Location on the device: its buttons, the numeric postal-code
// keypad and the result list. Pure layout, hit-testing and input rules,
// shared with host tests; drawing is in PostalCodeScreen.cpp and
// WifiSettingsScreen.cpp. The search itself is the phone page's search
// (DirectWeather requestPostalSearch -> the same geocoding request).
//
// Screen 320 x 240; content between the header (0..36) and the bottom bar
// (BACK_BAR_Y = 205). Touch targets are at least 36 px tall.

// --- Location settings buttons -------------------------------------------------------------
//   [ POSTAL CODE ]   [ SEARCH ON PHONE ]     y 120..160
//                     [ REMOVE ]              y 166..202 (a location is set)

constexpr int LOCATION_BUTTON_Y = 120;
constexpr int LOCATION_BUTTON_H = 40;
constexpr int LOCATION_REMOVE_Y = 166;
constexpr int LOCATION_REMOVE_H = 36;
constexpr int LOCATION_LEFT_X = 15;
constexpr int LOCATION_RIGHT_X = 165;
constexpr int LOCATION_BUTTON_W = 140;

enum class LocationButton : uint8_t { None, PostalCode, SearchOnPhone, Remove };

inline LocationButton locationButtonAt(int x, int y, bool configured) {
  bool left = x >= LOCATION_LEFT_X && x < LOCATION_LEFT_X + LOCATION_BUTTON_W;
  bool right = x >= LOCATION_RIGHT_X && x < LOCATION_RIGHT_X + LOCATION_BUTTON_W;
  if (y >= LOCATION_BUTTON_Y && y < LOCATION_BUTTON_Y + LOCATION_BUTTON_H) {
    return left ? LocationButton::PostalCode : right ? LocationButton::SearchOnPhone : LocationButton::None;
  }
  if (configured && right && y >= LOCATION_REMOVE_Y && y < LOCATION_REMOVE_Y + LOCATION_REMOVE_H) {
    return LocationButton::Remove;
  }
  return LocationButton::None;
}

// --- Numeric keypad --------------------------------------------------------------------------
// 3 x 4 keys on the right, the entered code on the left:
//   [1] [2] [3]
//   [4] [5] [6]
//   [7] [8] [9]
//   [<] [0] [OK]
// Keys 52 x 37 px; the small gaps between them are inert.

constexpr int KEYPAD_X0 = 142;
constexpr int KEYPAD_Y0 = 42;
constexpr int KEYPAD_KEY_W = 52;
constexpr int KEYPAD_KEY_H = 37;
constexpr int KEYPAD_PITCH_X = 57;
constexpr int KEYPAD_PITCH_Y = 41;

enum class KeypadKey : uint8_t { None, Digit, Backspace, Confirm };

struct KeypadHit {
  KeypadKey key;
  char digit;      // '0'..'9' for Digit.
};

// What key (row 0..3, column 0..2) shows.
inline KeypadHit keypadKey(int row, int column) {
  if (row < 3) return {KeypadKey::Digit, char('1' + row * 3 + column)};
  if (column == 0) return {KeypadKey::Backspace, 0};
  if (column == 1) return {KeypadKey::Digit, '0'};
  return {KeypadKey::Confirm, 0};
}

inline int keypadKeyX(int column) { return KEYPAD_X0 + column * KEYPAD_PITCH_X; }
inline int keypadKeyY(int row) { return KEYPAD_Y0 + row * KEYPAD_PITCH_Y; }

inline KeypadHit keypadKeyAt(int x, int y) {
  KeypadHit none = {KeypadKey::None, 0};
  if (x < KEYPAD_X0 || y < KEYPAD_Y0) return none;
  int column = (x - KEYPAD_X0) / KEYPAD_PITCH_X, row = (y - KEYPAD_Y0) / KEYPAD_PITCH_Y;
  if (column > 2 || row > 3) return none;
  if ((x - KEYPAD_X0) % KEYPAD_PITCH_X >= KEYPAD_KEY_W || (y - KEYPAD_Y0) % KEYPAD_PITCH_Y >= KEYPAD_KEY_H) return none;
  return keypadKey(row, column);
}

// --- Postal code entry ------------------------------------------------------------------------
// Digits only (alphanumeric codes such as Canada's or the UK's: SEARCH ON
// PHONE). Lengths differ by country, so any 3..10 digits may be searched.

constexpr int POSTAL_CODE_MIN = 3;
constexpr int POSTAL_CODE_MAX = 10;

struct PostalCodeEntry {
  char digits[POSTAL_CODE_MAX + 1] = "";
  uint8_t length = 0;
};

inline void postalClear(PostalCodeEntry& e) {
  e = PostalCodeEntry();
}

// False (unchanged) for a non-digit or when full.
inline bool postalAppend(PostalCodeEntry& e, char digit) {
  if (digit < '0' || digit > '9' || e.length >= POSTAL_CODE_MAX) return false;
  e.digits[e.length++] = digit;
  e.digits[e.length] = '\0';
  return true;
}

inline bool postalBackspace(PostalCodeEntry& e) {
  if (e.length == 0) return false;
  e.digits[--e.length] = '\0';
  return true;
}

inline bool validPostalCode(const char* text) {
  if (!text) return false;
  size_t length = strnlen(text, POSTAL_CODE_MAX + 1);
  if (length < size_t(POSTAL_CODE_MIN) || length > size_t(POSTAL_CODE_MAX)) return false;
  for (size_t i = 0; i < length; i++) {
    if (text[i] < '0' || text[i] > '9') return false;
  }
  return true;
}

inline bool postalCanSearch(const PostalCodeEntry& e) {
  return validPostalCode(e.digits);
}

// --- Results -----------------------------------------------------------------------------------
// Up to LOCATION_RESULTS_MAX, three per page, each row with its own USE button:
//   Springfield                    [ USE ]     y 42..80
//   Illinois, United States
//   ...                                        y 84..122, 126..164
//   [ PREV ]       1/2          [ NEXT ]       y 166..202 (more than three)
// One result: a larger card with USE LOCATION. Nothing is saved until a USE
// button is tapped.

constexpr int POSTAL_PER_PAGE = 3;
constexpr int POSTAL_ROW_Y0 = 42;
constexpr int POSTAL_ROW_H = 38;
constexpr int POSTAL_ROW_PITCH = 42;
constexpr int POSTAL_USE_X = 236;
constexpr int POSTAL_USE_W = 72;
constexpr int POSTAL_PAGER_Y = 166;
constexpr int POSTAL_PAGER_H = 36;
constexpr int POSTAL_PREV_X = 12;
constexpr int POSTAL_NEXT_X = 220;
constexpr int POSTAL_PAGER_W = 88;
// The single card's USE LOCATION and every failure's TRY AGAIN.
constexpr int POSTAL_ACTION_X = 60;
constexpr int POSTAL_ACTION_Y = 140;
constexpr int POSTAL_ACTION_W = 200;
constexpr int POSTAL_ACTION_H = 50;

inline int postalPageCount(int count) {
  return count <= POSTAL_PER_PAGE ? 1 : (count + POSTAL_PER_PAGE - 1) / POSTAL_PER_PAGE;
}

// Rows on a page: [first, first + n).
inline int postalPageFirst(int page) { return page * POSTAL_PER_PAGE; }
inline int postalPageRows(int page, int count) {
  int rows = count - postalPageFirst(page);
  return rows < 0 ? 0 : rows > POSTAL_PER_PAGE ? POSTAL_PER_PAGE : rows;
}

inline int postalRowY(int row) { return POSTAL_ROW_Y0 + row * POSTAL_ROW_PITCH; }

// The result whose USE button is under (x, y) on this page, or -1. Only for
// two or more results (one result: postalActionAt).
inline int postalUseAt(int x, int y, int page, int count) {
  if (count < 2 || count > LOCATION_RESULTS_MAX || page < 0 || page >= postalPageCount(count)) return -1;
  if (x < POSTAL_USE_X || x >= POSTAL_USE_X + POSTAL_USE_W || y < POSTAL_ROW_Y0) return -1;
  int row = (y - POSTAL_ROW_Y0) / POSTAL_ROW_PITCH;
  if (row >= postalPageRows(page, count) || (y - POSTAL_ROW_Y0) % POSTAL_ROW_PITCH >= POSTAL_ROW_H) return -1;
  return postalPageFirst(page) + row;
}

enum class PostalPager : uint8_t { None, Prev, Next };

inline PostalPager postalPagerAt(int x, int y, int page, int count) {
  int pages = postalPageCount(count);
  if (pages < 2 || y < POSTAL_PAGER_Y || y >= POSTAL_PAGER_Y + POSTAL_PAGER_H) return PostalPager::None;
  if (x >= POSTAL_PREV_X && x < POSTAL_PREV_X + POSTAL_PAGER_W && page > 0) return PostalPager::Prev;
  if (x >= POSTAL_NEXT_X && x < POSTAL_NEXT_X + POSTAL_PAGER_W && page + 1 < pages) return PostalPager::Next;
  return PostalPager::None;
}

inline bool postalActionAt(int x, int y) {
  return x >= POSTAL_ACTION_X && x < POSTAL_ACTION_X + POSTAL_ACTION_W && y >= POSTAL_ACTION_Y &&
         y < POSTAL_ACTION_Y + POSTAL_ACTION_H;
}

// What the results view shows for a search's state (LocationSearchSnapshot).
enum class PostalScreen : uint8_t { Searching, One, Several, Message };

inline PostalScreen postalScreenFor(LocationSearchState state, int count) {
  if (state == LocationSearchState::Idle || state == LocationSearchState::Searching) return PostalScreen::Searching;
  if (state == LocationSearchState::Done && count == 1) return PostalScreen::One;
  if (state == LocationSearchState::Done && count > 1) return PostalScreen::Several;
  return PostalScreen::Message;
}

struct PostalMessage {
  const char* title;
  const char* hint;
  bool retry;      // TRY AGAIN may help (a temporary problem).
};

// No coordinates, service names or technical errors.
inline PostalMessage postalMessage(LocationSearchState state, LocationSearchError error) {
  if (state == LocationSearchState::Done) {
    return {"No places found", "Check the code, or use SEARCH ON PHONE.", false};
  }
  switch (error) {
    case LocationSearchError::NoWifi: return {"Not connected", "Connect to Wi-Fi, then try again.", true};
    case LocationSearchError::NoTime: return {"Setting the clock", "Still setting the clock. Try again shortly.", true};
    case LocationSearchError::Rejected:
      return {"Search did not work", "Check the code, or use SEARCH ON PHONE.", false};
    case LocationSearchError::Network:
    case LocationSearchError::None:
      break;
  }
  return {"Can't reach search", "Can't reach location search. Try again.", true};
}
