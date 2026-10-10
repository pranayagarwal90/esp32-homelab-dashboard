// Backend service semantics -> firmware: true = online, false = offline,
// null or missing = unknown (grey, never an alert). Real ArduinoJson payloads
// go through the same expressions StatusClient.cpp and AlertManager.cpp use
// (checked against their source below), then the real ServicesLogic and
// AlertLogic.
#include <ArduinoJson.h>
#include <assert.h>
#include <fstream>
#include <sstream>
#include <stdio.h>
#include <string>
#include "AlertLogic.h"
#include "ServicesLogic.h"

struct Decoded {
  uint8_t reported = 0;      // ServiceId bits (SERVICES page).
  uint8_t online = 0;
  uint8_t alertReported = 0; // AlertService bits.
  uint8_t alertOnline = 0;
};

static const struct { const char* key; AlertService service; } MONITORED[] = {
  {"jellyfin", ALERT_JELLYFIN}, {"navidrome", ALERT_NAVIDROME}, {"metube", ALERT_METUBE},
  {"ollama", ALERT_OLLAMA}, {"cloudflare", ALERT_CLOUDFLARE},
};

// Mirrors StatusClient.cpp (services block) and AlertManager.cpp (online mask).
static Decoded decode(const char* json) {
  JsonDocument doc;
  assert(deserializeJson(doc, json) == DeserializationError::Ok);
  Decoded out;
  for (int id = 0; id < SVC_COUNT; id++) {
    JsonVariantConst value = doc["services"][SERVICE_LIST[id].key];
    if (!value.is<bool>()) continue;
    out.reported |= 1u << id;
    if (value.as<bool>()) out.online |= 1u << id;
  }
  for (const auto& monitored : MONITORED) {
    if (doc["services"][monitored.key].is<bool>()) out.alertReported |= 1u << monitored.service;
    bool online = doc["services"][monitored.key] | false; // services.serviceX
    if (online) out.alertOnline |= 1u << monitored.service;
  }
  return out;
}

static AlertTable apply(const Decoded& decoded, AlertTable table = AlertTable()) {
  AlertInput input;
  input.hostAvailable = false; // Services only.
  input.servicesReported = decoded.alertReported;
  input.servicesOnline = decoded.alertOnline;
  alertsApplyStatus(table, input, 1000);
  return table;
}

static std::string readSource(const char* name) {
  for (const char* prefix : {"firmware/src/", "src/", "../src/"}) {
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

// The decode() above must stay the firmware's logic.
static void testMirrorsFirmwareSource() {
  std::string status = readSource("StatusClient.cpp");
  assert(status.find("JsonVariantConst value = doc[\"services\"][SERVICE_LIST[id].key];") != std::string::npos);
  assert(status.find("if (!value.is<bool>()) continue;") != std::string::npos);
  assert(status.find("if (doc[\"services\"][monitored.key].is<bool>()) services.alertReported") != std::string::npos);
  assert(status.find("services.serviceJellyfin = doc[\"services\"][\"jellyfin\"] | false;") != std::string::npos);
  std::string manager = readSource("AlertManager.cpp");
  assert(manager.find("input.servicesReported = services.alertReported;") != std::string::npos);
}

static void testTrueFalseNullMissing() {
  // jellyfin true, navidrome false, metube null, ollama/cloudflare missing.
  Decoded d = decode("{\"services\":{\"jellyfin\":true,\"navidrome\":false,\"metube\":null,"
                     "\"nextcloud\":null,\"immich\":true}}");
  assert(serviceDot(d.reported, d.online, SVC_JELLYFIN) == ServiceDot::Online);
  assert(serviceDot(d.reported, d.online, SVC_NAVIDROME) == ServiceDot::Offline);
  assert(serviceDot(d.reported, d.online, SVC_METUBE) == ServiceDot::Unknown);   // null
  assert(serviceDot(d.reported, d.online, SVC_OLLAMA) == ServiceDot::Unknown);   // missing
  assert(serviceDot(d.reported, d.online, SVC_NEXTCLOUD) == ServiceDot::Unknown);
  assert(serviceDot(d.reported, d.online, SVC_IMMICH) == ServiceDot::Online);
  assert(serviceDot(d.reported, d.online, SVC_TECH_BLOG) == ServiceDot::Unknown);

  AlertTable table = apply(d);
  assert(table.slots[ALERT_JELLYFIN].severity == AlertSeverity::None);     // true: no alert
  assert(table.slots[ALERT_NAVIDROME].severity == AlertSeverity::Warning); // false: warning
  assert(table.slots[ALERT_METUBE].severity == AlertSeverity::None);       // null: no alert
  assert(table.slots[ALERT_OLLAMA].severity == AlertSeverity::None);       // missing: no alert
  assert(table.slots[ALERT_CLOUDFLARE].severity == AlertSeverity::None);
  assert(alertsActiveCount(table) == 1);
}

static void testGenericInstallHasNoServiceAlerts() {
  // SERVICES blank: every key null.
  Decoded d = decode("{\"services\":{\"jellyfin\":null,\"navidrome\":null,\"metube\":null,\"bazarr\":null,"
                     "\"ollama\":null,\"cloudflare\":null,\"mcp\":null,\"nextcloud\":null,"
                     "\"immich\":null,\"technical_blog\":null}}");
  assert(d.reported == 0 && d.alertReported == 0);
  for (int id = 0; id < SVC_COUNT; id++) assert(serviceDot(d.reported, d.online, id) == ServiceDot::Unknown);
  assert(alertsActiveCount(apply(d)) == 0);
  // An older backend's empty object, or no services block at all.
  assert(alertsActiveCount(apply(decode("{\"services\":{}}"))) == 0);
  assert(alertsActiveCount(apply(decode("{}"))) == 0);
}

static void testNonBooleanValuesAreUnknown() {
  Decoded d = decode("{\"services\":{\"jellyfin\":0,\"navidrome\":\"false\",\"metube\":1}}");
  assert(d.reported == 0 && d.alertReported == 0);
  assert(alertsActiveCount(apply(d)) == 0);
}

static void testUnlistingKeepsPreviousState() {
  // Existing AlertLogic rule: a service reported down that then becomes null
  // keeps its alert until it is reported again (or the device restarts).
  AlertTable table = apply(decode("{\"services\":{\"ollama\":false}}"));
  assert(table.slots[ALERT_OLLAMA].severity == AlertSeverity::Warning);
  table = apply(decode("{\"services\":{\"ollama\":null}}"), table);
  assert(table.slots[ALERT_OLLAMA].severity == AlertSeverity::Warning);
  table = apply(decode("{\"services\":{\"ollama\":true}}"), table);
  assert(table.slots[ALERT_OLLAMA].severity == AlertSeverity::None);
}

int main() {
  testMirrorsFirmwareSource();
  testTrueFalseNullMissing();
  testGenericInstallHasNoServiceAlerts();
  testNonBooleanValuesAreUnknown();
  testUnlistingKeepsPreviousState();
  puts("service status tests passed");
  return 0;
}
