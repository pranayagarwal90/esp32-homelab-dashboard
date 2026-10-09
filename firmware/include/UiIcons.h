#pragma once
#include <stdint.h>

// Small vector icons drawn with TFT primitives (no bitmaps, fonts or heap).
// Shapes are defined on a 24x24 grid (UiIconShapes.h) and scaled to `size`.
enum class UiIcon : uint8_t {
  Home, More, Settings, HomeServer, Services, Weather, Calendar, Photos, Alerts, Games,
  Clocks, Stopwatch, System, Connectivity, Display, Utilities, Wifi, Bluetooth, Brightness,
  Screensaver, Firmware, DeviceInfo, Restart, Sleep,
  Count
};

// Draws `icon` in a size x size box at (x, y). `color` is the main colour,
// `accent` the secondary (e.g. a sun), `background` what cut-outs restore.
void drawUiIcon(UiIcon icon, int x, int y, int size, uint16_t color, uint16_t accent, uint16_t background);
