#pragma once

// WEATHER RADAR (MORE > RADAR): loops the backend's cached radar frames.
// Downloads run on the PhotoClient worker (one at a time); decoding and
// drawing happen here on the main task.
void drawRadarPage();
void handleRadarTouch(int x, int y);
// Call every loop(): drops requests once the page is left, applies finished
// downloads, shows the next frame when due and submits the next request.
void updateRadar();
