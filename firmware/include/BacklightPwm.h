#pragma once
#include <stdint.h>
#include "BacklightControl.h"

// LEDC driver for DASHBOARD_BACKLIGHT_PIN; scheduling logic lives in the
// host-testable BacklightControl. Main task only.
void setupBacklight();
void updateBacklight();
void backlightSetSchedule(const SolarSchedule& schedule, uint32_t now);
void backlightAcceptedTouch(uint32_t now);
