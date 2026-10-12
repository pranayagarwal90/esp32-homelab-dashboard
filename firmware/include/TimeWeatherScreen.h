#pragma once

// CLOCKS: local time, weather and the backend's world clocks (three per
// page, PREV / NEXT when there are more). Main task only.
void drawTimePage();
void handleTimeTouch(int x, int y);
