#include "RadarLogic.h"
#include "UiIconShapes.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static void testPaths() {
  assert(radarPathValid("/radar/1791559800.jpg"));
  assert(!radarPathValid(nullptr) && !radarPathValid("") && !radarPathValid("/radar/"));
  assert(!radarPathValid("/photos/a.jpg"));                    // Only /radar/.
  assert(!radarPathValid("http://evil/radar/1.jpg"));          // No remote URLs.
  assert(!radarPathValid("https://tilecache.rainviewer.com/v2/radar/x"));
  assert(!radarPathValid("/radar/../server.py"));
  assert(!radarPathValid("/radar/1.jpg?x=1") && !radarPathValid("/radar/a b.jpg"));
  assert(!radarPathValid("/radar/sub/1.jpg"));
  char longPath[64] = "/radar/";
  memset(longPath + 7, '1', 30);
  assert(!radarPathValid(longPath));                           // Too long for the slot.
}

static void testMetadata() {
  RadarMeta meta;
  meta.available = true;
  assert(radarAddFrame(meta, 1000, "/radar/1000.jpg"));
  assert(!radarAddFrame(meta, 1000, "/radar/1000b.jpg")); // Not newer.
  assert(!radarAddFrame(meta, 900, "/radar/900.jpg"));    // Out of order.
  assert(!radarAddFrame(meta, 0, "/radar/0.jpg"));        // Missing time.
  assert(!radarAddFrame(meta, 2000, "http://x/2000.jpg")); // Malformed URL.
  for (uint32_t t = 1600; t <= 4000; t += 600) assert(radarAddFrame(meta, t, "/radar/f.jpg"));
  assert(meta.count == RADAR_MAX_FRAMES);
  assert(!radarAddFrame(meta, 9000, "/radar/9000.jpg"));  // Maximum frame count.
  assert(strcmp(meta.frames[0].path, "/radar/1000.jpg") == 0 && meta.frames[5].time == 4000);
  radarFinishMeta(meta);
  assert(meta.available);

  // More frames than fit: the newest are kept.
  assert(radarFirstFrame(0) == 0 && radarFirstFrame(6) == 0 && radarFirstFrame(9) == 3);

  // Empty or all-malformed metadata is unavailable, whatever it claimed.
  RadarMeta empty;
  empty.available = true;
  radarAddFrame(empty, 5, "../x");
  radarFinishMeta(empty);
  assert(!empty.available && empty.count == 0);

  assert(radarIndexOfTime(meta, 2200) == 2 && radarIndexOfTime(meta, 1234) == -1);
  RadarMeta copy = meta;
  assert(radarSameFrames(meta, copy));
  copy.frames[5].time = 4600;
  assert(!radarSameFrames(meta, copy));
}

static void testMetaSchedule() {
  RadarMeta meta;
  assert(radarMetaInterval(false, false, meta) == 0);                     // First fetch now.
  assert(radarMetaInterval(false, true, meta) == RADAR_META_RETRY_MS);     // Failed.
  meta.updating = true;
  assert(radarMetaInterval(true, false, meta) == RADAR_META_UPDATING_MS);  // Backend generating.
  meta.updating = false;
  assert(radarMetaInterval(true, false, meta) == RADAR_META_RETRY_MS);     // Unavailable.
  meta.available = true;
  assert(radarMetaInterval(true, false, meta) == RADAR_META_REFRESH_MS);
  assert(radarFrameRetryMs(1) == RADAR_FRAME_RETRY_MS);
  assert(radarFrameRetryMs(RADAR_FRAME_FAILURES_FOR_META) == RADAR_META_RETRY_MS);
}

