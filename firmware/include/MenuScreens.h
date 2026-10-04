#pragma once

// MORE and GAMES launcher menus. Main task only.
void drawMorePage();
void drawGamesPage();
// Returns false when the touch is outside the menu buttons, so the caller can
// fall through to the bottom navigation bar.
bool handleMoreTouch(int x, int y);
void handleGamesMenuTouch(int x, int y);
