// /api/status services and world clocks -> firmware (StatusParse.h), with
// real ArduinoJson payloads in the shapes each backend generation sends:
//   new backend (api_version 2): services_list / world_clocks, plus legacy keys;
//   old backend: only services {id: true/false/null} and timezones {...}.
// Then through the real AlertLogic. Needs ArduinoJson from a previous
// `pio run`; run from the repository root.
#include <ArduinoJson.h>
#include <assert.h>
#include <fstream>
#include <sstream>
#include <stdio.h>
#include <string.h>
#include <string>
#include "AlertLogic.h"
#include "Pages.h"
#include "StatusParse.h"

static JsonDocument parsed(const std::string& json) {
  JsonDocument doc;
  assert(deserializeJson(doc, json) == DeserializationError::Ok);
  return doc;
}

static ServiceState services(const std::string& json) {
  ServiceState state;
  JsonDocument doc = parsed(json);
  parseServices(doc.as<JsonVariantConst>(), state);
  return state;
}

static WorldClockState clocks(const std::string& json) {
  WorldClockState state;
  JsonDocument doc = parsed(json);
  parseWorldClocks(doc.as<JsonVariantConst>(), state);
  return state;
}

// The legacy keys a new backend still sends (and an old one sends alone).
static const char* LEGACY_SERVICES_JSON =
  "\"services\":{\"jellyfin\":true,\"navidrome\":true,\"metube\":false,\"bazarr\":null,\"ollama\":true,"
  "\"cloudflare\":true,\"mcp\":null,\"nextcloud\":true,\"immich\":false,\"technical_blog\":true}";
static const char* LEGACY_TIMEZONES_JSON =
  "\"timezones\":{\"local\":{\"time\":\"9:42 AM\",\"date\":\"Sun, Oct 11\",\"timezone\":\"America/New_York\"},"
  "\"india\":{\"time\":\"7:12 PM\",\"timezone\":\"Asia/Kolkata\",\"label\":\"India\"},"
  "\"singapore\":{\"time\":\"9:42 PM\",\"timezone\":\"Asia/Singapore\",\"label\":\"Singapore\"},"
  "\"london\":{\"time\":\"2:42 PM\",\"timezone\":\"Europe/London\",\"label\":\"London\"}}";

static std::string newBackend(const std::string& list, const std::string& worldClocks) {
  return std::string("{\"api_version\":2,") + LEGACY_SERVICES_JSON + ",\"services_list\":" + list + "," +
         LEGACY_TIMEZONES_JSON + ",\"world_clocks\":" + worldClocks + "}";
}

static void testDynamicServices() {
  ServiceState s = services(newBackend(
    "[{\"id\":\"jellyfin\",\"label\":\"Jellyfin\",\"status\":true},"
    "{\"id\":\"immich\",\"label\":\"Immich\",\"status\":false},"
    "{\"id\":\"home_assistant\",\"label\":\"Home Assistant\",\"status\":null}]", "[]"));
  // The list wins over the legacy object (which has 8 bools here).
  assert(s.received && s.count == 3);
  assert(strcmp(s.items[0].id, "jellyfin") == 0 && strcmp(s.items[0].label, "Jellyfin") == 0);
  assert(s.items[0].status == SERVICE_UP && s.items[1].status == SERVICE_DOWN);
  // A service this firmware never heard of renders from its label.
  assert(strcmp(s.items[2].label, "Home Assistant") == 0 && s.items[2].status == SERVICE_UNKNOWN);

  // Zero services configured: an empty list, not the legacy fallback.
  s = services(newBackend("[]", "[]"));
  assert(s.received && s.count == 0);
}

