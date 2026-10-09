#pragma once

// MORE > TIME / WEATHER > (tap the weather) > WEATHER: current conditions,
// today, six hours ahead and an animated scene. Main task only.
void drawWeatherPage();
// New status data: redraws only the text that changed and keeps the scene
// (rebuilt only if the condition or day/night changed).
void refreshWeatherPage();
void handleWeatherTouch(int x, int y);
// Every loop(): animation frames and day/night changes while visible.
void updateWeatherPage();
