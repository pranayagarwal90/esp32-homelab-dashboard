#pragma once

// Settings sub-pages for brightness, screensaver and wallpaper. Main task only.
void drawDisplaySettings();
void handleDisplaySettingsTouch(int x, int y);
void drawScreensaverSettings();
void handleScreensaverSettingsTouch(int x, int y);
void drawWallpaperSettings();
// Returns true if the touch was handled (the paging bar is shared with BACK).
bool handleWallpaperSettingsTouch(int x, int y);
void updateWallpaperSettings();
