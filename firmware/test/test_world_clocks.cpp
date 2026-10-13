// Local world clocks (Phase 8.7): the bounded list, duplicates, the NVS blob
// (CRC, partial writes, bad data), UTC + offset time and date, local/backend
// precedence, offset refresh, the common-city list, the phone page, and
// source checks that no backend, offset table or personal city is involved
// and that clock and location searches never write each other's data.
#include "CommonCities.h"
#include "LocalClockLogic.h"
#include "SetupPage.h"
#include "WeatherLogic.h"
#include "WorldClockUi.h"
#include <assert.h>
#include <fstream>
#include <sstream>
#include <stdio.h>
#include <string.h>
#include <string>

static WorldClockLocation place(const char* name, const char* zone, double lat, double lon) {
  WorldClockLocation c;
  snprintf(c.name, sizeof(c.name), "%s", name);
  snprintf(c.country, sizeof(c.country), "Country");
  snprintf(c.timezone, sizeof(c.timezone), "%s", zone);
  c.latitude = lat;
  c.longitude = lon;
  return c;
}

static WorldClockLocation withOffset(WorldClockLocation c, int32_t offset) {
  c.offsetKnown = true;
  c.utcOffsetSeconds = offset;
  return c;
}

static bool has(const std::string& text, const char* needle) {
  return text.find(needle) != std::string::npos;
}

// --- 1-6, 24, 25: the list --------------------------------------------------------------------

static void testList() {
  LocalClockList list;
  assert(list.count == 0);                                                  // 1. Empty.
  WorldClockLocation london = place("London", "Europe/London", 51.50853, -0.12574);
  assert(addLocalClock(list, london) == ClockAddResult::Added && list.count == 1);   // 2. One.
  assert(strcmp(list.items[0].name, "London") == 0 && !list.items[0].offsetKnown);
  // An offset is never taken from the input (not even a common city's): only a refresh sets it.
  LocalClockList other;
  assert(addLocalClock(other, withOffset(place("Lima", "America/Lima", -12.04, -77.03), -18000)) ==
         ClockAddResult::Added && !other.items[0].offsetKnown);

  assert(addLocalClock(list, place("Tokyo", "Asia/Tokyo", 35.6895, 139.69171)) == ClockAddResult::Added);
  assert(addLocalClock(list, place("Kathmandu", "Asia/Kathmandu", 27.70169, 85.3206)) == ClockAddResult::Added);
  assert(addLocalClock(list, place("Los Angeles", "America/Los_Angeles", 34.05223, -118.24368)) ==
         ClockAddResult::Added);
  assert(list.count == 4 && LOCAL_CLOCKS_MAX == 4);                           // 3. Four.
  LocalClockList before = list;
  // 4 / 25. A fifth is refused and nothing changes (no hidden fifth entry).
  assert(addLocalClock(list, place("Paris", "Europe/Paris", 48.85341, 2.3488)) == ClockAddResult::Full);
  assert(list.count == 4 && memcmp(&before, &list, sizeof(list)) == 0);
  // 5 / 24. Duplicates: exact, and the same city with slightly different
  // coordinates (phone search vs. common list); the check comes before "full".
  assert(addLocalClock(list, london) == ClockAddResult::Duplicate);
  assert(addLocalClock(list, place("London", "Europe/London", 51.5085, -0.1257)) == ClockAddResult::Duplicate);
  assert(memcmp(&before, &list, sizeof(list)) == 0);
  LocalClockList two;
  addLocalClock(two, london);
  assert(addLocalClock(two, place("London", "Europe/London", 51.50853, -0.12574)) == ClockAddResult::Duplicate &&
         two.count == 1);
  // Different places stay different: same zone far away, or a different zone.
  assert(!sameClockPlace(london, place("Manchester", "Europe/London", 53.48, -2.24)));
  assert(!sameClockPlace(london, place("London", "America/Toronto", 51.50853, -0.12574)));
  assert(findLocalClock(list, place("x", "Asia/Tokyo", 35.69, 139.69)) == 1);
  // Invalid places are refused.
  assert(addLocalClock(two, place("", "Europe/Paris", 1, 1)) == ClockAddResult::Invalid);
  assert(addLocalClock(two, place("Nowhere", "Europe/Paris", 91, 0)) == ClockAddResult::Invalid);
  assert(addLocalClock(two, place("Bad", "Europe/Paris\"", 1, 1)) == ClockAddResult::Invalid);
  assert(two.count == 1);

  // 6. Remove keeps the others in order; bad indexes do nothing.
  assert(removeLocalClock(list, 1) && list.count == 3);
  assert(strcmp(list.items[0].name, "London") == 0 && strcmp(list.items[1].name, "Kathmandu") == 0 &&
         strcmp(list.items[2].name, "Los Angeles") == 0 && list.items[3].name[0] == '\0');
  assert(!removeLocalClock(list, 3) && !removeLocalClock(list, -1) && list.count == 3);
  assert(addLocalClock(list, place("Tokyo", "Asia/Tokyo", 35.6895, 139.69171)) == ClockAddResult::Added);
  while (list.count) removeLocalClock(list, 0);
  assert(list.count == 0 && !removeLocalClock(list, 0));
  assert(strcmp(clockAddResultText(ClockAddResult::Duplicate), "Already added") == 0);
}

