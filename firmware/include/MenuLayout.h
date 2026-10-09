#pragma once
#include <stdint.h>

// Pure MORE / GAMES / TOOLS menu layout, hit-testing and BACK targets, shared
// with host tests.

// MORE: four rows of two 140x36 buttons (40 px pitch); TOOLS spans the last row.
enum class MoreItem : uint8_t { None, Time, Calendar, Games, Photos, Alerts, Settings, Tools };
constexpr int MORE_Y0 = 42;
constexpr int MORE_H = 36;
constexpr int MORE_PITCH = 40;

inline MoreItem moreItemAt(int x, int y) {
  if (y < MORE_Y0 - 2 || y > 200) return MoreItem::None; // Header / nav bar.
  int row = (y - (MORE_Y0 - 2)) / MORE_PITCH;
  if (row > 3) row = 3;
  static const MoreItem ITEMS[4][2] = {
    {MoreItem::Time, MoreItem::Calendar},
    {MoreItem::Games, MoreItem::Photos},
    {MoreItem::Alerts, MoreItem::Settings},
    {MoreItem::Tools, MoreItem::Tools},
  };
  return ITEMS[row][x < 160 ? 0 : 1];
}

// The tools/games part of the navigation tree.
enum class MenuNode : uint8_t {
  None, More, Tools, Stopwatch, Games, TicTacToe, Reaction, Snake, Memory, Simon
};

// Where BACK goes: tool -> TOOLS, game -> GAMES, TOOLS/GAMES -> MORE.
inline MenuNode menuParent(MenuNode node) {
  switch (node) {
    case MenuNode::Stopwatch: return MenuNode::Tools;
    case MenuNode::TicTacToe:
    case MenuNode::Reaction:
    case MenuNode::Snake:
    case MenuNode::Memory:
    case MenuNode::Simon: return MenuNode::Games;
    default: return MenuNode::More;
  }
}

// GAMES: 140x44 buttons in two columns (rows at 44, 96, 148); Simon Says
// spans the last row, as SETTINGS once did on MORE.
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

// TOOLS: one 270x55 button, room for more below later.
constexpr int TOOLS_Y = 50;
constexpr int TOOLS_H = 55;

inline MenuNode toolsItemAt(int, int y) {
  return y >= TOOLS_Y - 5 && y <= TOOLS_Y + TOOLS_H + 5 ? MenuNode::Stopwatch : MenuNode::None;
}
