#pragma once
#include "SettingsLogic.h"

// PAGE_SETTINGS: a menu plus sub-views, all drawn on the main task. The
// current sub-view survives screensaver wake and status redraws.

// Opens the Settings menu (from the MORE page).
void openSettings();
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