// --- 7-9: NVS blob -------------------------------------------------------------------------------

static void testBlob() {
  LocalClockList list;
  addLocalClock(list, place("London", "Europe/London", 51.50853, -0.12574));
  addLocalClock(list, place("Z\xC3\xBCrich", "Europe/Zurich", 47.36667, 8.55));
  addLocalClock(list, place("Somewhere", "", -33.9, 18.4)); // No zone: timezone=auto.
  bool changed = false;
  assert(setLocalClockOffset(list, list.items[0], 3600, 1791895320u, changed) && changed);
  uint8_t blob[CLOCK_BLOB_MAX];
  size_t length = encodeLocalClocks(list, blob, sizeof(blob));
  assert(length == 4 + 3 * CLOCK_ENTRY_BYTES + 4);                          // 7. Round trip.
  LocalClockList back;
  assert(decodeLocalClocks(blob, length, back) && back.count == 3);
  assert(memcmp(&back, &list, sizeof(list)) == 0);
  assert(back.items[0].offsetKnown && back.items[0].utcOffsetSeconds == 3600 &&
         back.items[0].offsetUpdatedAt == 1791895320u && !back.items[1].offsetKnown);
  // Empty and full lists.
  LocalClockList empty, full;
  assert(encodeLocalClocks(empty, blob, sizeof(blob)) == 8 && decodeLocalClocks(blob, 8, back) && back.count == 0);
  for (int i = 0; i < LOCAL_CLOCKS_MAX; i++) addLocalClock(full, commonCityClock(i));
  length = encodeLocalClocks(full, blob, sizeof(blob));
  assert(length == CLOCK_BLOB_MAX && decodeLocalClocks(blob, length, back) && back.count == 4);
  assert(encodeLocalClocks(full, blob, CLOCK_BLOB_MAX - 1) == 0);           // Never a cut blob.

  // 9. Partial write (any shorter blob) and 8. bad data: all rejected, as "no clocks".
  length = encodeLocalClocks(list, blob, sizeof(blob));
  for (size_t cut = 0; cut < length; cut++) {
    assert(!decodeLocalClocks(blob, cut, back) && back.count == 0);
  }
  for (size_t at = 0; at < length; at++) {                                  // Every single-byte corruption.
    uint8_t copy[CLOCK_BLOB_MAX];
    memcpy(copy, blob, length);
    copy[at] ^= 0x5A;
    assert(!decodeLocalClocks(copy, length, back) && back.count == 0);
  }
  assert(!decodeLocalClocks(nullptr, 0, back));
  // Well-formed (valid CRC) but invalid content is rejected too.
  auto resealed = [](uint8_t* data, size_t size) {
    clockblob::putU32(data + size - 4, clockCrc32(data, size - 4));
  };
  uint8_t bad[CLOCK_BLOB_MAX];
  memcpy(bad, blob, length);
  bad[2] = LOCAL_CLOCKS_NVS_VERSION + 1;                                    // Unknown version.
  resealed(bad, length);
  assert(!decodeLocalClocks(bad, length, back));
  memcpy(bad, blob, length);
  bad[3] = 5;                                                               // More than 4.
  resealed(bad, length);
  assert(!decodeLocalClocks(bad, length, back));
  memcpy(bad, blob, length);
  clockblob::putDouble(bad + 4 + CLOCK_TEXT_BYTES, 95.0);                   // Latitude out of range.
  resealed(bad, length);
  assert(!decodeLocalClocks(bad, length, back));
  memcpy(bad, blob, length);
  memset(bad + 4, 'x', LOCATION_NAME_MAX);                                  // Name without terminator.
  resealed(bad, length);
  assert(!decodeLocalClocks(bad, length, back));
  memcpy(bad, blob, length);
  clockblob::putU32(bad + 4 + CLOCK_TEXT_BYTES + 17, uint32_t(3601));       // Not a real offset (known).
  resealed(bad, length);
  assert(!decodeLocalClocks(bad, length, back));
  memcpy(bad, blob, length);
  bad[4 + CLOCK_TEXT_BYTES + 16] = 2;                                       // offsetKnown not 0/1.
  resealed(bad, length);
  assert(!decodeLocalClocks(bad, length, back));
  LocalClockList twice;                                                     // A duplicate entry.
  addLocalClock(twice, place("London", "Europe/London", 51.5, -0.1));
  length = encodeLocalClocks(twice, blob, sizeof(blob));
  memcpy(bad, blob, length - 4);
  memcpy(bad + 4 + CLOCK_ENTRY_BYTES, blob + 4, CLOCK_ENTRY_BYTES);
  bad[3] = 2;
  resealed(bad, 4 + 2 * CLOCK_ENTRY_BYTES + 4);
  assert(!decodeLocalClocks(bad, 4 + 2 * CLOCK_ENTRY_BYTES + 4, back));
}

