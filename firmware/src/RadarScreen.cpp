#include <Arduino.h>
#include <ArduinoJson.h>
#include <TJpg_Decoder.h>
#include "RadarScreen.h"
#include "ApiConfig.h"
#include "AppState.h"
#include "Display.h"
#include "PageRouter.h"
#include "PhotoClient.h"
#include "RadarLogic.h"
#include "UiHelpers.h"
#include "UiTheme.h"

enum class RadarJob : uint8_t { None, Meta, Frame };

static RadarMeta meta;
static bool haveMeta = false;
static bool metaFailed = false;
static bool metaAttempted = false;
static bool metaForced = false;
static uint32_t metaAttemptMs = 0;

static RadarPlayer player;
static RadarRequestSlot slot;
static RadarJob inFlightJob = RadarJob::None;
static uint32_t inFlightTime = 0; // Scan time of the frame being downloaded.

// At most one downloaded frame waits here for its turn (no framebuffer).
static uint8_t* pendingJpeg = nullptr;
static size_t pendingLength = 0;
static int pendingIndex = -1;

static int frameFailures = 0;
static uint32_t frameRetryAtMs = 0;
static bool active = false;
static RadarMessage messageShown = RadarMessage::None;
static bool messageDrawn = false;

static void handleRadarResult(PhotoResult& result);

static void releasePending() {
  free(pendingJpeg);
  pendingJpeg = nullptr;
  pendingLength = 0;
  pendingIndex = -1;
}

static void drawPlayButton() {
  drawBackBar(player.playing ? "PAUSE" : "PLAY", "BACK", nullptr);
}

// Footer right: loop position, or STALE when the backend could not refresh.
static void drawFooterStatus() {
  tft.fillRect(262, RADAR_FOOTER_Y - 1, 58, 10, TFT_BLACK);
  tft.setTextSize(1);
  char label[8];
  if (haveMeta && meta.stale) {
    tft.setTextColor(UiColor::Orange, TFT_BLACK);
    snprintf(label, sizeof(label), "STALE");
  } else {
    tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    radarPositionLabel(player.onScreen ? player.shown : -1, player.count, label, sizeof(label));
  }
  tft.setCursor(310 - tft.textWidth(label), RADAR_FOOTER_Y);
  tft.print(label);
}

// The image area says LOADING / UNAVAILABLE until a frame is on screen. Drawn
// only when the message changes; never over a frame.
static void updateMessage() {
  RadarMessage message = radarMessage(player.onScreen, haveMeta, metaFailed, meta, frameFailures);
  if (message == RadarMessage::None || (messageDrawn && message == messageShown)) return;
  messageShown = message;
  messageDrawn = true;
  tft.fillRect(RADAR_X, RADAR_Y, RADAR_W, RADAR_H, UiColor::Tile);
  const char* text = message == RadarMessage::Loading ? "LOADING RADAR..." : "RADAR UNAVAILABLE";
  tft.setTextSize(2);
  tft.setTextColor(message == RadarMessage::Loading ? TFT_LIGHTGREY : UiColor::Orange, UiColor::Tile);
  tft.setCursor(RADAR_X + (RADAR_W - tft.textWidth(text)) / 2, RADAR_Y + RADAR_H / 2 - 8);
  tft.print(text);
}

static void submit(RadarJob kind, const char* path, uint32_t maxBytes, uint32_t frameTime) {
  PhotoJob job = {};
  job.type = PhotoJobType::Fetch;
  job.client = WorkerClient::Radar;
  job.maxBytes = maxBytes;
  strncpy(job.name, path, sizeof(job.name) - 1);
  job.generation = slot.begin();
  inFlightJob = kind;
  inFlightTime = frameTime;
  if (kind == RadarJob::Meta) {
    metaAttempted = true;
    metaForced = false;
    metaAttemptMs = millis();
  }
  if (!submitPhotoJob(job)) {
    // No worker: fail now, as on a network error.
    PhotoResult failed = {job.generation, job.type, false, nullptr, nullptr, 0, WorkerClient::Radar};
    handleRadarResult(failed);
  }
}

static bool parseMeta(const uint8_t* body, size_t length, RadarMeta& out) {
  JsonDocument doc;
  if (deserializeJson(doc, (const char*)body, length)) return false;
  out = RadarMeta();
  out.available = doc["available"] | false;
  out.stale = doc["stale"] | false;
  out.updating = doc["updating"] | false;
  out.utcOffset = doc["utc_offset"] | 0;
  JsonArray frames = doc["frames"].as<JsonArray>();
  int first = radarFirstFrame((int)frames.size());
  int i = 0;
  for (JsonObject frame : frames) {
    if (i++ < first) continue;
    radarAddFrame(out, frame["time"] | 0u, frame["url"] | "");
  }
  radarFinishMeta(out);
  return true;
}

static void applyMeta(const PhotoResult& result) {
  RadarMeta parsed;
  if (!result.ok || !parseMeta(result.jpeg, result.jpegLength, parsed)) {
    metaFailed = true; // Keep animating any frames already known.
    return;
  }
  bool changed = !haveMeta || !radarSameFrames(meta, parsed);
  if (changed) {
    // Pending/shown indices referred to the old list.
    int keep = haveMeta && player.shown >= 0 && player.shown < meta.count
                   ? radarIndexOfTime(parsed, meta.frames[player.shown].time) : -1;
    releasePending();
    player.setFrames(parsed.count, keep);
    frameFailures = 0;
  }
  meta = parsed;
  haveMeta = true;
  metaFailed = false;
}

