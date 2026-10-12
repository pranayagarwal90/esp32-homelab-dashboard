#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <HTTPClient.h>
#include <esp_wifi.h>
#include <freertos/queue.h>
#include "WifiProvisioning.h"
#include "DeviceConfig.h"
#include "DeviceIdentity.h"
#include "NetworkManager.h"
#include "PowerManager.h"
#include "ProvisioningLogic.h"
#include "SetupPage.h"

static constexpr uint32_t SETUP_IDLE_TIMEOUT_MS = 10UL * 60UL * 1000UL;
static constexpr uint32_t CLOSE_AFTER_SUCCESS_MS = 20000;
static constexpr uint32_t RESTART_DELAY_MS = 8000;      // Time to read the result.
static constexpr uint16_t SERVER_CHECK_TIMEOUT_MS = 3000;
static constexpr uint32_t SETUP_STACK_BYTES = 6144;

// A validated submission (setup task -> main task, one queue slot).
struct SetupSubmission {
  WifiCredentials wifi;
  bool changeWifi;                 // False: keep the current network.
  bool keepBackend;                // First run: no backend field; keep what is stored.
  char backend[BACKEND_URL_MAX];   // Normalized; "" = no backend.
};

// --- Shared between the main task and the setup task --------------------------------

static volatile WifiSetupPhase phase = WifiSetupPhase::Off; // Written by main only.
static volatile ServerCheck serverCheck = ServerCheck::NotChecked; // Main only.
static volatile bool stopRequested = false;                // Main -> task.
static volatile bool taskRunning = false;                  // Task -> main.
static volatile bool firstRunMode = false;                 // Main, before the task starts.
static volatile bool haveNetwork = false;                  // Main, before the task starts.
static volatile uint32_t lastRequestMs = 0;                // Task -> main.
static QueueHandle_t submissions = nullptr;                 // Task -> main, 1 slot.
static portMUX_TYPE targetLock = portMUX_INITIALIZER_UNLOCKED;
static char target[33] = "";

// --- Main task only ------------------------------------------------------------------

static char apSsid[24] = "";
static char apPassword[12] = "";
static uint32_t revision = 0;
static unsigned long lastActivity = 0;
static unsigned long connectedAt = 0;
static unsigned long restartAt = 0;
static bool restartIssued = false;
static char pendingBackend[BACKEND_URL_MAX] = "";
static bool pendingBackendChanged = false;

// --- Setup task only -------------------------------------------------------------------

static WebServer server(80);
static DNSServer dns;
static bool routesRegistered = false;
static ScannedNetwork networks[SETUP_SCAN_MAX]; // Strongest first (ProvisioningLogic).
static int networkCount = 0;
static ScanMachine scan;           // ProvisioningLogic: delay, running, done, timeout.
static bool scanDone = false;      // A scan completed (finding nothing is a result).
static bool scanFailed = false;    // The last scan failed or timed out.
// Set by the driver's scan-done event (Arduino event task), read by the setup task.
static volatile bool scanDoneEvent = false;
static volatile bool scanDoneOk = false;
static bool scanEventRegistered = false; // Main task, once.
// The last submission's network and server, re-filled after a failure or a
// rejected form (never the password).
static char formSsid[33] = "";
static char formBackend[BACKEND_URL_MAX] = "";
static bool formFilled = false;

static void setTarget(const char* ssid) {
  portENTER_CRITICAL(&targetLock);
  strncpy(target, ssid, sizeof(target) - 1);
  target[sizeof(target) - 1] = '\0';
  portEXIT_CRITICAL(&targetLock);
}

static void copyTarget(char* out, size_t size) {
  portENTER_CRITICAL(&targetLock);
  strncpy(out, target, size - 1);
  out[size - 1] = '\0';
  portEXIT_CRITICAL(&targetLock);
}

static String htmlEscape(const char* text) {
  String out;
  for (const char* p = text; *p; p++) {
    switch (*p) {
      case '&': out += "&amp;"; break;
      case '<': out += "&lt;"; break;
      case '>': out += "&gt;"; break;
      case '"': out += "&quot;"; break;
      case '\'': out += "&#39;"; break;
      default: out += *p; break;
    }
  }
  return out;
}

