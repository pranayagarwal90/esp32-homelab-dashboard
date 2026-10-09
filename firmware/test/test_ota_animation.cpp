#include "OtaAnimationLogic.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static bool samePoint(WalkerPoint a, WalkerPoint b) {
  return a.x == b.x && a.y == b.y;
}

// Every point a pose draws, with bob applied as drawWalker() does.
static void checkInsideBox(const WalkerFrame& f) {
  const WalkerPoint bobbed[] = {WALKER_HIP, WALKER_SHOULDER, f.leftElbow, f.leftHand, f.rightElbow,
                                f.rightHand, f.leftKnee, f.rightKnee};
  for (const WalkerPoint& p : bobbed) {
    int y = p.y + f.bob;
    assert(p.x >= WALKER_MIN_X && p.x + 1 <= WALKER_MAX_X);
    assert(y >= WALKER_MIN_Y && y <= WALKER_MAX_Y);
  }
  const WalkerPoint feet[] = {f.leftFoot, f.rightFoot};
  for (const WalkerPoint& p : feet) {
    assert(p.x >= WALKER_MIN_X && p.x + 1 <= WALKER_MAX_X);
    assert(p.y >= WALKER_MIN_Y && p.y <= 0); // On or above the ground.
  }
  int headTop = WALKER_HEAD.y + f.bob - WALKER_HEAD_R;
  assert(headTop >= WALKER_MIN_Y);
  assert(WALKER_HEAD.x - WALKER_HEAD_R >= WALKER_MIN_X && WALKER_HEAD.x + WALKER_HEAD_R <= WALKER_MAX_X);
}

static void testWalkCycle() {
  for (int i = 0; i < WALKER_FRAMES; i++) {
    const WalkerFrame& f = WALK_CYCLE[i];
    checkInsideBox(f);
    // A real stride: consecutive frames move limbs, not just the position.
    const WalkerFrame& next = WALK_CYCLE[(i + 1) % WALKER_FRAMES];
    int changed = !samePoint(f.leftHand, next.leftHand) + !samePoint(f.rightHand, next.rightHand) +
                  !samePoint(f.leftFoot, next.leftFoot) + !samePoint(f.rightFoot, next.rightFoot);
    assert(changed >= 3);
    // The second half mirrors the first: left and right swap.
    const WalkerFrame& m = WALK_CYCLE[(i + WALKER_FRAMES / 2) % WALKER_FRAMES];
    assert(m.bob == f.bob);
    assert(samePoint(m.leftHand, f.rightHand) && samePoint(m.rightHand, f.leftHand));
    assert(samePoint(m.leftFoot, f.rightFoot) && samePoint(m.rightFoot, f.leftFoot));
    assert(samePoint(m.leftKnee, f.rightKnee) && samePoint(m.leftElbow, f.rightElbow));
    // At least one foot is planted on the ground in every frame.
    assert(f.leftFoot.y == 0 || f.rightFoot.y == 0);
  }
  // Contact frames: arms swing opposite to the legs on the same side.
  const WalkerFrame& contact = WALK_CYCLE[0];
  assert(contact.rightFoot.x > 0 && contact.leftFoot.x < 0);   // Right leg forward.
  assert(contact.leftHand.x > 0 && contact.rightHand.x < 0);   // Left arm forward.
  // The body bobs: lower on the down frame, higher when passing.
  assert(WALK_CYCLE[1].bob > WALK_CYCLE[0].bob && WALK_CYCLE[2].bob < WALK_CYCLE[0].bob);
  // Standing pose: both feet planted.
  checkInsideBox(WALKER_STANDING);
  assert(WALKER_STANDING.leftFoot.y == 0 && WALKER_STANDING.rightFoot.y == 0);
}

static void testProgressMapping() {
  assert(otaPercent(0, 1000) == 0);
  assert(otaPercent(500, 1000) == 50);
  assert(otaPercent(999, 1000) == 99); // Truncated: 100 only when done.
  assert(otaPercent(1000, 1000) == 100);
  assert(otaPercent(1500, 1000) == 100); // Clamped.
  assert(otaPercent(10, 0) == 0);        // No size reported.
  assert(otaPercent(4000000000u, 4000000001u) == 99); // No 32-bit overflow.

  assert(otaWalkerX(0) == OTA_WALK_START_X);
  assert(otaWalkerX(100) == OTA_WALK_END_X);
  assert(otaWalkerX(200) == OTA_WALK_END_X);
  int mid = otaWalkerX(50);
  assert(mid > 120 && mid < 150); // Roughly the centre.
  for (int p = 1; p <= 100; p++) assert(otaWalkerX(p) >= otaWalkerX(p - 1));
  // Never walks into the house or off the left edge.
  assert(OTA_WALK_START_X + WALKER_MIN_X >= 0);
  assert(OTA_WALK_END_X + WALKER_MAX_X < OTA_HOME_X);
  // The walking lane stays between the header and the progress bar.
  assert(OTA_GROUND_Y + WALKER_MIN_Y - 1 > 36);
  assert(OTA_GROUND_Y + 2 < OTA_BAR_Y);

  assert(otaBarFill(0) == 0 && otaBarFill(100) == OTA_BAR_W - 4 && otaBarFill(150) == OTA_BAR_W - 4);
  assert(otaBarFill(50) == (OTA_BAR_W - 4) / 2);
}

