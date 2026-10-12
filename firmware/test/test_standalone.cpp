// Standalone vs Full mode (BackendMode.h): mode decisions, what runs without a
// backend (status, alerts, updates), friendly copy, and source-level checks
// that the firmware gates every backend request on the configured state.
#include "AlertLogic.h"
#include "BackendMode.h"
#include "HomeLogic.h"
#include "ProvisioningLogic.h"
#include "UpdateLogic.h"
#include <assert.h>
#include <fstream>
#include <sstream>
#include <stdio.h>
#include <string.h>
#include <string>

typedef BackendConfig C;

static void testModes() {
  // Setup depends on Wi-Fi only.
  assert(setupRequired(false) && !setupRequired(true));
  assert(deviceMode(false, C::NotConfigured) == DeviceMode::Setup);
  assert(deviceMode(false, C::Configured) == DeviceMode::Setup);
  assert(deviceMode(false, C::Invalid) == DeviceMode::Setup);
  assert(deviceMode(true, C::NotConfigured) == DeviceMode::Standalone);
  assert(deviceMode(true, C::Invalid) == DeviceMode::Standalone);
  assert(deviceMode(true, C::Configured) == DeviceMode::Full);
  // Every backend request asks one question.
  assert(!backendRequestsAllowed(C::NotConfigured));
  assert(!backendRequestsAllowed(C::Invalid));
  assert(backendRequestsAllowed(C::Configured));
  // The boot reason never mentions a backend.
  assert(bootSetupReason(true, C::NotConfigured) == SetupReason::None);
  assert(bootSetupReason(true, C::Invalid) == SetupReason::None);
  assert(bootSetupReason(false, C::NotConfigured) == SetupReason::NoConfig);
  assert(bootSetupReason(false, C::Configured) == SetupReason::NoWifi);
  // One log line per mode, with the [MODE] tag.
  assert(strcmp(deviceModeLog(DeviceMode::Standalone, C::NotConfigured),
                "[MODE] Standalone - backend not configured") == 0);
  assert(strcmp(deviceModeLog(DeviceMode::Full, C::Configured), "[MODE] Full - backend configured") == 0);
  assert(strstr(deviceModeLog(DeviceMode::Setup, C::Configured), "backend kept"));
}

// Not configured is never "offline"; configured-but-unreachable is never
// "not configured".
static void testNotConfiguredIsNotOffline() {
  assert(backendIndicator(C::NotConfigured, false) == BackendIndicator::Hidden);
  assert(backendIndicator(C::Invalid, false) == BackendIndicator::Hidden);
  assert(backendIndicator(C::Configured, false) == BackendIndicator::Offline);
  assert(backendIndicator(C::Configured, true) == BackendIndicator::Live);
  assert(strcmp(backendStatusText(C::NotConfigured, false, true), "Not configured (optional)") == 0);
  assert(strcmp(backendStatusText(C::Configured, false, true), "Not responding") == 0);
  assert(strcmp(backendStatusText(C::Configured, false, false), "Unknown (no Wi-Fi)") == 0);
  assert(strcmp(backendStatusText(C::Configured, true, true), "Responding") == 0);
  assert(strcmp(backendStatusText(C::Invalid, false, true), "Saved address is not valid") == 0);
  assert(strcmp(homeHealthLabel(HomeHealth::Standalone), "STANDALONE") == 0);
}

// Ten minutes of the main loop's 10-second status schedule (StatusClient):
// a fetch happens only when backendRequestsAllowed(); an unreachable backend
// fails every fetch. Returns the active alerts at the end.
static int runStatusLoop(BackendConfig config, int& fetches) {
  AlertTable table;
  fetches = 0;
  for (uint32_t now = 0; now <= 600000; now += 10000) {
    if (!backendRequestsAllowed(config)) continue; // fetchHomelabStatus() returns early.
    fetches++;
    alertsFetchFailed(table, now);                   // Configured but unreachable.
  }
  return alertsActiveCount(table);
}

static void testNoAlertsWithoutBackend() {
  int fetches = 0;
  // Standalone: no request at all, so no failure, no stale alert, no service alerts.
  assert(runStatusLoop(C::NotConfigured, fetches) == 0 && fetches == 0);
  assert(runStatusLoop(C::Invalid, fetches) == 0 && fetches == 0);
  // A fresh table shows no health problem either.
  AlertTable fresh;
  assert(alertsActiveCount(fresh) == 0 && !fresh.haveStatus);
  // Configured but unreachable: still fetched, and data goes stale as before.
  assert(runStatusLoop(C::Configured, fetches) == 1 && fetches == 61);
}