static const char PAGE_HEAD[] PROGMEM =
  "<!doctype html><html><head><meta charset=utf-8>"
  "<meta name=viewport content='width=device-width,initial-scale=1'>"
  "<title>HomeDeck Mini Setup</title><style>"
  "body{font-family:sans-serif;max-width:420px;margin:1.5em auto;padding:0 1em}"
  "input,button{box-sizing:border-box;width:100%;padding:.6em;margin:.3em 0 .8em;font-size:1em}"
  "small{color:#555}.err{color:#b00}"
  "</style></head><body><h2>HomeDeck Mini Setup</h2>";

static const char* serverCheckText() {
  switch (serverCheck) {
    case ServerCheck::Reachable: return " The HomeDeck Backend answered.";
    case ServerCheck::NotReachable:
      return " The HomeDeck Backend did not answer; it is saved anyway (check it in Settings).";
    case ServerCheck::NotChecked: break;
  }
  return "";
}

static void appendStatus(String& page) {
  char ssid[33];
  copyTarget(ssid, sizeof(ssid));
  switch (phase) {
    case WifiSetupPhase::Connecting: page += "<p><b>Connecting to "; break;
    case WifiSetupPhase::Connected: page += "<p><b>Connected to "; break;
    case WifiSetupPhase::Failed:
      page += "<p class=err><b>Could not connect to &quot;";
      page += htmlEscape(ssid);
      page += "&quot;. Check the password or signal and try again. Nothing was saved.</b></p>";
      return;
    case WifiSetupPhase::Restarting:
      page += "<p><b>Saved. The display is restarting.";
      page += serverCheckText();
      page += "</b></p>";
      return;
    case WifiSetupPhase::NoChange: page += "<p><b>Nothing to change.</b></p>"; return;
    case WifiSetupPhase::SaveFailed: page += "<p class=err><b>Could not save the settings.</b></p>"; return;
    default: return;
  }
  page += htmlEscape(ssid);
  page += "</b></p>";
}

static const char* methodName() {
  switch (server.method()) {
    case HTTP_GET: return "GET";
    case HTTP_POST: return "POST";
    case HTTP_HEAD: return "HEAD";
    default: return "OTHER";
  }
}

static const char* wifiModeText() {
  wifi_mode_t mode = WiFi.getMode();
  return mode == WIFI_MODE_APSTA ? "AP+STA" : mode == WIFI_MODE_AP ? "AP" : mode == WIFI_MODE_STA ? "STA" : "OFF";
}

// Starts an async scan with a fully initialized configuration (the Arduino
// wrapper leaves home_chan_dwell_time unset). Same timing as the wrapper's
// defaults: active, 100-300 ms per channel, hidden networks skipped.
static bool beginScan() {
  if (!(WiFi.getMode() & WIFI_MODE_STA)) {
    Serial.printf("[SETUP] Scan not started: Wi-Fi mode %s has no station\n", wifiModeText());
    return false;
  }
  WiFi.scanDelete(); // Frees the previous results; the cached list is separate.
  scanDoneEvent = false;
  wifi_scan_config_t config;
  memset(&config, 0, sizeof(config));
  config.show_hidden = false;
  config.scan_type = WIFI_SCAN_TYPE_ACTIVE;
  config.scan_time.active.min = 100;
  config.scan_time.active.max = 300;
  config.home_chan_dwell_time = 30; // Back on the hotspot's channel between channels.
  esp_err_t err = esp_wifi_scan_start(&config, false);
  if (err != ESP_OK) {
    Serial.printf("[SETUP] Scan not started: %s; Refresh retries, manual entry still works\n",
                  esp_err_to_name(err));
    return false;
  }
  Serial.printf("[SETUP] Starting async network scan (mode %s)\n", wifiModeText());
  Serial.println("[SETUP] scanComplete -> RUNNING");
  return true;
}

static void collectScan(bool ok) {
  if (!ok) {
    Serial.println("[SETUP] scanComplete -> FAILED (driver reported a failed scan); keeping the previous list");
    scanFailed = true;
    WiFi.scanDelete();
    return;
  }
  int16_t found = WiFi.scanComplete(); // Results were copied by the scan-done event.
  if (found < 0) found = 0;
  networkCount = 0;
  for (int i = 0; i < found; i++) {
    String ssid = WiFi.SSID(i);
    networkCount = addScannedNetwork(networks, networkCount, SETUP_SCAN_MAX, ssid.c_str(), ssid.length(),
                                     WiFi.RSSI(i), WiFi.encryptionType(i) == WIFI_AUTH_OPEN);
  }
  WiFi.scanDelete();
  scanDone = true;
  scanFailed = false;
  Serial.printf("[SETUP] scanComplete -> %d networks (%d listed)\n", found, networkCount);
}

