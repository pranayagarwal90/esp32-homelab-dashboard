#pragma once
#include "BackendMode.h"

// Chrome shared by several screens. Main task only.
// Title plus LIVE / OFFLINE (hidden without a backend: BackendMode.h).
void drawHeader(const char* title);
// Header for tools and games: title plus an optional right-aligned value
// (score, moves) instead of the LIVE/OFFLINE status.
void drawTitleBar(const char* title, const char* right = nullptr);
// Redraws only the right-hand value of drawTitleBar().
void drawTitleBarValue(const char* right);
void drawNavigation();
void drawBackBar(const char* left = nullptr, const char* center = "BACK", const char* right = nullptr);
void drawMenuButton(int x, int y, int w, int h, const char* label);
// A backend feature without a backend: a calm, centred "not configured"
// title at y, its hint and where to set it up (BackendMode.h).
void drawBackendNotice(BackendFeature feature, int y);
