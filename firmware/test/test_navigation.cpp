#include "HomeLogic.h"
#include "MenuLayout.h"
#include "Pages.h"
#include "ServicesLogic.h"
#include "SettingsLogic.h"
#include "UiIconShapes.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

// MORE header "<  MORE 1/2  >": the targets follow the drawn arrows.
static void testMorePaging() {
  const int y = MORE_HEADER_TEXT_Y + 7;  // Middle of the size-2 text.
  // The visible glyphs are inside their targets (">" was dead before: x 166-176).
  assert(morePagingAt(MORE_PREV_X + 5, y) == MorePaging::Prev);
  assert(morePagingAt(MORE_NEXT_X + 5, y) == MorePaging::Next);
  assert(morePagingAt(171, y) == MorePaging::Next && morePagingAt(160, 10) == MorePaging::Next);
  // Edges of each target.
  assert(morePagingAt(0, 0) == MorePaging::Prev && morePagingAt(MORE_PREV_HIT_END - 1, MORE_HEADER_H - 1) == MorePaging::Prev);
  assert(morePagingAt(MORE_PREV_HIT_END, y) == MorePaging::None);
  assert(morePagingAt(MORE_NEXT_HIT_X - 1, y) == MorePaging::None);
  assert(morePagingAt(MORE_NEXT_HIT_X, 0) == MorePaging::Next && morePagingAt(319, MORE_HEADER_H - 1) == MorePaging::Next);
  assert(morePagingAt(300, 10) == MorePaging::Next);  // Over LIVE/OFFLINE (not a control).
  assert(morePagingAt(20, MORE_HEADER_H) == MorePaging::None && morePagingAt(300, 60) == MorePaging::None);
  assert(morePagingAt(-1, y) == MorePaging::None && morePagingAt(320, y) == MorePaging::None);
  // The title is never a target.
  for (int x = MORE_TITLE_X; x < MORE_TITLE_X + MORE_TITLE_CHARS * MORE_HEADER_CHAR_W; x++) {
    assert(morePagingAt(x, y) == MorePaging::None);
  }
  // Comfortable resistive targets.
  assert(MORE_PREV_HIT_END >= 40 && 320 - MORE_NEXT_HIT_X >= 160);
  // "<  MORE 1/2  >" at x 10: the pieces sit exactly where the old string put them.
  assert(MORE_TITLE_X == MORE_PREV_X + 3 * MORE_HEADER_CHAR_W);
  assert(MORE_NEXT_X == MORE_PREV_X + 13 * MORE_HEADER_CHAR_W);

  // Page steps: 1 -> 2 and back, always in range (the launcher wraps).
  assert(moreStepPage(0, MorePaging::Next) == 1);
  assert(moreStepPage(1, MorePaging::Prev) == 0);
  assert(moreStepPage(0, MorePaging::Prev) == 1);  // No underflow.
  assert(moreStepPage(1, MorePaging::Next) == 0);  // No overflow.
  assert(moreStepPage(1, MorePaging::None) == 1);
  assert(moreStepPage(7, MorePaging::Next) == 1 && moreStepPage(-3, MorePaging::Prev) == 1);
  // A tap on the drawn ">" on page 1 shows page 2, whose "<" returns to page 1.
  int page = moreStepPage(0, morePagingAt(MORE_NEXT_X + 5, y));
  assert(page == 1 && moreAppAt(moreTileX(0) + 30, moreTileY(0) + 30, page) == MORE_APP_COUNT - 1);
  assert(moreStepPage(page, morePagingAt(MORE_PREV_X + 5, y)) == 0);
  assert(moreStepPage(page, morePagingAt(MORE_TITLE_X + 40, y)) == page);  // Title tap: no change.
}

static void testRootNav() {
  NavTab tab;
  assert(!rootNavAt(160, 204, tab)); // Above the bar.
  assert(rootNavAt(0, 205, tab) && tab == NavTab::Home);
  assert(rootNavAt(106, 239, tab) && tab == NavTab::Home);
  assert(rootNavAt(107, 220, tab) && tab == NavTab::More);
  assert(rootNavAt(213, 220, tab) && tab == NavTab::More);
  assert(rootNavAt(214, 220, tab) && tab == NavTab::Settings);
  assert(rootNavAt(319, 239, tab) && tab == NavTab::Settings);
  // Three equal-ish cells, the whole cell tappable.
  assert(NAV_SPLIT_1 == 107 && NAV_SPLIT_2 - NAV_SPLIT_1 == 107 && 320 - NAV_SPLIT_2 == 106);

  // Active tab: HOME only on HOME; Settings hierarchy and its Stopwatch;
  // every app under MORE.
  assert(navTabForPage(PAGE_HOME) == NavTab::Home);
  assert(navTabForPage(PAGE_SETTINGS) == NavTab::Settings);
  assert(navTabForPage(PAGE_STOPWATCH) == NavTab::Settings);
  const Page apps[] = {PAGE_MORE, PAGE_HOMESERVER, PAGE_SERVICES, PAGE_WEATHER, PAGE_CALENDAR, PAGE_PHOTOS,
                       PAGE_ALERTS, PAGE_GAMES, PAGE_TTT, PAGE_SNAKE, PAGE_TIME};
  for (Page page : apps) assert(navTabForPage(page) == NavTab::More);
}