// Setup task, every pass: logs only state changes.
static void serviceScan() {
  WifiSetupPhase current = phase;
  bool joining = current == WifiSetupPhase::Connecting || current == WifiSetupPhase::Restarting;
  bool done = scanDoneEvent;
  switch (scanStep(scan, millis(), done, joining)) {
    case ScanAction::Start:
      scanStarted(scan, beginScan(), millis());
      break;
    case ScanAction::Collect:
      scanDoneEvent = false;
      collectScan(scanReplacesList(ScanAction::Collect, scanDoneOk));
      break;
    case ScanAction::Timeout:
      Serial.println("[SETUP] scanComplete -> FAILED (no result in time); keeping the previous list");
      esp_wifi_scan_stop();
      WiFi.scanDelete();
      scanFailed = true;
      break;
    case ScanAction::None:
      break;
  }
}

// The form; error, ssid and backend re-fill it after a rejected or failed
// submission (the password is never echoed). A listed ssid is pre-selected in
// the dropdown, any other one is put in the typed field. First run asks for
// Wi-Fi only; from Settings the optional HomeDeck Backend field is shown too.
static void sendForm(int code, const char* error, const char* ssid, const char* backend) {
  String page = FPSTR(PAGE_HEAD);
  appendStatus(page);
  if (error && error[0]) {
    page += "<p class=err><b>";
    page += htmlEscape(error);
    page += "</b></p>";
  }
  SetupFormView view;
  view.firstRun = firstRunMode;
  view.haveNetwork = haveNetwork;
  view.networks = networks;
  view.networkCount = networkCount;
  view.scanning = scanBusy(scan);
  view.scanFailed = scanFailed;
  view.ssid = ssid;
  view.backend = backend;
  renderSetupForm(page, view); // SetupPage.h: no backend field on first run.
  page += "</body></html>";
  server.send(code, "text/html", page);
}

static void handleRoot() {
  lastRequestMs = millis();
  Serial.printf("[SETUP] %s %s -> setup page\n", methodName(), server.uri().c_str());
  if (server.hasArg("rescan")) {
    if (scanRequest(scan)) Serial.println("[SETUP] Refresh: network scan requested");
    else Serial.println("[SETUP] Refresh ignored: a scan is already running");
  }
  sendForm(200, nullptr, formFilled ? formSsid : "", formFilled ? formBackend : backendUrl());
}

static void rememberForm(const char* ssid, size_t ssidLength, const char* backend) {
  size_t n = ssidLength < sizeof(formSsid) ? ssidLength : sizeof(formSsid) - 1;
  memcpy(formSsid, ssid, n);
  formSsid[n] = '\0';
  snprintf(formBackend, sizeof(formBackend), "%s", backend);
  formFilled = true;
}

static void handleSave() {
  lastRequestMs = millis();
  String typed = server.arg("ssid");
  String selected = server.arg("net");
  String pass = server.arg("pass");
  // First run has no backend field: whatever is stored is kept (nullptr);
  // so does a Settings request without the field (SetupPage.h).
  String backend = server.arg("server");
  const char* backendField = setupBackendArg(firstRunMode, server.hasArg("server"), backend.c_str());
  size_t ssidLength = 0;
  const char* ssid = chooseSetupSsid(typed.c_str(), typed.length(), selected.c_str(), selected.length(),
                                     ssidLength);
  int listed = findScannedNetwork(networks, networkCount, ssid, ssidLength);
  // Never the password, only whether one was given.
  Serial.printf("[SETUP] POST /save: network \"%.*s\" (%s), password %s, backend %s%s%s\n", int(ssidLength),
                ssid, typed.length() ? "typed" : ssidLength ? "from the list" : "keep current",
                pass.length() ? "given" : "blank", backendField ? "\"" : "",
                backendField ? backendField : "not asked (kept)", backendField ? "\"" : "");
  SetupForm form;
  SetupFormError error = validateSetupForm(ssid, ssidLength, pass.length(), backendField, haveNetwork, form,
                                           listed >= 0 && !networks[listed].open);
  Serial.printf("[SETUP] Validation: %s\n", error == SetupFormError::None ? "OK"
                : setupFormErrorText(error, form.backendError));
  rememberForm(ssid, ssidLength, backend.c_str());
  if (error != SetupFormError::None) {
    pass = "";
    sendForm(400, setupFormErrorText(error, form.backendError), formSsid, backend.c_str());
    return;
  }
  SetupSubmission submission;
  memset(&submission, 0, sizeof(submission));
  submission.changeWifi = form.changeWifi;
  submission.keepBackend = form.keepBackend;
  if (form.changeWifi) {
    memcpy(submission.wifi.ssid, ssid, ssidLength);
    memcpy(submission.wifi.password, pass.c_str(), pass.length());
  }
  memcpy(submission.backend, form.backend, sizeof(submission.backend));
  xQueueOverwrite(submissions, &submission);
  memset(&submission, 0, sizeof(submission));
  pass = "";

  String page = FPSTR(PAGE_HEAD);
  if (form.changeWifi) {
    page += "<p>Connecting to <b>";
    page += htmlEscape(formSsid);
    page += "</b>. Watch the dashboard screen for the result (up to 20 seconds).</p><p>This hotspot "
            "may drop briefly while the display tries the network. Reconnect to it and "
            "<a href=/>reload</a> to see the status.</p></body></html>";
  } else {
    page += "<p>Saving. Watch the dashboard screen; it restarts if the HomeDeck Backend "
            "changed. <a href=/>Reload</a> to see the status.</p></body></html>";
  }
  server.send(200, "text/html", page);
}

