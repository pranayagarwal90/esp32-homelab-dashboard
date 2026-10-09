#pragma once
#include <stdint.h>
#include "UiIcons.h"

// Icon geometry on a 24x24 grid (0..23), shared by the drawer and host tests.
// Steps run in order; ALT uses the accent colour, CUT the background colour
// (holes and crescents).

enum class IconOp : uint8_t { Line, Thick, Rect, FillRect, Circle, FillCircle, Triangle, RoundRect, FillRoundRect };
constexpr uint8_t ICON_ALT = 1;
constexpr uint8_t ICON_CUT = 2;
constexpr int ICON_GRID = 24;

// Line/Thick: a,b -> c,d. Rect/FillRect/RoundRect: x a, y b, w c, h d, radius e.
// Circle/FillCircle: centre a,b, radius c. Triangle: (a,b) (c,d) (e,f).
struct IconStep {
  IconOp op;
  uint8_t flags;
  int8_t a, b, c, d, e, f;
};

#define L(x0, y0, x1, y1) {IconOp::Line, 0, x0, y0, x1, y1, 0, 0}
#define T(x0, y0, x1, y1) {IconOp::Thick, 0, x0, y0, x1, y1, 0, 0}
#define R(x, y, w, h) {IconOp::Rect, 0, x, y, w, h, 0, 0}
#define FR(x, y, w, h) {IconOp::FillRect, 0, x, y, w, h, 0, 0}
#define C(x, y, r) {IconOp::Circle, 0, x, y, r, 0, 0, 0}
#define FC(x, y, r) {IconOp::FillCircle, 0, x, y, r, 0, 0, 0}
#define TRI(x0, y0, x1, y1, x2, y2) {IconOp::Triangle, 0, x0, y0, x1, y1, x2, y2}
#define RR(x, y, w, h, r) {IconOp::RoundRect, 0, x, y, w, h, r, 0}
#define FRR(x, y, w, h, r) {IconOp::FillRoundRect, 0, x, y, w, h, r, 0}
#define ALT(step) withFlags(step, ICON_ALT)
#define CUT(step) withFlags(step, ICON_CUT)

constexpr IconStep withFlags(IconStep s, uint8_t flags) {
  return {s.op, flags, s.a, s.b, s.c, s.d, s.e, s.f};
}