// --- 10-15: time and date --------------------------------------------------------------------

static void expectClock(int32_t offset, uint32_t utc, const char* time, const char* date) {
  char t[16], d[24];
  WorldClockLocation c = withOffset(place("X", "Etc/UTC", 0, 0), offset);
  assert(formatWorldClock(c, utc, t, sizeof(t), d, sizeof(d)));
  if (strcmp(t, time) != 0 || strcmp(d, date) != 0) fprintf(stderr, "%d: %s %s\n", int(offset), d, t);
  assert(strcmp(t, time) == 0 && strcmp(d, date) == 0);
}

static void testTime() {
  const uint32_t noon = 1791895320u;     // Tue 2026-10-13 12:42 UTC.
  const uint32_t evening = 1791921600u;  // Tue 2026-10-13 20:00 UTC.
  const uint32_t early = 1791946800u;    // Wed 2026-10-14 03:00 UTC.
  expectClock(0, noon, "12:42 PM", "Tue, Oct 13");                          // 10. UTC + offset.
  expectClock(-14400, noon, "8:42 AM", "Tue, Oct 13");                      // 11. Negative (UTC-04:00).
  expectClock(-36000, noon, "2:42 AM", "Tue, Oct 13");
  expectClock(19800, noon, "6:12 PM", "Tue, Oct 13");                       // 12. +05:30.
  expectClock(20700, noon, "6:27 PM", "Tue, Oct 13");                       // 13. +05:45.
  expectClock(32400, evening, "5:00 AM", "Wed, Oct 14");                    // 14. Forward: Tokyo is tomorrow.
  expectClock(20700, evening, "1:45 AM", "Wed, Oct 14");
  expectClock(46800, noon, "1:42 AM", "Wed, Oct 14");
  expectClock(-25200, early, "8:00 PM", "Tue, Oct 13");                     // 15. Backward: LA is yesterday.
  expectClock(-14400, early, "11:00 PM", "Tue, Oct 13");
  expectClock(19800, 1768501200u, "11:50 PM", "Thu, Jan 15");               // +05:30 just before midnight,
  expectClock(20700, 1768501200u, "12:05 AM", "Fri, Jan 16");               // +05:45 just after.
  // Year boundaries.
  CivilTime t = localCivil(1798759800u, 20700);                             // 2026-12-31 23:30 UTC.
  assert(t.year == 2027 && t.month == 1 && t.day == 1 && t.hour == 5 && t.minute == 15);
  t = localCivil(1798768800u, -18000);                                      // 2027-01-01 02:00 UTC.
  assert(t.year == 2026 && t.month == 12 && t.day == 31 && t.hour == 21);
  // Never a guess: no offset yet, or no NTP time -> "Syncing...".
  char time[16], date[24];
  assert(!formatWorldClock(place("New", "Asia/Tokyo", 1, 1), noon, time, sizeof(time), date, sizeof(date)));
  assert(time[0] == '\0' && date[0] == '\0');
  assert(!formatWorldClock(withOffset(place("X", "Asia/Tokyo", 1, 1), 32400), 0, time, sizeof(time), date, sizeof(date)));
  assert(!formatWorldClock(withOffset(place("X", "Asia/Tokyo", 1, 1), 32400), 12345, time, sizeof(time), date,
                           sizeof(date)));
  // Screen labels: capitals, ASCII only.
  char label[24];
  clockDisplayLabel("New York", label, sizeof(label));
  assert(strcmp(label, "NEW YORK") == 0);
  clockDisplayLabel("S\xC3\xA3o Paulo", label, sizeof(label));
  assert(strcmp(label, "S?O PAULO") == 0);
  clockDisplayLabel("Llanfairpwllgwyngyllgogerychwyrndrobwll", label, sizeof(label));
  assert(strlen(label) == sizeof(label) - 1);
}