static void testUpdaterSkipped() {
  // The automatic and the manual check both use updateCheckAllowed(); the
  // device passes backendConfigured() as hasBackend.
  assert(!updateCheckAllowed(false, true, backendRequestsAllowed(C::NotConfigured), UpdateState::Idle));
  assert(!updateCheckAllowed(false, true, backendRequestsAllowed(C::Invalid), UpdateState::Idle));
  assert(updateCheckAllowed(false, true, backendRequestsAllowed(C::Configured), UpdateState::Idle));
  // URLs are never built from an empty base.
  char url[UPDATE_URL_MAX];
  assert(!updateCheckUrl(url, sizeof(url), "", "homedeck-mini", "1.0.2") && url[0] == '\0');
  assert(!firmwareDownloadUrl(url, sizeof(url), "", "homedeck-mini-1.0.3.bin") && url[0] == '\0');
  char api[API_URL_MAX];
  assert(!joinBackendUrl(api, sizeof(api), "", API_STATUS_PATH) && api[0] == '\0');
  assert(!joinBackendUrl(api, sizeof(api), nullptr, API_STATUS_PATH) && api[0] == '\0');
}

static void testFriendlyCopy() {
  const char* forbidden[] = {"ERROR", "Error", "error", "API", "missing", "failed", "HTTP"};
  for (int f = int(BackendFeature::Services); f <= int(BackendFeature::Alerts); f++) {
    BackendNotice notice = backendNotice(BackendFeature(f));
    assert(strlen(notice.title) > 0 && strlen(notice.title) <= 22);  // Size 2 on 320 px.
    assert(strlen(notice.hint) > 0 && strlen(notice.hint) <= 48);    // Size 1.
    for (const char* word : forbidden) assert(!strstr(notice.title, word) && !strstr(notice.hint, word));
  }
  assert(strlen(BACKEND_SETUP_HINT) <= 48);
  assert(strcmp(backendNotice(BackendFeature::Updates).hint, "Update service not configured") == 0);
  assert(strcmp(backendNotice(BackendFeature::Ai).title, "AI not configured") == 0);
}

// --- Source-level checks -----------------------------------------------------------------

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

static bool contains(const std::string& text, const char* needle) {
  return text.find(needle) != std::string::npos;
}

// The gate comes before the work: position of the gate < position of the action.
static void assertGatedBefore(const std::string& body, const char* gate, const char* action) {
  size_t g = body.find(gate), a = body.find(action);
  assert(g != std::string::npos && a != std::string::npos && g < a);
}

static void testSetupIgnoresBackend() {
  std::string main = readSource("src/main.cpp");
  assert(contains(main, "bootSetupReason(hasWifiNetwork(), backendConfig())"));
  std::string logic = readSource("include/ProvisioningLogic.h");
  std::string reason = functionBody(logic, "inline SetupReason bootSetupReason(");
  assert(contains(reason, "if (!setupRequired(hasNetwork)) return SetupReason::None;"));
  assert(!contains(logic, "NoBackend") && !contains(logic, "BadBackend"));
  // The first-run page asks for Wi-Fi only (rendered HTML: test_setup_page).
  std::string portal = readSource("src/WifiProvisioning.cpp");
  assert(contains(functionBody(portal, "static void sendForm("), "view.firstRun = firstRunMode;"));
  assert(contains(functionBody(portal, "static void handleSave()"), "setupBackendArg(firstRunMode,"));
}

static void testPublicBuildHasNoBackendDefault() {
  std::string ini = readSource("platformio.ini");
  for (const char* flag : {"DASHBOARD_DEV_DEFAULTS", "DASHBOARD_DEV_BACKEND_URL", "HOMEDECK_DEV_OTA"}) {
    assert(!contains(ini, (std::string("-D ") + flag).c_str()));
  }
  std::string defaults = readSource("include/DevDefaults.h");
  assert(contains(defaults, "#if defined(DASHBOARD_DEV_DEFAULTS) && defined(DASHBOARD_DEV_BACKEND_URL)"));
  assert(contains(defaults, "constexpr const char* DEV_BACKEND_URL = nullptr;"));
}

