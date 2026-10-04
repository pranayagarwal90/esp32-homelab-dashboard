#pragma once

// Chrome shared by several screens. Main task only.
void drawHeader(const char* title);
void drawNavigation();
void drawBackBar(const char* left = nullptr, const char* center = "BACK", const char* right = nullptr);
