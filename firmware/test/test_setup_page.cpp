// The setup page as the phone receives it (SetupPage.h rendered into a
// std::string), and what POST /save does with the backend in each context.
#include "SetupPage.h"
#include <assert.h>
#include <fstream>
#include <sstream>
#include <stdio.h>
#include <string.h>
#include <string>

static const ScannedNetwork NETWORKS[] = {{"Home", -50, false}, {"Cafe <5G>", -70, true}};

static std::string render(bool firstRun, bool haveNetwork = false, const char* backend = "") {
  SetupFormView view;
  view.firstRun = firstRun;
  view.haveNetwork = haveNetwork;
  view.networks = NETWORKS;
  view.networkCount = 2;
  view.backend = backend;
  std::string page;
  renderSetupForm(page, view);
  return page;
}

static bool has(const std::string& page, const char* text) {
  return page.find(text) != std::string::npos;
}

static std::string lower(std::string text) {
  for (char& c : text) c = c >= 'A' && c <= 'Z' ? char(c - 'A' + 'a') : c;
  return text;
}

// The <input ...> tag that carries name=<field>, or "".
static std::string inputTag(const std::string& page, const char* field) {
  size_t at = page.find(std::string("<input name=") + field + " ");
  if (at == std::string::npos) return "";
  return page.substr(at, page.find('>', at) - at + 1);
}

static void testFirstRunPageIsWifiOnly() {
  // Fresh device and Setup Mode with a backend already saved: identical page.
  for (bool haveNetwork : {false, true}) {
    std::string page = render(true, haveNetwork, "http://192.0.2.10:8090");
    std::string text = lower(page);
    // Wi-Fi: dropdown, typed name, password, Connect.
    assert(has(page, "<select name=net>") && has(page, "<option value=\"Home\">Home - strong</option>"));
    assert(has(page, "Cafe &lt;5G&gt; - good, open"));
    assert(!inputTag(page, "ssid").empty() && !inputTag(page, "pass").empty());
    assert(has(inputTag(page, "pass"), "type=password") && !has(inputTag(page, "pass"), "value="));
    assert(has(page, "<button>Connect</button>"));
    // Nothing about a backend, server or API.
    assert(inputTag(page, "server").empty() && !has(page, "name=server") && !has(page, "name=backend"));
    // ("api" as a word: "autocapitalize" contains the letters.)
    for (const char* word : {"dashboard server", "backend", " api", "/api", "api url", "server", "8090", "type=url",
                             "inputmode=url"}) {
      if (has(text, word)) fprintf(stderr, "first-run page contains \"%s\"\n", word);
      assert(!has(text, word));
    }
    // No field is required by the browser: an empty form submits and the
    // device answers with its own message.
    assert(!has(text, "required"));
    assert(!has(page, "192.0.2.10")); // The saved backend is not shown, so it cannot be edited here.
  }
}

static void testSettingsPageHasOptionalBackend() {
  std::string page = render(false, true, "http://192.0.2.10:8090");
  std::string field = inputTag(page, "server");
  assert(has(page, "HomeDeck Backend (optional)") && !field.empty());
  assert(!has(field, "required") && !has(lower(page), "required"));
  assert(has(field, "value=\"http://192.0.2.10:8090\""));
  assert(has(page, "Leave blank to use HomeDeck on its own."));
  assert(has(page, "<button>Save and connect</button>"));
  assert(!has(page, "Dashboard Server"));
  // Not configured: an empty value, still optional.
  field = inputTag(render(false, true, ""), "server");
  assert(has(field, "value=\"\"") && !has(field, "required"));
  // Re-filled values are escaped.
  assert(has(render(false, true, "\"><script>"), "value=\"&quot;&gt;&lt;script&gt;\""));
}

// --- POST /save -------------------------------------------------------------------------

struct Nvs {
  char wifi[33];
  char backend[BACKEND_URL_MAX];
};