static void testBackendRequestsAreGated() {
  // One source of truth, no raw empty-string checks elsewhere.
  std::string config = readSource("src/DeviceConfig.cpp");
  assert(contains(functionBody(config, "bool backendConfigured()"), "backendRequestsAllowed(config)"));
  assertGatedBefore(functionBody(config, "bool apiUrl("), "backendConfigured()", "joinBackendUrl");
  assertGatedBefore(functionBody(config, "bool photoUrl("), "backendConfigured()", "joinBackendUrl");
  for (const char* file : {"src/main.cpp", "src/StatusClient.cpp", "src/PhotoScreen.cpp", "src/AiAssistantScreen.cpp",
                           "src/FirmwareUpdate.cpp", "src/SettingsScreen.cpp", "src/WifiSettingsScreen.cpp",
                           "src/WifiProvisioning.cpp", "src/HomeScreen.cpp", "src/UiHelpers.cpp"}) {
    std::string source = readSource(file);
    assert(!contains(source, "hasBackendUrl") && !contains(source, "backendUrl()[0]"));
  }

  std::string main = readSource("src/main.cpp");
  assert(contains(main, "if (backendConfigured()) setupStatusWorker();"));
  std::string status = readSource("src/StatusClient.cpp");
  assertGatedBefore(functionBody(status, "void fetchHomelabStatus()"), "if (!backendConfigured())", "xQueueSend");
  assert(contains(functionBody(status, "bool statusRefreshDue()"), "backendConfigured() &&"));

  std::string photos = readSource("src/PhotoScreen.cpp");
  assertGatedBefore(functionBody(photos, "static void pumpPhotoRequests()"), "!backendConfigured()", "submitPhotoJob");
  assertGatedBefore(functionBody(photos, "bool requestScreensaverPhoto()"), "!backendConfigured()", "want(");
  assertGatedBefore(functionBody(photos, "void drawPhotosPage()"), "!backendConfigured()", "want(");

  std::string ai = readSource("src/AiAssistantScreen.cpp");
  assertGatedBefore(functionBody(ai, "void drawAiMenuPage()"), "!backendConfigured()", "submitAiJob");
  assertGatedBefore(functionBody(ai, "void openAiResult("), "!backendConfigured()", "session.ask");

  std::string update = readSource("src/FirmwareUpdate.cpp");
  assert(contains(functionBody(update, "bool requestUpdateCheck()"), "backendConfigured(), firmwareUpdateState()"));
  assert(contains(functionBody(update, "void serviceFirmwareUpdates()"), "backendConfigured(), firmwareUpdateState()"));
}

static void testSignedUpdatesUntouched() {
  // Verification does not know about modes: it is compiled the same in both.
  for (const char* file : {"src/UpdateVerifier.cpp", "include/UpdateVerifier.h", "include/ManifestSignature.h",
                           "include/TrustedUpdateKeys.h", "include/UpdateResponse.h", "include/UpdateLogic.h"}) {
    std::string source = readSource(file);
    assert(!contains(source, "BackendMode") && !contains(source, "backendConfigured") &&
           !contains(source, "DeviceConfig"));
  }
  // A configured backend still goes through the same verified path.
  std::string update = readSource("src/FirmwareUpdate.cpp");
  assert(contains(update, "TRUSTED_UPDATE_KEY_SLOTS, deviceEcdsaP256Verify, offer, error, check)"));
  assert(contains(update, "UpdateError::VerificationFailed"));
}

static void testWifiAndBackendStorageIndependent() {
  std::string config = readSource("src/DeviceConfig.cpp");
  assert(contains(config, "DEVICE_NAMESPACE = \"device\"") && contains(config, "BACKEND_KEY = \"backend\""));
  assert(!contains(config, "\"wifi\"") && !contains(config, "WifiCredentials"));
  std::string clear = functionBody(config, "bool clearBackendUrl()");
  assert(contains(clear, "putString(BACKEND_KEY, \"\")") && !contains(clear, "clear()") && !contains(clear, "wifi"));
  std::string store = readSource("src/SettingsStore.cpp");
  // (Its "device" is the display-settings blob key inside "settings", not the namespace.)
  assert(contains(store, "WIFI_NAMESPACE = \"wifi\"") && !contains(store, "DEVICE_NAMESPACE") &&
         !contains(store, "backend") && !contains(store, "BACKEND"));
  // The setup flow writes the backend only through DeviceConfig, only when it changed.
  std::string portal = readSource("src/WifiProvisioning.cpp");
  std::string apply = functionBody(portal, "static void applySubmission()");
  assertGatedBefore(apply, "if (pendingBackendChanged) {", "saveBackendUrl(pendingBackend) : clearBackendUrl()");
  assert(!contains(apply, "saveWifi") && !contains(apply, "Preferences"));
  // Removing the backend from Settings restarts without touching Wi-Fi.
  std::string settings = readSource("src/SettingsScreen.cpp");
  std::string run = functionBody(settings, "static void runConfirmed(");
  size_t remove = run.find("case ConfirmAction::RemoveBackend:");
  assert(remove != std::string::npos);
  std::string removeCase = run.substr(remove, run.find("break;", remove) - remove);
  assert(contains(removeCase, "clearBackendUrl()") && !contains(removeCase, "Wifi") &&
         !contains(removeCase, "wifi"));
}

int main() {
  testModes();
  testNotConfiguredIsNotOffline();
  testNoAlertsWithoutBackend();
  testUpdaterSkipped();
  testFriendlyCopy();
  testSetupIgnoresBackend();
  testPublicBuildHasNoBackendDefault();
  testBackendRequestsAreGated();
  testSignedUpdatesUntouched();
  testWifiAndBackendStorageIndependent();
  puts("standalone tests passed");
  return 0;
}
