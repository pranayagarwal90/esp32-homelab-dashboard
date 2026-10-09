#include "WeatherAnimationLogic.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef WeatherCondition C;

static void testMapping() {
  const struct { int code; C expected; } cases[] = {
    {0, C::Clear}, {1, C::Clear}, {2, C::PartlyCloudy}, {3, C::Cloudy},
    {45, C::Fog}, {48, C::Fog},
    {51, C::Drizzle}, {53, C::Drizzle}, {55, C::Drizzle}, {56, C::Drizzle}, {57, C::Drizzle},
    {61, C::Rain}, {63, C::Rain}, {66, C::Rain}, {80, C::Rain}, {81, C::Rain},
    {65, C::HeavyRain}, {67, C::HeavyRain}, {82, C::HeavyRain},
    {71, C::Snow}, {73, C::Snow}, {75, C::Snow}, {77, C::Snow}, {85, C::Snow}, {86, C::Snow},
    {95, C::Thunderstorm}, {96, C::Thunderstorm}, {99, C::Thunderstorm},
    {-1, C::Unknown}, {4, C::Unknown}, {50, C::Unknown}, {100, C::Unknown},
  };
  for (const auto& c : cases) assert(weatherConditionFor(c.code) == c.expected);
  // Every WMO code 0..99 maps to something (no gaps crash), unknowns included.
  for (int code = 0; code < 100; code++) (void)weatherConditionFor(code);
}

static void testScenes() {
  assert(weatherSceneFor(C::Clear, true) == WeatherScene::Sun);
  assert(weatherSceneFor(C::Clear, false) == WeatherScene::Moon);
  assert(weatherSceneFor(C::PartlyCloudy, true) == WeatherScene::SunClouds);
  assert(weatherSceneFor(C::PartlyCloudy, false) == WeatherScene::MoonClouds);
  assert(weatherSceneFor(C::Cloudy, false) == WeatherScene::Clouds);
  assert(weatherSceneFor(C::Drizzle, true) == WeatherScene::Drizzle);
  assert(weatherSceneFor(C::Rain, true) == WeatherScene::Rain);
  assert(weatherSceneFor(C::HeavyRain, true) == WeatherScene::HeavyRain);
  assert(weatherSceneFor(C::Thunderstorm, true) == WeatherScene::Storm);
  assert(weatherSceneFor(C::Snow, true) == WeatherScene::Snow);
  assert(weatherSceneFor(C::Fog, true) == WeatherScene::Fog);
  assert(weatherSceneFor(C::Unknown, true) == WeatherScene::Neutral);
}

static void testDayNight() {
  const uint32_t rise = 1791544380, set = 1791585600;
  assert(weatherIsDay(rise, rise, set));
  assert(weatherIsDay(rise + 3600, rise, set));
  assert(!weatherIsDay(set, rise, set));
  assert(!weatherIsDay(rise - 1, rise, set));
  assert(!weatherIsDay(set + 7200, rise, set));
  // Unknown data: day.
  assert(weatherIsDay(0, rise, set) && weatherIsDay(rise, 0, set) && weatherIsDay(rise, set, rise));
  // Device clock estimate, across millis() rollover.
  assert(weatherNowEpoch(0, 0, 5000) == 0);
  assert(weatherNowEpoch(1000, 2000, 7999) == 1005);
  assert(weatherNowEpoch(1000, 0xFFFFF000u, 0x00001000u) == 1008); // 8.192 s later.
  // Next solar event and the local offset (EDT -4 h, IST +5:30).
  assert(weatherShowSunrise(rise - 60, rise) && !weatherShowSunrise(rise + 60, rise));
  assert(!weatherShowSunrise(0, rise));
  assert(weatherUtcOffset(1791518400) == -4 * 3600);      // 2026-10-09 00:00 EDT.
  assert(weatherUtcOffset(1791484200) == 19800);          // Local midnight at UTC+5:30.
  assert(weatherUtcOffset(0) == 0);
  char text[16];
  formatClock12(rise, -4 * 3600, text, sizeof(text));
  assert(strcmp(text, "7:13 AM") == 0);
  formatClock12(set, -4 * 3600, text, sizeof(text));
  assert(strcmp(text, "6:40 PM") == 0);
}

