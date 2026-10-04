#pragma once

// Tic-tac-toe against the ESP32. Owns the board; main task only.
void resetTTT();
void drawTTTPage();
void handleTTTTouch(int x, int y);
