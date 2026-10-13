#pragma once

// Settings > Location > POSTAL CODE: a numeric keypad, then the search's
// results on the device. No hotspot: the search uses the normal Wi-Fi and
// runs on the DirectWeather worker. Nothing is saved until USE is tapped.
// Main task only; layout and rules in LocationUiLogic.h.

// From Location settings: a cleared keypad.
void openPostalCode();
void drawPostalCode();
void handlePostalCodeTouch(int x, int y);
void drawPostalResults();
void handlePostalResultsTouch(int x, int y);
// Every loop while the results view is shown: redraws when the search ends.
void updatePostalResults();
// Leaving the results (BACK): a search still running is ignored when it ends.
void cancelPostalSearch();
