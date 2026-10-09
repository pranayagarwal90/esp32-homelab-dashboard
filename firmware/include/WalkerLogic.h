#pragma once
#include <stdint.h>

// Pure stick-figure walk cycle shared by the OTA and sleep animations (and
// their host tests). Drawn by WalkerDraw.

struct WalkerPoint {
  int8_t x, y;
};

// Coordinates relative to the point on the ground under the hips (y up is
// negative). Head, shoulders, hips, elbows, hands and knees move with `bob`;
// feet are on the ground and do not. "Left" is the far side (drawn dimmer).
struct WalkerFrame {
  int8_t bob; // Body offset, positive = lower.
  WalkerPoint leftElbow, leftHand, rightElbow, rightHand;
  WalkerPoint leftKnee, rightKnee;
  WalkerPoint leftFoot, rightFoot;
};

constexpr WalkerPoint WALKER_HIP = {0, -26};
constexpr WalkerPoint WALKER_SHOULDER = {2, -45}; // Slight forward lean.
constexpr WalkerPoint WALKER_HEAD = {4, -54};
constexpr int WALKER_HEAD_R = 7;

// Six frames: contact, down, passing, then the same with the sides swapped.
// Each arm swings opposite to the leg on its side.
constexpr int WALKER_FRAMES = 6;
static const WalkerFrame WALK_CYCLE[WALKER_FRAMES] = {
  // Contact: right leg forward, left arm forward.
  {0, {6, -35}, {11, -26}, {-2, -35}, {-8, -27}, {-5, -13}, {6, -14}, {-11, -2}, {11, 0}},
  // Down: weight on the right leg, left foot lifting.
  {1, {5, -35}, {8, -26}, {-1, -35}, {-5, -26}, {-3, -12}, {5, -13}, {-9, -4}, {7, 0}},
  // Passing: left leg swings through, arms close to the body.
  {-1, {3, -35}, {4, -25}, {0, -35}, {-1, -25}, {4, -13}, {2, -13}, {-1, -5}, {1, 0}},
  // Mirror: left leg forward, right arm forward.
  {0, {-2, -35}, {-8, -27}, {6, -35}, {11, -26}, {6, -14}, {-5, -13}, {11, 0}, {-11, -2}},
  {1, {-1, -35}, {-5, -26}, {5, -35}, {8, -26}, {5, -13}, {-3, -12}, {7, 0}, {-9, -4}},
  {-1, {0, -35}, {-1, -25}, {3, -35}, {4, -25}, {2, -13}, {4, -13}, {1, 0}, {-1, -5}},
};

// Arrived: standing with the near arm raised.
static const WalkerFrame WALKER_STANDING = {
  0, {1, -35}, {1, -25}, {10, -47}, {14, -56}, {-1, -13}, {2, -13}, {-3, 0}, {4, 0}
};

// Every pose lies inside this box (local coordinates), so erasing it is safe.
constexpr int WALKER_MIN_X = -14;
constexpr int WALKER_MAX_X = 15; // Lines are drawn 2 px wide (x and x + 1).
constexpr int WALKER_MIN_Y = -62;
constexpr int WALKER_MAX_Y = 0;