namespace IconShapes {
static const IconStep HOME_SHAPE[] = {
  T(2, 12, 11, 3), T(12, 3, 21, 12), R(5, 11, 14, 11), FR(10, 15, 4, 7),
};
static const IconStep MORE_SHAPE[] = {
  FRR(3, 3, 8, 8, 2), FRR(13, 3, 8, 8, 2), FRR(3, 13, 8, 8, 2), FRR(13, 13, 8, 8, 2),
};
static const IconStep GEAR_SHAPE[] = {
  FC(12, 12, 8), FR(10, 1, 4, 4), FR(10, 19, 4, 4), FR(1, 10, 4, 4), FR(19, 10, 4, 4),
  T(4, 4, 6, 6), T(4, 5, 6, 7), T(18, 4, 16, 6), T(18, 5, 16, 7),
  T(4, 19, 6, 17), T(4, 18, 6, 16), T(18, 19, 16, 17), T(18, 18, 16, 16), CUT(FC(12, 12, 3)),
};
static const IconStep HOMESERVER_SHAPE[] = {
  RR(3, 2, 18, 6, 1), RR(3, 9, 18, 6, 1), RR(3, 16, 18, 6, 1),
  L(6, 5, 11, 5), L(6, 12, 11, 12), L(6, 19, 11, 19),
  ALT(FC(17, 5, 1)), ALT(FC(17, 12, 1)), ALT(FC(17, 19, 1)),
};
static const IconStep SERVICES_SHAPE[] = {
  T(12, 12, 4, 5), T(12, 12, 19, 5), T(12, 12, 11, 20),
  FC(12, 12, 4), FC(4, 5, 3), FC(20, 5, 3), FC(12, 20, 3),
};
static const IconStep WEATHER_SHAPE[] = {
  ALT(FC(9, 9, 5)), ALT(L(9, 1, 9, 2)), ALT(L(1, 9, 2, 9)), ALT(L(3, 3, 4, 4)), ALT(L(15, 3, 14, 4)),
  FC(11, 16, 4), FC(16, 14, 5), FC(20, 17, 3), FR(9, 16, 12, 5),
};
static const IconStep CALENDAR_SHAPE[] = {
  RR(2, 4, 20, 18, 2), FR(2, 4, 20, 5), T(6, 1, 6, 6), T(16, 1, 16, 6),
  FR(6, 12, 3, 3), FR(11, 12, 3, 3), FR(16, 12, 3, 3), FR(6, 17, 3, 3), FR(11, 17, 3, 3),
};
static const IconStep PHOTOS_SHAPE[] = {
  RR(1, 3, 22, 18, 2), TRI(3, 19, 9, 10, 15, 19), TRI(11, 19, 16, 13, 21, 19), ALT(FC(17, 8, 2)),
};
static const IconStep ALERTS_SHAPE[] = {
  FC(12, 10, 6), FR(6, 10, 12, 7), FR(4, 16, 16, 2), FC(12, 20, 2), FC(12, 3, 1),
};
static const IconStep GAMES_SHAPE[] = {
  FRR(1, 6, 22, 12, 5), CUT(FR(4, 11, 7, 2)), CUT(FR(6, 9, 3, 6)), CUT(FC(16, 10, 1)), CUT(FC(19, 13, 1)),
};
static const IconStep CLOCKS_SHAPE[] = {
  C(12, 12, 10), C(12, 12, 9), T(11, 12, 11, 6), T(12, 12, 16, 15),
};
static const IconStep STOPWATCH_SHAPE[] = {
  C(12, 13, 9), C(12, 13, 8), FR(10, 1, 4, 3), T(18, 4, 20, 6), T(11, 13, 11, 8), FC(12, 13, 1),
};
static const IconStep CONNECTIVITY_SHAPE[] = {
  T(11, 10, 11, 22), T(8, 22, 15, 22), FC(12, 10, 2),
  T(7, 5, 5, 10), T(5, 10, 7, 15), T(16, 5, 18, 10), T(18, 10, 16, 15),
  L(3, 3, 1, 10), L(1, 10, 3, 17), L(20, 3, 22, 10), L(22, 10, 20, 17),
};
static const IconStep DISPLAY_SHAPE[] = {
  RR(1, 3, 22, 14, 2), RR(2, 4, 20, 12, 1), T(11, 17, 11, 20), T(6, 21, 17, 21),
};
static const IconStep UTILITIES_SHAPE[] = {
  T(5, 19, 14, 10), T(6, 20, 15, 11), FC(17, 7, 5), CUT(FC(20, 4, 2)), CUT(FR(18, 2, 4, 3)), FC(5, 19, 2),
};
// Arcs (45..135 degrees, radii 16 / 11 / 6 about (12, 20)) as segments.
// AI: a large four-point sparkle with a small one (accent).
static const IconStep AI_SPARKLE_SHAPE[] = {
  TRI(10, 4, 7, 13, 13, 13), TRI(10, 22, 7, 13, 13, 13), TRI(1, 13, 10, 10, 10, 16), TRI(19, 13, 10, 10, 10, 16),
  ALT(TRI(19, 1, 18, 5, 20, 5)), ALT(TRI(19, 9, 18, 5, 20, 5)), ALT(TRI(15, 5, 19, 4, 19, 6)), ALT(TRI(23, 5, 19, 4, 19, 6)),
};
// Explain status: a pulse line in a monitor.
static const IconStep AI_STATUS_SHAPE[] = {
  RR(1, 3, 22, 18, 3), T(4, 13, 8, 13), T(8, 13, 10, 7), T(10, 7, 13, 17), T(13, 17, 15, 11), T(15, 11, 19, 11),
};
// Explain alerts: warning triangle with a small sparkle (accent).
static const IconStep AI_ALERTS_SHAPE[] = {
  T(1, 21, 10, 4), T(10, 4, 18, 21), T(1, 21, 18, 21), FR(9, 10, 3, 6), FR(9, 17, 3, 2),
  ALT(TRI(20, 1, 19, 4, 21, 4)), ALT(TRI(20, 7, 19, 4, 21, 4)), ALT(TRI(17, 4, 20, 3, 20, 5)), ALT(TRI(23, 4, 20, 3, 20, 5)),
};
// Needs attention: an eye.
static const IconStep AI_ATTENTION_SHAPE[] = {
  T(1, 12, 6, 7), T(6, 7, 16, 7), T(16, 7, 21, 12), T(1, 12, 6, 17), T(6, 17, 16, 17), T(16, 17, 21, 12),
  FC(11, 12, 4), CUT(FC(11, 12, 1)),
};
// Suggest action: a light bulb with rays (accent).
static const IconStep AI_ACTION_SHAPE[] = {
  FC(12, 9, 6), FR(9, 14, 7, 4), FR(10, 19, 5, 2),
  ALT(L(2, 9, 3, 9)), ALT(L(21, 9, 22, 9)), ALT(L(4, 2, 5, 3)), ALT(L(20, 2, 19, 3)),
};
// Server summary: a server with a document in front (accent).
static const IconStep AI_SUMMARY_SHAPE[] = {
  RR(1, 2, 14, 6, 1), RR(1, 10, 14, 6, 1), FC(4, 5, 1), FC(4, 13, 1),
  CUT(FR(12, 8, 11, 15)), ALT(R(12, 8, 11, 15)), ALT(L(14, 12, 20, 12)), ALT(L(14, 15, 20, 15)), ALT(L(14, 18, 18, 18)),
};
static const IconStep WIFI_SHAPE[] = {
  T(1, 9, 6, 5), T(6, 5, 11, 4), T(12, 4, 17, 5), T(17, 5, 22, 9),
  T(4, 12, 8, 10), T(8, 10, 11, 9), T(12, 9, 15, 10), T(15, 10, 19, 12),
  T(8, 16, 10, 14), T(10, 14, 14, 14), T(14, 14, 15, 16), FC(12, 20, 2),
};
static const IconStep BLUETOOTH_SHAPE[] = {
  T(11, 2, 11, 22), T(11, 2, 17, 7), T(17, 7, 6, 17), T(11, 22, 17, 17), T(17, 17, 6, 7),
};
static const IconStep BRIGHTNESS_SHAPE[] = {
  FC(12, 12, 5), T(11, 1, 11, 4), T(11, 20, 11, 23), T(1, 11, 4, 11), T(19, 11, 22, 11),
  T(4, 4, 6, 6), T(18, 4, 16, 6), T(4, 19, 6, 17), T(18, 19, 16, 17),
};
static const IconStep SCREENSAVER_SHAPE[] = {
  RR(1, 2, 22, 15, 2), ALT(FC(12, 9, 5)), CUT(FC(15, 7, 4)), T(11, 17, 11, 20), T(6, 21, 17, 21),
};
static const IconStep FIRMWARE_SHAPE[] = {
  FRR(6, 6, 12, 12, 1), CUT(R(9, 9, 6, 6)),
  L(9, 2, 9, 5), L(12, 2, 12, 5), L(15, 2, 15, 5), L(9, 18, 9, 21), L(12, 18, 12, 21), L(15, 18, 15, 21),
  L(2, 9, 5, 9), L(2, 12, 5, 12), L(2, 15, 5, 15), L(18, 9, 21, 9), L(18, 12, 21, 12), L(18, 15, 21, 15),
};
static const IconStep DEVICE_INFO_SHAPE[] = {
  C(12, 12, 10), FR(11, 5, 3, 3), FR(11, 10, 3, 8),
};
static const IconStep RESTART_SHAPE[] = {
  C(12, 13, 9), C(12, 13, 8), CUT(TRI(12, 13, 12, 0, 22, 2)), TRI(10, 1, 17, 4, 10, 8),
};
static const IconStep SLEEP_SHAPE[] = {
  FC(11, 12, 9), CUT(FC(15, 8, 8)), ALT(FC(19, 17, 1)), ALT(FC(16, 21, 1)),
};
} // namespace IconShapes

