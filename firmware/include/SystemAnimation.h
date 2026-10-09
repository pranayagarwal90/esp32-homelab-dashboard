#pragma once
#include <stdint.h>
#include "SystemAnimationLogic.h"

// Lightweight system animations drawn with TFT primitives (no task, no heap,
// no delay()). Whoever starts one owns the display until it is finished or
// cancelled: the startup screen in setup(), restart and sleep through
// updatePower(), which makes loop() skip everything else. Main task only.

// Clears the screen, draws the static scene and the first frame.
void systemAnimationStart(SystemAnimationType type);
// Draws what changed since the last call. False once a timed animation has
// played to its end (its last frame stays on screen); looping ones return
// true until replaced or cancelled.
bool systemAnimationUpdate();
bool systemAnimationActive();
void systemAnimationCancel();

// Startup (setup() only, from connectWiFi()): boot or wake intro, then the
// Wi-Fi arcs, CONNECTED or WI-FI FAILED. The connection itself is unchanged
// and runs underneath; these only draw.
void startupAnimationBegin();
// The network being tried (SSID only; never a password).
void startupAnimationSetNetwork(const char* ssid);
// Call while waiting for a connection attempt.
void startupAnimationService();
// A connection attempt timed out (the boot loop tries the other network).
void startupAnimationAttemptFailed();
// Connected: finishes the intro if needed and shows CONNECTED briefly.
void startupAnimationFinish();

// Reusable 3-dot loader: dots centred on cx, resting on baseY. Pass
// loadingPhase(elapsed); draw only when the phase changes.
void drawLoadingDots(int cx, int baseY, int phase, uint16_t color);