// handleSave + applySubmission (after a successful join), with the real
// rules: setupBackendArg -> validateSetupForm -> setupBackendChanged.
static SetupFormError post(Nvs& nvs, bool firstRun, const char* ssid, const char* pass, bool fieldSent,
                           const char* server, bool& restart) {
  SetupForm form;
  const char* backend = setupBackendArg(firstRun, fieldSent, server);
  SetupFormError error = validateSetupForm(ssid, strlen(ssid), strlen(pass), backend, nvs.wifi[0] != '\0', form);
  restart = false;
  if (error != SetupFormError::None) return error;
  bool changed = setupBackendChanged(form, nvs.backend);
  if (form.changeWifi) snprintf(nvs.wifi, sizeof(nvs.wifi), "%s", ssid);
  if (changed) snprintf(nvs.backend, sizeof(nvs.backend), "%s", form.backend);
  restart = setupNeedsRestart(firstRun, changed);
  return error;
}

static void testPost() {
  const char* url = "http://192.0.2.10:8090";
  bool restart = false;
  // Fresh: SSID + password, no backend field -> accepted, Wi-Fi saved,
  // backend left unconfigured, restart into the dashboard (Standalone).
  Nvs nvs = {"", ""};
  assert(post(nvs, true, "Home", "password1", false, "", restart) == SetupFormError::None);
  assert(restart && strcmp(nvs.wifi, "Home") == 0 && nvs.backend[0] == '\0');
  // Fresh, open network (blank password) -> accepted too.
  nvs = Nvs{"", ""};
  assert(post(nvs, true, "Cafe", "", false, "", restart) == SetupFormError::None && restart);
  // First run ignores a backend value even if a client sends one.
  nvs = Nvs{"", ""};
  assert(post(nvs, true, "Home", "password1", true, "not a url", restart) == SetupFormError::None);
  assert(nvs.backend[0] == '\0');

  // Saved backend, Wi-Fi reconfigured in Setup Mode (no backend field) -> backend preserved.
  nvs = Nvs{"", ""};
  snprintf(nvs.backend, sizeof(nvs.backend), "%s", url);
  assert(post(nvs, true, "Office", "password1", false, "", restart) == SetupFormError::None);
  assert(restart && strcmp(nvs.wifi, "Office") == 0 && strcmp(nvs.backend, url) == 0);
  // From Settings, a request without the field also keeps it.
  assert(post(nvs, false, "Home", "password1", false, "", restart) == SetupFormError::None);
  assert(!restart && strcmp(nvs.wifi, "Home") == 0 && strcmp(nvs.backend, url) == 0);
  // From Settings, Wi-Fi changed and the pre-filled backend sent back -> kept, no restart.
  assert(post(nvs, false, "Office", "password1", true, url, restart) == SetupFormError::None);
  assert(!restart && strcmp(nvs.backend, url) == 0);

  // Settings: a valid backend -> saved, restart (Full Mode).
  assert(post(nvs, false, "", "", true, "http://192.0.2.20:8090/", restart) == SetupFormError::None);
  assert(restart && strcmp(nvs.backend, "http://192.0.2.20:8090") == 0 && strcmp(nvs.wifi, "Office") == 0);
  // Settings: an invalid backend -> rejected, nothing changes.
  assert(post(nvs, false, "", "", true, "http://x/api/status", restart) == SetupFormError::Backend);
  assert(strcmp(nvs.backend, "http://192.0.2.20:8090") == 0);
  // Settings: explicit blank -> cleared, restart (Standalone); Wi-Fi kept.
  assert(post(nvs, false, "", "", true, "", restart) == SetupFormError::None);
  assert(restart && nvs.backend[0] == '\0' && strcmp(nvs.wifi, "Office") == 0);
}

// --- The device uses this renderer and rule, and nothing else builds the form --------------

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

static void testDeviceUsesTheRenderer() {
  std::string portal = readSource("src/WifiProvisioning.cpp");
  // The Wi-Fi page is drawn for the portal mode (test_portal covers the routing).
  assert(has(portal, "renderPortalRoot(page, portalMode, view, LocationPageView());"));
  assert(has(portal, "setupBackendArg(firstRunMode, server.hasArg(\"server\"), backend.c_str())"));
  // No second copy of the form, and no old required server field anywhere.
  for (const char* text : {"<form", "name=server", "name=pass", "Dashboard Server", "required"}) {
    assert(!has(portal, text));
  }
}

int main() {
  testFirstRunPageIsWifiOnly();
  testSettingsPageHasOptionalBackend();
  testPost();
  testDeviceUsesTheRenderer();
  puts("setup page tests passed");
  return 0;
}
