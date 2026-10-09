#include <Arduino.h>
#include <WiFi.h>
#include <esp_sleep.h>
#include "SystemAnimation.h"
#include "Display.h"
#include "WalkerDraw.h"

static constexpr uint32_t STARTUP_POLL_MS = 20;
static constexpr uint16_t LED_OFF = 0x2104; // Very dark grey.

static SystemAnimationState anim;
// What the screen currently shows, so each update draws only the difference.
static int shownA = -1, shownB = -1, shownC = -1;
static char networkName[33] = "";

static void printCentered(const char* text, int y, uint8_t size, uint16_t color) {
  tft.setTextSize(size);
  tft.setTextColor(color, TFT_BLACK);
  tft.setCursor((320 - tft.textWidth(text)) / 2, y);
  tft.print(text);
}

static void clearTextBand(int y, int h) {
  tft.fillRect(0, y, 320, h, TFT_BLACK);
}

// --- Server rack (boot and restart) ---------------------------------------------------

static constexpr int RACK_W = 80;
static constexpr int RACK_TOP = 52;
static constexpr int UNIT_H = 26;
static constexpr int UNIT_PITCH = 30;

static void drawRack(int x) {
  tft.drawRoundRect(x - 6, RACK_TOP - 6, RACK_W + 12, BOOT_LEDS * UNIT_PITCH + 8, 6, TFT_LIGHTGREY);
  for (int i = 0; i < BOOT_LEDS; i++) {
    int y = RACK_TOP + i * UNIT_PITCH;
    tft.fillRoundRect(x, y, RACK_W, UNIT_H, 4, TFT_DARKGREY);
    for (int slot = 0; slot < 3; slot++) tft.drawFastHLine(x + 8, y + 8 + slot * 5, 34, TFT_LIGHTGREY);
  }
}

static void drawRackLed(int x, int index, bool on) {
  int cx = x + RACK_W - 16, cy = RACK_TOP + index * UNIT_PITCH + UNIT_H / 2;
  tft.fillCircle(cx, cy, 5, on ? TFT_GREEN : LED_OFF);
  if (on) tft.drawCircle(cx, cy, 6, TFT_DARKGREEN);
  else tft.drawCircle(cx, cy, 6, TFT_DARKGREY);
}

// --- Boot -----------------------------------------------------------------------------

static constexpr int BOOT_RACK_X = 120;
static const int BOOT_NODE_X[BOOT_LINKS] = {90, 160, 230};
static constexpr int BOOT_NODE_Y = 176;

static void startBoot() {
  drawRack(BOOT_RACK_X);
  for (int i = 0; i < BOOT_LEDS; i++) drawRackLed(BOOT_RACK_X, i, false);
  printCentered("STARTING UP", 204, 2, TFT_LIGHTGREY);
}

static void updateBoot(uint32_t elapsed) {
  int leds = bootLedsOn(elapsed);
  for (int i = max(shownA, 0); i < leds; i++) drawRackLed(BOOT_RACK_X, i, true);
  shownA = max(shownA, leds);
  int links = bootLinks(elapsed);
  for (int i = max(shownB, 0); i < links; i++) {
    int fromY = RACK_TOP + BOOT_LEDS * UNIT_PITCH + 2;
    tft.drawLine(160, fromY, BOOT_NODE_X[i], BOOT_NODE_Y - 7, TFT_CYAN);
    tft.drawLine(161, fromY, BOOT_NODE_X[i] + 1, BOOT_NODE_Y - 7, TFT_CYAN);
    tft.fillCircle(BOOT_NODE_X[i], BOOT_NODE_Y, 6, TFT_CYAN);
  }
  shownB = max(shownB, links);
  if (shownC < 1 && elapsed >= BOOT_READY_AT) {
    clearTextBand(200, 24);
    printCentered("HOMELAB READY", 204, 2, TFT_GREEN);
    shownC = 1;
  }
}

// --- Wake -----------------------------------------------------------------------------

static constexpr int HORIZON_Y = 150;
static constexpr int SUN_X = 160;
static constexpr int SUN_R = 18;

static int sunY(int step) {
  return HORIZON_Y + SUN_R - step * 8;
}

