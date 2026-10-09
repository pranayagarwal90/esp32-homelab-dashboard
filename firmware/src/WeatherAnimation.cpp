#include <Arduino.h>
#include "WeatherAnimation.h"
#include "Display.h"

static constexpr uint16_t MOON_COLOR = 0xC69F;  // Pale blue-grey.
static constexpr uint16_t FOG_DARK = 0x52AA;
static constexpr uint16_t STAR_COLOR = 0xFFF0;  // Warm white.

static WeatherSceneState scene;
static unsigned long lastFrame = 0;

static uint16_t cloudColor(int index) {
  switch (scene.scene) {
    case WeatherScene::Storm: return TFT_DARKGREY;
    case WeatherScene::HeavyRain: return 0x9CD3;
    case WeatherScene::Rain:
    case WeatherScene::Drizzle: return TFT_LIGHTGREY;
    case WeatherScene::Neutral: return TFT_DARKGREY;
    default: return index % 2 ? TFT_LIGHTGREY : TFT_WHITE;
  }
}

static uint16_t skyColor(int x, int y) {
  switch (skyPixelAt(scene.scene, x, y)) {
    case SkyPixel::Sun: return TFT_YELLOW;
    case SkyPixel::Moon: return MOON_COLOR;
    default: return TFT_BLACK;
  }
}

// --- Elements (scene coordinates; the viewport clips and offsets) --------------------

static void drawRays(int phase, uint16_t color) {
  for (int ray = 0; ray < SUN_RAYS; ray++) {
    int k = sunRayAngle(ray, phase);
    int c = gearCos(k), s = gearSin(k);
    int x0 = SUN_CX + SUN_RAY_IN * c / 64, y0 = SUN_CY - SUN_RAY_IN * s / 64;
    int x1 = SUN_CX + SUN_RAY_OUT * c / 64, y1 = SUN_CY - SUN_RAY_OUT * s / 64;
    tft.drawLine(x0, y0, x1, y1, color);
    tft.drawLine(x0 + 1, y0, x1 + 1, y1, color);
  }
}

static void drawSkyBody() {
  if (sceneHasSun(scene.scene)) {
    tft.fillCircle(SUN_CX, SUN_CY, SUN_R, TFT_YELLOW);
    drawRays(scene.sunPhase, TFT_YELLOW);
  } else if (sceneHasMoon(scene.scene)) {
    tft.fillCircle(MOON_CX, MOON_CY, MOON_R, MOON_COLOR);
    tft.fillCircle(MOON_CX + MOON_BITE_DX, MOON_CY + MOON_BITE_DY, MOON_BITE_R, TFT_BLACK);
  }
}

static void drawCloud(const Cloud& c, int x, uint16_t color) {
  CloudCircle circles[3];
  cloudCircles(c, x, circles);
  for (const CloudCircle& k : circles) tft.fillCircle(k.x, k.y, k.r, color);
  tft.fillRect(x + c.size, c.y, 2 * c.size, c.size + 1, color);
}

// Repaints only the pixels a cloud vacated moving right by `dx`, with
// whatever is behind them (sky, sun or moon): no flicker, no holes.
static void eraseCloudTrail(const Cloud& c, int oldX, int dx) {
  if (dx <= 0 || dx > 4) return; // A wrap: the old cloud was off-scene.
  CloudCircle circles[3];
  cloudCircles(c, oldX, circles);
  for (const CloudCircle& k : circles) {
    for (int dy = -k.r; dy <= k.r; dy++) {
      int w = isqrtInt(k.r * k.r - dy * dy);
      for (int px = k.x - w; px < k.x - w + dx && px <= k.x + w; px++) {
        tft.drawPixel(px, k.y + dy, skyColor(px, k.y + dy));
      }
    }
  }
  for (int px = oldX + c.size; px < oldX + c.size + dx; px++) {
    for (int py = c.y; py <= c.y + c.size; py++) tft.drawPixel(px, py, skyColor(px, py));
  }
}

static void drawDrop(const Drop& d, int length, uint16_t color) {
  int top = max(d.y - length, RAIN_TOP);
  if (d.y < RAIN_TOP) return;
  tft.drawFastVLine(d.x, top, d.y - top + 1, color);
}

static void drawFlake(const Flake& f, uint16_t color) {
  if (f.y < 0) return;
  tft.drawPixel(f.x, f.y, color);
  tft.drawPixel(f.x - 1, f.y, color);
  tft.drawPixel(f.x + 1, f.y, color);
  tft.drawPixel(f.x, f.y - 1, color);
  tft.drawPixel(f.x, f.y + 1, color);
}

static void drawFogColumn(const FogBand& b, int x, bool on) {
  tft.drawPixel(x, b.y, on ? FOG_DARK : TFT_BLACK);
  tft.drawPixel(x, b.y + 1, on ? TFT_LIGHTGREY : TFT_BLACK);
  tft.drawPixel(x, b.y + 2, on ? TFT_DARKGREY : TFT_BLACK);
}

static void drawFogBand(const FogBand& b) {
  tft.drawFastHLine(b.x, b.y, b.width, FOG_DARK);
  tft.drawFastHLine(b.x, b.y + 1, b.width, TFT_LIGHTGREY);
  tft.drawFastHLine(b.x, b.y + 2, b.width, TFT_DARKGREY);
}

