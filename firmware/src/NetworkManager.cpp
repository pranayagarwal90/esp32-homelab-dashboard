#include <Arduino.h>
#include <WiFi.h>
#include <freertos/semphr.h>
#include "NetworkManager.h"
#include "DevDefaults.h"
#include "ProvisioningLogic.h"
#include "SystemAnimation.h"

static constexpr uint32_t SWITCH_TIMEOUT_MS = 20000;

// Serializes WiFi.disconnect()/begin() between the status worker's reconnect
// and the main task's network switch. The main task only ever try-takes it.
static SemaphoreHandle_t wifiControl = nullptr;

// Active credentials are read by the worker and replaced by the main task.
static portMUX_TYPE credentialsLock = portMUX_INITIALIZER_UNLOCKED;
static WifiCredentials activeCredentials;
static WifiSource activeSource = WifiSource::BuiltIn;
static WifiCredentials savedCredentials;
static bool savedAvailable = false;
static uint8_t reconnectFailures = 0; // Status worker only.

static WifiSwitchState switchState = WifiSwitchState::Idle;
static WifiCredentials switchTarget;
static WifiSource switchSource = WifiSource::BuiltIn;
static WifiCredentials switchPrevious;
static WifiSource switchPreviousSource = WifiSource::BuiltIn;
static unsigned long switchStarted = 0;
static volatile bool gotIp = false;

bool builtInWifiCredentials(WifiCredentials& out) {
  memset(&out, 0, sizeof(out));
  if (!DEV_BUILTIN_WIFI) return false;
  strncpy(out.ssid, DEV_WIFI_SSID, sizeof(out.ssid) - 1);
  strncpy(out.password, DEV_WIFI_PASSWORD, sizeof(out.password) - 1);
  return out.ssid[0] != '\0';
}

bool hasBuiltInWifi() {
  return DEV_BUILTIN_WIFI && DEV_WIFI_SSID[0] != '\0';
}

bool hasWifiNetwork() {
  return savedAvailable || hasBuiltInWifi();
}

static void credentialsFor(WifiSource source, WifiCredentials& out) {
  portENTER_CRITICAL(&credentialsLock);
  bool saved = source == WifiSource::Saved && savedAvailable;
  if (saved) out = savedCredentials;
  portEXIT_CRITICAL(&credentialsLock);
  if (!saved) builtInWifiCredentials(out);
}

static void setActive(const WifiCredentials& credentials, WifiSource source) {
  portENTER_CRITICAL(&credentialsLock);
  activeCredentials = credentials;
  activeSource = source;
  portEXIT_CRITICAL(&credentialsLock);
}

WifiSource activeWifiSource() {
  portENTER_CRITICAL(&credentialsLock);
  WifiSource source = activeSource;
  portEXIT_CRITICAL(&credentialsLock);
  return source;
}

void initNetwork() {
  if (!wifiControl) wifiControl = xSemaphoreCreateMutex();
  savedAvailable = loadSavedWifi(savedCredentials);
  WiFi.onEvent([](arduino_event_id_t, arduino_event_info_t) { gotIp = true; },
               ARDUINO_EVENT_WIFI_STA_GOT_IP);
  WiFi.mode(WIFI_STA);
}

bool connectWiFi() {
  if (!hasWifiNetwork()) return false;
  // Boot (or wake) animation, then Wi-Fi arcs; drawn while connecting.
  startupAnimationBegin();

  WifiSource source = savedAvailable ? WifiSource::Saved : WifiSource::BuiltIn;
  WifiCredentials credentials;
  bool connected = false;
  for (uint8_t attempt = 0; attempt < BOOT_CONNECT_ATTEMPTS && !connected; attempt++) {
    credentialsFor(source, credentials);
    startupAnimationSetNetwork(credentials.ssid);
    WiFi.begin(credentials.ssid, credentials.password);
    unsigned long start = millis(), lastDot = start;
    // Polled every 20 ms (yielding) so the animation can run.
    while (WiFi.status() != WL_CONNECTED && millis() - start < BOOT_ATTEMPT_MS) {
      startupAnimationService();
      vTaskDelay(pdMS_TO_TICKS(20));
      if (millis() - lastDot >= 500) {
        lastDot = millis();
        Serial.print(".");
      }
    }
    connected = WiFi.status() == WL_CONNECTED;
    if (connected) break;
    startupAnimationAttemptFailed();
    WiFi.disconnect();
    // Alternate when both a saved and a built-in network exist.
    if (savedAvailable && hasBuiltInWifi()) {
      source = source == WifiSource::Saved ? WifiSource::BuiltIn : WifiSource::Saved;
    }
  }
  Serial.println();
  if (!connected) {
    // Stop the station so the setup hotspot keeps a stable channel.
    memset(&credentials, 0, sizeof(credentials));
    WiFi.disconnect();
    systemAnimationCancel();
    Serial.println("Wi-Fi: no network joined; starting setup");
    return false;
  }
  setActive(credentials, source);
  memset(&credentials, 0, sizeof(credentials));
  WiFi.setSleep(false);
  Serial.print("ESP32 IP: ");
  Serial.println(WiFi.localIP());
  startupAnimationFinish();
  return true;
}

