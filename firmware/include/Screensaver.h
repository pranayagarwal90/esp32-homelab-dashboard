#pragma once

// Idle clock/photo screensaver. Main task only.
void drawScreensaverClock();
void exitScreensaver();
// Enters after the idle timeout and rotates clock/photo while active.
void updateScreensaver();
