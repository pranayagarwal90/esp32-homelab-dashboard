// Boot decisions (Setup Mode vs dashboard), backend URL resolution from NVS
// including the developer migration seed, the setup form and the retry rule.
#include "ProvisioningLogic.h"
#include <assert.h>
#include <fstream>
#include <sstream>
#include <stdio.h>
#include <string.h>
#include <string>

// What setup() decides, before and after the connect attempt.
struct Boot {
  SetupReason reason;
  BackendBootAction backend;
  char url[BACKEND_URL_MAX];
};

// storedBackend nullptr = no device/backend key in NVS. fallback is a
// developer build's DASHBOARD_DEV_BACKEND_URL (nullptr in public builds).
static Boot boot(bool savedWifi, bool builtInWifi, const char* storedBackend, const char* fallback,
                 bool wifiJoins = true) {
  Boot b;
  b.backend = resolveBootBackend(storedBackend != nullptr, storedBackend, fallback, b.url);
  bool stored = storedBackend != nullptr || b.backend == BackendBootAction::Seed;
  b.reason = bootSetupReason(savedWifi || builtInWifi, stored, b.url[0] != '\0');
  if (b.reason == SetupReason::None && !wifiJoins) b.reason = SetupReason::WifiFailed;
  return b;
}

static void testBootStates() {
  const char* url = "http://192.0.2.10:8090";
  // 1. Saved Wi-Fi + backend: normal.
  Boot b = boot(true, false, url, nullptr);
  assert(b.reason == SetupReason::None && b.backend == BackendBootAction::Use && strcmp(b.url, url) == 0);
  // 2. No Wi-Fi, backend saved: setup.
  assert(boot(false, false, url, nullptr).reason == SetupReason::NoWifi);
  // 3. Wi-Fi saved (e.g. by older firmware), no backend: setup asks for it.
  b = boot(true, false, nullptr, nullptr);
  assert(b.reason == SetupReason::NoBackend && b.backend == BackendBootAction::Setup && b.url[0] == '\0');
  // 4. Fresh device: neither.
  assert(boot(false, false, nullptr, nullptr).reason == SetupReason::NoConfig);
  // 5. Corrupted backend value: setup (never silently replaced).
  assert(boot(true, false, "not a url", nullptr).reason == SetupReason::BadBackend);
  assert(boot(true, false, "", nullptr).reason == SetupReason::BadBackend);            // Overlong reads as "".
  assert(boot(true, false, "garbage", url).reason == SetupReason::BadBackend);         // Even with a fallback.
  assert(boot(true, false, "garbage", url).backend == BackendBootAction::Setup);
  // 6. Current device upgrading with a developer build: Wi-Fi in NVS (or the
  //    built-in network), no backend key, fallback configured: seeded once.
  b = boot(true, true, nullptr, "  http://192.0.2.10:8090/ ");
  assert(b.reason == SetupReason::None && b.backend == BackendBootAction::Seed && strcmp(b.url, url) == 0);
  b = boot(false, true, nullptr, url); // Built-in network only (secrets.h), as before.
  assert(b.reason == SetupReason::None && b.backend == BackendBootAction::Seed);
  // After seeding, NVS holds it: later boots (any build) just use it.
  b = boot(true, false, url, nullptr);
  assert(b.backend == BackendBootAction::Use && b.reason == SetupReason::None);
  // A stored value always wins over the fallback.
  b = boot(true, true, "http://192.0.2.20:8090", url);
  assert(b.backend == BackendBootAction::Use && strcmp(b.url, "http://192.0.2.20:8090") == 0);
  // An invalid developer fallback is ignored.
  assert(boot(true, true, nullptr, "192.0.2.10:8090").reason == SetupReason::NoBackend);
  // 7. Public build: no fallback, no built-in network.
  assert(boot(true, false, nullptr, nullptr).reason == SetupReason::NoBackend);
  assert(boot(false, false, url, nullptr).reason == SetupReason::NoWifi);
  // A stored value that is valid but not normalized is used, normalized.
  b = boot(true, false, "http://Server.LAN:8090/", nullptr);
  assert(b.backend == BackendBootAction::Use && strcmp(b.url, "http://server.lan:8090") == 0);
  // Configured but the network cannot be joined: setup (credentials kept).
  assert(boot(true, false, url, nullptr, false).reason == SetupReason::WifiFailed);
  // Missing configuration never even tries to connect.
  assert(boot(false, false, nullptr, nullptr, false).reason == SetupReason::NoConfig);

  for (int r = int(SetupReason::NoConfig); r <= int(SetupReason::WifiFailed); r++) {
    assert(strlen(setupReasonText(SetupReason(r))) > 0);
  }
  assert(setupReasonText(SetupReason::None)[0] == '\0');
}