static void testMalformedServices() {
  ServiceState s = services(
    "{\"services_list\":[1,\"x\",null,[],{},{\"label\":\"No id\",\"status\":true},"
    "{\"id\":\"Bad Id\",\"status\":true},{\"id\":\"\",\"status\":true},{\"id\":42,\"status\":true},"
    "{\"id\":\"ok\",\"label\":\"OK\",\"status\":\"up\"},"
    "{\"id\":\"ok\",\"label\":\"Duplicate\",\"status\":true},"
    "{\"id\":\"no_label\",\"status\":false},"
    "{\"id\":\"num_label\",\"label\":7,\"status\":1},"
    "{\"id\":\"long\",\"label\":\"An extremely long service label that will not fit\",\"status\":true},"
    "{\"id\":\"ctrl\",\"label\":\"Tab\\there\",\"status\":true},"
    "{\"id\":\"abcdefghijklmnopqrstuvwxyz\",\"status\":true}]}");
  assert(s.count == 5);
  assert(strcmp(s.items[0].id, "ok") == 0 && s.items[0].status == SERVICE_UNKNOWN); // "up" is not a bool.
  assert(strcmp(s.items[0].label, "OK") == 0);                                     // First of a repeated id.
  assert(strcmp(s.items[1].label, "no_label") == 0 && s.items[1].status == SERVICE_DOWN);
  assert(strcmp(s.items[2].label, "num_label") == 0 && s.items[2].status == SERVICE_UNKNOWN);
  assert(strlen(s.items[3].label) == SERVICE_LABEL_SIZE - 1 &&
         strncmp(s.items[3].label, "An extremely long servi", SERVICE_LABEL_SIZE - 1) == 0);
  assert(strcmp(s.items[4].label, "Tab?here") == 0);
  // A 26-character id cannot be a key: skipped.
  assert(findService(s, "abcdefghijklmnopqrstuvwxyz") < 0);

  // services_list of the wrong type: the legacy object is used instead.
  s = services(std::string("{\"services_list\":{\"jellyfin\":true},") + LEGACY_SERVICES_JSON + "}");
  assert(s.count == 8);
}

static void testTooManyServices() {
  std::string list = "[";
  for (int i = 0; i < 40; i++) {
    char item[80];
    snprintf(item, sizeof(item), "%s{\"id\":\"service%d\",\"label\":\"Service %d\",\"status\":%s}", i ? "," : "", i,
             i, i % 2 ? "true" : "false");
    list += item;
  }
  ServiceState s = services("{\"services_list\":" + list + "]}");
  assert(s.count == MAX_SERVICES);
  assert(strcmp(s.items[MAX_SERVICES - 1].id, "service15") == 0);
}

static void testLegacyServices() {
  // New firmware + old backend: the legacy object, monitored (bool) keys only,
  // in the order older firmware showed them; null (unconfigured) omitted.
  ServiceState s = services(std::string("{") + LEGACY_SERVICES_JSON + "}");
  assert(s.received && s.count == 8);
  const char* order[] = {"jellyfin", "navidrome", "metube", "ollama", "cloudflare", "nextcloud", "immich",
                         "technical_blog"};
  for (int i = 0; i < 8; i++) assert(strcmp(s.items[i].id, order[i]) == 0);
  assert(strcmp(s.items[7].label, "Technical Blog") == 0);
  assert(s.items[2].status == SERVICE_DOWN && s.items[6].status == SERVICE_DOWN);
  assert(findService(s, "bazarr") < 0 && findService(s, "mcp") < 0);

  // Only a few monitored (Phase 2 backend with SERVICES=jellyfin,immich).
  s = services("{\"services\":{\"jellyfin\":true,\"navidrome\":null,\"immich\":false}}");
  assert(s.count == 2 && strcmp(s.items[1].id, "immich") == 0);

  // Nothing at all (missing keys, or a status without services): empty, no crash.
  assert(services("{}").count == 0);
  assert(services("{\"services\":null}").count == 0);
  assert(services("{\"services\":[]}").count == 0);
}