static void testFormatting() {
  char text[16];
  formatDegrees(145, text, sizeof(text));
  assert(strcmp(text, "15" WEATHER_DEGREE) == 0);
  formatDegrees(144, text, sizeof(text));
  assert(strcmp(text, "14" WEATHER_DEGREE) == 0);
  formatDegrees(-15, text, sizeof(text));
  assert(strcmp(text, "-2" WEATHER_DEGREE) == 0);
  formatDegrees(-4, text, sizeof(text));
  assert(strcmp(text, "0" WEATHER_DEGREE) == 0);
  formatDegrees(WEATHER_NO_VALUE, text, sizeof(text));
  assert(strcmp(text, "--") == 0);
  formatDegreesC1(145, text, sizeof(text));
  assert(strcmp(text, "14.5" WEATHER_DEGREE "C") == 0);
  formatDegreesC1(-32, text, sizeof(text));
  assert(strcmp(text, "-3.2" WEATHER_DEGREE "C") == 0);
  formatDegreesC1(-5, text, sizeof(text));
  assert(strcmp(text, "-0.5" WEATHER_DEGREE "C") == 0);
  formatPercent(20, text, sizeof(text));
  assert(strcmp(text, "20%") == 0);
  formatPercent(-1, text, sizeof(text));
  assert(strcmp(text, "--") == 0);
  formatPercent(101, text, sizeof(text));
  assert(strcmp(text, "--") == 0);
  formatHour12(0, text, sizeof(text));
  assert(strcmp(text, "12AM") == 0);
  formatHour12(12, text, sizeof(text));
  assert(strcmp(text, "12PM") == 0);
  formatHour12(17, text, sizeof(text));
  assert(strcmp(text, "5PM") == 0);
  formatHour12(24, text, sizeof(text));
  assert(strcmp(text, "--") == 0);
  assert(toTenths(14.54f) == 145 && toTenths(-0.25f) == -3 && toTenths(1e9f) == WEATHER_NO_VALUE);
  assert(validHour(0) && validHour(23) && !validHour(24) && !validHour(-1));
}

static void testHourlyStorage() {
  // Up to 8 stored, 6 shown; 6 bytes each.
  assert(WEATHER_HOURLY_MAX == 8 && WEATHER_HOURLY_SHOWN == 6);
  assert(sizeof(HourlyForecast) == 6);
  HourlyForecast h;
  assert(h.precip == -1 && h.code == -1);
  // Six columns of 52 px fit the 320 px width.
  assert(6 + WEATHER_HOURLY_SHOWN * 52 <= 320);
}

static void testSunPhases() {
  // Rays rotate one table step per frame and repeat after SUN_PHASES.
  for (int ray = 0; ray < SUN_RAYS; ray++) {
    for (int phase = 0; phase < SUN_PHASES; phase++) {
      assert(sunRayAngle(ray, phase) == (ray * 4 + phase) % GEAR_ANGLES);
    }
  }
  assert(sunRayAngle(0, SUN_PHASES) == sunRayAngle(1, 0)); // Symmetric: looks continuous.
  WeatherSceneState s;
  weatherSceneInit(s, WeatherScene::Sun, 1, 0);
  for (int i = 1; i <= 9; i++) {
    weatherSceneStep(s, i * WEATHER_FRAME_MS);
    assert(s.sunPhase == i % SUN_PHASES);
  }
  // Rays stay inside the scene.
  assert(SUN_CX - SUN_RAY_OUT >= 0 && SUN_CY - SUN_RAY_OUT >= 0 && SUN_CY + SUN_RAY_OUT < SCENE_H);
  // Sky pixels behind clouds.
  assert(skyPixelAt(WeatherScene::SunClouds, SUN_CX, SUN_CY) == SkyPixel::Sun);
  assert(skyPixelAt(WeatherScene::SunClouds, SUN_CX + SUN_R + 1, SUN_CY) == SkyPixel::Empty);
  assert(skyPixelAt(WeatherScene::MoonClouds, MOON_CX - MOON_R + 2, MOON_CY) == SkyPixel::Moon);
  assert(skyPixelAt(WeatherScene::MoonClouds, MOON_CX + MOON_BITE_DX, MOON_CY + MOON_BITE_DY) == SkyPixel::Empty);
  assert(skyPixelAt(WeatherScene::Clouds, SUN_CX, SUN_CY) == SkyPixel::Empty);
}