static void handleRadarResult(PhotoResult& result) {
  bool current = slot.complete(result.generation) && active;
  RadarJob kind = inFlightJob;
  inFlightJob = RadarJob::None;
  // By scan time: the frame list may have changed during the download.
  int index = haveMeta ? radarIndexOfTime(meta, inFlightTime) : -1;

  if (current && kind == RadarJob::Meta) {
    applyMeta(result);
    drawFooterStatus();
  } else if (current && kind == RadarJob::Frame) {
    if (result.ok && index >= 0) {
      releasePending();
      pendingJpeg = result.jpeg; // Ownership moves to the pending slot.
      pendingLength = result.jpegLength;
      pendingIndex = index;
      result.jpeg = nullptr;
      frameFailures = 0;
    } else {
      // Keep the frame on screen and try again shortly.
      frameFailures++;
      frameRetryAtMs = millis() + radarFrameRetryMs(frameFailures);
      // The frame may have been pruned: re-read the list (once per streak).
      if (frameFailures == RADAR_FRAME_FAILURES_FOR_META) metaForced = true;
    }
  }
  if (current) updateMessage();
  free(result.jpeg); // Stale, cancelled, failed or metadata.
  result.jpeg = nullptr;
}

static void showPendingFrame(uint32_t now) {
  if (!pendingJpeg || !player.readyToShow(now)) return;
  uint16_t w = 0, h = 0;
  if (TJpgDec.getJpgSize(&w, &h, pendingJpeg, pendingLength) != JDR_OK || w == 0 || h == 0 ||
      w > RADAR_W || h > RADAR_H) {
    Serial.printf("Radar frame rejected: %ux%u\n", w, h);
    releasePending();
    frameFailures++;
    frameRetryAtMs = now + radarFrameRetryMs(frameFailures);
    return;
  }
  // Drawn straight over the previous frame: no clear, so no black flash.
  if (!player.onScreen && (w < RADAR_W || h < RADAR_H)) tft.fillRect(RADAR_X, RADAR_Y, RADAR_W, RADAR_H, TFT_BLACK);
  TJpgDec.drawJpg(RADAR_X + (RADAR_W - w) / 2, RADAR_Y + (RADAR_H - h) / 2, pendingJpeg, pendingLength);
  player.showed(pendingIndex, now);
  messageDrawn = false;

  char label[12];
  radarTimeLabel(meta.frames[pendingIndex].time, meta.utcOffset, label, sizeof(label));
  drawTitleBarValue(label);
  releasePending();
  drawFooterStatus();
}

static void pumpRadar(uint32_t now) {
  if (slot.busy()) return;
  bool metaDue = !metaAttempted || metaForced ||
                 now - metaAttemptMs >= radarMetaInterval(haveMeta, metaFailed, meta);
  if (metaDue) {
    submit(RadarJob::Meta, API_RADAR_PATH, RADAR_META_MAX_BYTES, 0);
    return;
  }
  if (!haveMeta || !meta.available || pendingJpeg) return;
  int index = player.wanted();
  if (index < 0 || index >= meta.count) return;
  if (frameFailures > 0 && (int32_t)(now - frameRetryAtMs) < 0) return;
  submit(RadarJob::Frame, meta.frames[index].path, RADAR_JPEG_MAX_BYTES, meta.frames[index].time);
}

static void deactivateRadar() {
  active = false;
  slot.invalidate(); // An in-flight result is freed, never drawn.
  releasePending();
  player.offScreen();
}

void drawRadarPage() {
  app.currentPage = PAGE_RADAR;
  active = true;
  slot.invalidate(); // Nothing requested before this redraw may draw on it.
  releasePending();
  player.offScreen();
  frameFailures = 0;
  messageDrawn = false;

  tft.fillScreen(TFT_BLACK);
  drawTitleBar("WEATHER RADAR");
  tft.setTextSize(1);
  tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
  tft.setCursor(RADAR_X, RADAR_FOOTER_Y);
  tft.print("Radar: RainViewer  Map: (c) OpenStreetMap");
  drawFooterStatus();
  drawPlayButton();
  updateMessage();
  pumpRadar(millis());
}

void handleRadarTouch(int x, int y) {
  switch (radarHitAt(x, y)) {
    case RadarHit::PlayPause:
      player.playing = !player.playing;
      // Paused: hold the current frame and drop the one waiting behind it.
      if (!player.playing && player.onScreen) releasePending();
      drawPlayButton();
      break;
    case RadarHit::Back:
      deactivateRadar();
      showPage(appBackTarget(PAGE_RADAR, PAGE_MORE));
      break;
    case RadarHit::None:
      break;
  }
}

void updateRadar() {
  // Cancel before draining, so a result for a page just left is discarded.
  if (active && app.currentPage != PAGE_RADAR) deactivateRadar();

  PhotoResult result;
  if (receivePhotoResult(WorkerClient::Radar, result)) handleRadarResult(result);
  if (!active) return;

  uint32_t now = millis();
  showPendingFrame(now);
  pumpRadar(now);
}