static void testBackTargets() {
  // Alerts and Weather return to where they were opened from.
  assert(appBackTarget(PAGE_ALERTS, PAGE_HOME) == PAGE_HOME);
  assert(appBackTarget(PAGE_ALERTS, PAGE_MORE) == PAGE_MORE);
  assert(appBackTarget(PAGE_ALERTS, PAGE_HOMESERVER) == PAGE_HOMESERVER);
  assert(appBackTarget(PAGE_ALERTS, PAGE_SCREENSAVER) == PAGE_MORE); // Unknown origin: MORE.
  assert(appBackTarget(PAGE_WEATHER, PAGE_HOME) == PAGE_HOME);
  assert(appBackTarget(PAGE_WEATHER, PAGE_TIME) == PAGE_TIME);
  assert(appBackTarget(PAGE_WEATHER, PAGE_MORE) == PAGE_MORE);
  // Apps -> MORE; games -> GAMES; Stopwatch -> Settings.
  const Page apps[] = {PAGE_HOMESERVER, PAGE_SERVICES, PAGE_CALENDAR, PAGE_PHOTOS, PAGE_GAMES, PAGE_TIME,
                       PAGE_AI};
  for (Page page : apps) assert(appBackTarget(page, PAGE_HOME) == PAGE_MORE);
  const Page games[] = {PAGE_TTT, PAGE_REACTION, PAGE_SNAKE, PAGE_MEMORY, PAGE_SIMON};
  for (Page page : games) assert(appBackTarget(page, PAGE_MORE) == PAGE_GAMES);
  assert(appBackTarget(PAGE_STOPWATCH, PAGE_MORE) == PAGE_SETTINGS);
  // Status-fetch suppression is unchanged: games in play only.
  for (Page page : games) assert(isGamePlayPage(page));
  assert(!isGamePlayPage(PAGE_GAMES) && !isGamePlayPage(PAGE_HOME) && !isGamePlayPage(PAGE_SETTINGS));
}

static void testHome() {
  assert(homeHealth(false, 0, 0) == HomeHealth::NoData);
  assert(homeHealth(true, 0, 0) == HomeHealth::Healthy);
  assert(homeHealth(true, 0, 2) == HomeHealth::Attention);
  assert(homeHealth(true, 1, 2) == HomeHealth::Critical);
  assert(homeHealth(true, 1, 0) == HomeHealth::Critical);
  assert(homeHealth(false, 0, 1) == HomeHealth::Attention); // Stale data before any status.
  assert(strcmp(homeHealthLabel(HomeHealth::Healthy), "HEALTHY") == 0);
  assert(strcmp(homeHealthLabel(HomeHealth::Attention), "ATTENTION") == 0);
  assert(strcmp(homeHealthLabel(HomeHealth::Critical), "CRITICAL") == 0);
  assert(strcmp(homeHealthLabel(HomeHealth::NoData), "NO DATA") == 0);

  char text[40];
  formatAlertCounts(true, 0, 0, text, sizeof(text));
  assert(strcmp(text, "No active alerts") == 0);
  formatAlertCounts(false, 0, 0, text, sizeof(text));
  assert(strcmp(text, "Waiting for status") == 0);
  formatAlertCounts(true, 0, 1, text, sizeof(text));
  assert(strcmp(text, "1 warning") == 0);
  formatAlertCounts(true, 0, 2, text, sizeof(text));
  assert(strcmp(text, "2 warnings") == 0);
  formatAlertCounts(true, 1, 0, text, sizeof(text));
  assert(strcmp(text, "1 critical") == 0);
  formatAlertCounts(true, 1, 2, text, sizeof(text));
  assert(strcmp(text, "1 critical, 2 warnings") == 0);

  formatCpuRam(true, 18.4f, 71.6f, text, sizeof(text));
  assert(strcmp(text, "CPU 18%   RAM 72%") == 0);
  formatCpuRam(false, 18.4f, 71.6f, text, sizeof(text)); // Host unavailable: never stale values.
  assert(strcmp(text, "CPU --   RAM --") == 0);
  formatCpuRam(true, -3, 140, text, sizeof(text));
  assert(strcmp(text, "CPU 0%   RAM 100%") == 0);

  // Touch areas: weather, health (alerts), CPU/RAM (HomeServer); date/time inert.
  assert(homeHitAt(160, 20) == HomeHit::None);
  assert(homeHitAt(160, 55) == HomeHit::None);
  assert(homeHitAt(160, 100) == HomeHit::Weather);
  assert(homeHitAt(160, 150) == HomeHit::Alerts);
  assert(homeHitAt(160, 190) == HomeHit::HomeServer);
  assert(homeHitAt(160, 210) == HomeHit::None); // Bottom navigation.
  assert(HOME_METRICS_BOTTOM < NAV_Y && HOME_WEATHER_BOTTOM < HOME_HEALTH_TOP &&
         HOME_HEALTH_BOTTOM < HOME_METRICS_TOP);
  assert(HOME_HEALTH_BOTTOM - HOME_HEALTH_TOP >= 30); // Resistive-friendly.
}

