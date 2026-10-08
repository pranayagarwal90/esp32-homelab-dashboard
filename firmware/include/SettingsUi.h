#pragma once
#include <Arduino.h>

// Drawing helpers shared by the Settings sub-pages (rows from SettingsLogic.h).
void drawSettingsFrame(const char* title, const char* backLabel = "BACK");
void drawSettingRow(int row, const char* label, const char* value);
// Redraws only the value area of a row (no full-screen flicker).
void drawSettingValue(int row, const char* value);
void drawSettingToggle(int row, bool on);
void drawSettingSteppers(int row);
// Left-aligned info line for read-only pages (y in pixels, text size 1).
void drawInfoLine(int y, const char* label, const char* value);
void drawInfoValue(int y, const char* value);