static void testClouds() {
  Cloud c;
  c.xq = (SCENE_W - 1) * 4;
  c.size = 8;
  c.speedq = 4;
  cloudStep(c);
  assert(cloudX(c) == -cloudWidth(c)); // Wrapped fully off the left.
  // Drift: sub-pixel speeds still move.
  Cloud slow;
  slow.xq = 0;
  slow.speedq = 1;
  for (int i = 0; i < 4; i++) cloudStep(slow);
  assert(cloudX(slow) == 1);
  // Partly cloudy: two clouds, different speeds; cloudy: 3-4 clouds.
  WeatherSceneState s;
  weatherSceneInit(s, WeatherScene::SunClouds, 7, 0);
  assert(s.cloudCount == 2 && s.clouds[0].speedq != s.clouds[1].speedq);
  weatherSceneInit(s, WeatherScene::Clouds, 7, 0);
  assert(s.cloudCount >= 3 && s.cloudCount <= MAX_CLOUDS);
  // Over many frames every cloud stays within [-width, SCENE_W).
  for (int f = 0; f < 2000; f++) {
    weatherSceneStep(s, f * WEATHER_FRAME_MS);
    for (int i = 0; i < s.cloudCount; i++) {
      int x = cloudX(s.clouds[i]);
      assert(x >= -cloudWidth(s.clouds[i]) && x < SCENE_W);
    }
  }
  // Neutral: one static cloud.
  weatherSceneInit(s, WeatherScene::Neutral, 1, 0);
  int x = cloudX(s.clouds[0]);
  weatherSceneStep(s, 1000);
  assert(s.cloudCount == 1 && cloudX(s.clouds[0]) == x);
  assert(isqrtInt(0) == 0 && isqrtInt(80) == 8 && isqrtInt(81) == 9 && isqrtInt(-4) == 0);
}

static void testRain() {
  RainStyle drizzle = rainStyleFor(WeatherScene::Drizzle), rain = rainStyleFor(WeatherScene::Rain);
  RainStyle heavy = rainStyleFor(WeatherScene::HeavyRain);
  assert(drizzle.drops < rain.drops && rain.drops < heavy.drops && heavy.drops <= MAX_DROPS);
  assert(drizzle.maxSpeed < rain.minSpeed && rain.maxSpeed < heavy.maxSpeed);
  assert(rainStyleFor(WeatherScene::Sun).drops == 0);
  const WeatherScene scenes[] = {WeatherScene::Drizzle, WeatherScene::Rain, WeatherScene::HeavyRain, WeatherScene::Storm};
  for (WeatherScene scene : scenes) {
    WeatherSceneState s;
    weatherSceneInit(s, scene, 3, 0);
    RainStyle style = rainStyleFor(scene);
    assert(s.dropCount == style.drops);
    int wrapped = 0;
    for (int f = 0; f < 500; f++) {
      int before[MAX_DROPS];
      for (int i = 0; i < s.dropCount; i++) before[i] = s.drops[i].y;
      weatherSceneStep(s, f * WEATHER_FRAME_MS);
      for (int i = 0; i < s.dropCount; i++) {
        const Drop& d = s.drops[i];
        assert(d.x >= 2 && d.x < SCENE_W - 1);                    // Constrained horizontally.
        assert(d.speed >= style.minSpeed && d.speed <= style.maxSpeed);
        assert(d.y <= SCENE_H - 1 + style.maxSpeed);
        if (d.y < before[i]) {
          wrapped++;
          assert(d.y <= RAIN_TOP && d.y >= RAIN_TOP - 5);         // Respawned under the clouds.
        } else {
          assert(d.y == before[i] + d.speed);                    // Falling.
        }
      }
    }
    assert(wrapped > s.dropCount); // Visibly cycling.
  }
}