static void testParsedAlerts() {
  // New backend: alerts follow the list and use its labels.
  AlertTable table;
  AlertInput input;
  input.hostAvailable = false;
  ServiceState s = services(newBackend(
    "[{\"id\":\"jellyfin\",\"label\":\"Jellyfin\",\"status\":true},"
    "{\"id\":\"immich\",\"label\":\"Immich Photos\",\"status\":false}]", "[]"));
  input.services = &s;
  alertsApplyStatus(table, input, 1000);
  assert(alertsActiveCount(table) == 1);
  char title[32];
  alertTitle(table, alertServiceSlot(table, "immich"), title, sizeof(title));
  assert(strcmp(title, "Immich Photos") == 0);

  // SERVICES changed on the backend (immich removed): its alert goes away.
  s = services(newBackend("[{\"id\":\"jellyfin\",\"label\":\"Jellyfin\",\"status\":true}]", "[]"));
  assert(alertsApplyStatus(table, input, 2000));
  assert(alertsActiveCount(table) == 0 && alertServiceSlot(table, "immich") < 0);

  // Old backend: the same alert rules from the legacy object.
  AlertTable legacy;
  s = services(std::string("{") + LEGACY_SERVICES_JSON + "}");
  alertsApplyStatus(legacy, input, 1000);
  assert(alertsActiveCount(legacy) == 2); // MeTube and Immich.
  alertTitle(legacy, alertServiceSlot(legacy, "metube"), title, sizeof(title));
  assert(strcmp(title, "MeTube") == 0);

  // No services configured: no service warning.
  AlertTable empty;
  s = services(newBackend("[]", "[]"));
  alertsApplyStatus(empty, input, 1000);
  char summary[24];
  alertSummary(empty, summary, sizeof(summary));
  assert(strcmp(summary, "ALL GOOD") == 0);
}

static void testDynamicClocks() {
  WorldClockState c = clocks(newBackend("[]",
    "[{\"label\":\"New York\",\"timezone\":\"America/New_York\",\"time\":\"9:42 AM\"},"
    "{\"label\":\"Tokyo\",\"timezone\":\"Asia/Tokyo\",\"time\":\"10:42 PM\"}]"));
  // The list wins over the legacy India/Singapore/London entries.
  assert(c.count == 2);
  assert(strcmp(c.items[0].label, "New York") == 0 && strcmp(c.items[0].time, "9:42 AM") == 0);
  assert(strcmp(c.items[1].label, "Tokyo") == 0 && strcmp(c.items[1].time, "10:42 PM") == 0);

  // Zero clocks configured: none (the legacy placeholders are ignored).
  c = clocks(newBackend("[]", "[]"));
  assert(c.count == 0);

  // Malformed entries skipped; bad times shown as "--:--"; long labels cut.
  c = clocks("{\"world_clocks\":[1,null,{},{\"time\":\"1:00 PM\"},{\"label\":\"\",\"time\":\"1:00 PM\"},"
             "{\"label\":5,\"time\":\"1:00 PM\"},{\"label\":\"No time\"},"
             "{\"label\":\"Long time\",\"time\":\"12:00:00 PM UTC\"},"
             "{\"label\":\"A clock label longer than twenty\",\"time\":\"1:00 PM\"}]}");
  assert(c.count == 3);
  assert(strcmp(c.items[0].label, "No time") == 0 && strcmp(c.items[0].time, "--:--") == 0);
  assert(strcmp(c.items[1].time, "--:--") == 0);
  assert(strlen(c.items[2].label) == CLOCK_LABEL_SIZE - 1);

  // Too many: the first MAX_WORLD_CLOCKS.
  std::string list = "[";
  for (int i = 0; i < 12; i++) {
    char item[64];
    snprintf(item, sizeof(item), "%s{\"label\":\"Clock %d\",\"time\":\"%d:00 AM\"}", i ? "," : "", i, i + 1);
    list += item;
  }
  c = clocks("{\"world_clocks\":" + list + "]}");
  assert(c.count == MAX_WORLD_CLOCKS && strcmp(c.items[7].label, "Clock 7") == 0);
}

