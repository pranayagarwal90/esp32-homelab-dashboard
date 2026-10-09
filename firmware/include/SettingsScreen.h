#pragma once
#include "SettingsLogic.h"

// PAGE_SETTINGS: a menu plus sub-views, all drawn on the main task. The
// current sub-view survives screensaver wake and status redraws.

// Opens the Settings root (the SETTINGS tab), or a given view (BACK from the
// Stopwatch returns to Utilities).
void openSettings();
void openSettingsAt(SettingsView view);
void drawSettingsPage();
void handleSettingsTouch(int x, int y);
// Call every loop(): debounced saves, periodic value refresh, Wi-Fi setup and
// sleep sequencing. Never blocks.
void updateSettings();
// True while a view must stay visible (Wi-Fi setup, confirmations, sleep).
bool settingsBlocksScreensaver();

// For Settings sub-modules.
void showSettingsView(SettingsView view);
// Shows a confirmation dialog; CANCEL returns to returnTo.
void askConfirm(ConfirmAction action, SettingsView returnTo);