static void testSnow() {
  WeatherSceneState s;
  weatherSceneInit(s, WeatherScene::Snow, 5, 0);
  assert(s.flakeCount == MAX_FLAKES && s.dropCount == 0 && s.cloudCount == 0);
  bool sideways = false, slowAndFast = false;
  for (int i = 1; i < s.flakeCount; i++) slowAndFast |= s.flakes[i].speed != s.flakes[0].speed;
  assert(slowAndFast);
  int startX[MAX_FLAKES];
  for (int i = 0; i < s.flakeCount; i++) startX[i] = s.flakes[i].x;
  int maxStep = 0;
  for (int f = 0; f < 400; f++) {
    int before[MAX_FLAKES];
    for (int i = 0; i < s.flakeCount; i++) before[i] = s.flakes[i].y;
    weatherSceneStep(s, f * WEATHER_FRAME_MS);
    for (int i = 0; i < s.flakeCount; i++) {
      const Flake& fl = s.flakes[i];
      assert(fl.x >= 2 && fl.x <= SCENE_W - 3);
      if (fl.x != startX[i]) sideways = true;
      if (fl.y >= before[i]) {
        int step = fl.y - before[i];
        assert(step <= 2); // Slow.
        if (step > maxStep) maxStep = step;
      } else {
        assert(fl.y == -2); // Wrapped to the top.
      }
    }
  }
  assert(sideways && maxStep >= 1);
  // Sway is a gentle +-1 wave.
  for (int p = 0; p < 16; p++) assert(flakeSway(p) >= -1 && flakeSway(p) <= 1);
}

static void testFog() {
  WeatherSceneState s;
  weatherSceneInit(s, WeatherScene::Fog, 9, 0);
  assert(s.fogCount == MAX_FOG);
  int16_t start[MAX_FOG];
  for (int i = 0; i < MAX_FOG; i++) start[i] = s.fog[i].x;
  bool bounced = false;
  for (int f = 0; f < 3000; f++) {
    int16_t before[MAX_FOG];
    for (int i = 0; i < MAX_FOG; i++) before[i] = s.fog[i].x;
    weatherSceneStep(s, f * WEATHER_FRAME_MS);
    for (int i = 0; i < MAX_FOG; i++) {
      const FogBand& b = s.fog[i];
      assert(b.x >= 0 && b.x + b.width <= SCENE_W);     // Stays in the scene.
      assert(b.x - before[i] >= -1 && b.x - before[i] <= 1); // At most 1 px per frame.
      if (b.dir != (i % 2 ? -1 : 1)) bounced = true;
    }
  }
  assert(bounced);
  // Bands move at different rates.
  bool differ = false;
  for (int i = 1; i < MAX_FOG; i++) differ |= s.fog[i].period != s.fog[0].period;
  assert(differ);
  (void)start;
}

static void testLightning() {
  GameRandom rng;
  rng.seed(42);
  for (int i = 0; i < 1000; i++) {
    uint32_t d = nextLightningDelay(rng);
    assert(d >= LIGHTNING_MIN_MS && d <= LIGHTNING_MAX_MS);
  }
  WeatherSceneState s;
  weatherSceneInit(s, WeatherScene::Storm, 11, 1000);
  assert(s.boltAt - 1000 >= LIGHTNING_MIN_MS && s.boltAt - 1000 <= LIGHTNING_MAX_MS);
  int flashes = 0;
  uint32_t lastFlash = 0, visibleFrames = 0;
  for (uint32_t now = 1000; now < 1000 + 120000; now += WEATHER_FRAME_MS) {
    bool was = s.boltVisible;
    weatherSceneStep(s, now);
    if (s.boltVisible) visibleFrames++;
    if (s.boltVisible && !was) {
      if (flashes) {
        uint32_t gap = now - lastFlash;
        assert(gap >= LIGHTNING_MIN_MS && gap <= LIGHTNING_MAX_MS + 2 * WEATHER_FRAME_MS);
      }
      lastFlash = now;
      flashes++;
      assert(s.boltX >= 24 && s.boltX < SCENE_W - 24);
    }
  }
  assert(flashes >= 12 && flashes <= 40);       // Occasional, not strobing.
  assert(visibleFrames <= (uint32_t)flashes * 2); // Brief.
  // Bolt geometry stays inside the scene.
  int8_t pts[4][2];
  lightningPoints(SCENE_W - 25, pts);
  for (int i = 0; i < 4; i++) assert(pts[i][0] >= 0 && pts[i][0] < SCENE_W && pts[i][1] < SCENE_H);
  // Lightning across millis() rollover.
  WeatherSceneState w;
  weatherSceneInit(w, WeatherScene::Storm, 3, 0xFFFFF000u);
  bool flashed = false;
  for (uint32_t t = 0xFFFFF000u, i = 0; i < 100; i++, t += WEATHER_FRAME_MS) {
    weatherSceneStep(w, t);
    flashed |= w.boltVisible;
  }
  assert(flashed); // 12.5 s covers the 3-8 s window across the wrap.
}