static void handleRedirect() {
  Serial.printf("[SETUP] %s %s -> redirect to the setup page\n", methodName(), server.uri().c_str());
  server.sendHeader("Location", String("http://") + WiFi.softAPIP().toString() + "/", true);
  server.send(302, "text/plain", "");
}

// Catch-all for captive-portal probes (e.g. iOS /hotspot-detect.html) and any
// other URI: redirects to the setup page. Registered after "/" and "/save",
// which therefore always match first (handlers are tried in order). As a
// real handler it also avoids the WebServer's "request handler not found"
// error log that onNotFound() requests produce.
class RedirectEverything : public RequestHandler {
public:
  bool canHandle(HTTPMethod, String) override { return true; }
  bool handle(WebServer&, HTTPMethod, String) override {
    handleRedirect();
    return true;
  }
};

static void setupTask(void*) {
  if (!routesRegistered) {
    server.on("/", HTTP_GET, handleRoot);
    server.on("/save", HTTP_POST, handleSave);
    server.addHandler(new RedirectEverything()); // Last: never shadows the routes above.
    routesRegistered = true;
  }
  dns.start(53, "*", WiFi.softAPIP());
  server.begin();
  // The first scan starts SETUP_SCAN_START_DELAY_MS after the hotspot, without blocking.
  scanBegin(scan, millis());
  Serial.printf("[SETUP] Wi-Fi mode: %s; AP active: %s (%s)\n", wifiModeText(),
                WiFi.getMode() & WIFI_MODE_AP ? "yes" : "no", WiFi.softAPIP().toString().c_str());
  while (!stopRequested) {
    dns.processNextRequest();
    server.handleClient();
    serviceScan();
    vTaskDelay(pdMS_TO_TICKS(5));
  }
  server.stop();
  dns.stop();
  if (scan.state == ScanState::Running) {
    // Stop the scan; the driver then frees its records (and the done event
    // copies whatever it found), so wait briefly for that before freeing.
    esp_wifi_scan_stop();
    for (int i = 0; i < 20 && !scanDoneEvent; i++) vTaskDelay(pdMS_TO_TICKS(10));
    scanStarted(scan, false, millis());
    scan.state = ScanState::Idle;
  }
  WiFi.scanDelete();
  taskRunning = false;
  vTaskDelete(nullptr);
}

// --- Main task API ---------------------------------------------------------------------

static void setPhase(WifiSetupPhase next) {
  phase = next;
  revision++;
}

static void makeApCredentials() {
  static const char alphabet[] = "abcdefghjkmnpqrstuvwxyz23456789"; // No 0/O/1/l/i.
  snprintf(apSsid, sizeof(apSsid), "%s", deviceIdentity().setupAp); // HomeDeck-Mini-XXXX
  for (int i = 0; i < 8; i++) apPassword[i] = alphabet[esp_random() % (sizeof(alphabet) - 1)];
  apPassword[8] = '\0';
}

