#include <Arduino.h>
#include "BacklightPwm.h"

static BacklightControl backlight;
static constexpr uint8_t BACKLIGHT_CHANNEL = 0;
static int appliedBacklightDuty = -1;
static bool backlightPwmReady = false;

void updateBacklight() {
  if (!backlightPwmReady) return;
  int duty = backlight.duty(millis());
  if (duty != appliedBacklightDuty) {
    ledcWrite(BACKLIGHT_CHANNEL, duty);
    appliedBacklightDuty = duty;
  }
}

void setupBacklight() {
  pinMode(DASHBOARD_BACKLIGHT_PIN, OUTPUT);
  digitalWrite(DASHBOARD_BACKLIGHT_PIN, HIGH);
  if (ledcSetup(BACKLIGHT_CHANNEL, 5000, 8) == 0) {
    Serial.println("Backlight PWM unavailable; using full brightness");
    return;
  }
  ledcAttachPin(DASHBOARD_BACKLIGHT_PIN, BACKLIGHT_CHANNEL);
  backlightPwmReady = true;
  appliedBacklightDuty = -1;
  updateBacklight();
}

void backlightSetSchedule(const SolarSchedule& schedule, uint32_t now) {
  backlight.setSchedule(schedule, now);
}

void backlightAcceptedTouch(uint32_t now) {
  backlight.acceptedTouch(now);
}
