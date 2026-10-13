// Location persistence (LocationNvs.h over a fake Preferences), the location
// page HTML (SetupPage.h), and source-level checks for Phase 8.6: local time
// and direct weather never depend on the backend, no API key, no personal
// location, and the location namespace is independent of Wi-Fi and backend.
#include "LocationNvs.h"
#include "SetupPage.h"
#include <assert.h>
#include <fstream>
#include <map>
#include <sstream>
#include <stdio.h>
#include <string.h>
#include <string>

// The subset of Arduino Preferences that LocationNvs uses, in memory.
struct FakePrefs {
  std::map<std::string, std::string> strings;
  std::map<std::string, double> doubles;
  std::map<std::string, int32_t> ints;
  std::map<std::string, uint8_t> chars;
  bool failWrites = false;
  int removed = 0;          // remove() of a key that existed.
  int removedMissing = 0;   // remove() of a missing key (NVS logs NOT_FOUND).

  bool isKey(const char* k) {
    return strings.count(k) || doubles.count(k) || ints.count(k) || chars.count(k);
  }
  bool remove(const char* k) {
    bool had = isKey(k);
    strings.erase(k), doubles.erase(k), ints.erase(k), chars.erase(k);
    (had ? removed : removedMissing)++;
    return had;
  }
  size_t getString(const char* k, char* out, size_t size) {
    auto it = strings.find(k);
    if (it == strings.end() || it->second.size() + 1 > size) return 0; // Like NVS: too long -> nothing.
    memcpy(out, it->second.c_str(), it->second.size() + 1);
    return it->second.size();
  }
  double getDouble(const char* k, double fallback) { return doubles.count(k) ? doubles[k] : fallback; }
  int32_t getInt(const char* k, int32_t fallback) { return ints.count(k) ? ints[k] : fallback; }
  uint8_t getUChar(const char* k, uint8_t fallback) { return chars.count(k) ? chars[k] : fallback; }
  size_t putString(const char* k, const char* v) {
    if (failWrites) return 0;
    strings[k] = v;
    return strlen(v);
  }
  size_t putDouble(const char* k, double v) { return failWrites ? 0 : (doubles[k] = v, sizeof(double)); }
  size_t putInt(const char* k, int32_t v) { return failWrites ? 0 : (ints[k] = v, sizeof(int32_t)); }
  size_t putUChar(const char* k, uint8_t v) { return failWrites ? 0 : (chars[k] = v, 1); }
};

static Location place(const char* name, double lat, double lon) {
  Location l;
  snprintf(l.name, sizeof(l.name), "%s", name);
  snprintf(l.admin1, sizeof(l.admin1), "Region");
  snprintf(l.country, sizeof(l.country), "Country");
  snprintf(l.timezone, sizeof(l.timezone), "Europe/London");
  l.latitude = lat;
  l.longitude = lon;
  return l;
}

static void testPersistence() {
  FakePrefs nvs;
  Location out;
  assert(!readLocation(nvs, out) && out.name[0] == '\0');          // Missing: a valid "none".
  Location l = place("London", 51.50853, -0.12574);
  l.offsetKnown = true;                                             // Never trusted from a pick.
  l.utcOffset = 3600;
  assert(writeLocation(nvs, l));
  assert(readLocation(nvs, out) && strcmp(out.name, "London") == 0 && out.latitude == 51.50853 &&
         out.longitude == -0.12574 && strcmp(out.timezone, "Europe/London") == 0);
  assert(!out.offsetKnown);                                         // Supplied by the first forecast.
  assert(writeLocationOffset(nvs, 3600) && readLocation(nvs, out) && out.offsetKnown && out.utcOffset == 3600);
  assert(!writeLocationOffset(nvs, 3601) && !writeLocationOffset(nvs, 20 * 3600)); // Validated.
  assert(readLocation(nvs, out) && out.utcOffset == 3600);
  // A new place drops the old offset.
  assert(writeLocation(nvs, place("Kathmandu", 27.7, 85.3)) && readLocation(nvs, out) && !out.offsetKnown);
  // Invalid input is never written.
  FakePrefs fresh;
  assert(!writeLocation(fresh, place("Nowhere", 91, 0)) && !fresh.isKey("v"));
  assert(!writeLocation(fresh, place("", 1, 1)));
  // A save cut short (power loss before "v"): reads as no location.
  FakePrefs cut;
  assert(writeLocation(cut, place("Oslo", 59.9, 10.7)));
  cut.remove("v");
  assert(!readLocation(cut, out));
  // A failing write reports failure (and "v" is not set).
  FakePrefs broken;
  broken.failWrites = true;
  assert(!writeLocation(broken, place("Oslo", 59.9, 10.7)) && !broken.isKey("v"));
  // Corrupted values: rejected, never half-used.
  FakePrefs bad;
  assert(writeLocation(bad, place("Lima", -12.0, -77.0)));
  bad.doubles["latitude"] = 123;
  assert(!readLocation(bad, out));
  bad.doubles["latitude"] = -12.0;
  bad.strings["name"] = std::string(80, 'x');                      // Too long to read.
  assert(!readLocation(bad, out));
  bad.strings["name"] = "Lima";
  bad.ints["utc_offset"] = 999;                                     // Not a real offset.
  assert(!readLocation(bad, out));
  bad.chars["v"] = 2;                                               // Unknown version.
  bad.ints.erase("utc_offset");
  assert(!readLocation(bad, out));
}

