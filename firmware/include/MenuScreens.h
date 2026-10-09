#pragma once
#include "MenuLayout.h"

// MORE (root-tab app launcher) and GAMES menus. Main task only.
void drawMorePage();
void drawGamesPage();
// Opens GAMES, MORE or a game (also the target of their BACK buttons).
void showMenuNode(MenuNode node);
// Returns false when the touch is outside the menu buttons, so the caller can
// fall through to the bottom navigation bar.
bool handleMoreTouch(int x, int y);
void handleGamesMenuTouch(int x, int y);