static void testMore() {
  // Seven required apps plus Clocks (world clocks) and the AI Assistant, each with an icon.
  const struct { Page page; UiIcon icon; } expected[] = {
    {PAGE_HOMESERVER, UiIcon::HomeServer}, {PAGE_SERVICES, UiIcon::Services}, {PAGE_WEATHER, UiIcon::Weather},
    {PAGE_CALENDAR, UiIcon::Calendar}, {PAGE_PHOTOS, UiIcon::Photos}, {PAGE_ALERTS, UiIcon::Alerts},
    {PAGE_GAMES, UiIcon::Games}, {PAGE_TIME, UiIcon::Clocks}, {PAGE_AI, UiIcon::AiAssistant},
  };
  assert(MORE_APP_COUNT == 9);
  for (int i = 0; i < MORE_APP_COUNT; i++) {
    assert(MORE_APPS[i].page == expected[i].page && MORE_APPS[i].icon == expected[i].icon);
    assert(strlen(MORE_APPS[i].label) * 6 <= (size_t)MORE_TILE_W); // Size-1 label fits its tile.
    for (int j = 0; j < i; j++) assert(MORE_APPS[j].page != MORE_APPS[i].page);
  }
  // The ninth app (AI ASSISTANT) opens a second page, reached by header paging.
  assert(morePageCount() == 2);
  testMorePaging();
  // Tile centres map to their apps; gaps belong to a neighbour; bars excluded.
  for (int i = 0; i < MORE_APP_COUNT; i++) {
    int page = i / MORE_PER_PAGE, slot = i % MORE_PER_PAGE;
    int col = slot % MORE_COLS, row = slot / MORE_COLS;
    int cx = moreTileX(col) + MORE_TILE_W / 2, cy = moreTileY(row) + MORE_TILE_H / 2;
    assert(moreAppAt(cx, cy, page) == i);
  }
  assert(MORE_APPS[moreAppAt(moreTileX(0) + 30, moreTileY(0) + 30, 1)].page == PAGE_AI);
  assert(moreAppAt(moreTileX(1) + 30, moreTileY(0) + 30, 1) == -1); // Empty slots.
  assert(moreAppAt(moreTileX(1) - 2, 60, 0) >= 0);  // Gap between tiles.
  assert(moreAppAt(160, 20, 0) == -1);              // Header.
  assert(moreAppAt(160, 210, 0) == -1);             // Bottom navigation.
  assert(moreAppAt(160, 100, 2) == -1);             // No third page.
  // Tiles fit the screen above the navigation and are large.
  assert(moreTileX(MORE_COLS - 1) + MORE_TILE_W <= 320);
  assert(moreTileY(MORE_ROWS - 1) + MORE_TILE_H < NAV_Y);
  assert(MORE_TILE_W >= 70 && MORE_TILE_H >= 70 && MORE_ICON_BOX >= 40);
  // Icon glyph contrast.
  assert(glyphColorOn(UiColor::Yellow) == UiColor::Black);
  assert(glyphColorOn(UiColor::Blue) == UiColor::White);
  assert(glyphColorOn(UiColor::Grey) == UiColor::White);
}