// --- 16-19: precedence, offsets, schedule -------------------------------------------------------

static void testPrecedenceAndRefresh() {
  typedef WorldClockSource S;
  assert(worldClockSourceFor(1, false, 0) == S::Local);                     // 16. Local wins,
  assert(worldClockSourceFor(2, true, 5) == S::Local);                      //     even with backend clocks.
  assert(worldClockSourceFor(0, true, 3) == S::Backend);                    // 17. Backend fallback.
  assert(worldClockSourceFor(0, true, 0) == S::None);                       //     "No clocks added".
  assert(worldClockSourceFor(0, false, 3) == S::None);                      //     Stale backend data: no backend.
  assert(worldClockSourceFor(0, false, 0) == S::None);

  // 18. Offset updates: matched by identity, validated, date recorded.
  LocalClockList list;
  addLocalClock(list, place("New York", "America/New_York", 40.71427, -74.00597));
  addLocalClock(list, place("Kathmandu", "Asia/Kathmandu", 27.70169, 85.3206));
  bool changed = false;
  assert(setLocalClockOffset(list, place("x", "America/New_York", 40.71, -74.0), -14400, 1791895320u, changed));
  assert(changed && list.items[0].offsetKnown && list.items[0].utcOffsetSeconds == -14400 &&
         list.items[0].offsetUpdatedAt == 1791895320u);
  assert(setLocalClockOffset(list, list.items[0], -14400, 1791900000u, changed) && !changed &&
         list.items[0].offsetUpdatedAt == 1791900000u);
  assert(setLocalClockOffset(list, list.items[0], -18000, 1791910000u, changed) && changed && // DST ends.
         list.items[0].utcOffsetSeconds == -18000);
  assert(setLocalClockOffset(list, list.items[1], 20700, 1791910000u, changed) && list.items[1].utcOffsetSeconds == 20700);
  // 19. A failed or bad refresh keeps the last offset (nothing is applied).
  assert(!setLocalClockOffset(list, list.items[0], 3601, 1791920000u, changed) && !changed);
  assert(!setLocalClockOffset(list, list.items[0], 15 * 3600, 1791920000u, changed));
  assert(list.items[0].utcOffsetSeconds == -18000 && list.items[0].offsetUpdatedAt == 1791910000u);
  // A clock removed while its refresh ran is not recreated.
  assert(!setLocalClockOffset(list, place("Gone", "Europe/Oslo", 59.9, 10.7), 3600, 1791920000u, changed));
  assert(list.count == 2);
  // NVS: written for a new or changed offset, otherwise at most daily.
  assert(clockOffsetNeedsSave(true, 1791910000u, 1791910001u));
  assert(!clockOffsetNeedsSave(false, 1791910000u, 1791910000u + 6 * 3600));
  assert(clockOffsetNeedsSave(false, 1791910000u, 1791910000u + 24 * 3600));
  assert(clockOffsetNeedsSave(false, 0, 1791910000u));

  // Schedule: every clock once after boot, then every 6 hours.
  assert(clockOffsetDue(false, 0, 1000));
  assert(!clockOffsetDue(true, 1000, 1000 + CLOCK_OFFSET_REFRESH_MS - 1));
  assert(clockOffsetDue(true, 1000, 1000 + CLOCK_OFFSET_REFRESH_MS));
  assert(CLOCK_OFFSET_REFRESH_MS == 6UL * 3600UL * 1000UL);
  ClockOffsetSchedule s;
  assert(clockBatchAllowed(s, 0));
  clockBatchFinished(s, false, 5000);                                       // Failed: retry in 2, 5, 10... min.
  assert(!clockBatchAllowed(s, 5000 + 2 * 60000 - 1) && clockBatchAllowed(s, 5000 + 2 * 60000));
  clockBatchFinished(s, false, 200000);
  assert(s.waitMs == 5 * 60000UL);
  for (int i = 0; i < 10; i++) clockBatchFinished(s, false, 300000);
  assert(s.waitMs == 30 * 60000UL);                                         // Never more often than every 30 min.
  clockBatchFinished(s, true, 400000);
  assert(clockBatchAllowed(s, 400001) && s.failures == 0);                   // Then each clock's due time decides.
  clockBatchFinished(s, false, 500000);
  clockBatchReset(s);                                                        // A new clock: try now.
  assert(clockBatchAllowed(s, 500001));
}

