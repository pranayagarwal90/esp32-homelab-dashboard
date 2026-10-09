#pragma once
#include <stdint.h>
#include "Pages.h"
#include "UiIcons.h"
#include "UiTheme.h"

// Pure MORE launcher and GAMES menu layout, shared with host tests.

// --- MORE: a data-driven app launcher ------------------------------------------------
// Four columns of 74x78 tiles, two rows per page. Adding an app is one row in
// MORE_APPS; a ninth app makes a second page automatically.

struct MoreApp {
  const char* label;
  UiIcon icon;
  uint16_t tile;   // Tile colour.
  uint16_t accent; // Icon accent (sun, LEDs).
  Page page;
};

static const MoreApp MORE_APPS[] = {
  {"HOMESERVER", UiIcon::HomeServer, UiColor::Grey, UiColor::Green, PAGE_HOMESERVER},
  {"SERVICES", UiIcon::Services, UiColor::Teal, UiColor::White, PAGE_SERVICES},
  {"WEATHER", UiIcon::Weather, UiColor::Blue, UiColor::Yellow, PAGE_WEATHER},
  {"CALENDAR", UiIcon::Calendar, UiColor::Red, UiColor::White, PAGE_CALENDAR},
  {"PHOTOS", UiIcon::Photos, UiColor::Purple, UiColor::Yellow, PAGE_PHOTOS},
  {"ALERTS", UiIcon::Alerts, UiColor::Orange, UiColor::White, PAGE_ALERTS},
  {"GAMES", UiIcon::Games, UiColor::Green, UiColor::White, PAGE_GAMES},
  {"CLOCKS", UiIcon::Clocks, UiColor::Grey, UiColor::White, PAGE_TIME},
};
constexpr int MORE_APP_COUNT = sizeof(MORE_APPS) / sizeof(MORE_APPS[0]);

constexpr int MORE_COLS = 4;
constexpr int MORE_ROWS = 2;
constexpr int MORE_PER_PAGE = MORE_COLS * MORE_ROWS;
constexpr int MORE_TILE_X0 = 6;
constexpr int MORE_TILE_W = 74;
constexpr int MORE_TILE_GAP_X = 4;
constexpr int MORE_TILE_Y0 = 42;
constexpr int MORE_TILE_H = 78;
constexpr int MORE_TILE_GAP_Y = 4;
constexpr int MORE_ICON_BOX = 44;   // Coloured rounded square.
constexpr int MORE_ICON = 30;       // Glyph inside it.

inline int morePageCount() {
  return (MORE_APP_COUNT + MORE_PER_PAGE - 1) / MORE_PER_PAGE;
}

inline int moreTileX(int col) { return MORE_TILE_X0 + col * (MORE_TILE_W + MORE_TILE_GAP_X); }
inline int moreTileY(int row) { return MORE_TILE_Y0 + row * (MORE_TILE_H + MORE_TILE_GAP_Y); }

// App index under a touch on a MORE page, or -1. Tiles own the gaps around
// them (resistive touch); the header and bottom bar are excluded.
inline int moreAppAt(int x, int y, int page) {
  if (x < 0 || x >= 320 || y < MORE_TILE_Y0 - 2 || y >= 205) return -1;
  int row = (y - (MORE_TILE_Y0 - 2)) / (MORE_TILE_H + MORE_TILE_GAP_Y);
  if (row >= MORE_ROWS) row = MORE_ROWS - 1;
  int col = (x - MORE_TILE_X0 + MORE_TILE_GAP_X / 2) / (MORE_TILE_W + MORE_TILE_GAP_X);
  if (col < 0) col = 0;
  if (col >= MORE_COLS) col = MORE_COLS - 1;
  int index = page * MORE_PER_PAGE + row * MORE_COLS + col;
  return page >= 0 && index < MORE_APP_COUNT ? index : -1;
}

// Paging (only drawn when there is more than one page): the header's left and
// right thirds.
enum class MorePaging : uint8_t { None, Prev, Next };
inline MorePaging morePagingAt(int x, int y) {
  if (morePageCount() < 2 || y >= 36) return MorePaging::None;
  if (x < 107) return MorePaging::Prev;
  if (x >= 214) return MorePaging::Next;
  return MorePaging::None;
}

// --- GAMES -----------------------------------------------------------------------------

enum class MenuNode : uint8_t { None, More, Games, TicTacToe, Reaction, Snake, Memory, Simon };

// BACK: game -> GAMES -> MORE.
inline MenuNode menuParent(MenuNode node) {
  switch (node) {
    case MenuNode::TicTacToe:
    case MenuNode::Reaction:
    case MenuNode::Snake:
    case MenuNode::Memory:
    case MenuNode::Simon: return MenuNode::Games;
    default: return MenuNode::More;
  }
}

// GAMES: 140x44 buttons in two columns (rows at 44, 96, 148); Simon Says
// spans the last row.
constexpr int GAMES_Y[3] = {44, 96, 148};
constexpr int GAMES_H = 44;

inline MenuNode gamesItemAt(int x, int y) {
  if (y < 40 || y > 200) return MenuNode::None;
  static const MenuNode ITEMS[3][2] = {
    {MenuNode::TicTacToe, MenuNode::Reaction},
    {MenuNode::Snake, MenuNode::Memory},
    {MenuNode::Simon, MenuNode::Simon},
  };
  int row = y <= 92 ? 0 : y <= 144 ? 1 : 2;
  return ITEMS[row][x < 160 ? 0 : 1];
}

// Glyph colour readable on a tile: black on bright tiles, otherwise white.
inline uint16_t glyphColorOn(uint16_t tile) {
  int r = (tile >> 11) & 0x1F, g = (tile >> 5) & 0x3F, b = tile & 0x1F;
  int luma = r * 2 * 299 + g * 587 + b * 2 * 114; // ~0..63000
  return luma > 40000 ? UiColor::Black : UiColor::White;
}
