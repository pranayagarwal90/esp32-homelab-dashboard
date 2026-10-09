#include <Arduino.h>
#include <WiFi.h>
#include <esp_sleep.h>
#include "PowerManager.h"
#include "BacklightPwm.h"
#include "BluetoothControl.h"
#include "Display.h"
#include "SettingsLogic.h"
#include "SettingsStore.h"
#include "SystemAnimation.h"
#include "TouchHandler.h"

static bool restarting = false;
static bool sleeping = false;
static bool releasedSeen = false;
static uint32_t releasedSince = 0;

void restartDevice() {
  // Settings are saved first; the reboot follows the ~0.9 s animation.
  flushSettings();
  Serial.println("Restarting (Settings)");
  restarting = true;
  systemAnimationStart(SystemAnimationType::Restart);
}

void beginSleep() {
  flushSettings();
  sleeping = true;
  releasedSeen = false;
  systemAnimationStart(SystemAnimationType::Sleep);
}

bool sleepPending() {
  return sleeping;
}

static void enterDeepSleep() {
  flushSettings();
  // ILI9341: display off, then sleep in (lowest panel current).
  tft.writecommand(0x28);
  tft.writecommand(0x10);
  backlightOffForSleep();

  // PENIRQ idles high only if the line is pulled up; otherwise touch wake is
  // unreliable and only RST wakes the device.
  pinMode(TOUCH_IRQ_PIN, INPUT);
  bool touchWake = digitalRead(TOUCH_IRQ_PIN) == HIGH;
  if (touchWake) esp_sleep_enable_ext0_wakeup((gpio_num_t)TOUCH_IRQ_PIN, 0);
  Serial.printf("Entering deep sleep; touch wake %s, RST always wakes\n", touchWake ? "armed" : "unavailable");
  Serial.flush();

  stopBluetoothForSleep();
  WiFi.disconnect(true);
  esp_deep_sleep_start();
}

bool updatePower() {
  if (restarting) {
    if (systemAnimationUpdate()) return true;
    Serial.flush();
    ESP.restart();
  }
  if (!sleeping) return false;
  // The release wait runs alongside the animation, as before; deep sleep
  // starts once both are done.
  bool playing = systemAnimationUpdate();
  bool released = sleepReady(touchPressed(), millis(), releasedSeen, releasedSince);
  if (!playing && released) enterDeepSleep();
  return true;
}

void logWakeReason() {
  esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
  if (cause == ESP_SLEEP_WAKEUP_EXT0) Serial.println("Woke from Sleep by touch");
  else if (cause != ESP_SLEEP_WAKEUP_UNDEFINED) Serial.printf("Woke from sleep (cause %d)\n", (int)cause);
}