// --- Offset request and common cities -----------------------------------------------------------

static void testRequestAndCities() {
  char url[OPEN_METEO_URL_MAX];
  assert(clockOffsetUrl(url, sizeof(url), place("Kathmandu", "Asia/Kathmandu", 27.70169, 85.3206)));
  assert(strcmp(url, "https://api.open-meteo.com/v1/forecast?latitude=27.7017&longitude=85.3206"
                     "&timezone=Asia%2FKathmandu") == 0);
  // Metadata only: no weather variables, no key.
  for (const char* text : {"current", "hourly", "daily", "apikey", "forecast_days"}) assert(!strstr(url, text));
  assert(clockOffsetUrl(url, sizeof(url), place("Somewhere", "", -33.9, 18.4)) && strstr(url, "&timezone=auto"));
  assert(!clockOffsetUrl(url, sizeof(url), place("Bad", "", 95, 0)) && url[0] == '\0');
  assert(!clockOffsetUrl(url, 40, place("Kathmandu", "Asia/Kathmandu", 27.7, 85.3)) && url[0] == '\0');

  // Common cities: a few, valid, distinct, with zones and no offsets.
  assert(COMMON_CITY_COUNT >= 6 && COMMON_CITY_COUNT <= 12);
  LocalClockList all;
  for (int i = 0; i < COMMON_CITY_COUNT; i++) {
    WorldClockLocation c = commonCityClock(i);
    assert(validClock(c) && c.timezone[0] && strchr(c.timezone, '/') && !c.offsetKnown && c.utcOffsetSeconds == 0);
    assert(strlen(c.name) * 12 <= CITY_BUTTON_W - 8);                       // Fits its button at text size 2.
    for (int j = 0; j < i; j++) assert(!sameClockPlace(c, commonCityClock(j)));
    if (all.count < LOCAL_CLOCKS_MAX) assert(addLocalClock(all, c) == ClockAddResult::Added);
  }
  assert(!validClock(commonCityClock(COMMON_CITY_COUNT)) && !validClock(commonCityClock(-1)));
  // The grid: each button maps to its city; gaps, other pages and the bar are inert.
  int pages = pageCountFor(COMMON_CITY_COUNT, CITIES_PER_PAGE);
  for (int index = 0; index < COMMON_CITY_COUNT; index++) {
    int slot = index % CITIES_PER_PAGE;
    assert(commonCityAt(cityGridX(slot) + 10, cityGridY(slot) + 10, index / CITIES_PER_PAGE) == index);
  }
  assert(commonCityAt(cityGridX(0) + CITY_BUTTON_W + 2, cityGridY(0) + 10, 0) == -1);
  assert(commonCityAt(cityGridX(0) + 10, NAV_Y + 5, 0) == -1 && commonCityAt(cityGridX(0) + 10, 50, pages) == -1);
  assert(cityGridY(CITIES_PER_PAGE - 1) + CITY_BUTTON_H <= NAV_Y);

  // Screen hit areas stay above the bottom bar and do not overlap the weather block.
  assert(CLOCKS_EDIT_TOP > TIME_WEATHER_BOTTOM && clocksEditHit(160, 130) && !clocksEditHit(160, 110));
  assert(clocksAddHit(CLOCKS_ADD_X + 5, CLOCKS_ADD_Y + 5) && CLOCKS_ADD_Y + CLOCKS_ADD_H <= NAV_Y);
  assert(LOCAL_CLOCK_ROW_Y0 + (LOCAL_CLOCKS_PER_PAGE - 1) * LOCAL_CLOCK_ROW_PITCH + 28 <= NAV_Y + 3);
  assert(clockListBarAt(300, 220, 2) == ClockListHit::Add && clockListBarAt(160, 220, 2) == ClockListHit::Back);
  assert(clockListBarAt(300, 220, 0) == ClockListHit::Back);                // No clocks: the bar is BACK only,
  assert(clockListBarAt(CLOCK_EMPTY_ADD_X + 5, CLOCK_EMPTY_ADD_Y + 5, 0) == ClockListHit::Add); // ADD is the button.
  assert(clockListBarAt(CLOCK_EMPTY_ADD_X + 5, CLOCK_EMPTY_ADD_Y + 5, 1) == ClockListHit::None);
  assert(addClockAt(ADD_CLOCK_X + 5, ADD_CLOCK_COMMON_Y + 5, 3) == AddClockHit::CommonCities);
  assert(addClockAt(ADD_CLOCK_X + 5, ADD_CLOCK_PHONE_Y + 5, 0) == AddClockHit::SearchOnPhone);
  assert(addClockAt(ADD_CLOCK_X + 5, ADD_CLOCK_COMMON_Y + 5, 4) == AddClockHit::None);   // 25. Full: no add.
  assert(addClockAt(ADD_CLOCK_X + 5, ADD_CLOCK_PHONE_Y + 5, 4) == AddClockHit::None);
  assert(clockRemoveHit(CLOCK_REMOVE_X + 5, CLOCK_REMOVE_Y + 5) && !clockRemoveHit(5, 5));
}

