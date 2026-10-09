#pragma once
#include <stdint.h>

// Pages and the root navigation, pure so they can be host-tested. Page values
// are never persisted (the screensaver keeps its return page in RAM only).

enum Page {
  PAGE_HOME,          // Glanceable dashboard (root tab).
  PAGE_DOCKER,        // Legacy, not reachable.
  PAGE_SERVICES,
  PAGE_MORE,          // App launcher (root tab).
  PAGE_TIME,          // Clocks: local time, weather summary, world clocks.
  PAGE_CALENDAR,
  PAGE_GAMES,
  PAGE_TTT,
  PAGE_REACTION,
  PAGE_PHOTOS,
  PAGE_SCREENSAVER,
  PAGE_SETTINGS,      // Settings hierarchy (root tab).
  PAGE_ALERTS,
  PAGE_STOPWATCH,
  PAGE_SNAKE,
  PAGE_MEMORY,
  PAGE_SIMON,
  PAGE_WEATHER,
  PAGE_HOMESERVER     // Detailed host metrics (the former HOME).
};

// Games in play own the screen: no status fetches and no status redraws.
inline bool isGamePlayPage(Page page) {
  return page == PAGE_TTT || page == PAGE_REACTION || page == PAGE_SNAKE ||
         page == PAGE_MEMORY || page == PAGE_SIMON;
}

// --- Root navigation: HOME | MORE | SETTINGS -------------------------------------------

enum class NavTab : uint8_t { Home, More, Settings };

constexpr int NAV_Y = 205;          // Touch threshold (bar drawn from 208).
constexpr int NAV_SPLIT_1 = 107;
constexpr int NAV_SPLIT_2 = 214;

// The tab under a touch in the bottom bar (three equal cells, whole cell).
inline bool rootNavAt(int x, int y, NavTab& tab) {
  if (y < NAV_Y) return false;
  tab = x < NAV_SPLIT_1 ? NavTab::Home : x < NAV_SPLIT_2 ? NavTab::More : NavTab::Settings;
  return true;
}

// Which tab a page belongs to (highlighted in the bar): HOME only on HOME,
// SETTINGS for the Settings hierarchy and its utilities, MORE for apps.
inline NavTab navTabForPage(Page page) {
  switch (page) {
    case PAGE_HOME: return NavTab::Home;
    case PAGE_SETTINGS:
    case PAGE_STOPWATCH: return NavTab::Settings;
    default: return NavTab::More;
  }
}

// --- BACK targets ------------------------------------------------------------------------

// Where BACK leads from a page. Alerts and Weather can be opened from more than
// one place and return to their origin (HOME, MORE, Clocks, HomeServer);
// games return to GAMES; other apps to MORE. Stopwatch returns to Settings
// (Utilities) and is handled by the Settings module.
inline Page appBackTarget(Page page, Page origin) {
  switch (page) {
    case PAGE_ALERTS:
    case PAGE_WEATHER:
      return origin == PAGE_HOME || origin == PAGE_TIME || origin == PAGE_HOMESERVER ? origin : PAGE_MORE;
    case PAGE_TTT:
    case PAGE_REACTION:
    case PAGE_SNAKE:
    case PAGE_MEMORY:
    case PAGE_SIMON:
      return PAGE_GAMES;
    case PAGE_STOPWATCH:
      return PAGE_SETTINGS;
    default:
      return PAGE_MORE;
  }
}