static void drawStar(const Star& s, bool bright) {
  tft.drawFastHLine(s.x - 2, s.y, 5, bright ? STAR_COLOR : TFT_BLACK);
  tft.drawFastVLine(s.x, s.y - 2, 5, bright ? STAR_COLOR : TFT_BLACK);
  tft.drawPixel(s.x, s.y, bright ? STAR_COLOR : TFT_DARKGREY);
}

static void drawBolt(int x, uint16_t color) {
  int8_t points[4][2];
  lightningPoints(x, points);
  for (int i = 0; i < 3; i++) {
    tft.drawLine(points[i][0], points[i][1], points[i + 1][0], points[i + 1][1], color);
    tft.drawLine(points[i][0] + 1, points[i][1], points[i + 1][0] + 1, points[i + 1][1], color);
  }
}

static void beginScene() {
  tft.setViewport(SCENE_X, SCENE_Y, SCENE_W, SCENE_H, true);
}

// --- Public --------------------------------------------------------------------------

void weatherAnimationShow(WeatherScene next) {
  if (weatherSceneNeedsReset(scene, next)) weatherSceneInit(scene, next, esp_random(), millis());
  beginScene();
  tft.fillRect(0, 0, SCENE_W, SCENE_H, TFT_BLACK);
  drawSkyBody();
  for (int i = 0; i < scene.starCount; i++) drawStar(scene.stars[i], starBright(scene.stars[i], scene.frame));
  for (int i = 0; i < scene.cloudCount; i++) drawCloud(scene.clouds[i], cloudX(scene.clouds[i]), cloudColor(i));
  RainStyle style = rainStyleFor(scene.scene);
  for (int i = 0; i < scene.dropCount; i++) drawDrop(scene.drops[i], style.length, TFT_CYAN);
  for (int i = 0; i < scene.flakeCount; i++) drawFlake(scene.flakes[i], TFT_WHITE);
  for (int i = 0; i < scene.fogCount; i++) drawFogBand(scene.fog[i]);
  tft.resetViewport();
  scene.boltVisible = false;
  lastFrame = millis();
}

void weatherAnimationUpdate() {
  if (!scene.ready || scene.scene == WeatherScene::Neutral) return;
  unsigned long now = millis();
  if (now - lastFrame < WEATHER_FRAME_MS) return;
  lastFrame = now;

  RainStyle style = rainStyleFor(scene.scene);
  int oldCloudX[MAX_CLOUDS];
  int16_t oldFogX[MAX_FOG];
  uint8_t oldPhase = scene.sunPhase;
  bool boltWasVisible = scene.boltVisible;
  int boltX = scene.boltX;

  beginScene();
  // Erase what moves (at the old positions).
  if (sceneHasSun(scene.scene)) drawRays(oldPhase, TFT_BLACK);
  for (int i = 0; i < scene.cloudCount; i++) oldCloudX[i] = cloudX(scene.clouds[i]);
  for (int i = 0; i < scene.dropCount; i++) drawDrop(scene.drops[i], style.length, TFT_BLACK);
  for (int i = 0; i < scene.flakeCount; i++) drawFlake(scene.flakes[i], TFT_BLACK);
  for (int i = 0; i < scene.fogCount; i++) oldFogX[i] = scene.fog[i].x;

  weatherSceneStep(scene, now);

  // Draw back to front: sky, stars, clouds, precipitation, mist, lightning.
  for (int i = 0; i < scene.cloudCount; i++) {
    eraseCloudTrail(scene.clouds[i], oldCloudX[i], cloudX(scene.clouds[i]) - oldCloudX[i]);
  }
  if (sceneHasSun(scene.scene)) drawRays(scene.sunPhase, TFT_YELLOW);
  for (int i = 0; i < scene.starCount; i++) {
    const Star& star = scene.stars[i];
    bool bright = starBright(star, scene.frame);
    if (bright != starBright(star, scene.frame - 1)) drawStar(star, bright);
  }
  for (int i = 0; i < scene.cloudCount; i++) drawCloud(scene.clouds[i], cloudX(scene.clouds[i]), cloudColor(i));
  for (int i = 0; i < scene.dropCount; i++) drawDrop(scene.drops[i], style.length, TFT_CYAN);
  for (int i = 0; i < scene.flakeCount; i++) drawFlake(scene.flakes[i], TFT_WHITE);
  for (int i = 0; i < scene.fogCount; i++) {
    const FogBand& band = scene.fog[i];
    int from = oldFogX[i], to = band.x;
    if (from == to) continue;
    // 1 px move: clear the column left behind, draw the new leading column.
    if (to > from) {
      drawFogColumn(band, from, false);
      drawFogColumn(band, to + band.width - 1, true);
    } else {
      drawFogColumn(band, from + band.width - 1, false);
      drawFogColumn(band, to, true);
    }
  }
  if (boltWasVisible && !scene.boltVisible) drawBolt(boltX, TFT_BLACK);
  if (scene.boltVisible && !boltWasVisible) drawBolt(scene.boltX, TFT_YELLOW);
  tft.resetViewport();
}
