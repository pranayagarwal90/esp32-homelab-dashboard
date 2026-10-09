#pragma once
#include "Pages.h"

// MORE > WEATHER (also from HOME and Clocks): current conditions,
// today, six hours ahead and an animated scene. Main task only.
// Opens WEATHER; BACK returns to `origin` (HOME, MORE or Clocks).
void openWeather(Page origin);
void drawWeatherPage();
// New status data: redraws only the text that changed and keeps the scene
// (rebuilt only if the condition or day/night changed).
void refreshWeatherPage();
void handleWeatherTouch(int x, int y);
// Every loop(): animation frames and day/night changes while visible.
void updateWeatherPage();
