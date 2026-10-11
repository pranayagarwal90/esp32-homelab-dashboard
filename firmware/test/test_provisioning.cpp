// Boot decisions (Setup Mode vs dashboard), backend URL resolution from NVS
// including the developer migration seed, the setup form and the retry rule.
#include "ProvisioningLogic.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

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

int main() {
  testBootStates();
  testBackendBootAction();
  testSetupForm();
  testRestartAndRetry();
  puts("provisioning tests passed");
  return 0;
}