// --- 22-25: the phone page ------------------------------------------------------------------------

static void testPhonePage() {
  WorldClockPageView v;
  v.wifiConnected = true;
  v.count = 1;
  std::string page;
  renderPortalRoot(page, PortalMode::WorldClock, SetupFormView(), LocationPageView(), v);       // 22.
  assert(has(page, "<h3>Add World Clock</h3>") && has(page, "Town or city") && has(page, "action=/clock/search") &&
         has(page, "<button>Search</button>") && has(page, "1 of 4 clocks added."));
  for (const char* word : {"latitude", "longitude", "Open-Meteo", "API", "backend", "http://", "required",
                           "/location/", "name=pass"}) {
    assert(!has(page, word));
  }
  Location results[3];
  snprintf(results[0].name, sizeof(results[0].name), "London");
  snprintf(results[0].admin1, sizeof(results[0].admin1), "England");
  snprintf(results[0].country, sizeof(results[0].country), "United Kingdom");
  snprintf(results[1].name, sizeof(results[1].name), "London");
  snprintf(results[1].admin1, sizeof(results[1].admin1), "Ontario");
  snprintf(results[1].country, sizeof(results[1].country), "Canada");
  snprintf(results[2].name, sizeof(results[2].name), "<b>");
  bool added[3] = {true, false, false};
  v.state = LocationSearchState::Done;
  v.query = "London";
  v.generation = 7;
  v.results = results;
  v.alreadyAdded = added;
  v.resultCount = 3;
  page.clear();
  renderWorldClockPage(page, v);
  assert(has(page, "<b>London, England, United Kingdom</b></p><p>Already added</p>"));      // 24.
  assert(has(page, "<b>London, Ontario, Canada</b></p><form method=post action=/clock/add>"
                   "<input type=hidden name=g value=7><input type=hidden name=i value=1><button>Add clock</button>"));
  assert(!has(page, "name=i value=0>") && has(page, "&lt;b&gt;"));
  v.count = LOCAL_CLOCKS_MAX;                                                                    // 25. Full.
  page.clear();
  renderWorldClockPage(page, v);
  assert(has(page, "4 of 4 clocks added.") && has(page, "Remove a clock on HomeDeck to add another."));
  assert(!has(page, "action=/clock/search") && !has(page, "Add clock"));
  WorldClockPageView sent;
  sent.added = "Tokyo, Japan";
  sent.count = 2;
  sent.error = "Already added";
  page.clear();
  renderWorldClockPage(page, sent);
  assert(has(page, "Added: Tokyo, Japan.") && has(page, "Already added") && has(page, "not connected to Wi-Fi"));
  // 23. Every captive probe in WorldClock mode lands on this page; its forms
  // only exist in this mode.
  for (const char* path : {"/hotspot-detect.html", "/generate_204", "/connecttest.txt", "/nope", "/clock/add"}) {
    PortalAction a = portalRoute(PortalMode::WorldClock, false, path);
    assert(a == PortalAction::RedirectRoot && portalRoute(PortalMode::WorldClock, false, "/") == PortalAction::RootPage);
  }
  assert(portalRoute(PortalMode::WorldClock, true, "/clock/add") == PortalAction::ClockAdd);
  assert(portalRoute(PortalMode::Location, true, "/clock/add") == PortalAction::RedirectRoot);
  assert(portalRoute(PortalMode::WorldClock, true, "/location/use") == PortalAction::RedirectRoot);
  assert(portalRoute(PortalMode::WorldClock, true, "/save") == PortalAction::RedirectRoot);
}

// --- Source checks ---------------------------------------------------------------------------------