static void drawMoon(int x, int y, uint16_t color) {
  tft.fillCircle(x, y, 14, color);
  if (color != TFT_BLACK) tft.fillCircle(x + 7, y - 5, 12, TFT_BLACK); // Crescent.
}

static void startWake() {
  tft.drawFastHLine(20, HORIZON_Y, 280, TFT_DARKGREY);
  drawMoon(250, 70, TFT_LIGHTGREY);
}

static void updateWake(uint32_t elapsed) {
  if (shownA < 1 && elapsed >= WAKE_MOON_GONE_AT) {
    tft.fillRect(234, 54, 34, 34, TFT_BLACK); // The moon sets.
    shownA = 1;
  }
  int step = wakeSunStep(elapsed);
  if (step != max(shownB, 0)) {
    if (shownB > 0) tft.fillCircle(SUN_X, sunY(shownB), SUN_R, TFT_BLACK);
    tft.fillCircle(SUN_X, sunY(step), SUN_R, TFT_YELLOW);
    // Clip at the horizon.
    tft.fillRect(SUN_X - SUN_R - 1, HORIZON_Y + 1, 2 * SUN_R + 3, SUN_R + 2, TFT_BLACK);
    tft.drawFastHLine(20, HORIZON_Y, 280, TFT_DARKGREY);
    shownB = step;
  }
  if (shownC < 1 && elapsed >= WAKE_RAYS_AT) {
    int cy = sunY(WAKE_SUN_STEPS);
    for (int k = 0; k < GEAR_ANGLES; k += 4) {
      int s = gearSin(k), c = gearCos(k);
      if (cy - (SUN_R + 4) * s / 64 > HORIZON_Y - 2) continue; // Only above the horizon.
      tft.drawLine(SUN_X + (SUN_R + 5) * c / 64, cy - (SUN_R + 5) * s / 64,
                   SUN_X + (SUN_R + 12) * c / 64, cy - (SUN_R + 12) * s / 64, TFT_YELLOW);
    }
    printCentered("GOOD MORNING", 186, 2, TFT_WHITE);
    printCentered("Welcome back", 212, 1, TFT_LIGHTGREY);
    shownC = 1;
  }
}

// --- Wi-Fi ------------------------------------------------------------------------------

static constexpr int WIFI_X = 160;
static constexpr int WIFI_Y = 124;     // Arc centre (the "dot").
static const int ARC_R[WIFI_ARCS] = {18, 32, 46};
static constexpr int ARC_W = 7;
static constexpr int WIFI_ARC_FROM = 4; // 45 degrees, in gear-table steps.
static constexpr int WIFI_ARC_TO = 12;  // 135 degrees.

// Integer arc opening upwards (45..135 degrees) from filled quads, using the
// gear's sine table: no float trigonometry, and erasable by redrawing black.
static void drawWifiArc(int r, int width, uint16_t color) {
  int inner = r - width;
  for (int k = WIFI_ARC_FROM; k < WIFI_ARC_TO; k++) {
    int ox0 = WIFI_X + r * gearCos(k) / 64, oy0 = WIFI_Y - r * gearSin(k) / 64;
    int ox1 = WIFI_X + r * gearCos(k + 1) / 64, oy1 = WIFI_Y - r * gearSin(k + 1) / 64;
    int ix0 = WIFI_X + inner * gearCos(k) / 64, iy0 = WIFI_Y - inner * gearSin(k) / 64;
    int ix1 = WIFI_X + inner * gearCos(k + 1) / 64, iy1 = WIFI_Y - inner * gearSin(k + 1) / 64;
    tft.fillTriangle(ox0, oy0, ox1, oy1, ix0, iy0, color);
    tft.fillTriangle(ix0, iy0, ox1, oy1, ix1, iy1, color);
  }
}

static void drawArcs(int count, uint16_t color) {
  for (int i = 0; i < count; i++) drawWifiArc(ARC_R[i], ARC_W, color);
}