bool ensureWiFiConnected(uint32_t timeoutMs) {
  if (WiFi.status() == WL_CONNECTED) {
    reconnectFailures = 0;
    return true;
  }
  if (wifiControl && xSemaphoreTake(wifiControl, 0) != pdTRUE) return false;

  WifiSource source = reconnectSource(activeWifiSource(), savedAvailable, hasBuiltInWifi(), reconnectFailures);
  WifiCredentials credentials;
  credentialsFor(source, credentials);
  WiFi.disconnect();
  WiFi.begin(credentials.ssid, credentials.password);
  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < timeoutMs) {
    vTaskDelay(pdMS_TO_TICKS(250));
  }
  bool connected = WiFi.status() == WL_CONNECTED;
  if (connected) {
    setActive(credentials, source);
    reconnectFailures = 0;
  } else if (reconnectFailures < 255) {
    reconnectFailures++;
  }
  if (wifiControl) xSemaphoreGive(wifiControl);
  return connected;
}

void requestWifiSwitch(const WifiCredentials& target, WifiSource targetSource) {
  if (switchState == WifiSwitchState::Connecting) return; // One switch at a time.
  switchTarget = target;
  switchSource = targetSource;
  switchState = WifiSwitchState::Pending;
}

static bool beginWifiSwitch() {
  if (!wifiControl || xSemaphoreTake(wifiControl, 0) != pdTRUE) return false;
  portENTER_CRITICAL(&credentialsLock);
  switchPrevious = activeCredentials;
  switchPreviousSource = activeSource;
  portEXIT_CRITICAL(&credentialsLock);
  // STA only: a running setup hotspot (AP) stays up.
  WiFi.disconnect(false, false);
  gotIp = false;
  WiFi.begin(switchTarget.ssid, switchTarget.password);
  switchStarted = millis();
  switchState = WifiSwitchState::Connecting;
  return true;
}

WifiSwitchState updateWifiSwitch() {
  if (switchState == WifiSwitchState::Pending) beginWifiSwitch();
  if (switchState != WifiSwitchState::Connecting) return switchState;
  bool joined = gotIp && WiFi.status() == WL_CONNECTED && WiFi.SSID() == switchTarget.ssid;
  if (joined) {
    if (switchSource == WifiSource::Saved) {
      if (!saveWifi(switchTarget)) Serial.println("Could not store Wi-Fi network");
      portENTER_CRITICAL(&credentialsLock);
      savedCredentials = switchTarget;
      savedAvailable = true;
      portEXIT_CRITICAL(&credentialsLock);
    } else {
      forgetSavedWifi();
      portENTER_CRITICAL(&credentialsLock);
      savedAvailable = false;
      portEXIT_CRITICAL(&credentialsLock);
    }
    setActive(switchTarget, switchSource);
    WiFi.setSleep(false);
    switchState = WifiSwitchState::Connected;
    xSemaphoreGive(wifiControl);
    return switchState;
  }
  if (millis() - switchStarted < SWITCH_TIMEOUT_MS) return switchState;

  // Failed: restore the previous network (if any, e.g. not on first run);
  // the status worker keeps retrying it.
  WiFi.disconnect(false, false);
  if (switchPrevious.ssid[0]) WiFi.begin(switchPrevious.ssid, switchPrevious.password);
  setActive(switchPrevious, switchPreviousSource);
  switchState = WifiSwitchState::Failed;
  xSemaphoreGive(wifiControl);
  return switchState;
}

WifiSwitchState wifiSwitchState() {
  return switchState;
}