// The first save on a fresh device removes nothing (no NOT_FOUND errors);
// replacing a place still removes its old version marker and offset.
static void testNoRemoveOfMissingKeys() {
  FakePrefs fresh;
  assert(writeLocation(fresh, place("London", 51.5, -0.1)));
  assert(fresh.removedMissing == 0 && fresh.removed == 0);
  Location out;
  assert(readLocation(fresh, out) && strcmp(out.name, "London") == 0);
  // Saved again before any forecast: "v" exists, "utc_offset" does not.
  assert(writeLocation(fresh, place("Paris", 48.85, 2.35)));
  assert(fresh.removedMissing == 0 && fresh.removed == 1);
  // With a known offset: both existing keys are removed, the offset is gone.
  assert(writeLocationOffset(fresh, 3600) && fresh.isKey("utc_offset"));
  assert(writeLocation(fresh, place("Oslo", 59.9, 10.7)));
  assert(fresh.removedMissing == 0 && fresh.removed == 3);
  assert(readLocation(fresh, out) && strcmp(out.name, "Oslo") == 0 && !out.offsetKnown && !fresh.isKey("utc_offset"));
}

// --- Location page -------------------------------------------------------------------------

static bool has(const std::string& page, const char* text) {
  return page.find(text) != std::string::npos;
}

static void testLocationPage() {
  LocationPageView v;
  v.wifiConnected = true;
  std::string page;
  renderLocationPage(page, v);
  assert(has(page, "action=/location/search") && has(page, "<input name=q ") && has(page, "<button>Search</button>"));
  assert(has(page, "Set your location"));
  // No technical words for normal users.
  for (const char* word : {"latitude", "longitude", "timezone", "Open-Meteo", "API", "backend", "http://", "required"}) {
    assert(!has(page, word));
  }

  Location results[2] = {place("London", 51.5, -0.1), place("London", 42.98, -81.23)};
  snprintf(results[1].admin1, sizeof(results[1].admin1), "Ontario");
  snprintf(results[1].country, sizeof(results[1].country), "Canada");
  v.state = LocationSearchState::Done;
  v.query = "London";
  v.generation = 4294967295u;
  v.results = results;
  v.count = 2;
  page.clear();
  renderLocationPage(page, v);
  assert(has(page, "<b>London, Region, Country</b>") && has(page, "<b>London, Ontario, Canada</b>"));
  assert(has(page, "<input type=hidden name=g value=4294967295><input type=hidden name=i value=1>"));
  assert(has(page, "Use this location"));
  assert(!has(page, "51.5") && !has(page, "Europe/London")); // Coordinates and zone stay hidden.

  v.count = 0;
  page.clear();
  renderLocationPage(page, v);
  assert(has(page, "No places found for &quot;London&quot;"));

  v.state = LocationSearchState::Searching;
  v.query = "<b>";
  page.clear();
  renderLocationPage(page, v);
  assert(has(page, "http-equiv=refresh") && has(page, "&lt;b&gt;") && !has(page, "for &quot;<b>"));

  v.state = LocationSearchState::Failed;
  for (LocationSearchError e : {LocationSearchError::NoWifi, LocationSearchError::NoTime, LocationSearchError::Network,
                                LocationSearchError::Rejected}) {
    v.searchError = e;
    page.clear();
    renderLocationPage(page, v);
    assert(has(page, locationSearchErrorText(e)) && strlen(locationSearchErrorText(e)) > 0);
  }
  LocationPageView offline;
  offline.wifiConnected = false;
  offline.saved = "Oslo, Norway";
  page.clear();
  renderLocationPage(page, offline);
  assert(has(page, "not connected to Wi-Fi") && has(page, "Location saved: Oslo, Norway."));
}

// --- Source-level checks ----------------------------------------------------------------------

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

static void testClockAndCalendarSources() {
  // The backend's clock strings are applied only when the device has no local time.
  std::string status = readSource("src/StatusClient.cpp");
  std::string apply = functionBody(status, "static void applyStatusSnapshot(");
  size_t gate = apply.find("if (clockSource() == ClockSource::Local) {");
  size_t backendTime = apply.find("app.time = snapshot.time;");
  assert(gate != std::string::npos && backendTime != std::string::npos && gate < backendTime);
  assert(apply.find("app.time.worldClocks = snapshot.time.worldClocks;") < backendTime);
  // Backend weather and solar schedule only while the backend is the weather source.
  size_t weatherGate = apply.find("if (weatherSource() == WeatherSource::Backend) {");
  assert(weatherGate != std::string::npos && weatherGate < apply.find("backlightSetSchedule(snapshot.solar"));
  assert(weatherGate < apply.find("app.weather = snapshot.weather;"));
  // The local clock never reads backend state; the calendar follows it.
  std::string local = readSource("src/LocalTime.cpp");
  assert(local.find("snapshot") == std::string::npos && local.find("backendUrl") == std::string::npos);
  assert(local.find("configTime(0, 0, \"pool.ntp.org\", \"time.nist.gov\")") != std::string::npos);
  assert(local.find("syncCalendarIfUnset(t.year, t.month);") != std::string::npos);
  std::string main = readSource("src/main.cpp");
  assert(main.find("beginLocalTime();") != std::string::npos);
  assert(main.find("if (updateLocalClock()) refreshClockViews();") != std::string::npos);
  assert(main.find("setCalendarMonth(app.time.currentYear") == std::string::npos); // No Jan 2026 default at boot.
}