static void testBackendBootAction() {
  // (present, valid, fallbackValid)
  assert(backendBootAction(true, true, false) == BackendBootAction::Use);
  assert(backendBootAction(true, true, true) == BackendBootAction::Use);
  assert(backendBootAction(false, false, true) == BackendBootAction::Seed);
  assert(backendBootAction(false, false, false) == BackendBootAction::Setup);
  assert(backendBootAction(true, false, true) == BackendBootAction::Setup); // Never overwrites.
  assert(backendBootAction(true, false, false) == BackendBootAction::Setup);
}

static SetupFormError form(const char* ssid, const char* password, const char* backend, bool haveNetwork,
                           SetupForm& out) {
  return validateSetupForm(ssid, strlen(ssid), strlen(password), backend, haveNetwork, out);
}

static void testSetupForm() {
  SetupForm f;
  // First run: everything.
  assert(form("Home", "password1", " http://192.0.2.10:8090/ ", false, f) == SetupFormError::None);
  assert(f.changeWifi && strcmp(f.backend, "http://192.0.2.10:8090") == 0);
  assert(form("Cafe", "", "http://dash:8090", false, f) == SetupFormError::None && f.changeWifi); // Open network.
  // First run without a network name.
  assert(form("", "", "http://dash:8090", false, f) == SetupFormError::NeedNetwork);
  // A network exists: Wi-Fi may be left blank (backend-only change).
  assert(form("", "", "http://dash:8090", true, f) == SetupFormError::None && !f.changeWifi);
  assert(form("", "password1", "http://dash:8090", true, f) == SetupFormError::PasswordWithoutNetwork);
  // Wi-Fi limits (unchanged from the existing page).
  assert(form("Home", "short", "http://dash:8090", false, f) == SetupFormError::Wifi);
  assert(form("123456789012345678901234567890123", "password1", "http://dash:8090", false, f) ==
         SetupFormError::Wifi);
  // Backend is required and validated; nothing is accepted partially.
  assert(form("Home", "password1", "", false, f) == SetupFormError::Backend &&
         f.backendError == BackendUrlError::Empty && !f.changeWifi && f.backend[0] == '\0');
  assert(form("Home", "password1", "http://dash:8090/api/status", false, f) == SetupFormError::Backend &&
         f.backendError == BackendUrlError::Path && !f.changeWifi);
  assert(form("Home", "password1", "192.0.2.10", true, f) == SetupFormError::Backend &&
         f.backendError == BackendUrlError::Scheme);
  // Error texts.
  assert(strstr(setupFormErrorText(SetupFormError::Backend, BackendUrlError::Path), "/api/status"));
  assert(strlen(setupFormErrorText(SetupFormError::NeedNetwork, BackendUrlError::None)) > 0);
  assert(setupFormErrorText(SetupFormError::None, BackendUrlError::None)[0] == '\0');
}