static std::string readSource(const char* name) {
  for (const char* prefix : {"firmware/", "", "../"}) {
    std::ifstream file(std::string(prefix) + name);
    if (file) {
      std::stringstream text;
      text << file.rdbuf();
      return text.str();
    }
  }
  fprintf(stderr, "cannot find %s (run from the repository root)\n", name);
  assert(false);
  return "";
}

static std::string functionBody(const std::string& source, const char* signature) {
  size_t start = source.find(signature);
  if (start == std::string::npos) fprintf(stderr, "missing %s\n", signature);
  assert(start != std::string::npos);
  size_t end = source.find("\n}\n", start);
  assert(end != std::string::npos);
  return source.substr(start, end - start);
}

static size_t occurrences(const std::string& text, const char* needle) {
  size_t count = 0;
  for (size_t at = text.find(needle); at != std::string::npos; at = text.find(needle, at + 1)) count++;
  return count;
}

static void testSources() {
  std::string weather = readSource("src/DirectWeather.cpp");
  std::string store = readSource("src/LocalClockStore.cpp");
  std::string screen = readSource("src/WorldClocksScreen.cpp");
  std::string time = readSource("src/TimeWeatherScreen.cpp");
  std::string cities = readSource("include/CommonCities.h");
  std::string portal = readSource("src/WifiProvisioning.cpp");

  // 20. A world-clock pick saves to the clock list only; 21. a location pick never touches clocks.
  std::string addFromSearch = functionBody(weather, "ClockAddResult addClockFromSearch(");
  assert(has(addFromSearch, "return addLocalClockPlace(clockFromLocation(chosen));"));
  assert(!has(addFromSearch, "saveLocation") && !has(addFromSearch, "clearLocation"));
  std::string choose = functionBody(weather, "bool chooseLocationResult(");
  assert(has(choose, "saveLocation(chosen)") && !has(choose, "Clock"));
  assert(occurrences(weather, "saveLocation(") == 1 && occurrences(weather, "addLocalClockPlace(") == 1);
  assert(!has(functionBody(portal, "static void handleLocationUse() {"), "Clock"));
  // Both use the one search (request builder, parser, worker).
  assert(has(functionBody(weather, "bool requestClockSearch(const char* text) {"), "return startSearch(text,"));
  assert(occurrences(weather, "geocodingUrl(") == 1 && occurrences(weather, "parseGeocoding(") == 1 &&
         occurrences(weather, "xTaskCreate(") == 1);

  // No backend needed: the refresh waits for Wi-Fi and NTP only; no backend URL anywhere.
  std::string service = functionBody(weather, "static void serviceWorldClocks() {");
  assert(has(service, "if (clockTargetCount == 0 || WiFi.status() != WL_CONNECTED || !hasUtcTime()) return;"));
  assert(!has(service, "backendConfigured()) return") && !has(service, "serverOnline"));
  assert(has(weather, "serviceWorldClocks(); // Independent of the weather source"));
  for (const std::string* source : {&store, &screen}) {
    for (const char* text : {"backendUrl(", "statusUrl(", "apiUrl(", "saveBackend", "clearBackendUrl", "serverOnline"}) {
      assert(!has(*source, text));
    }
  }
  // Sequential, one request per clock on the shared worker; verified TLS only.
  std::string run = functionBody(weather, "static void runClocks() {");
  assert(has(run, "httpsGet(\"WORLD CLOCK\", url, body, length, status)") && has(run, "parseUtcOffset(") &&
         has(run, "clockOffsetUrl(url, sizeof(url), place)"));
  assert(!has(weather, ".setInsecure") && occurrences(weather, "client.setCACert(OPEN_METEO_ROOT_CAS);") == 1);
  // A failed refresh keeps the last offset: only successes are applied.
  assert(has(service, "if (job.ok && applyLocalClockOffset(targetPlace(t), job.offset)) {"));

  // No offset table: common cities carry zones and coordinates only.
  size_t table = cities.find("constexpr CommonCity COMMON_CITIES[]");
  std::string entries = cities.substr(table, cities.find("};", table) - table);
  size_t fields = cities.find("struct CommonCity {");
  assert(fields != std::string::npos && !has(cities.substr(fields, cities.find("};", fields) - fields), "ffset"));
  for (const char* text : {"offset", "Offset", "3600", "UTC+", "UTC-", "dst", "DST"}) {
    if (has(entries, text)) {
      fprintf(stderr, "CommonCities.h contains %s\n", text);
      assert(false);
    }
  }
  // Every displayed world-clock time is NTP UTC + offset (utcNow, formatWorldClock).
  assert(has(functionBody(time, "static void drawLocalClocks() {"), "uint32_t utc = utcNow();"));
  assert(has(functionBody(time, "static void drawLocalClocks() {"), "formatWorldClock(c, utc,"));
  for (const std::string* source : {&time, &screen}) {
    for (const char* text : {"time(nullptr)", "getLocalTime", "gmtime", "localtime", "setenv(\"TZ\"", "configTime"}) {
      assert(!has(*source, text));
    }
  }
  // Bounded: 4 clocks, fixed buffers.
  assert(LOCAL_CLOCKS_MAX == 4 && CLOCK_BLOB_MAX == 4 + 4 * CLOCK_ENTRY_BYTES + 4);
  // No growing containers; the store's only allocation is one fixed-size
  // scratch for the duration of a change, always freed.
  assert(!has(screen, "malloc(") && !has(screen, "new ") && !has(screen, "std::vector") && !has(store, "std::vector"));
  assert(occurrences(store, "malloc(") == 1 && has(store, "Scratch* s = (Scratch*)malloc(sizeof(Scratch));"));
  assert(occurrences(store, "takeScratch();") == 4 && occurrences(store, "free(s);") == 4);
  // Own namespace; never another one's data.
  assert(has(store, "CLOCKS_NAMESPACE = \"worldclocks\""));
  for (const char* text : {"\"wifi\"", "\"device\"", "\"location\"", "\"settings\"", "saveLocation", "clearLocation",
                           "WifiCredentials", ".clear()"}) {
    assert(!has(store, text));
  }
  // Nothing is added by itself: no defaults at load.
  assert(!has(functionBody(store, "void loadLocalClocks() {"), "addLocalClock"));
  // No personal city or postal code anywhere in this feature.
  for (const char* file : {"include/CommonCities.h", "include/LocalClockLogic.h", "src/LocalClockStore.cpp",
                           "src/WorldClocksScreen.cpp", "include/WorldClockUi.h", "src/TimeWeatherScreen.cpp",
                           "src/DirectWeather.cpp", "include/SetupPage.h"}) {
    std::string source = readSource(file);
    for (const char* text : {"Ashburn", "Virginia", "20147", "20148", "Loudoun"}) {
      if (has(source, text)) fprintf(stderr, "%s contains %s\n", file, text);
      assert(!has(source, text));
    }
  }
  // Backend clocks still work as the fallback (parser, status apply, drawing).
  assert(has(readSource("src/StatusClient.cpp"), "app.time.worldClocks = snapshot.time.worldClocks;"));
  assert(has(readSource("include/StatusParse.h"), "inline void parseWorldClocks("));
  assert(has(time, "case WorldClockSource::Backend:\n      drawBackendClocks(backendClocks);"));
  assert(has(time, "return worldClockSourceFor(localClockCount(), backendConfigured(), app.time.worldClocks.count);"));
  // Settings: SEARCH ON PHONE for a clock starts the WorldClock portal; REMOVE only the clock store.
  std::string settings = readSource("src/SettingsScreen.cpp");
  std::string confirmed = functionBody(settings, "static void runConfirmed(");
  size_t at = confirmed.find("case ConfirmAction::StartClockSetup:");
  assert(has(confirmed.substr(at, confirmed.find("break;", at) - at), "startWorldClockSetup();"));
  at = confirmed.find("case ConfirmAction::RemoveClock:");
  std::string remove = confirmed.substr(at, confirmed.find("break;", at) - at);
  assert(has(remove, "removeSelectedClock();") && !has(remove, "clearLocation") && !has(remove, "restartDevice"));
  assert(has(functionBody(screen, "void removeSelectedClock() {"), "removeLocalClockAt(selected)"));
  // Logs.
  for (const char* log : {"[WORLD CLOCK] Added: %s\\n", "[WORLD CLOCK] Removed: %s\\n"}) assert(has(store, log));
  for (const char* log : {"[WORLD CLOCK] Offset %s: %s\\n", "[WORLD CLOCK] Offset refresh failed: %s (%s)\\n",
                          "[WORLD CLOCK] Source: %s\\n"}) {
    assert(has(weather, log));
  }
}

int main() {
  testList();
  testBlob();
  testTime();
  testPrecedenceAndRefresh();
  testRequestAndCities();
  testPhonePage();
  testSources();
  puts("world clock tests passed");
  return 0;
}