#undef L
#undef T
#undef R
#undef FR
#undef C
#undef FC
#undef TRI
#undef RR
#undef FRR

struct IconShape {
  const IconStep* steps;
  uint8_t count;
};

#define ICON_SHAPE(table) IconShape{table, (uint8_t)(sizeof(table) / sizeof(table[0]))}

// The one icon -> geometry mapping. System shares the settings gear.
inline IconShape uiIconShape(UiIcon icon) {
  using namespace IconShapes;
  switch (icon) {
    case UiIcon::Home: return ICON_SHAPE(HOME_SHAPE);
    case UiIcon::More: return ICON_SHAPE(MORE_SHAPE);
    case UiIcon::Settings: return ICON_SHAPE(GEAR_SHAPE);
    case UiIcon::System: return ICON_SHAPE(GEAR_SHAPE);
    case UiIcon::HomeServer: return ICON_SHAPE(HOMESERVER_SHAPE);
    case UiIcon::Services: return ICON_SHAPE(SERVICES_SHAPE);
    case UiIcon::Weather: return ICON_SHAPE(WEATHER_SHAPE);
    case UiIcon::Calendar: return ICON_SHAPE(CALENDAR_SHAPE);
    case UiIcon::Photos: return ICON_SHAPE(PHOTOS_SHAPE);
    case UiIcon::Alerts: return ICON_SHAPE(ALERTS_SHAPE);
    case UiIcon::Games: return ICON_SHAPE(GAMES_SHAPE);
    case UiIcon::Clocks: return ICON_SHAPE(CLOCKS_SHAPE);
    case UiIcon::Stopwatch: return ICON_SHAPE(STOPWATCH_SHAPE);
    case UiIcon::Connectivity: return ICON_SHAPE(CONNECTIVITY_SHAPE);
    case UiIcon::Display: return ICON_SHAPE(DISPLAY_SHAPE);
    case UiIcon::Utilities: return ICON_SHAPE(UTILITIES_SHAPE);
    case UiIcon::Wifi: return ICON_SHAPE(WIFI_SHAPE);
    case UiIcon::Bluetooth: return ICON_SHAPE(BLUETOOTH_SHAPE);
    case UiIcon::Brightness: return ICON_SHAPE(BRIGHTNESS_SHAPE);
    case UiIcon::Screensaver: return ICON_SHAPE(SCREENSAVER_SHAPE);
    case UiIcon::Firmware: return ICON_SHAPE(FIRMWARE_SHAPE);
    case UiIcon::DeviceInfo: return ICON_SHAPE(DEVICE_INFO_SHAPE);
    case UiIcon::Restart: return ICON_SHAPE(RESTART_SHAPE);
    case UiIcon::Sleep: return ICON_SHAPE(SLEEP_SHAPE);
    case UiIcon::AiAssistant: return ICON_SHAPE(AI_SPARKLE_SHAPE);
    case UiIcon::AiStatus: return ICON_SHAPE(AI_STATUS_SHAPE);
    case UiIcon::AiAlerts: return ICON_SHAPE(AI_ALERTS_SHAPE);
    case UiIcon::AiAttention: return ICON_SHAPE(AI_ATTENTION_SHAPE);
    case UiIcon::AiAction: return ICON_SHAPE(AI_ACTION_SHAPE);
    case UiIcon::AiSummary: return ICON_SHAPE(AI_SUMMARY_SHAPE);
    case UiIcon::Count: break;
  }
  return IconShape{nullptr, 0};
}

#undef ICON_SHAPE
#undef ALT
#undef CUT

// Scales a grid coordinate to a size-pixel icon.
inline int iconScale(int v, int size) {
  return v * size / ICON_GRID;
}
