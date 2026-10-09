#pragma once

// HOME (root tab): date, time, weather summary, homelab health from Alerts,
// and CPU / RAM at a glance. Main task only.
void drawHomePage();
// New status or alert data: redraws only the areas that changed.
void refreshHomePage();
// False when the touch is not on a HOME area (the caller handles the nav bar).
bool handleHomeTouch(int x, int y);