static void drawRouter(uint16_t dot) {
  tft.fillCircle(WIFI_X, WIFI_Y, 5, dot);
  tft.fillRoundRect(WIFI_X - 30, WIFI_Y + 12, 60, 18, 5, TFT_DARKGREY);
  for (int i = 0; i < 3; i++) tft.fillCircle(WIFI_X - 14 + i * 14, WIFI_Y + 21, 2, TFT_GREEN);
}

static void drawNetworkLine(int y) {
  if (!networkName[0]) return;
  char line[40];
  snprintf(line, sizeof(line), "%.32s", networkName);
  printCentered(line, y, 1, TFT_LIGHTGREY);
}

static void startWifiConnecting() {
  drawRouter(TFT_CYAN);
  printCentered("CONNECTING TO WI-FI", 172, 2, TFT_CYAN);
  drawNetworkLine(200);
}

static void updateWifiConnecting(uint32_t elapsed) {
  int arcs = wifiArcCount(elapsed);
  if (arcs == shownA) return;
  if (arcs < shownA) drawArcs(shownA, TFT_BLACK); // Wrapped: start over.
  else drawArcs(arcs, TFT_CYAN);
  shownA = arcs;
}

static void startWifiConnected() {
  drawRouter(TFT_GREEN);
  drawArcs(WIFI_ARCS, TFT_GREEN);
  printCentered("CONNECTED", 172, 2, TFT_GREEN);
  if (WiFi.status() == WL_CONNECTED) {
    IPAddress ip = WiFi.localIP();
    char text[20];
    snprintf(text, sizeof(text), "%u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]);
    printCentered(text, 200, 1, TFT_LIGHTGREY);
  }
}

static void updateWifiConnected(uint32_t elapsed) {
  // A pulse: thin rings ripple outwards beyond the arcs.
  int rings = wifiPulseRings(elapsed);
  for (int i = max(shownA, 0); i < rings; i++) {
    int r = ARC_R[WIFI_ARCS - 1] + 6 + i * 6;
    drawWifiArc(r, 2, i == rings - 1 ? TFT_GREEN : TFT_DARKGREEN);
  }
  shownA = max(shownA, rings);
}

static void startWifiFailed() {
  drawRouter(TFT_RED);
  drawArcs(WIFI_ARCS, TFT_MAROON);
  int cx = WIFI_X, cy = WIFI_Y - 24;
  for (int d = -1; d <= 1; d++) {
    tft.drawLine(cx - 12 + d, cy - 12, cx + 12 + d, cy + 12, TFT_RED);
    tft.drawLine(cx + 12 + d, cy - 12, cx - 12 + d, cy + 12, TFT_RED);
  }
  printCentered("WI-FI FAILED", 172, 2, TFT_RED);
  printCentered("Trying the other network...", 200, 1, TFT_LIGHTGREY);
}

// --- Restart --------------------------------------------------------------------------

static constexpr int RESTART_RACK_X = 56;
static constexpr int GEAR_X = 236;
static constexpr int GEAR_Y = 98;

// Solid trapezoid teeth (wide at the rim, narrower at the tip).
static void drawGearTeeth(int step, uint16_t color) {
  for (int t = 0; t < GEAR_TEETH; t++) {
    int k = gearToothAngle(t, step);
    int c = gearCos(k), s = gearSin(k);
    int bx = GEAR_X + 19 * c / 64, by = GEAR_Y + 19 * s / 64; // Base centre.
    int tx = GEAR_X + 28 * c / 64, ty = GEAR_Y + 28 * s / 64; // Tip centre.
    int bw = 5, tw = 3;                                       // Half widths.
    int b1x = bx - s * bw / 64, b1y = by + c * bw / 64, b2x = bx + s * bw / 64, b2y = by - c * bw / 64;
    int t1x = tx - s * tw / 64, t1y = ty + c * tw / 64, t2x = tx + s * tw / 64, t2y = ty - c * tw / 64;
    tft.fillTriangle(b1x, b1y, b2x, b2y, t1x, t1y, color);
    tft.fillTriangle(b2x, b2y, t2x, t2y, t1x, t1y, color);
  }
}

static void drawGearHub() {
  tft.fillCircle(GEAR_X, GEAR_Y, 20, TFT_CYAN);
  tft.fillCircle(GEAR_X, GEAR_Y, 7, TFT_BLACK);
}

static void startRestart() {
  drawRack(RESTART_RACK_X);
  for (int i = 0; i < BOOT_LEDS; i++) drawRackLed(RESTART_RACK_X, i, true);
  drawGearHub();
  printCentered("RESTARTING...", 190, 2, TFT_WHITE);
  shownB = BOOT_LEDS;
}

static void updateRestart(uint32_t elapsed) {
  int step = restartGearStep(elapsed);
  if (step != shownA) {
    if (shownA >= 0) drawGearTeeth(shownA, TFT_BLACK);
    drawGearHub(); // Repairs the rim where old teeth were erased.
    drawGearTeeth(step, TFT_CYAN);
    shownA = step;
  }
  int leds = restartLedsOn(elapsed);
  for (int i = shownB - 1; i >= leds; i--) drawRackLed(RESTART_RACK_X, i, false); // Bottom first.
  shownB = min(shownB, leds);
}

// --- Sleep ----------------------------------------------------------------------------

static constexpr int FLOOR_Y = 160;
static constexpr int BED_X = 222;

static void drawBed() {
  tft.fillRect(BED_X + 76, FLOOR_Y - 40, 6, 40, TFT_BROWN);       // Headboard.
  tft.fillRect(BED_X, FLOOR_Y - 10, 4, 10, TFT_BROWN);            // Foot leg.
  tft.fillRect(BED_X, FLOOR_Y - 20, 76, 10, TFT_LIGHTGREY);       // Mattress.
  tft.fillRoundRect(BED_X + 58, FLOOR_Y - 28, 17, 8, 3, TFT_WHITE); // Pillow.
}

static void drawStar(int x, int y) {
  tft.drawFastHLine(x - 4, y, 9, TFT_YELLOW);
  tft.drawFastVLine(x, y - 4, 9, TFT_YELLOW);
  tft.drawPixel(x - 2, y - 2, TFT_YELLOW);
  tft.drawPixel(x + 2, y + 2, TFT_YELLOW);
}

static const int STAR_XY[SLEEP_STARS][2] = {{116, 38}, {160, 66}, {36, 92}};

static void startSleep() {
  tft.drawFastHLine(10, FLOOR_Y + 2, 300, TFT_DARKGREY);
  drawBed();
  printCentered("Wake: tap screen if supported, or press RST.", 220, 1, TFT_DARKGREY);
}

static void updateSleep(uint32_t elapsed) {
  SleepPhase phase = sleepPhase(elapsed);
  if (phase == SleepPhase::Walk) {
    int step = sleepWalkStep(elapsed);
    if (step == shownA) return;
    if (shownA >= 0) drawWalker(sleepWalkerX(shownA), FLOOR_Y, WALK_CYCLE[shownA % WALKER_FRAMES], true);
    drawWalker(sleepWalkerX(step), FLOOR_Y, WALK_CYCLE[step % WALKER_FRAMES], false);
    shownA = step;
    return;
  }
  if (shownB < 1) {
    // Lie down: the walker is replaced by a head on the pillow under a blanket.
    if (shownA >= 0) drawWalker(sleepWalkerX(shownA), FLOOR_Y, WALK_CYCLE[shownA % WALKER_FRAMES], true);
    tft.fillCircle(BED_X + 66, FLOOR_Y - 34, 7, TFT_WHITE);
    tft.fillCircle(BED_X + 66, FLOOR_Y - 34, 5, TFT_BLACK);
    tft.fillRoundRect(BED_X + 4, FLOOR_Y - 30, 54, 12, 4, TFT_BLUE);
    tft.drawRoundRect(BED_X + 4, FLOOR_Y - 30, 54, 12, 4, TFT_NAVY);
    shownB = 1;
  }
  if (phase != SleepPhase::Night) return;
  if (shownC < 0) {
    tft.fillCircle(60, 52, 16, TFT_YELLOW);
    tft.fillCircle(68, 46, 14, TFT_BLACK);
    printCentered("GOOD NIGHT", 186, 2, TFT_WHITE);
    tft.setTextSize(1);
    tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    tft.setCursor(BED_X + 70, FLOOR_Y - 58);
    tft.print("z Z");
    shownC = 0;
  }
  int stars = sleepStars(elapsed);
  for (int i = shownC; i < stars; i++) drawStar(STAR_XY[i][0], STAR_XY[i][1]);
  shownC = max(shownC, stars);
}

// --- Loading ----------------------------------------------------------------------------

void drawLoadingDots(int cx, int baseY, int phase, uint16_t color) {
  tft.fillRect(cx - 20, baseY - 12, 41, 17, TFT_BLACK);
  for (int i = 0; i < LOADING_DOTS; i++) {
    tft.fillCircle(cx + (i - 1) * 14, baseY - loadingDotLift(phase, i), 4, color);
  }
}

static void startLoading() {
  printCentered("LOADING", 100, 2, TFT_CYAN);
}

static void updateLoading(uint32_t elapsed) {
  int phase = loadingPhase(elapsed);
  if (phase == shownA) return;
  drawLoadingDots(160, 140, phase, TFT_CYAN);
  shownA = phase;
}

// --- Dispatch ---------------------------------------------------------------------------

void systemAnimationStart(SystemAnimationType type) {
  anim.start(type, millis());
  shownA = shownB = shownC = -1;
  if (type == SystemAnimationType::None) return;
  tft.fillScreen(TFT_BLACK);
  switch (type) {
    case SystemAnimationType::Boot: startBoot(); break;
    case SystemAnimationType::Wake: startWake(); break;
    case SystemAnimationType::WifiConnecting: startWifiConnecting(); break;
    case SystemAnimationType::WifiConnected: startWifiConnected(); break;
    case SystemAnimationType::WifiFailed: startWifiFailed(); break;
    case SystemAnimationType::Restart: startRestart(); break;
    case SystemAnimationType::Sleep: startSleep(); break;
    case SystemAnimationType::Loading: startLoading(); break;
    case SystemAnimationType::None: break;
  }
  systemAnimationUpdate();
}

bool systemAnimationUpdate() {
  if (!anim.active()) return false;
  uint32_t now = millis();
  uint32_t elapsed = anim.elapsed(now);
  uint32_t duration = systemAnimationDuration(anim.type);
  if (duration && elapsed > duration) elapsed = duration; // Settle on the last frame.
  switch (anim.type) {
    case SystemAnimationType::Boot: updateBoot(elapsed); break;
    case SystemAnimationType::Wake: updateWake(elapsed); break;
    case SystemAnimationType::WifiConnecting: updateWifiConnecting(elapsed); break;
    case SystemAnimationType::WifiConnected: updateWifiConnected(elapsed); break;
    case SystemAnimationType::Restart: updateRestart(elapsed); break;
    case SystemAnimationType::Sleep: updateSleep(elapsed); break;
    case SystemAnimationType::Loading: updateLoading(elapsed); break;
    case SystemAnimationType::WifiFailed:
    case SystemAnimationType::None: break;
  }
  return !anim.finished(now);
}

bool systemAnimationActive() {
  return anim.active();
}

void systemAnimationCancel() {
  anim.cancel();
}

// --- Startup sequence -------------------------------------------------------------------

static StartupSequence startup;

void startupAnimationBegin() {
  startup.begin(startupIntroFor((int)esp_sleep_get_wakeup_cause()), millis());
  systemAnimationStart(startup.animation());
}

void startupAnimationSetNetwork(const char* ssid) {
  snprintf(networkName, sizeof(networkName), "%s", ssid ? ssid : "");
}

static void serviceStartup(bool connected) {
  if (startup.update(millis(), connected)) systemAnimationStart(startup.animation());
  systemAnimationUpdate();
}

void startupAnimationService() {
  serviceStartup(false);
}

void startupAnimationAttemptFailed() {
  if (startup.attemptFailed(millis())) systemAnimationStart(SystemAnimationType::WifiFailed);
}

void startupAnimationFinish() {
  // setup() only: a short, bounded wait (intro remainder + CONNECTED) that
  // yields to other tasks, like the connect loop around it.
  while (!startup.done()) {
    serviceStartup(true);
    vTaskDelay(pdMS_TO_TICKS(STARTUP_POLL_MS));
  }
  systemAnimationCancel();
}
