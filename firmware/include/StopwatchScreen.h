#pragma once

// TOOLS > STOPWATCH. Keeps timing while away (other pages, screensaver);
// only RESET stops and clears it. Main task only.
void drawStopwatchPage();
void handleStopwatchTouch(int x, int y);
// Call every loop(): refreshes only the time digits, at most every 100 ms.
void updateStopwatch();
