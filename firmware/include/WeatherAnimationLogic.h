#pragma once
#include <stdint.h>
#include "SystemAnimationLogic.h" // gearSin/gearCos: 32-step integer sine table.
#include "WeatherLogic.h"
#include "games/GameRandom.h"

// Pure weather-scene state and motion, shared with host tests. Coordinates
// are relative to the scene region (SCENE_W x SCENE_H). The drawer erases
// what moved, calls weatherSceneStep() once per frame, and draws again.

constexpr int SCENE_X = 8;
constexpr int SCENE_Y = 40;
constexpr int SCENE_W = 132;
constexpr int SCENE_H = 84;
constexpr uint32_t WEATHER_FRAME_MS = 125; // 8 fps.

enum class WeatherScene : uint8_t {
  Sun, Moon, SunClouds, MoonClouds, Clouds, Drizzle, Rain, HeavyRain, Storm, Snow, Fog, Neutral
};

inline WeatherScene weatherSceneFor(WeatherCondition condition, bool day) {
  switch (condition) {
    case WeatherCondition::Clear: return day ? WeatherScene::Sun : WeatherScene::Moon;
    case WeatherCondition::PartlyCloudy: return day ? WeatherScene::SunClouds : WeatherScene::MoonClouds;
    case WeatherCondition::Cloudy: return WeatherScene::Clouds;
    case WeatherCondition::Fog: return WeatherScene::Fog;
    case WeatherCondition::Drizzle: return WeatherScene::Drizzle;
    case WeatherCondition::Rain: return WeatherScene::Rain;
    case WeatherCondition::HeavyRain: return WeatherScene::HeavyRain;
    case WeatherCondition::Thunderstorm: return WeatherScene::Storm;
    case WeatherCondition::Snow: return WeatherScene::Snow;
    default: return WeatherScene::Neutral;
  }
}

// --- Sky bodies ------------------------------------------------------------------------

constexpr int SUN_CX = 42, SUN_CY = 36, SUN_R = 15;
constexpr int SUN_RAYS = 8;
constexpr int SUN_RAY_IN = 19, SUN_RAY_OUT = 26;
constexpr int SUN_PHASES = 4;          // Rays repeat every 4 table steps.
constexpr int MOON_CX = 44, MOON_CY = 34, MOON_R = 15;
constexpr int MOON_BITE_DX = 7, MOON_BITE_DY = -5, MOON_BITE_R = 13;

inline bool sceneHasSun(WeatherScene s) { return s == WeatherScene::Sun || s == WeatherScene::SunClouds; }
inline bool sceneHasMoon(WeatherScene s) { return s == WeatherScene::Moon || s == WeatherScene::MoonClouds; }

// Table angle of a ray in a rotation phase (0..SUN_PHASES-1).
inline int sunRayAngle(int ray, int phase) {
  return (ray * (GEAR_ANGLES / SUN_RAYS) + phase) % GEAR_ANGLES;
}

// What lies behind a cloud at (x, y): used to repaint vacated cloud pixels.
enum class SkyPixel : uint8_t { Empty, Sun, Moon };
inline SkyPixel skyPixelAt(WeatherScene scene, int x, int y) {
  if (sceneHasSun(scene)) {
    int dx = x - SUN_CX, dy = y - SUN_CY;
    if (dx * dx + dy * dy <= SUN_R * SUN_R) return SkyPixel::Sun;
  } else if (sceneHasMoon(scene)) {
    int dx = x - MOON_CX, dy = y - MOON_CY;
    int bx = x - (MOON_CX + MOON_BITE_DX), by = y - (MOON_CY + MOON_BITE_DY);
    if (dx * dx + dy * dy <= MOON_R * MOON_R && bx * bx + by * by > MOON_BITE_R * MOON_BITE_R) {
      return SkyPixel::Moon;
    }
  }
  return SkyPixel::Empty;
}

// --- Clouds ----------------------------------------------------------------------------

constexpr int MAX_CLOUDS = 4;

struct Cloud {
  int16_t xq = 0;     // Left edge, quarter pixels (sub-pixel drift).
  int8_t y = 0;       // Baseline (bottom of the lobes' centres).
  uint8_t size = 8;   // Lobe radius.
  uint8_t speedq = 2; // Quarter pixels per frame.
};

inline int cloudX(const Cloud& c) { return c.xq >> 2; }
inline int cloudWidth(const Cloud& c) { return 4 * c.size; }

// Shape: three lobes and a flat base.
struct CloudCircle { int x, y, r; };
inline void cloudCircles(const Cloud& c, int x, CloudCircle out[3]) {
  int s = c.size;
  out[0] = {x + s, c.y, s};
  out[1] = {x + 2 * s, c.y - s / 2, s + s / 3};
  out[2] = {x + 3 * s, c.y, s};
}

// Advance; a cloud fully past the right edge re-enters from the left.
inline void cloudStep(Cloud& c) {
  c.xq += c.speedq;
  if (cloudX(c) >= SCENE_W) c.xq = (int16_t)(-cloudWidth(c) * 4);
}

// --- Particles ------------------------------------------------------------------------