static void testFrameTiming() {
  OtaAnimation a;
  assert(a.screen == OtaScreen::Idle);
  assert(!a.tick(5000) && !a.setProgress(10, 20)); // Inactive: nothing animates.

  a.begin(1000);
  assert(a.screen == OtaScreen::Running && a.frame == 0 && a.percent == 0);
  assert(!a.tick(1000 + OTA_FRAME_MS - 1));
  assert(a.tick(1000 + OTA_FRAME_MS) && a.frame == 1);
  assert(!a.tick(1000 + OTA_FRAME_MS + 10));
  // Frames cycle through all six and wrap.
  uint32_t now = 1000 + OTA_FRAME_MS;
  for (int i = 2; i < 2 + WALKER_FRAMES; i++) {
    now += OTA_FRAME_MS;
    assert(a.tick(now) && a.frame == i % WALKER_FRAMES);
  }
  // A long callback gap advances only one frame (no catch-up burst).
  uint8_t before = a.frame;
  assert(a.tick(now + 5000) && a.frame == (before + 1) % WALKER_FRAMES);
  assert(!a.tick(now + 5001));

  // Across millis() rollover.
  OtaAnimation w;
  w.begin(0xFFFFFFF0u);
  assert(!w.tick(0x00000050u)); // 96 ms.
  assert(w.tick(0x00000080u));  // 144 ms.

  // Progress: redraw only when the percentage changes.
  OtaAnimation p;
  p.begin(0);
  assert(!p.setProgress(0, 1000));
  assert(p.setProgress(5, 100) && p.percent == 5);
  assert(!p.setProgress(5, 100));
  assert(!p.setProgress(59, 1000)); // Still 5 %.
  assert(p.setProgress(60, 1000) && p.percent == 6);
}

static void testCompletionAndErrors() {
  OtaAnimation a;
  a.begin(0);
  a.setProgress(40, 100);
  a.complete();
  assert(a.screen == OtaScreen::Complete && a.percent == 100);
  assert(!a.tick(10000) && !a.setProgress(50, 100)); // Walking stops.
  assert(!a.holdError(10000));

  // Error: first one wins, held for the hold time, then idle.
  OtaAnimation e;
  e.begin(0);
  e.setProgress(47, 100);
  assert(e.fail(2, 1000));
  assert(!e.fail(4, 1001)); // Connect error followed by end error.
  assert(e.errorCode == 2 && e.percent == 47);
  assert(!e.tick(5000)); // No animation after failure.
  assert(e.holdError(1000) && e.holdError(1000 + OTA_ERROR_HOLD_MS - 1));
  assert(!e.holdError(1000 + OTA_ERROR_HOLD_MS));
  assert(e.screen == OtaScreen::Idle);
  assert(!e.holdError(1000 + OTA_ERROR_HOLD_MS + 1));

  // Errors before onStart (begin/auth) still show and expire.
  OtaAnimation early;
  assert(early.fail(1, 0xFFFFF000u));
  assert(early.holdError(0x00000100u)); // Rollover inside the hold.
  assert(!early.holdError(0xFFFFF000u + OTA_ERROR_HOLD_MS));

  // A new attempt during the hold restarts cleanly.
  OtaAnimation retry;
  retry.fail(3, 0);
  retry.begin(500);
  assert(retry.screen == OtaScreen::Running && retry.errorCode == -1 && !retry.holdError(600));
  assert(retry.fail(3, 700)); // And can fail again.

  assert(strcmp(otaErrorText(3), "Receive failed") == 0);
  assert(strcmp(otaErrorText(4), "Could not finish update") == 0);
  assert(strcmp(otaErrorText(99), "Update failed") == 0);
}

int main() {
  testWalkCycle();
  testProgressMapping();
  testFrameTiming();
  testCompletionAndErrors();
  puts("OTA animation tests passed: walk cycle, progress mapping, frame timing, completion, errors");
}
