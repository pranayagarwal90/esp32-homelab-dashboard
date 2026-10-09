#pragma once

// Simon Says. Owns its sequence; playback restarts the round after leaving
// the page. Main task only.
void drawSimonPage();
void updateSimonGame();
void handleSimonTouch(int x, int y);