static void testRestartAndRetry() {
  // The backend URL is fixed per boot: restart on first run or when it changed.
  assert(setupNeedsRestart(true, false));
  assert(setupNeedsRestart(false, true));
  assert(!setupNeedsRestart(false, false)); // Settings: Wi-Fi-only change, as before.
  // After a failed connect, an idle hotspot retries (restarts) after 5 minutes.
  assert(!setupShouldRetry(SetupReason::WifiFailed, true, SETUP_RETRY_IDLE_MS - 1));
  assert(setupShouldRetry(SetupReason::WifiFailed, true, SETUP_RETRY_IDLE_MS));
  assert(!setupShouldRetry(SetupReason::WifiFailed, false, SETUP_RETRY_IDLE_MS * 2)); // Someone is using it.
  // Missing configuration never retries by itself: restarting cannot fix it.
  const SetupReason others[] = {SetupReason::NoConfig, SetupReason::NoWifi, SetupReason::NoBackend,
                                SetupReason::BadBackend};
  for (SetupReason r : others) assert(!setupShouldRetry(r, true, SETUP_RETRY_IDLE_MS * 10));
  // Boot connect is bounded: no endless loop on a network that is not there.
  assert(uint32_t(BOOT_CONNECT_ATTEMPTS) * BOOT_ATTEMPT_MS <= 60000);
}

static void testWifiSwitch() {
  typedef WifiSwitchState S;
  // Joined only with an IP, a connected station and the requested SSID.
  assert(wifiSwitchJoined(true, true, true));
  assert(!wifiSwitchJoined(false, true, true) && !wifiSwitchJoined(true, false, true) &&
         !wifiSwitchJoined(true, true, false));
  // Still connecting: nothing saved, nothing restored, until the timeout.
  WifiSwitchStep step = wifiSwitchStep(S::Connecting, false, 0, true);
  assert(step.next == S::Connecting && !step.saveCredentials && !step.restorePrevious);
  step = wifiSwitchStep(S::Connecting, false, WIFI_SWITCH_TIMEOUT_MS - 1, true);
  assert(step.next == S::Connecting && !step.saveCredentials);
  // Success: the setup page's network is saved only now.
  step = wifiSwitchStep(S::Connecting, true, 5000, true);
  assert(step.next == S::Connected && step.saveCredentials && !step.restorePrevious);
  // USE BUILT-IN (developer builds): joined, but nothing is saved.
  step = wifiSwitchStep(S::Connecting, true, 5000, false);
  assert(step.next == S::Connected && !step.saveCredentials);
  // Timeout (wrong password, network out of range): failed, never saved.
  step = wifiSwitchStep(S::Connecting, false, WIFI_SWITCH_TIMEOUT_MS, true);
  assert(step.next == S::Failed && !step.saveCredentials && step.restorePrevious);
  // Joined exactly at the deadline still counts as success.
  assert(wifiSwitchStep(S::Connecting, true, WIFI_SWITCH_TIMEOUT_MS, true).next == S::Connected);
  // Other states are left alone (Pending is started by updateWifiSwitch()).
  const S others[] = {S::Idle, S::Pending, S::Connected, S::Failed};
  for (S state : others) {
    step = wifiSwitchStep(state, true, WIFI_SWITCH_TIMEOUT_MS * 2, true);
    assert(step.next == state && !step.saveCredentials && !step.restorePrevious);
  }
}

static void testSetupAfterSwitch() {
  typedef WifiSwitchState S;
  // Waiting while the switch is pending or connecting (first run or not).
  for (bool firstRun : {true, false}) {
    assert(setupAfterSwitch(S::Pending, firstRun, true) == SetupJoinOutcome::Waiting);
    assert(setupAfterSwitch(S::Connecting, firstRun, true) == SetupJoinOutcome::Waiting);
    assert(setupAfterSwitch(S::Failed, firstRun, true) == SetupJoinOutcome::Failed);
  }
  // First run: joined -> save the backend and restart.
  assert(setupAfterSwitch(S::Connected, true, false) == SetupJoinOutcome::Apply);
  assert(setupAfterSwitch(S::Connected, true, true) == SetupJoinOutcome::Apply);
  // Settings: Wi-Fi-only change stays connected (no restart), as before.
  assert(setupAfterSwitch(S::Connected, false, false) == SetupJoinOutcome::ConnectedOnly);
  assert(setupAfterSwitch(S::Connected, false, true) == SetupJoinOutcome::Apply);
}