constexpr int MAX_DROPS = 20;
constexpr int MAX_FLAKES = 16;
constexpr int MAX_FOG = 5;
constexpr int MAX_STARS = 6;
constexpr int RAIN_TOP = 34;            // Below the clouds.

struct Drop { int16_t x = 0; int8_t y = 0; uint8_t speed = 4; };
struct Flake { int16_t x = 0; int8_t y = 0; uint8_t speed = 1; uint8_t phase = 0; };
struct FogBand { int16_t x = 0; int8_t y = 0; uint8_t width = 60; int8_t dir = 1; uint8_t timer = 0; uint8_t period = 3; };
struct Star { int8_t x = 0; int8_t y = 0; uint8_t period = 7; uint8_t offset = 0; };

struct RainStyle { uint8_t drops, minSpeed, maxSpeed, length; };
inline RainStyle rainStyleFor(WeatherScene scene) {
  switch (scene) {
    case WeatherScene::Drizzle: return {8, 2, 3, 3};
    case WeatherScene::HeavyRain: return {20, 7, 10, 7};
    case WeatherScene::Storm: return {16, 6, 9, 6};
    case WeatherScene::Rain: return {14, 4, 6, 5};
    default: return {0, 0, 0, 0};
  }
}

// Snow sway: a small integer wave, +-1 px.
inline int flakeSway(uint8_t phase) {
  static const int8_t WAVE[8] = {0, 1, 1, 1, 0, -1, -1, -1};
  return WAVE[phase % 8];
}

// Star brightness: bright for one frame in each `period`.
inline bool starBright(const Star& s, uint32_t frame) {
  return (frame + s.offset) % s.period == 0;
}

// --- Lightning -------------------------------------------------------------------------

constexpr uint32_t LIGHTNING_MIN_MS = 3000;
constexpr uint32_t LIGHTNING_MAX_MS = 8000;
constexpr uint32_t LIGHTNING_SHOW_MS = 150;

inline uint32_t nextLightningDelay(GameRandom& rng) {
  return LIGHTNING_MIN_MS + rng.below(LIGHTNING_MAX_MS - LIGHTNING_MIN_MS + 1);
}

// --- Scene state ------------------------------------------------------------------------

struct WeatherSceneState {
  WeatherScene scene = WeatherScene::Neutral;
  bool ready = false;
  uint32_t frame = 0;
  uint8_t sunPhase = 0;
  uint8_t cloudCount = 0, dropCount = 0, flakeCount = 0, fogCount = 0, starCount = 0;
  Cloud clouds[MAX_CLOUDS];
  Drop drops[MAX_DROPS];
  Flake flakes[MAX_FLAKES];
  FogBand fog[MAX_FOG];
  Star stars[MAX_STARS];
  uint32_t boltAt = 0;     // Next lightning (absolute ms).
  uint32_t boltShownAt = 0;
  bool boltVisible = false;
  int8_t boltX = 0;
  GameRandom rng;
};

inline void respawnDrop(WeatherSceneState& s, Drop& d, const RainStyle& style, bool anywhere) {
  d.x = (int16_t)(2 + s.rng.below(SCENE_W - 4));
  d.y = (int8_t)(anywhere ? RAIN_TOP + s.rng.below(SCENE_H - RAIN_TOP) : RAIN_TOP - s.rng.below(6));
  d.speed = (uint8_t)(style.minSpeed + s.rng.below(style.maxSpeed - style.minSpeed + 1));
}

inline void respawnFlake(WeatherSceneState& s, Flake& f, bool anywhere) {
  f.x = (int16_t)(3 + s.rng.below(SCENE_W - 6));
  f.y = (int8_t)(anywhere ? s.rng.below(SCENE_H) : -2);
  f.speed = (uint8_t)(1 + s.rng.below(2));
  f.phase = (uint8_t)s.rng.below(8);
}

// True when the scene must be rebuilt (condition or day/night changed, or
// nothing built yet). An unchanged scene keeps its particles.
inline bool weatherSceneNeedsReset(const WeatherSceneState& s, WeatherScene next) {
  return !s.ready || s.scene != next;
}