static void testDirectWeatherIsIndependent() {
  std::string weather = readSource("src/DirectWeather.cpp");
  for (const char* text : {"backendUrl(", "apiUrl(", "statusUrl(", "apikey", ".setInsecure", "http://"}) {
    if (weather.find(text) != std::string::npos) fprintf(stderr, "DirectWeather.cpp contains %s\n", text);
    assert(weather.find(text) == std::string::npos);
  }
  assert(weather.find("client.setCACert(OPEN_METEO_ROOT_CAS);") != std::string::npos);
  // Every request URL is built by LocationLogic.h (HTTPS, no key).
  assert(weather.find("forecastUrl(url, sizeof(url)") != std::string::npos);
  assert(weather.find("geocodingUrl(url, sizeof(url)") != std::string::npos);
  std::string logic = readSource("include/LocationLogic.h");
  assert(logic.find("apikey") == std::string::npos && logic.find("customer-") == std::string::npos);
  std::string certs = readSource("include/OpenMeteoCerts.h");
  assert(certs.find("PRIVATE") == std::string::npos);
  size_t count = 0;
  for (size_t at = certs.find("BEGIN CERTIFICATE"); at != std::string::npos; at = certs.find("BEGIN CERTIFICATE", at + 1)) {
    count++;
  }
  assert(count == 4);
}

static void testNoPersonalLocation() {
  // No compiled-in place, coordinates or timezone: a location exists only once picked.
  for (const char* file : {"include/LocationLogic.h", "include/LocationStore.h", "src/LocationStore.cpp",
                           "include/LocationNvs.h", "src/DirectWeather.cpp", "src/LocalTime.cpp",
                           "include/TimeLogic.h", "include/SetupPage.h", "src/WifiSettingsScreen.cpp",
                           "include/DevDefaults.h", "platformio.ini", "include/LocationUiLogic.h",
                           "src/PostalCodeScreen.cpp"}) {
    std::string source = readSource(file);
    for (const char* text : {"Ashburn", "America/New_York", "192.168.", "LATITUDE=", "WEATHER_LAT", "20148"}) {
      if (source.find(text) != std::string::npos) fprintf(stderr, "%s contains %s\n", file, text);
      assert(source.find(text) == std::string::npos);
    }
  }
}

static void testLocationNamespaceIndependent() {
  std::string store = readSource("src/LocationStore.cpp");
  assert(store.find("LOCATION_NAMESPACE = \"location\"") != std::string::npos);
  // Keys are removed only by writeLocation (guarded by isKey); REMOVE clears the namespace.
  assert(store.find(".remove(") == std::string::npos);
  std::string nvs = readSource("include/LocationNvs.h");
  assert(nvs.find("if (prefs.isKey(\"v\")) prefs.remove(\"v\");") != std::string::npos);
  assert(nvs.find("if (prefs.isKey(\"utc_offset\")) prefs.remove(\"utc_offset\");") != std::string::npos);
  assert(occurrences(nvs, "prefs.remove(") == 2);
  for (const char* other : {"\"wifi\"", "\"device\"", "\"settings\"", "BackendUrl", "WifiCredentials", "saveWifi"}) {
    assert(store.find(other) == std::string::npos);
  }
  // Removing the location clears only its own namespace, without a restart.
  std::string settings = readSource("src/SettingsScreen.cpp");
  std::string run = functionBody(settings, "static void runConfirmed(");
  size_t at = run.find("case ConfirmAction::RemoveLocation:");
  assert(at != std::string::npos);
  std::string removeCase = run.substr(at, run.find("break;", at) - at);
  assert(removeCase.find("clearLocation()") != std::string::npos &&
         removeCase.find("restartDevice") == std::string::npos && removeCase.find("clearBackendUrl") == std::string::npos);
  // The backend and Wi-Fi stores never mention the location.
  for (const char* file : {"src/DeviceConfig.cpp", "src/SettingsStore.cpp"}) {
    std::string source = readSource(file);
    assert(source.find("\"location\"") == std::string::npos && source.find("Location") == std::string::npos);
  }
}

int main() {
  testPersistence();
  testNoRemoveOfMissingKeys();
  testLocationPage();
  testClockAndCalendarSources();
  testDirectWeatherIsIndependent();
  testNoPersonalLocation();
  testLocationNamespaceIndependent();
  puts("location tests passed");
  return 0;
}
