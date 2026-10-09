#pragma once

// Chrome shared by several screens. Main task only.
void drawHeader(const char* title);
// Header for tools and games: title plus an optional right-aligned value
// (score, moves) instead of the LIVE/OFFLINE status.
void drawTitleBar(const char* title, const char* right = nullptr);
// Redraws only the right-hand value of drawTitleBar().
void drawTitleBarValue(const char* right);
void drawNavigation();
void drawBackBar(const char* left = nullptr, const char* center = "BACK", const char* right = nullptr);
void drawMenuButton(int x, int y, int w, int h, const char* label);
