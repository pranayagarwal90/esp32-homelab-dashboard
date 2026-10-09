#pragma once
#include "WeatherAnimationLogic.h"

// Animated weather scene in a fixed region of the WEATHER page (SCENE_X/Y,
// SCENE_W x SCENE_H), clipped with a TFT viewport. Main task only.

// Draws the whole scene (page entry, or a changed scene). The particles are
// rebuilt only when the scene differs from the current one.
void weatherAnimationShow(WeatherScene scene);
// One frame (8 fps) while the WEATHER page is visible; never blocks.
void weatherAnimationUpdate();