static void testPlayer() {
  RadarPlayer p;
  assert(p.wanted() == -1);                  // No frames.
  p.setFrames(6, -1);
  assert(p.wanted() == 5);                   // Newest first.
  assert(p.readyToShow(0));                  // Nothing on screen: show at once.
  p.showed(5, 1000);
  assert(p.wanted() == 0);                   // Then loop from the oldest (wrap).
  assert(!p.readyToShow(1000 + RADAR_FRAME_MS)); // Newest frame is held longer.
  assert(p.readyToShow(1000 + RADAR_LAST_FRAME_MS));
  p.showed(0, 3000);
  assert(p.wanted() == 1);
  assert(!p.readyToShow(3000 + RADAR_FRAME_MS - 1) && p.readyToShow(3000 + RADAR_FRAME_MS));
  for (int i = 1; i < 6; i++) p.showed(i, 4000 + i);
  assert(p.wanted() == 0);
  // Rollover-safe timing.
  p.showed(2, 0xFFFFFF00u);
  assert(p.readyToShow(0xFFFFFF00u + RADAR_FRAME_MS));

  // Pause holds the current frame.
  p.playing = false;
  assert(p.wanted() == -1);
  p.playing = true;
  assert(p.wanted() == 3);

  // Screensaver / leaving: the same frame is fetched again on return.
  p.offScreen();
  assert(p.wanted() == 2 && p.readyToShow(0xFFFFFF01u));
  p.playing = false;
  assert(p.wanted() == 2);                   // Even paused, the screen is refilled.
  p.showed(2, 10);
  assert(p.wanted() == -1);
  p.playing = true;

  // A new frame list: follow the scan that is shown, or restart at the newest.
  p.setFrames(4, 1);
  assert(p.wanted() == 2);
  p.setFrames(4, -1);
  assert(p.wanted() == 3);
  p.offScreen();
  p.setFrames(3, -1);
  assert(p.wanted() == 2);
  p.setFrames(0, -1);
  assert(p.wanted() == -1);
}

static void testRequestSlot() {
  RadarRequestSlot slot;
  assert(!slot.busy());
  uint32_t a = slot.begin();
  assert(slot.busy());
  assert(slot.complete(a) && !slot.busy());   // Current result.
  uint32_t b = slot.begin();
  slot.invalidate();                          // Page left (or screensaver) mid-download.
  assert(slot.busy());                        // Still one job outstanding...
  assert(!slot.complete(b) && !slot.busy());  // ...whose result is discarded.
  uint32_t c = slot.begin();
  assert(c != b && slot.complete(c));
}

static void testLabelsAndMessages() {
  char out[16];
  radarTimeLabel(1791560400, -4 * 3600, out, sizeof(out)); // 2026-10-09 15:40 UTC.
  assert(strcmp(out, "11:40 AM") == 0);
  radarTimeLabel(1791560400, 0, out, sizeof(out));
  assert(strcmp(out, "3:40 PM") == 0);
  radarTimeLabel(0, 0, out, sizeof(out));
  assert(strcmp(out, "--") == 0);
  radarPositionLabel(2, 6, out, sizeof(out));
  assert(strcmp(out, "3/6") == 0);
  radarPositionLabel(-1, 6, out, sizeof(out));
  assert(out[0] == '\0');

  RadarMeta meta;
  using M = RadarMessage;
  assert(radarMessage(false, false, false, meta, 0) == M::Loading);     // First metadata pending.
  assert(radarMessage(false, false, true, meta, 0) == M::Unavailable);  // Backend unreachable.
  assert(radarMessage(false, true, false, meta, 0) == M::Unavailable);  // available: false.
  meta.updating = true;
  assert(radarMessage(false, true, false, meta, 0) == M::Loading);      // Being generated.
  meta.available = true;
  assert(radarMessage(false, true, false, meta, 1) == M::Loading);
  assert(radarMessage(false, true, false, meta, RADAR_FRAME_FAILURES_FOR_META) == M::Unavailable);
  assert(radarMessage(true, true, true, meta, 9) == M::None);           // Never over a frame.
}

static void testLayoutAndTouch() {
  assert(radarHitAt(50, 220) == RadarHit::PlayPause);
  assert(radarHitAt(160, 220) == RadarHit::Back);
  assert(radarHitAt(300, 220) == RadarHit::None);
  assert(radarHitAt(160, 100) == RadarHit::None); // Image is inert.
  // Image below the 36 px title bar, footer between it and the bottom bar.
  assert(RADAR_X + RADAR_W <= 320 && RADAR_Y >= 36);
  assert(RADAR_Y + RADAR_H < RADAR_FOOTER_Y && RADAR_FOOTER_Y + 8 <= 208);
  // Attribution line fits beside the position / STALE label (size 1 = 6 px).
  assert(10 + strlen("Radar: RainViewer  Map: (c) OpenStreetMap") * 6 < 262);
  // Frame timing 600-900 ms.
  assert(RADAR_FRAME_MS >= 600 && RADAR_FRAME_MS <= 900);
}

static void testIcon() {
  IconShape shape = uiIconShape(UiIcon::Radar);
  assert(shape.steps && shape.count > 0);
  assert(shape.steps != uiIconShape(UiIcon::Weather).steps);
}

int main() {
  testPaths();
  testMetadata();
  testMetaSchedule();
  testPlayer();
  testRequestSlot();
  testLabelsAndMessages();
  testLayoutAndTouch();
  testIcon();
  puts("Radar logic tests passed: paths, metadata, schedule, player, requests, labels, touch, icon");
}