// End to end with the pure pieces: submit -> join -> save -> restart, and
// submit -> timeout -> nothing saved -> retry possible.
static void testFlowModel() {
  typedef WifiSwitchState S;
  SetupForm form;
  assert(validateSetupForm("Home", 4, 9, "http://192.0.2.10:8090", false, form) == SetupFormError::None);
  assert(form.changeWifi);
  S state = S::Connecting; // requestWifiSwitch() + the first updateWifiSwitch().
  bool saved = false;
  uint32_t t = 0;
  for (; t < WIFI_SWITCH_TIMEOUT_MS && state == S::Connecting; t += 500) {
    WifiSwitchStep step = wifiSwitchStep(state, t >= 4000, t, true); // Joins after 4 s.
    state = step.next;
    saved |= step.saveCredentials;
  }
  assert(state == S::Connected && saved && t <= 4500);
  assert(setupAfterSwitch(state, true, true) == SetupJoinOutcome::Apply);

  state = S::Connecting;
  saved = false;
  for (t = 0; state == S::Connecting; t += 500) {
    WifiSwitchStep step = wifiSwitchStep(state, false, t, true); // Wrong password.
    state = step.next;
    saved |= step.saveCredentials;
    assert(t <= WIFI_SWITCH_TIMEOUT_MS); // Bounded: never hangs.
  }
  assert(state == S::Failed && !saved && setupAfterSwitch(state, true, true) == SetupJoinOutcome::Failed);

  // A malformed server never starts a connection.
  assert(validateSetupForm("Home", 4, 9, "http://192.0.2.10:8090/api/status", false, form) ==
         SetupFormError::Backend && !form.changeWifi);
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

static std::string functionBody(const std::string& source, const char* signature) {
  size_t start = source.find(signature);
  assert(start != std::string::npos);
  size_t end = source.find("\n}\n", start);
  assert(end != std::string::npos);
  return source.substr(start, end - start);
}

// Regression (hardware, Phase 3+4): Setup Mode's loop never ran
// updateWifiSwitch(), so a submitted network stayed Pending forever
// ("Connecting..." with no WiFi.begin and no timeout).
static void testSetupLoopDrivesTheSwitch() {
  std::string setup = functionBody(readSource("WifiProvisioning.cpp"), "void updateWifiSetup() {");
  assert(setup.find("updateWifiSwitch();") != std::string::npos);
  std::string mode = functionBody(readSource("SetupScreen.cpp"), "void updateSetupMode() {");
  assert(mode.find("updateWifiSetup();") != std::string::npos);
  std::string loop = functionBody(readSource("main.cpp"), "void loop() {");
  size_t setupBranch = loop.find("if (setupModeActive()) {");
  assert(setupBranch != std::string::npos);
  assert(loop.find("updateSetupMode();", setupBranch) < loop.find("return;\n  }", setupBranch));
  // The POST handler only validates and queues: no blocking join inside it.
  std::string save = functionBody(readSource("WifiProvisioning.cpp"), "static void handleSave() {");
  assert(save.find("WiFi.begin") == std::string::npos && save.find("requestWifiSwitch") == std::string::npos);
  assert(save.find("xQueueOverwrite(submissions") != std::string::npos);
  // The catch-all redirect is registered after the real routes, so it can
  // never swallow GET / or POST /save; the form offers the scanned list.
  std::string source = readSource("WifiProvisioning.cpp");
  size_t root = source.find("server.on(\"/\", HTTP_GET, handleRoot);");
  size_t post = source.find("server.on(\"/save\", HTTP_POST, handleSave);");
  size_t all = source.find("server.addHandler(new RedirectEverything());");
  assert(root != std::string::npos && post != std::string::npos && all != std::string::npos);
  assert(root < all && post < all);
  assert(source.find("server.onNotFound(") == std::string::npos);
  assert(source.find("<select name=net>") != std::string::npos);
  // Scanning: the driver's done event, never the wrapper's 6 s scanComplete()
  // timeout, and a fully initialized scan configuration.
  assert(source.find("WiFi.scanNetworks(") == std::string::npos);
  std::string begin = functionBody(source, "static bool beginScan() {");
  assert(begin.find("memset(&config, 0, sizeof(config));") != std::string::npos);
  assert(begin.find("esp_wifi_scan_start(&config, false)") != std::string::npos);
  assert(source.find("ARDUINO_EVENT_WIFI_SCAN_DONE") != std::string::npos);
  assert(source.find("name=pass type=password") != std::string::npos);
  // The password is never written back into the page: its input has no value.
  std::string form = functionBody(source, "static void sendForm(");
  assert(form.find("<input name=pass type=password maxlength=63 autocomplete=off>") != std::string::npos);
}

// --- Scanned networks ---------------------------------------------------------------

static int add(ScannedNetwork* list, int count, int max, const char* ssid, int rssi, bool open = false) {
  return addScannedNetwork(list, count, max, ssid, strlen(ssid), rssi, open);
}

static void testScanList() {
  ScannedNetwork list[SETUP_SCAN_MAX];
  int n = 0;
  n = add(list, n, SETUP_SCAN_MAX, "Home", -70);
  n = add(list, n, SETUP_SCAN_MAX, "", -30);                  // Hidden network: ignored.
  n = add(list, n, SETUP_SCAN_MAX, "Cafe", -50, true);
  n = add(list, n, SETUP_SCAN_MAX, "Home", -40);              // Second access point, stronger.
  n = add(list, n, SETUP_SCAN_MAX, "Home", -90);              // Weaker duplicate: ignored.
  n = add(list, n, SETUP_SCAN_MAX, "Neighbour", -80);
  assert(addScannedNetwork(list, n, SETUP_SCAN_MAX, "123456789012345678901234567890123", 33, -10, false) == n);
  assert(n == 3);
  // Strongest first, one entry per SSID, the strongest signal kept.
  assert(strcmp(list[0].ssid, "Home") == 0 && list[0].rssi == -40 && !list[0].open);
  assert(strcmp(list[1].ssid, "Cafe") == 0 && list[1].open);
  assert(strcmp(list[2].ssid, "Neighbour") == 0);
  assert(findScannedNetwork(list, n, "Cafe", 4) == 1 && findScannedNetwork(list, n, "Caf", 3) == -1);
  assert(findScannedNetwork(list, n, "Office", 6) == -1);
  // A 32-character SSID (the maximum) is kept intact.
  const char* longest = "12345678901234567890123456789012";
  n = add(list, n, SETUP_SCAN_MAX, longest, -45);
  assert(n == 4 && strcmp(list[1].ssid, longest) == 0);

  // The cap keeps the strongest SETUP_SCAN_MAX networks.
  ScannedNetwork capped[SETUP_SCAN_MAX];
  int c = 0;
  char name[8];
  for (int i = 0; i < 40; i++) {
    snprintf(name, sizeof(name), "net%02d", i);
    c = add(capped, c, SETUP_SCAN_MAX, name, -95 + i); // net39 is the strongest.
  }
  assert(c == SETUP_SCAN_MAX && strcmp(capped[0].ssid, "net39") == 0);
  assert(strcmp(capped[SETUP_SCAN_MAX - 1].ssid, "net25") == 0);
  for (int i = 1; i < c; i++) assert(capped[i - 1].rssi >= capped[i].rssi);
  // Weaker than a full list: not added.
  assert(add(capped, c, SETUP_SCAN_MAX, "far", -100) == c && findScannedNetwork(capped, c, "far", 3) == -1);
  // Equal signal: first seen stays first (stable).
  ScannedNetwork tie[3];
  int t = add(tie, 0, 3, "A", -60);
  t = add(tie, t, 3, "B", -60);
  assert(strcmp(tie[0].ssid, "A") == 0 && strcmp(tie[1].ssid, "B") == 0);
  assert(strcmp(signalLabel(-55), "strong") == 0 && strcmp(signalLabel(-70), "good") == 0 &&
         strcmp(signalLabel(-85), "weak") == 0);
}

static void testScanMachine() {
  ScanMachine m;
  scanBegin(m, 1000); // Hotspot started at t = 1000.
  // Startup delay: no scan right after softAP(), but the page says Scanning.
  assert(m.state == ScanState::WaitingForAp && scanBusy(m));
  assert(scanStep(m, 1000, false, false) == ScanAction::None);
  assert(scanStep(m, 1000 + SETUP_SCAN_START_DELAY_MS - 1, false, false) == ScanAction::None);
  assert(scanStep(m, 1000 + SETUP_SCAN_START_DELAY_MS, false, false) == ScanAction::Start);
  scanStarted(m, true, 1800);
  assert(m.state == ScanState::Running && scanBusy(m) && !m.requested);
  // Running: no result yet.
  assert(scanStep(m, 3000, false, false) == ScanAction::None);
  // Refresh while running is ignored (no second scan, no restart of this one).
  assert(!scanRequest(m) && !m.requested && m.state == ScanState::Running);
  // Success: the driver's done event; list replaced.
  assert(scanStep(m, 6500, true, false) == ScanAction::Collect && m.state == ScanState::Idle);
  assert(scanReplacesList(ScanAction::Collect, true));
  // A scan longer than the Arduino wrapper's 6 s is still collected (here 7 s).
  scanRequest(m);
  assert(scanStep(m, 10000, false, false) == ScanAction::Start);
  scanStarted(m, true, 10000);
  assert(scanStep(m, 16000, false, false) == ScanAction::None);
  assert(scanStep(m, 17000, true, false) == ScanAction::Collect);
  // Idle, nothing requested: nothing happens.
  assert(!scanBusy(m) && scanStep(m, 20000, false, false) == ScanAction::None);

  // Driver failure (done event, failed status): previous list kept, Refresh retries.
  assert(scanRequest(m) && scanStep(m, 21000, false, false) == ScanAction::Start);
  scanStarted(m, true, 21000);
  assert(scanStep(m, 23000, true, false) == ScanAction::Collect);
  assert(!scanReplacesList(ScanAction::Collect, false));
  assert(scanRequest(m) && scanStep(m, 24000, false, false) == ScanAction::Start);

  // Start failure (esp_wifi_scan_start error): not running, request consumed,
  // Refresh can try again.
  scanStarted(m, false, 24000);
  assert(m.state == ScanState::Idle && !scanBusy(m) && scanStep(m, 24100, false, false) == ScanAction::None);
  assert(scanRequest(m) && scanStep(m, 24200, false, false) == ScanAction::Start);

  // Timeout: abandoned after SETUP_SCAN_TIMEOUT_MS, list kept, retry possible.
  scanStarted(m, true, 30000);
  assert(scanStep(m, 30000 + SETUP_SCAN_TIMEOUT_MS - 1, false, false) == ScanAction::None);
  assert(scanStep(m, 30000 + SETUP_SCAN_TIMEOUT_MS, false, false) == ScanAction::Timeout);
  assert(m.state == ScanState::Idle && !scanReplacesList(ScanAction::Timeout, true));
  assert(scanRequest(m));

  // Never starts while a network is being joined; starts once it is over.
  assert(scanStep(m, 50000, false, true) == ScanAction::None && m.requested);
  assert(scanStep(m, 50100, false, false) == ScanAction::Start);
  // The first scan also waits for a join in progress after the delay.
  ScanMachine first;
  scanBegin(first, 0);
  assert(scanStep(first, SETUP_SCAN_START_DELAY_MS, false, true) == ScanAction::None && first.requested);
  assert(scanStep(first, SETUP_SCAN_START_DELAY_MS + 10, false, false) == ScanAction::Start);
  // millis() wrap-around: elapsed time is unsigned.
  ScanMachine wrap;
  scanBegin(wrap, 0xFFFFFF00u);
  assert(scanStep(wrap, 0xFFFFFF00u + SETUP_SCAN_START_DELAY_MS, false, false) == ScanAction::Start);
}

// The cache itself: a failed refresh leaves the previous networks in place.
static void testCacheKeptOnFailedRefresh() {
  ScannedNetwork list[SETUP_SCAN_MAX];
  int count = add(list, 0, SETUP_SCAN_MAX, "Home", -50);
  count = add(list, count, SETUP_SCAN_MAX, "Cafe", -70, true);
  const ScanAction outcomes[] = {ScanAction::Timeout, ScanAction::Collect};
  for (ScanAction outcome : outcomes) {
    if (scanReplacesList(outcome, false)) count = 0; // What collectScan() would do.
  }
  assert(count == 2 && strcmp(list[0].ssid, "Home") == 0 && strcmp(list[1].ssid, "Cafe") == 0);
}

static void testSsidChoice() {
  size_t length = 99;
  // Dropdown only.
  const char* ssid = chooseSetupSsid("", 0, "Home", 4, length);
  assert(length == 4 && memcmp(ssid, "Home", 4) == 0);
  // A typed name deliberately overrides the dropdown.
  ssid = chooseSetupSsid("Office", 6, "Home", 4, length);
  assert(length == 6 && memcmp(ssid, "Office", 6) == 0);
  // Neither: nothing chosen -> validation error on first run, keep otherwise.
  ssid = chooseSetupSsid("", 0, "", 0, length);
  assert(length == 0);
  SetupForm f;
  assert(validateSetupForm(ssid, length, 0, "http://dash:8090", false, f) == SetupFormError::NeedNetwork);
  assert(validateSetupForm(ssid, length, 0, "http://dash:8090", true, f) == SetupFormError::None && !f.changeWifi);
  assert(strstr(setupFormErrorText(SetupFormError::NeedNetwork, BackendUrlError::None), "Choose"));
}

static void testOpenAndSecuredNetworks() {
  SetupForm f;
  // Open network (scan says open, or typed): a blank password is allowed.
  assert(validateSetupForm("Cafe", 4, 0, "http://dash:8090", false, f, false) == SetupFormError::None &&
         f.changeWifi);
  // A listed secured network needs a password: caught before any join.
  assert(validateSetupForm("Home", 4, 0, "http://dash:8090", false, f, true) == SetupFormError::NeedPassword &&
         !f.changeWifi);
  assert(validateSetupForm("Home", 4, 12, "http://dash:8090", false, f, true) == SetupFormError::None);
  // The WPA2 length rule still applies.
  assert(validateSetupForm("Home", 4, 5, "http://dash:8090", false, f, true) == SetupFormError::Wifi);
  assert(strlen(setupFormErrorText(SetupFormError::NeedPassword, BackendUrlError::None)) > 0);
}

// --- What ends up in NVS ----------------------------------------------------------------
// Models the main-task flow with the firmware's own pure steps: Wi-Fi is
// written only on wifiSwitchStep().saveCredentials, the backend only when the
// setup outcome is Apply (WifiProvisioning::applySubmission).

struct Nvs {
  char wifi[33];
  char backend[BACKEND_URL_MAX];
};

struct Outcome {
  WifiSwitchState state;
  bool restart;
  bool retryPossible;
};

// joinAfterMs < 0: the network is never joined (wrong password).
static Outcome submit(Nvs& nvs, bool firstRun, const char* ssid, const char* backend, int joinAfterMs) {
  SetupForm form;
  bool haveNetwork = nvs.wifi[0] != '\0';
  assert(validateSetupForm(ssid, strlen(ssid), 9, backend, haveNetwork, form) == SetupFormError::None);
  bool backendChanged = strcmp(form.backend, nvs.backend) != 0;
  WifiSwitchState state = WifiSwitchState::Connecting;
  for (uint32_t t = 0; state == WifiSwitchState::Connecting; t += 250) {
    WifiSwitchStep step = wifiSwitchStep(state, joinAfterMs >= 0 && t >= uint32_t(joinAfterMs), t, true);
    if (step.saveCredentials) snprintf(nvs.wifi, sizeof(nvs.wifi), "%s", ssid);
    state = step.next;
    assert(t <= WIFI_SWITCH_TIMEOUT_MS);
  }
  Outcome out = {state, false, false};
  switch (setupAfterSwitch(state, firstRun, backendChanged)) {
    case SetupJoinOutcome::Apply:
      if (backendChanged || !nvs.backend[0]) snprintf(nvs.backend, sizeof(nvs.backend), "%s", form.backend);
      out.restart = setupNeedsRestart(firstRun, backendChanged);
      break;
    case SetupJoinOutcome::Failed:
      out.retryPossible = true; // Hotspot stays up; a new submission is accepted.
      break;
    default:
      break;
  }
  return out;
}

static void testNvsOutcomes() {
  // First run, selected network + correct password: join, save both, restart.
  Nvs nvs = {"", ""};
  Outcome o = submit(nvs, true, "Home", "http://192.0.2.10:8090", 3000);
  assert(o.state == WifiSwitchState::Connected && o.restart);
  assert(strcmp(nvs.wifi, "Home") == 0 && strcmp(nvs.backend, "http://192.0.2.10:8090") == 0);

  // First run, wrong password: timeout, nothing saved, retry, then success.
  nvs = Nvs{"", ""};
  o = submit(nvs, true, "Home", "http://192.0.2.10:8090", -1);
  assert(o.state == WifiSwitchState::Failed && !o.restart && o.retryPossible);
  assert(nvs.wifi[0] == '\0' && nvs.backend[0] == '\0');
  o = submit(nvs, true, "Home", "http://192.0.2.10:8090", 2000);
  assert(o.restart && strcmp(nvs.wifi, "Home") == 0);

  // Existing device (Settings): a failed change keeps the old network and server.
  nvs = Nvs{"Home", "http://192.0.2.10:8090"};
  o = submit(nvs, false, "Home", "http://192.0.2.10:8090", -1);       // Same SSID, wrong password.
  assert(o.state == WifiSwitchState::Failed && o.retryPossible);
  assert(strcmp(nvs.wifi, "Home") == 0 && strcmp(nvs.backend, "http://192.0.2.10:8090") == 0);
  o = submit(nvs, false, "Office", "http://192.0.2.99:8090", -1);     // New SSID and server: neither saved.
  assert(strcmp(nvs.wifi, "Home") == 0 && strcmp(nvs.backend, "http://192.0.2.10:8090") == 0);
  // The retry with the right password: Wi-Fi-only change, no restart.
  o = submit(nvs, false, "Home", "http://192.0.2.10:8090", 1000);
  assert(o.state == WifiSwitchState::Connected && !o.restart && strcmp(nvs.wifi, "Home") == 0);
  // A server change that joins: saved, restart.
  o = submit(nvs, false, "Home", "http://192.0.2.20:8090", 1000);
  assert(o.restart && strcmp(nvs.backend, "http://192.0.2.20:8090") == 0);
}

int main() {
  testScanList();
  testScanMachine();
  testCacheKeptOnFailedRefresh();
  testSsidChoice();
  testOpenAndSecuredNetworks();
  testNvsOutcomes();
  testWifiSwitch();
  testSetupAfterSwitch();
  testFlowModel();
  testSetupLoopDrivesTheSwitch();
  testBootStates();
  testBackendBootAction();
  testSetupForm();
  testRestartAndRetry();
  puts("provisioning tests passed");
  return 0;
}