static void testServices() {
  // Online / offline / unknown (missing key) per service.
  uint8_t reported = (1u << SVC_JELLYFIN) | (1u << SVC_OLLAMA) | (1u << SVC_IMMICH);
  uint8_t online = (1u << SVC_JELLYFIN) | (1u << SVC_NAVIDROME); // Navidrome not reported.
  assert(serviceDot(reported, online, SVC_JELLYFIN) == ServiceDot::Online);
  assert(serviceDot(reported, online, SVC_OLLAMA) == ServiceDot::Offline);
  assert(serviceDot(reported, online, SVC_NAVIDROME) == ServiceDot::Unknown);
  assert(serviceDot(reported, online, SVC_METUBE) == ServiceDot::Unknown);
  assert(serviceDot(0xFF, 0xFF, -1) == ServiceDot::Unknown);
  assert(serviceDot(0xFF, 0xFF, SVC_COUNT) == ServiceDot::Unknown);
  // The list: real health checks only (no Bazarr / MCP), all rows above BACK.
  const char* keys[] = {"jellyfin", "navidrome", "metube", "ollama", "cloudflare", "nextcloud", "immich",
                        "technical_blog"};
  assert(SVC_COUNT == 8);
  for (int i = 0; i < SVC_COUNT; i++) {
    assert(strcmp(SERVICE_LIST[i].key, keys[i]) == 0);
    assert(strcmp(SERVICE_LIST[i].key, "bazarr") != 0 && strcmp(SERVICE_LIST[i].key, "mcp") != 0);
  }
  assert(SERVICES_Y0 + SVC_COUNT * SERVICES_PITCH <= NAV_Y);
  assert(SERVICES_PITCH >= 20);
}

// Bounding box of one step on the 24 grid (x0, y0, x1, y1 inclusive).
static void stepBounds(const IconStep& s, int b[4]) {
  switch (s.op) {
    case IconOp::Line:
    case IconOp::Thick:
      b[0] = s.a < s.c ? s.a : s.c;
      b[2] = (s.a > s.c ? s.a : s.c) + (s.op == IconOp::Thick ? 1 : 0);
      b[1] = s.b < s.d ? s.b : s.d;
      b[3] = s.b > s.d ? s.b : s.d;
      break;
    case IconOp::Rect:
    case IconOp::FillRect:
    case IconOp::RoundRect:
    case IconOp::FillRoundRect:
      b[0] = s.a; b[1] = s.b; b[2] = s.a + s.c - 1; b[3] = s.b + s.d - 1;
      break;
    case IconOp::Circle:
    case IconOp::FillCircle:
      b[0] = s.a - s.c; b[1] = s.b - s.c; b[2] = s.a + s.c; b[3] = s.b + s.c;
      break;
    case IconOp::Triangle: {
      int xs[3] = {s.a, s.c, s.e}, ys[3] = {s.b, s.d, s.f};
      b[0] = b[2] = xs[0];
      b[1] = b[3] = ys[0];
      for (int i = 1; i < 3; i++) {
        if (xs[i] < b[0]) b[0] = xs[i];
        if (xs[i] > b[2]) b[2] = xs[i];
        if (ys[i] < b[1]) b[1] = ys[i];
        if (ys[i] > b[3]) b[3] = ys[i];
      }
      break;
    }
  }
}

static void testIcons() {
  int totalSteps = 0;
  for (int i = 0; i < (int)UiIcon::Count; i++) {
    IconShape shape = uiIconShape((UiIcon)i);
    assert(shape.steps && shape.count > 0); // Every icon has a drawing.
    totalSteps += shape.count;
    bool visible = false;
    for (int k = 0; k < shape.count; k++) {
      int b[4];
      stepBounds(shape.steps[k], b);
      // Inside the 24x24 grid, so a size-px icon stays in its size-px box.
      assert(b[0] >= 0 && b[1] >= 0 && b[2] < ICON_GRID && b[3] < ICON_GRID);
      if (!(shape.steps[k].flags & ICON_CUT)) visible = true;
    }
    assert(visible);
  }
  assert(uiIconShape(UiIcon::Count).count == 0);
  assert(totalSteps * sizeof(IconStep) < 2048); // Tiny flash footprint.
  // Scaling keeps the 24 grid inside the requested size.
  const int sizes[] = {14, 16, 18, 24, 30, 34};
  for (int size : sizes) {
    assert(iconScale(0, size) == 0 && iconScale(ICON_GRID - 1, size) < size);
  }
  // Root tabs and categories have their own glyphs.
  assert(uiIconShape(UiIcon::Home).steps != uiIconShape(UiIcon::More).steps);
  assert(uiIconShape(UiIcon::Settings).steps == uiIconShape(UiIcon::System).steps); // Shared gear.
}

int main() {
  testRootNav();
  testBackTargets();
  testHome();
  testMore();
  testServices();
  testIcons();
  puts("Navigation tests passed: root nav, back targets, home, more, services, icons");
}