static void testLegacyClocks() {
  // New firmware + old backend: timezones entries except "local", with the
  // backend's label or the old built-in name.
  WorldClockState c = clocks(std::string("{") + LEGACY_TIMEZONES_JSON + "}");
  assert(c.count == 3);
  assert(strcmp(c.items[0].label, "India") == 0 && strcmp(c.items[0].time, "7:12 PM") == 0);
  assert(strcmp(c.items[2].label, "London") == 0);
  // Pre-label backends: fixed names from the keys.
  c = clocks("{\"timezones\":{\"local\":{\"time\":\"1:00 AM\"},\"singapore\":{\"time\":\"2:00 PM\"}}}");
  assert(c.count == 1 && strcmp(c.items[0].label, "Singapore") == 0);
  // A Phase 1 backend with other clocks: unconfigured legacy names are "--:--"
  // placeholders and are skipped.
  c = clocks("{\"timezones\":{\"local\":{\"time\":\"1:00 AM\"},"
             "\"tokyo\":{\"time\":\"3:00 PM\",\"label\":\"Tokyo\",\"timezone\":\"Asia/Tokyo\"},"
             "\"india\":{\"time\":\"--:--\",\"date\":\"\",\"timezone\":null},"
             "\"singapore\":{\"time\":\"--:--\",\"date\":\"\",\"timezone\":null},"
             "\"london\":{\"time\":\"--:--\",\"date\":\"\",\"timezone\":null}}}");
  assert(c.count == 1 && strcmp(c.items[0].label, "Tokyo") == 0);
  // Nothing: no clocks, no crash.
  assert(clocks("{}").count == 0);
  assert(clocks("{\"timezones\":\"x\"}").count == 0);
}

static void testClockPaging() {
  assert(CLOCKS_ROW_Y0 + CLOCKS_PER_PAGE * CLOCKS_ROW_PITCH <= NAV_Y);
  assert(pageCountFor(0, CLOCKS_PER_PAGE) == 1 && pageCountFor(3, CLOCKS_PER_PAGE) == 1);
  assert(pageCountFor(4, CLOCKS_PER_PAGE) == 2 && pageCountFor(MAX_WORLD_CLOCKS, CLOCKS_PER_PAGE) == 3);
  assert(clampPageIndex(2, 3, CLOCKS_PER_PAGE) == 0);
}

// --- Source checks ------------------------------------------------------------------------

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

static bool contains(const std::string& text, const char* needle) {
  return text.find(needle) != std::string::npos;
}

static void testSourceChecks() {
  std::string client = readSource("src/StatusClient.cpp");
  assert(contains(client, "parseServices(doc.as<JsonVariantConst>(), services);"));
  assert(contains(client, "parseWorldClocks(doc.as<JsonVariantConst>(), time.worldClocks);"));
  // No fixed services or clocks outside the legacy shim (StatusParse.h).
  const char* files[] = {"src/StatusClient.cpp", "src/AlertManager.cpp", "src/ServicesScreen.cpp",
                         "src/TimeWeatherScreen.cpp", "include/AppState.h", "include/AlertLogic.h",
                         "include/AiLogic.h", "include/ServicesLogic.h", "src/HomeScreen.cpp"};
  const char* fixed[] = {"jellyfin", "Jellyfin", "navidrome", "metube", "MeTube", "cloudflare", "technical_blog",
                         "serviceJellyfin", "indiaTime", "londonTime", "Singapore", "\"India\""};
  for (const char* file : files) {
    std::string text = readSource(file);
    for (const char* name : fixed) {
      if (contains(text, name)) {
        fprintf(stderr, "%s still mentions %s\n", file, name);
        assert(false);
      }
    }
  }
  assert(contains(readSource("src/AlertManager.cpp"), "input.services = &services;"));
}

int main() {
  testDynamicServices();
  testMalformedServices();
  testTooManyServices();
  testLegacyServices();
  testParsedAlerts();
  testDynamicClocks();
  testLegacyClocks();
  testClockPaging();
  testSourceChecks();
  puts("status parse tests passed: services, malformed, limits, legacy, alerts, clocks, legacy clocks, paging");
  return 0;
}
