#pragma once
#include <stdint.h>
#include "LocalClockLogic.h"
#include "Pages.h"

// World clocks on screen: the CLOCKS page rows and the Settings views that
// manage local clocks. Pure layout and hit-testing (host-tested); drawing is
// in TimeWeatherScreen.cpp and WorldClocksScreen.cpp. 320 x 240, bottom bar
// from NAV_Y (205).

// --- CLOCKS page -------------------------------------------------------------------------------
// Local clocks, two per page, readable at a glance:
//   NEW YORK                       8:42 AM     (time: text size 2)
//   Mon, Oct 13
// The "WORLD CLOCKS  EDIT >" title row opens the clock list; with no clocks,
// ADD CLOCK opens ADD WORLD CLOCK.
constexpr int LOCAL_CLOCKS_PER_PAGE = 2;
constexpr int LOCAL_CLOCK_ROW_Y0 = 142;
constexpr int LOCAL_CLOCK_ROW_PITCH = 32;
constexpr int CLOCKS_EDIT_TOP = 121;
constexpr int CLOCKS_EDIT_BOTTOM = 140;
constexpr int CLOCKS_ADD_X = 90;
constexpr int CLOCKS_ADD_Y = 162;
constexpr int CLOCKS_ADD_W = 140;
constexpr int CLOCKS_ADD_H = 36;

inline bool clocksEditHit(int x, int y) {
  return x >= 0 && x < 320 && y >= CLOCKS_EDIT_TOP && y <= CLOCKS_EDIT_BOTTOM;
}

inline bool clocksAddHit(int x, int y) {
  return x >= CLOCKS_ADD_X && x < CLOCKS_ADD_X + CLOCKS_ADD_W && y >= CLOCKS_ADD_Y && y < CLOCKS_ADD_Y + CLOCKS_ADD_H;
}

// --- WORLD CLOCKS (Settings view) ------------------------------------------------------------
// One full-width row per clock (Settings list geometry: x 10..310, 36 px
// tall, 41 px pitch; up to 4), tap for its page. Bottom bar: BACK | ADD CLOCK
// (right third). Without clocks: a large ADD CLOCK button instead.
constexpr int CLOCK_EMPTY_ADD_X = 90;
constexpr int CLOCK_EMPTY_ADD_Y = 120;
constexpr int CLOCK_EMPTY_ADD_W = 140;
constexpr int CLOCK_EMPTY_ADD_H = 46;

enum class ClockListHit : uint8_t { None, Back, Add };

inline ClockListHit clockListBarAt(int x, int y, int count) {
  if (y >= NAV_Y) return count > 0 && x >= NAV_SPLIT_2 ? ClockListHit::Add : ClockListHit::Back;
  if (count == 0 && x >= CLOCK_EMPTY_ADD_X && x < CLOCK_EMPTY_ADD_X + CLOCK_EMPTY_ADD_W &&
      y >= CLOCK_EMPTY_ADD_Y && y < CLOCK_EMPTY_ADD_Y + CLOCK_EMPTY_ADD_H) {
    return ClockListHit::Add;
  }
  return ClockListHit::None;
}

// --- ADD WORLD CLOCK ---------------------------------------------------------------------------
//   n of 4 clocks added
//   [ COMMON CITIES ]        y 76..122
//   [ SEARCH ON PHONE ]      y 132..178
// Full: "4 of 4 clocks added" and no buttons. Bottom bar: CANCEL.
constexpr int ADD_CLOCK_X = 30;
constexpr int ADD_CLOCK_W = 260;
constexpr int ADD_CLOCK_H = 46;
constexpr int ADD_CLOCK_COMMON_Y = 76;
constexpr int ADD_CLOCK_PHONE_Y = 132;

enum class AddClockHit : uint8_t { None, CommonCities, SearchOnPhone };

inline AddClockHit addClockAt(int x, int y, int count) {
  if (count >= LOCAL_CLOCKS_MAX || x < ADD_CLOCK_X || x >= ADD_CLOCK_X + ADD_CLOCK_W) return AddClockHit::None;
  if (y >= ADD_CLOCK_COMMON_Y && y < ADD_CLOCK_COMMON_Y + ADD_CLOCK_H) return AddClockHit::CommonCities;
  if (y >= ADD_CLOCK_PHONE_Y && y < ADD_CLOCK_PHONE_Y + ADD_CLOCK_H) return AddClockHit::SearchOnPhone;
  return AddClockHit::None;
}

// --- One clock: details and REMOVE ------------------------------------------------------------
constexpr int CLOCK_REMOVE_X = 90;
constexpr int CLOCK_REMOVE_Y = 150;
constexpr int CLOCK_REMOVE_W = 140;
constexpr int CLOCK_REMOVE_H = 46;

inline bool clockRemoveHit(int x, int y) {
  return x >= CLOCK_REMOVE_X && x < CLOCK_REMOVE_X + CLOCK_REMOVE_W && y >= CLOCK_REMOVE_Y &&
         y < CLOCK_REMOVE_Y + CLOCK_REMOVE_H;
}
