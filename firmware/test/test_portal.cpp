// The setup hotspot's three modes, through the same routing the device uses
// (SetupPage.h portalRoute + renderPortalRoot): what a phone sees after
// joining, whatever URL it opens first (captive-portal probes included), and
// that a form from another mode (a stale tab) changes nothing.
#include "SetupPage.h"
#include <assert.h>
#include <fstream>
#include <sstream>
#include <stdio.h>
#include <string.h>
#include <string>

static const ScannedNetwork NETWORKS[] = {{"Home", -50, false}};

static bool has(const std::string& page, const char* text) {
  return page.find(text) != std::string::npos;
}

// What a phone ends up with after requesting `path`: follow at most one
// redirect (to "/", as the device sends), then the page for the mode.
static std::string phoneOpens(PortalMode mode, const char* path, bool post = false) {
  PortalAction action = portalRoute(mode, post, path);
  if (action == PortalAction::RedirectRoot) action = portalRoute(mode, false, "/");
  assert(action == PortalAction::RootPage);
  SetupFormView setup;
  setup.firstRun = true;   // Deliberately wrong: the mode must decide.
  setup.haveNetwork = true;
  setup.networks = NETWORKS;
  setup.networkCount = 1;
  setup.backend = "http://192.0.2.10:8090";
  LocationPageView location;
  location.wifiConnected = true;
  std::string page;
  renderPortalRoot(page, mode, setup, location);
  return page;
}

static const char* PROBES[] = {
  "/", "/hotspot-detect.html", "/library/test/success.html", "/generate_204", "/gen_204",
  "/connecttest.txt", "/ncsi.txt", "/redirect", "/canonical.html", "/success.txt", "/favicon.ico",
  "/does-not-exist", "/location", "/save", "/location/search",  // GETs of POST routes: not actions.
};

static void assertWifiPage(const std::string& page) {
  assert(has(page, "<select name=net>") && has(page, "<input name=pass type=password"));
  assert(!has(page, "Town, city or postal code") && !has(page, "/location/search"));
}

static void testFirstRunWifi() {
  for (const char* path : PROBES) {
    std::string page = phoneOpens(PortalMode::FirstRunWifi, path);
    assertWifiPage(page);
    assert(has(page, "<button>Connect</button>") && !has(page, "name=server") && !has(page, "HomeDeck Backend"));
  }
}

static void testBackend() {
  for (const char* path : PROBES) {
    std::string page = phoneOpens(PortalMode::WifiAndBackend, path);
    assertWifiPage(page);
    assert(has(page, "HomeDeck Backend (optional)") && has(page, "<input name=server "));
    size_t field = page.find("<input name=server ");
    std::string tag = page.substr(field, page.find('>', field) - field);
    assert(!has(tag, "required"));                   // Optional: blank = no backend.
    assert(has(page, "<button>Save and connect</button>"));
  }
}

static void testLocation() {
  for (const char* path : PROBES) {
    std::string page = phoneOpens(PortalMode::Location, path);
    assert(has(page, "<h3>Location</h3>") && has(page, "Town, city or postal code") &&
           has(page, "<button>Search</button>") && has(page, "action=/location/search"));
    // Nothing else: no Wi-Fi selector or password, no backend address.
    for (const char* other : {"<select name=net>", "name=pass", "type=password", "name=server", "HomeDeck Backend",
                              "action=/save", "Connect</button>"}) {
      if (has(page, other)) fprintf(stderr, "location page (%s) contains %s\n", path, other);
      assert(!has(page, other));
    }
  }
}

