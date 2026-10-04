#pragma once

// Idle clock/photo screensaver. Main task only.
void drawScreensaverClock();
void exitScreensaver();
// Enters after the idle timeout and rotates clock/photo while active.
void updateScreensaver();
// Called by PhotoScreen only for a current (not stale/cancelled) screensaver
// request: true once the photo is drawn, false to fall back to the clock.
void onScreensaverPhotoResult(bool shown);
