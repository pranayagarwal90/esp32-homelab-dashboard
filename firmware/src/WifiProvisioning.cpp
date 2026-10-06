#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <freertos/queue.h>
#include "WifiProvisioning.h"
#include "NetworkManager.h"

static constexpr uint32_t SETUP_IDLE_TIMEOUT_MS = 10UL * 60UL * 1000UL;
static constexpr uint32_t CLOSE_AFTER_SUCCESS_MS = 20000;
static constexpr uint32_t SETUP_STACK_BYTES = 6144;
static constexpr int MAX_SCANNED = 12;

// --- Shared between the main task and the setup task --------------------------------

static volatile WifiSetupPhase phase = WifiSetupPhase::Off; // Written by main only.
static volatile bool stopRequested = false;                // Main -> task.
static volatile bool taskRunning = false;                  // Task -> main.
static QueueHandle_t submissions = nullptr;                 // Task -> main, 1 slot.
static portMUX_TYPE targetLock = portMUX_INITIALIZER_UNLOCKED;
static char target[33] = "";

// --- Main task only ------------------------------------------------------------------

static char apSsid[24] = "";
static char apPassword[12] = "";
static uint32_t revision = 0;
static unsigned long lastActivity = 0;
static unsigned long connectedAt = 0;

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
  "<title>Homelab Display Wi-Fi</title><style>"
  "body{font-family:sans-serif;max-width:420px;margin:1.5em auto;padding:0 1em}"
  "input,button{box-sizing:border-box;width:100%;padding:.6em;margin:.3em 0 .8em;font-size:1em}"
  "</style></head><body><h2>Homelab Display Wi-Fi</h2>";

static const char* phaseText() {
  switch (phase) {
    case WifiSetupPhase::Connecting: return "Connecting to ";
    case WifiSetupPhase::Connected: return "Connected to ";
    case WifiSetupPhase::Failed: return "Could not join ";
    default: return nullptr;
  }
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

static void handleRoot() {
  if (server.hasArg("rescan")) startScan();
  String page = FPSTR(PAGE_HEAD);
  const char* status = phaseText();
  if (status) {
    char ssid[33];
    copyTarget(ssid, sizeof(ssid));
    page += "<p><b>";
    page += status;
    page += htmlEscape(ssid);
    page += "</b></p>";
  }
  page += "<form method=post action=/save><label>Network name"
          "<input name=ssid list=nets required maxlength=32 autocomplete=off></label>"
          "<datalist id=nets>";
  for (int i = 0; i < scannedCount; i++) {
    page += "<option value=\"";
    page += htmlEscape(scanned[i]);
    page += "\">";
  }
  page += "</datalist><label>Password (blank for an open network)"
          "<input name=pass type=password maxlength=63></label>"
          "<button>Save and connect</button></form><p>";
  page += scanRunning ? "Scanning for networks..." : "<a href=/?rescan=1>Rescan networks</a>";
  page += "</p></body></html>";
  server.send(200, "text/html", page);
}

static void handleSave() {
  String ssid = server.arg("ssid");
  String pass = server.arg("pass");
  String page = FPSTR(PAGE_HEAD);
  if (!validWifiInput(ssid.c_str(), ssid.length(), pass.length())) {
    page += "<p>Network name must be 1-32 characters and the password 8-63 characters "
            "(or blank).</p><p><a href=/>Back</a></p></body></html>";
    server.send(400, "text/html", page);
    return;
  }
  WifiCredentials credentials;
  memset(&credentials, 0, sizeof(credentials));
  memcpy(credentials.ssid, ssid.c_str(), ssid.length());
  memcpy(credentials.password, pass.c_str(), pass.length());
  xQueueOverwrite(submissions, &credentials);
  memset(&credentials, 0, sizeof(credentials));
  page += "<p>Connecting to <b>";
  page += htmlEscape(ssid.c_str());
  page += "</b>. Watch the dashboard screen for the result.</p><p>This hotspot may drop "
          "briefly while the display changes networks. Reconnect to it and "
          "<a href=/>reload</a> to see the status.</p></body></html>";
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

bool startWifiSetup() {
  if (wifiSetupRunning()) return true;
  if (!submissions) submissions = xQueueCreate(1, sizeof(WifiCredentials));
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
  lastActivity = millis();
  Serial.printf("Wi-Fi setup hotspot started: %s\n", apSsid);
  setPhase(WifiSetupPhase::Waiting);
  return true;
}

void stopWifiSetup() {
  if (!wifiSetupRunning()) return;
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
    Serial.println("Wi-Fi setup hotspot stopped");
    setPhase(WifiSetupPhase::Off);
    return;
  }

  WifiCredentials credentials;
  if (current != WifiSetupPhase::Connecting && xQueueReceive(submissions, &credentials, 0) == pdTRUE) {
    setTarget(credentials.ssid);
    requestWifiSwitch(credentials, WifiSource::Saved);
    memset(&credentials, 0, sizeof(credentials));
    lastActivity = millis();
    setPhase(WifiSetupPhase::Connecting);
    return;
  }

  if (current == WifiSetupPhase::Connecting) {
    WifiSwitchState state = wifiSwitchState();
    if (state == WifiSwitchState::Connected) {
      connectedAt = millis();
      setPhase(WifiSetupPhase::Connected);
    } else if (state == WifiSwitchState::Failed) {
      lastActivity = millis();
      setPhase(WifiSetupPhase::Failed);
    }
    return;
  }

  if (current == WifiSetupPhase::Connected && millis() - connectedAt >= CLOSE_AFTER_SUCCESS_MS) {
    stopWifiSetup();
  } else if (millis() - lastActivity >= SETUP_IDLE_TIMEOUT_MS) {
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

uint32_t wifiSetupRevision() {
  return revision;
}