static void testActionsFollowTheMode() {
  typedef PortalAction A;
  // Wi-Fi save: only in the Wi-Fi modes. A stale Wi-Fi tab in Location mode changes nothing.
  assert(portalRoute(PortalMode::FirstRunWifi, true, "/save") == A::SaveWifi);
  assert(portalRoute(PortalMode::WifiAndBackend, true, "/save") == A::SaveWifi);
  assert(portalRoute(PortalMode::Location, true, "/save") == A::RedirectRoot);
  // Location search / pick: only in Location mode; never a Wi-Fi save.
  for (PortalMode m : {PortalMode::FirstRunWifi, PortalMode::WifiAndBackend}) {
    assert(portalRoute(m, true, "/location/search") == A::RedirectRoot);
    assert(portalRoute(m, true, "/location/use") == A::RedirectRoot);
  }
  assert(portalRoute(PortalMode::Location, true, "/location/search") == A::LocationSearch);
  assert(portalRoute(PortalMode::Location, true, "/location/use") == A::LocationUse);
  // GETs of action paths and odd input do nothing.
  for (PortalMode m : {PortalMode::FirstRunWifi, PortalMode::WifiAndBackend, PortalMode::Location}) {
    assert(portalRoute(m, false, "/save") == A::RedirectRoot);
    assert(portalRoute(m, false, "/location/use") == A::RedirectRoot);
    assert(portalRoute(m, true, "/") == A::RedirectRoot);
    assert(portalRoute(m, false, nullptr) == A::RedirectRoot);
    assert(portalRoute(m, false, "/save/../") == A::RedirectRoot);
  }
  assert(strcmp(portalModeName(PortalMode::FirstRunWifi), "Wi-Fi") == 0);
  assert(strcmp(portalModeName(PortalMode::WifiAndBackend), "Backend") == 0);
  assert(strcmp(portalModeName(PortalMode::Location), "Location") == 0);
}

// --- The device uses exactly this routing --------------------------------------------------

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

static void testDeviceRouting() {
  std::string portal = readSource("src/WifiProvisioning.cpp");
  // One handler for every request, routed by the mode.
  assert(!has(portal, "server.on(") && occurrences(portal, "server.addHandler(") == 1);
  assert(has(portal, "server.addHandler(new PortalRequests());"));
  assert(has(portal, "switch (portalRoute(portalMode, method == HTTP_POST, uri.c_str())) {"));
  // "/" is drawn for the mode; both senders go through renderPortalRoot.
  std::string root = functionBody(portal, "static void handleRoot() {");
  assert(has(root, "if (portalMode == PortalMode::Location) {"));
  assert(occurrences(portal, "renderPortalRoot(page, ") == 2);
  assert(!has(portal, "renderSetupForm(") && !has(portal, "renderLocationPage("));
  // Never cached: one server.send() in total, inside sendNoStore().
  assert(occurrences(portal, "server.send(") == 1 && has(portal, "server.sendHeader(\"Cache-Control\", \"no-store\");"));
  // Starting while a session is open switches the mode (or refuses), never
  // silently keeps the old page.
  std::string start = functionBody(portal, "static bool startSetup(PortalMode mode) {");
  assert(has(start, "if (portalMode == mode) return true;") && has(start, "beginPortalMode(mode);"));
  assert(!has(start, "if (wifiSetupRunning()) return true;"));
  // Runtime only: the mode is never stored.
  assert(!has(portal, "Preferences") && !has(portal, "putUChar") && !has(portal, "putString"));
  // Each entry point starts its own mode.
  assert(has(portal, "return startSetup(firstRun ? PortalMode::FirstRunWifi : PortalMode::WifiAndBackend);"));
  assert(has(portal, "return startSetup(PortalMode::Location);"));
  std::string settings = readSource("src/SettingsScreen.cpp");
  std::string run = functionBody(settings, "static void runConfirmed(");
  auto caseBody = [&run](const char* label) {
    size_t at = run.find(label);
    assert(at != std::string::npos);
    return run.substr(at, run.find("break;", at) - at);
  };
  assert(has(caseBody("case ConfirmAction::StartLocationSetup:"), "startLocationSetup();"));
  assert(has(caseBody("case ConfirmAction::StartWifiSetup:"), "startWifiSetup();"));
  assert(!has(caseBody("case ConfirmAction::StartWifiSetup:"), "startLocationSetup"));
  assert(has(readSource("src/SetupScreen.cpp"), "startWifiSetup(true);"));
  std::string location = readSource("src/WifiSettingsScreen.cpp");
  std::string touch = functionBody(location, "void handleLocationSettingsTouch(int x, int y) {");
  assert(has(touch, "askConfirm(ConfirmAction::StartLocationSetup, SettingsView::Location)"));
  std::string server = functionBody(location, "void handleServerSettingsTouch(int x, int y) {");
  assert(has(server, "askConfirm(ConfirmAction::StartWifiSetup, SettingsView::Server)"));
  // One-line logs per mode.
  assert(has(portal, "Serial.printf(\"[SETUP] Portal mode: %s\\n\", portalModeName(mode));"));
  assert(has(portal, "Serial.println(\"[LOCATION] Portal started\");"));
}

int main() {
  testFirstRunWifi();
  testBackend();
  testLocation();
  testActionsFollowTheMode();
  testDeviceRouting();
  puts("portal tests passed");
  return 0;
}