inline void weatherSceneInit(WeatherSceneState& s, WeatherScene scene, uint32_t seed, uint32_t now) {
  s = WeatherSceneState();
  s.scene = scene;
  s.ready = true;
  s.rng.seed(seed);
  auto addCloud = [&s](int x, int y, int size, int speedq) {
    Cloud& c = s.clouds[s.cloudCount++];
    c.xq = (int16_t)(x * 4);
    c.y = (int8_t)y;
    c.size = (uint8_t)size;
    c.speedq = (uint8_t)speedq;
  };
  switch (scene) {
    case WeatherScene::SunClouds:
    case WeatherScene::MoonClouds:
      addCloud(30, 46, 9, 3);
      addCloud(-20, 64, 7, 5);
      break;
    case WeatherScene::Clouds:
      addCloud(4, 30, 10, 2);
      addCloud(70, 46, 8, 3);
      addCloud(-30, 66, 9, 4);
      addCloud(96, 74, 6, 5);
      break;
    case WeatherScene::Drizzle:
    case WeatherScene::Rain:
    case WeatherScene::HeavyRain:
    case WeatherScene::Storm:
      addCloud(0, 18, 10, 2);
      addCloud(64, 22, 9, 3);
      if (scene != WeatherScene::Drizzle) addCloud(110, 16, 8, 2);
      break;
    case WeatherScene::Neutral:
      addCloud(34, 50, 12, 0); // Static.
      break;
    default:
      break;
  }
  RainStyle style = rainStyleFor(scene);
  s.dropCount = style.drops;
  for (int i = 0; i < s.dropCount; i++) respawnDrop(s, s.drops[i], style, true);
  if (scene == WeatherScene::Snow) {
    s.flakeCount = MAX_FLAKES;
    for (int i = 0; i < s.flakeCount; i++) respawnFlake(s, s.flakes[i], true);
  }
  if (scene == WeatherScene::Fog) {
    static const uint8_t WIDTHS[MAX_FOG] = {96, 70, 110, 80, 64};
    static const uint8_t PERIODS[MAX_FOG] = {3, 2, 4, 2, 3};
    s.fogCount = MAX_FOG;
    for (int i = 0; i < MAX_FOG; i++) {
      FogBand& band = s.fog[i];
      band.y = (int8_t)(14 + i * 14);
      band.width = WIDTHS[i];
      band.x = (int16_t)(i * 9 % (SCENE_W - band.width));
      band.dir = i % 2 ? -1 : 1;
      band.period = PERIODS[i];
    }
  }
  if (sceneHasMoon(scene)) {
    static const int8_t STAR_XY[MAX_STARS][2] = {{80, 12}, {104, 30}, {122, 10}, {12, 66}, {94, 60}, {70, 76}};
    s.starCount = scene == WeatherScene::Moon ? MAX_STARS : 3;
    for (int i = 0; i < s.starCount; i++) {
      s.stars[i].x = STAR_XY[i][0];
      s.stars[i].y = STAR_XY[i][1];
      s.stars[i].period = (uint8_t)(5 + i % 4);
      s.stars[i].offset = (uint8_t)(i * 3);
    }
  }
  if (scene == WeatherScene::Storm) s.boltAt = now + nextLightningDelay(s.rng);
}

// Fog: each band glides 1 px every `period` frames and turns at the edges.
inline void fogStep(FogBand& band) {
  if (++band.timer < band.period) return;
  band.timer = 0;
  band.x += band.dir;
  if (band.x <= 0) {
    band.x = 0;
    band.dir = 1;
  } else if (band.x + band.width >= SCENE_W) {
    band.x = (int16_t)(SCENE_W - band.width);
    band.dir = -1;
  }
}

// One frame of motion. Lightning uses `now` (rollover-safe).
inline void weatherSceneStep(WeatherSceneState& s, uint32_t now) {
  s.frame++;
  s.sunPhase = (uint8_t)((s.sunPhase + 1) % SUN_PHASES);
  for (int i = 0; i < s.cloudCount; i++) {
    if (s.clouds[i].speedq) cloudStep(s.clouds[i]);
  }
  RainStyle style = rainStyleFor(s.scene);
  for (int i = 0; i < s.dropCount; i++) {
    Drop& d = s.drops[i];
    d.y = (int8_t)(d.y + d.speed);
    if (d.y > SCENE_H - 1) respawnDrop(s, d, style, false);
  }
  for (int i = 0; i < s.flakeCount; i++) {
    Flake& f = s.flakes[i];
    f.phase = (uint8_t)((f.phase + 1) % 8);
    if (s.frame % 2 == 0) f.y = (int8_t)(f.y + f.speed); // ~1-2 px every other frame.
    f.x = (int16_t)(f.x + flakeSway(f.phase) - flakeSway(f.phase - 1 + 8));
    if (f.x < 2) f.x = 2;
    if (f.x > SCENE_W - 3) f.x = SCENE_W - 3;
    if (f.y > SCENE_H) respawnFlake(s, f, false);
  }
  for (int i = 0; i < s.fogCount; i++) fogStep(s.fog[i]);
  if (s.scene == WeatherScene::Storm) {
    if (s.boltVisible && now - s.boltShownAt >= LIGHTNING_SHOW_MS) s.boltVisible = false;
    if (!s.boltVisible && (int32_t)(now - s.boltAt) >= 0) {
      s.boltVisible = true;
      s.boltShownAt = now;
      s.boltX = (int8_t)(24 + s.rng.below(SCENE_W - 48));
      s.boltAt = now + nextLightningDelay(s.rng);
    }
  }
}

// Jagged bolt: four points from the cloud base downward.
inline void lightningPoints(int x, int8_t out[4][2]) {
  const int8_t SHAPE[4][2] = {{0, 30}, {-7, 46}, {3, 50}, {-5, 70}};
  for (int i = 0; i < 4; i++) {
    out[i][0] = (int8_t)(x + SHAPE[i][0]);
    out[i][1] = SHAPE[i][1];
  }
}

// Integer square root (cloud sliver erase).
inline int isqrtInt(int v) {
  if (v <= 0) return 0;
  int r = 0;
  while ((r + 1) * (r + 1) <= v) r++;
  return r;
}