static void testStarsAndTransitions() {
  WeatherSceneState s;
  weatherSceneInit(s, WeatherScene::Moon, 2, 0);
  assert(s.starCount == MAX_STARS && s.cloudCount == 0);
  // Stars twinkle independently (not all bright on the same frames).
  bool independent = false;
  for (uint32_t f = 0; f < 50; f++) {
    int bright = 0;
    for (int i = 0; i < s.starCount; i++) bright += starBright(s.stars[i], f);
    if (bright > 0 && bright < s.starCount) independent = true;
  }
  assert(independent);
  weatherSceneInit(s, WeatherScene::MoonClouds, 2, 0);
  assert(s.starCount == 3 && s.cloudCount == 2);

  // Transitions: same scene keeps its particles; a new scene resets.
  WeatherSceneState t;
  assert(weatherSceneNeedsReset(t, WeatherScene::Clouds)); // Nothing built yet.
  weatherSceneInit(t, WeatherScene::Rain, 4, 0);
  for (int f = 0; f < 10; f++) weatherSceneStep(t, f * WEATHER_FRAME_MS);
  assert(!weatherSceneNeedsReset(t, WeatherScene::Rain));
  assert(weatherSceneNeedsReset(t, WeatherScene::HeavyRain));   // Condition change.
  WeatherSceneState d;
  weatherSceneInit(d, WeatherScene::Sun, 1, 0);
  assert(weatherSceneNeedsReset(d, WeatherScene::Moon));        // Day -> night.
  weatherSceneInit(t, WeatherScene::HeavyRain, 4, 0);
  assert(t.frame == 0 && t.dropCount == rainStyleFor(WeatherScene::HeavyRain).drops);
}

static void testTouch() {
  // TIME / WEATHER: only the weather block opens WEATHER.
  assert(timeWeatherBlockHit(160, 95) && timeWeatherBlockHit(300, 88) && timeWeatherBlockHit(10, 110));
  assert(!timeWeatherBlockHit(160, 50));  // Clock.
  assert(!timeWeatherBlockHit(160, 140)); // World clocks.
  assert(!timeWeatherBlockHit(160, 220)); // BACK bar.
  assert(TIME_WEATHER_BOTTOM - TIME_WEATHER_TOP >= 30); // Resistive-friendly height.
  // WEATHER: BACK bar only (returns to TIME / WEATHER).
  assert(weatherHitAt(160, 220) == WeatherHit::Back);
  assert(weatherHitAt(10, 205) == WeatherHit::Back);
  assert(weatherHitAt(160, 100) == WeatherHit::None);
  // The scene stays clear of the header, the text column and the details.
  assert(SCENE_Y >= 36 && SCENE_X + SCENE_W <= 150 && SCENE_Y + SCENE_H <= 128);
}

int main() {
  testMapping();
  testScenes();
  testDayNight();
  testFormatting();
  testHourlyStorage();
  testSunPhases();
  testClouds();
  testRain();
  testSnow();
  testFog();
  testLightning();
  testStarsAndTransitions();
  testTouch();
  puts("Weather tests passed: mapping, scenes, day/night, formatting, hourly, sun, clouds, rain, snow, fog, "
       "lightning, stars/transitions, touch");
}
