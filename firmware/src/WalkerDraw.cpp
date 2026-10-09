#include <Arduino.h>
#include "WalkerDraw.h"
#include "Display.h"

static void limb(int x, int y, WalkerPoint a, WalkerPoint b, int bobA, int bobB, uint16_t color) {
  // Two 1 px lines side by side read as a 2 px limb.
  for (int dx = 0; dx <= 1; dx++) {
    tft.drawLine(x + a.x + dx, y + a.y + bobA, x + b.x + dx, y + b.y + bobB, color);
  }
}

void drawWalker(int x, int groundY, const WalkerFrame& pose, bool erase) {
  int y = groundY, bob = pose.bob;
  uint16_t near = erase ? TFT_BLACK : TFT_WHITE;
  uint16_t far = erase ? TFT_BLACK : TFT_LIGHTGREY;
  // Far limbs first, then body, then near limbs on top.
  limb(x, y, WALKER_SHOULDER, pose.leftElbow, bob, bob, far);
  limb(x, y, pose.leftElbow, pose.leftHand, bob, bob, far);
  limb(x, y, WALKER_HIP, pose.leftKnee, bob, bob, far);
  limb(x, y, pose.leftKnee, pose.leftFoot, bob, 0, far);
  limb(x, y, WALKER_SHOULDER, WALKER_HIP, bob, bob, near);
  tft.drawCircle(x + WALKER_HEAD.x, y + WALKER_HEAD.y + bob, WALKER_HEAD_R, near);
  tft.drawCircle(x + WALKER_HEAD.x, y + WALKER_HEAD.y + bob, WALKER_HEAD_R - 1, near);
  limb(x, y, WALKER_HIP, pose.rightKnee, bob, bob, near);
  limb(x, y, pose.rightKnee, pose.rightFoot, bob, 0, near);
  limb(x, y, WALKER_SHOULDER, pose.rightElbow, bob, bob, near);
  limb(x, y, pose.rightElbow, pose.rightHand, bob, bob, near);
}
