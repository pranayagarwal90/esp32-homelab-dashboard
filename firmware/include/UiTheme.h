#pragma once
#include <stdint.h>

// Accent colours (RGB565) for icons and the root navigation, in one place.
// Restrained, phone-settings style; everything else uses the TFT_* basics.
namespace UiColor {
constexpr uint16_t Grey = 0x8C71;
constexpr uint16_t Blue = 0x3D7F;
constexpr uint16_t Cyan = 0x07FF;
constexpr uint16_t Green = 0x2E68;
constexpr uint16_t Yellow = 0xFE40;
constexpr uint16_t Orange = 0xFC60;
constexpr uint16_t Red = 0xF1E7;
constexpr uint16_t Purple = 0xA27F;
constexpr uint16_t Teal = 0x04F1;
constexpr uint16_t White = 0xFFFF;
constexpr uint16_t Black = 0x0000;
constexpr uint16_t NavBar = 0x18E3;     // Bottom navigation background.
constexpr uint16_t NavIdle = 0x9CF3;    // Inactive tab icon/label.
constexpr uint16_t NavActive = 0x07FF;  // Active tab icon.
constexpr uint16_t Tile = 0x2124;       // List rows / tiles background.
}