// One GET <backend>/api/status with short timeouts (main task, blocks at most
// a few seconds). Informational only: the URL is saved either way.
static bool serverAnswers(const char* base) {
  char url[API_URL_MAX];
  if (!joinBackendUrl(url, sizeof(url), base, API_STATUS_PATH)) return false;
  WiFiClient client;
  HTTPClient http;
  http.setConnectTimeout(SERVER_CHECK_TIMEOUT_MS);
  http.setTimeout(SERVER_CHECK_TIMEOUT_MS);
  http.setReuse(false);
  http.useHTTP10(true);
  Serial.printf("[BACKEND] Checking %s\n", url);
  if (!http.begin(client, url)) return false;
  int code = http.GET();
  http.end();
  Serial.printf("[BACKEND] Check result: %d (%s)\n", code, code == HTTP_CODE_OK ? "answered" : "no answer");
  return code == HTTP_CODE_OK;
}

// The network is in place (joined, or kept): save the backend (an address,
// or "" to remove it) and restart, or report that nothing changed. Only
// device/backend is written here; Wi-Fi was saved by the switch itself.
static void applySubmission() {
  if (!setupNeedsRestart(firstRunMode, pendingBackendChanged)) {
    Serial.println("[SETUP] Nothing changed; no restart");
    setPhase(WifiSetupPhase::NoChange);
    return;
  }
  if (pendingBackendChanged) {
    bool saved = pendingBackend[0] ? saveBackendUrl(pendingBackend) : clearBackendUrl();
    if (!saved) {
      Serial.println("[BACKEND] URL NOT saved (NVS write failed); no restart");
      setPhase(WifiSetupPhase::SaveFailed);
      return;
    }
    if (pendingBackend[0]) Serial.printf("[BACKEND] URL saved: %s\n", pendingBackend);
    else Serial.println("[BACKEND] Removed; Standalone Mode after the restart");
  } else {
    Serial.println("[BACKEND] Unchanged");
  }
  serverCheck = !pendingBackend[0] || WiFi.status() != WL_CONNECTED ? ServerCheck::NotChecked
                : serverAnswers(pendingBackend) ? ServerCheck::Reachable : ServerCheck::NotReachable;
  if (pendingBackend[0] && serverCheck == ServerCheck::NotChecked) {
    Serial.println("[BACKEND] Check skipped: Wi-Fi not connected");
  }
  Serial.printf("[SETUP] Restarting in %lu s (%s)\n", (unsigned long)(RESTART_DELAY_MS / 1000),
                firstRunMode ? "first run" : "backend changed");
  restartAt = millis();
  setPhase(WifiSetupPhase::Restarting);
}

bool startWifiSetup(bool firstRun) {
  if (wifiSetupRunning()) return true;
  if (!submissions) submissions = xQueueCreate(1, sizeof(SetupSubmission));
  if (!scanEventRegistered) {
    // Runs in the Arduino event task after the WiFi library has copied the
    // results (WiFi.SSID(i) etc.); only flags are set here.
    WiFi.onEvent([](arduino_event_id_t, arduino_event_info_t info) {
      scanDoneOk = info.wifi_scan_done.status == 0;
      scanDoneEvent = true;
    }, ARDUINO_EVENT_WIFI_SCAN_DONE);
    scanEventRegistered = true;
  }
  firstRunMode = firstRun;
  haveNetwork = hasWifiNetwork();
  serverCheck = ServerCheck::NotChecked;
  restartIssued = false;
  makeApCredentials();
  setTarget("");
  formFilled = false; // The setup task is not running yet.
  WiFi.mode(WIFI_AP_STA);
  if (!submissions || !WiFi.softAP(apSsid, apPassword)) {
    WiFi.softAPdisconnect(true);
    setPhase(WifiSetupPhase::StartFailed);
    return false;
  }
  xQueueReset(submissions);
  stopRequested = false;
  taskRunning = true;
  if (xTaskCreate(setupTask, "wifisetup", SETUP_STACK_BYTES, nullptr, 1, nullptr) != pdPASS) {
    taskRunning = false;
    WiFi.softAPdisconnect(true);
    setPhase(WifiSetupPhase::StartFailed);
    return false;
  }
  lastActivity = lastRequestMs = millis();
  Serial.printf("[SETUP] Hotspot started: %s at http://%s/ (%s mode)\n", apSsid,
                WiFi.softAPIP().toString().c_str(), firstRun ? "first-run" : "settings");
  setPhase(WifiSetupPhase::Waiting);
  return true;
}

void stopWifiSetup() {
  if (!wifiSetupRunning() || phase == WifiSetupPhase::Restarting) return;
  stopRequested = true;
  setPhase(WifiSetupPhase::Stopping);
}

