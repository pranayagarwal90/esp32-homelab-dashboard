#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <HTTPClient.h>
#include <freertos/queue.h>
#include "WifiProvisioning.h"
#include "DeviceConfig.h"
#include "NetworkManager.h"
#include "PowerManager.h"
#include "ProvisioningLogic.h"

static constexpr uint32_t SETUP_IDLE_TIMEOUT_MS = 10UL * 60UL * 1000UL;
static constexpr uint32_t CLOSE_AFTER_SUCCESS_MS = 20000;
static constexpr uint32_t RESTART_DELAY_MS = 8000;      // Time to read the result.
static constexpr uint16_t SERVER_CHECK_TIMEOUT_MS = 3000;
static constexpr uint32_t SETUP_STACK_BYTES = 6144;
static constexpr int MAX_SCANNED = 12;

// A validated submission (setup task -> main task, one queue slot).
struct SetupSubmission {
  WifiCredentials wifi;
  bool changeWifi;                 // False: keep the current network.
  char backend[BACKEND_URL_MAX];   // Normalized.
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
static char scanned[MAX_SCANNED][33];
static int scannedCount = 0;
static bool scanRunning = false;

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
  "<title>Homelab Display Setup</title><style>"
  "body{font-family:sans-serif;max-width:420px;margin:1.5em auto;padding:0 1em}"
  "input,button{box-sizing:border-box;width:100%;padding:.6em;margin:.3em 0 .8em;font-size:1em}"
  "small{color:#555}.err{color:#b00}"
  "</style></head><body><h2>Homelab Display Setup</h2>";

static const char* serverCheckText() {
  switch (serverCheck) {
    case ServerCheck::Reachable: return " The dashboard server answered.";
    case ServerCheck::NotReachable:
      return " The dashboard server did not answer; it is saved anyway (check it in Settings).";
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
    case WifiSetupPhase::Failed: page += "<p class=err><b>Could not join "; break;
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

static void startScan() {
  if (WiFi.scanNetworks(true, false) == WIFI_SCAN_RUNNING) scanRunning = true;
}

static void collectScan() {
  if (!scanRunning) return;
  int16_t found = WiFi.scanComplete();
  if (found == WIFI_SCAN_RUNNING) return;
  scanRunning = false;
  scannedCount = 0;
  for (int i = 0; i < found && scannedCount < MAX_SCANNED; i++) {
    String ssid = WiFi.SSID(i);
    if (ssid.length() == 0 || ssid.length() > 32) continue;
    bool duplicate = false;
    for (int j = 0; j < scannedCount; j++) duplicate |= ssid == scanned[j];
    if (duplicate) continue;
    strcpy(scanned[scannedCount++], ssid.c_str());
  }
  WiFi.scanDelete();
}

// The form; error, ssid and backend re-fill it after a rejected submission
// (the password is never echoed).
static void sendForm(int code, const char* error, const char* ssid, const char* backend) {
  String page = FPSTR(PAGE_HEAD);
  appendStatus(page);
  if (error && error[0]) {
    page += "<p class=err><b>";
    page += htmlEscape(error);
    page += "</b></p>";
  }
  page += "<form method=post action=/save><label>Wi-Fi network name"
          "<input name=ssid list=nets maxlength=32 autocomplete=off value=\"";
  page += htmlEscape(ssid);
  page += haveNetwork ? "\" placeholder=\"Leave blank to keep the current network\">" : "\" required>";
  page += "</label><datalist id=nets>";
  for (int i = 0; i < scannedCount; i++) {
    page += "<option value=\"";
    page += htmlEscape(scanned[i]);
    page += "\">";
  }
  page += "</datalist><label>Wi-Fi password (blank for an open network)"
          "<input name=pass type=password maxlength=63></label>"
          "<label>Dashboard Server<input name=server required maxlength=127 inputmode=url "
          "autocapitalize=off autocorrect=off spellcheck=false placeholder=\"http://192.168.1.50:8090\" value=\"";
  page += htmlEscape(backend);
  page += "\"></label><small>Enter the address of the Homelab Dashboard backend, "
          "for example http://192.168.1.50:8090 (no /api/status).</small>"
          "<button>Save and connect</button></form><p>";
  page += scanRunning ? "Scanning for networks..." : "<a href=/?rescan=1>Rescan networks</a>";
  page += "</p></body></html>";
  server.send(code, "text/html", page);
}

static void handleRoot() {
  lastRequestMs = millis();
  if (server.hasArg("rescan")) startScan();
  sendForm(200, nullptr, "", backendUrl());
}

static void handleSave() {
  lastRequestMs = millis();
  String ssid = server.arg("ssid");
  String pass = server.arg("pass");
  String backend = server.arg("server");
  SetupForm form;
  SetupFormError error = validateSetupForm(ssid.c_str(), ssid.length(), pass.length(), backend.c_str(),
                                           haveNetwork, form);
  if (error != SetupFormError::None) {
    pass = "";
    sendForm(400, setupFormErrorText(error, form.backendError), ssid.c_str(), backend.c_str());
    return;
  }
  SetupSubmission submission;
  memset(&submission, 0, sizeof(submission));
  submission.changeWifi = form.changeWifi;
  if (form.changeWifi) {
    memcpy(submission.wifi.ssid, ssid.c_str(), ssid.length());
    memcpy(submission.wifi.password, pass.c_str(), pass.length());
  }
  memcpy(submission.backend, form.backend, sizeof(submission.backend));
  xQueueOverwrite(submissions, &submission);
  memset(&submission, 0, sizeof(submission));
  pass = "";

  String page = FPSTR(PAGE_HEAD);
  if (form.changeWifi) {
    page += "<p>Connecting to <b>";
    page += htmlEscape(ssid.c_str());
    page += "</b>. Watch the dashboard screen for the result.</p><p>This hotspot may drop "
            "briefly while the display changes networks. Reconnect to it and "
            "<a href=/>reload</a> to see the status.</p></body></html>";
  } else {
    page += "<p>Saving. Watch the dashboard screen; it restarts if the dashboard server "
            "changed. <a href=/>Reload</a> to see the status.</p></body></html>";
  }
  server.send(200, "text/html", page);
}

static void handleRedirect() {
  server.sendHeader("Location", String("http://") + WiFi.softAPIP().toString() + "/", true);
  server.send(302, "text/plain", "");
}

static void setupTask(void*) {
  if (!routesRegistered) {
    server.on("/", HTTP_GET, handleRoot);
    server.on("/save", HTTP_POST, handleSave);
    server.onNotFound(handleRedirect);
    routesRegistered = true;
  }
  dns.start(53, "*", WiFi.softAPIP());
  server.begin();
  startScan();
  while (!stopRequested) {
    dns.processNextRequest();
    server.handleClient();
    collectScan();
    vTaskDelay(pdMS_TO_TICKS(5));
  }
  server.stop();
  dns.stop();
  if (scanRunning) {
    // Let an in-flight scan finish before freeing its results.
    for (int i = 0; i < 100 && WiFi.scanComplete() == WIFI_SCAN_RUNNING; i++) vTaskDelay(pdMS_TO_TICKS(50));
    WiFi.scanDelete();
    scanRunning = false;
  }
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
  uint8_t mac[6];
  WiFi.macAddress(mac);
  snprintf(apSsid, sizeof(apSsid), "Homelab-Setup-%02X%02X", mac[4], mac[5]);
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
  if (!http.begin(client, url)) return false;
  int code = http.GET();
  http.end();
  return code == HTTP_CODE_OK;
}

// The network is in place (joined, or kept): save the backend and restart,
// or report that nothing changed.
static void applySubmission() {
  if (!setupNeedsRestart(firstRunMode, pendingBackendChanged)) {
    setPhase(WifiSetupPhase::NoChange);
    return;
  }
  if ((pendingBackendChanged || !hasBackendUrl()) && !saveBackendUrl(pendingBackend)) {
    Serial.println("Could not store the backend URL");
    setPhase(WifiSetupPhase::SaveFailed);
    return;
  }
  serverCheck = WiFi.status() != WL_CONNECTED ? ServerCheck::NotChecked
                : serverAnswers(pendingBackend) ? ServerCheck::Reachable : ServerCheck::NotReachable;
  restartAt = millis();
  setPhase(WifiSetupPhase::Restarting);
}

bool startWifiSetup(bool firstRun) {
  if (wifiSetupRunning()) return true;
  if (!submissions) submissions = xQueueCreate(1, sizeof(SetupSubmission));
  firstRunMode = firstRun;
  haveNetwork = hasWifiNetwork();
  serverCheck = ServerCheck::NotChecked;
  restartIssued = false;
  makeApCredentials();
  setTarget("");
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
  Serial.printf("Setup hotspot started: %s\n", apSsid);
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
      restartDevice();
    }
    return;
  }

  SetupSubmission submission;
  if (current != WifiSetupPhase::Connecting && xQueueReceive(submissions, &submission, 0) == pdTRUE) {
    memcpy(pendingBackend, submission.backend, sizeof(pendingBackend));
    pendingBackendChanged = strcmp(pendingBackend, backendUrl()) != 0;
    lastActivity = millis();
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
    WifiSwitchState state = wifiSwitchState();
    if (state == WifiSwitchState::Connected) {
      if (setupNeedsRestart(firstRunMode, pendingBackendChanged)) {
        applySubmission();
      } else {
        connectedAt = millis();
        setPhase(WifiSetupPhase::Connected);
      }
    } else if (state == WifiSwitchState::Failed) {
      lastActivity = millis();
      setPhase(WifiSetupPhase::Failed);
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