void updateWifiSetup() {
  WifiSetupPhase current = phase;
  if (current == WifiSetupPhase::Off || current == WifiSetupPhase::StartFailed) return;
  // The setup flow waits on the network switch it requested, so it drives
  // that switch itself (WiFi.begin, join check, timeout): Setup Mode's loop
  // does not run updateSettings(), which also calls this.
  updateWifiSwitch();

  if (current == WifiSetupPhase::Stopping) {
    if (taskRunning) return;
    WiFi.softAPdisconnect(true); // Back to STA only; the station link is kept.
    xQueueReset(submissions);
    Serial.println("Setup hotspot stopped");
    setPhase(WifiSetupPhase::Off);
    return;
  }

  if (current == WifiSetupPhase::Restarting) {
    if (!restartIssued && millis() - restartAt >= RESTART_DELAY_MS) {
      restartIssued = true;
      Serial.println("[SETUP] Restarting now");
      restartDevice();
    }
    return;
  }

  SetupSubmission submission;
  if (current != WifiSetupPhase::Connecting && xQueueReceive(submissions, &submission, 0) == pdTRUE) {
    // A kept backend (first run) stays exactly as stored, even an invalid one.
    SetupForm applied;
    applied.keepBackend = submission.keepBackend;
    memcpy(applied.backend, submission.backend, sizeof(applied.backend));
    pendingBackendChanged = setupBackendChanged(applied, backendUrl());
    snprintf(pendingBackend, sizeof(pendingBackend), "%s", submission.keepBackend ? backendUrl() : submission.backend);
    lastActivity = millis();
    Serial.printf("[SETUP] Submission: %s; backend %s; Wi-Fi mode %s, AP %s, %u client(s)\n",
                  submission.changeWifi ? "join a new network" : "keep the current network",
                  pendingBackendChanged ? "changed" : "unchanged",
                  WiFi.getMode() == WIFI_MODE_APSTA ? "AP+STA" : WiFi.getMode() == WIFI_MODE_AP ? "AP" : "STA",
                  WiFi.getMode() & WIFI_MODE_AP ? "up" : "off", unsigned(WiFi.softAPgetStationNum()));
    if (submission.changeWifi) {
      setTarget(submission.wifi.ssid);
      requestWifiSwitch(submission.wifi, WifiSource::Saved);
      setPhase(WifiSetupPhase::Connecting);
    } else {
      applySubmission();
    }
    memset(&submission, 0, sizeof(submission));
    return;
  }

  if (current == WifiSetupPhase::Connecting) {
    switch (setupAfterSwitch(wifiSwitchState(), firstRunMode, pendingBackendChanged)) {
      case SetupJoinOutcome::Apply:
        applySubmission();
        break;
      case SetupJoinOutcome::ConnectedOnly:
        Serial.println("[SETUP] Joined; server unchanged, no restart");
        connectedAt = millis();
        setPhase(WifiSetupPhase::Connected);
        break;
      case SetupJoinOutcome::Failed:
        Serial.println("[SETUP] Join failed; hotspot stays open for another try");
        lastActivity = millis();
        setPhase(WifiSetupPhase::Failed);
        break;
      case SetupJoinOutcome::Waiting:
        break;
    }
    return;
  }

  // Setup Mode stays open until it restarts; from Settings it closes itself.
  if (firstRunMode) return;
  if (current == WifiSetupPhase::Connected && millis() - connectedAt >= CLOSE_AFTER_SUCCESS_MS) {
    stopWifiSetup();
  } else if (wifiSetupIdleMs() >= SETUP_IDLE_TIMEOUT_MS) {
    stopWifiSetup();
  }
}

WifiSetupPhase wifiSetupPhase() {
  return phase;
}

bool wifiSetupRunning() {
  WifiSetupPhase current = phase;
  return current != WifiSetupPhase::Off && current != WifiSetupPhase::StartFailed;
}

const char* wifiSetupApSsid() {
  return apSsid;
}

const char* wifiSetupApPassword() {
  return apPassword;
}

const char* wifiSetupTarget() {
  return target; // Main task: only the main task writes it (via setTarget).
}

ServerCheck wifiSetupServerCheck() {
  return serverCheck;
}

uint32_t wifiSetupIdleMs() {
  unsigned long now = millis();
  uint32_t sinceSubmit = now - lastActivity;
  uint32_t sinceRequest = now - lastRequestMs;
  return sinceSubmit < sinceRequest ? sinceSubmit : sinceRequest;
}

uint32_t wifiSetupRevision() {
  return revision;
}
