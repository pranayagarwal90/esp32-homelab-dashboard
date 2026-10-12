// HomeDeck Mini identity and pull-update logic, plus source-level checks of
// the public/developer OTA split. Needs ArduinoJson (header-only) from a
// previous `pio run`; run from the repository root.
#include "BuildInfo.h"
#include "ProductIdentity.h"
#include "UpdateLogic.h"
#include "UpdateResponse.h"
#include <assert.h>
#include <fstream>
#include <sstream>
#include <stdio.h>
#include <string.h>
#include <string>

static const char* SHA = "6a0123456789abcdef0123456789abcdef0123456789abcdef0123456789abff";
static const uint32_t SLOT = 0x1E0000; // min_spiffs.csv app slot.

// --- Identity -------------------------------------------------------------------------

static void testIdentity() {
  const uint8_t mac[6] = {0x24, 0x0A, 0xC4, 0x12, 0x97, 0x9C};
  DeviceIdentity id;
  makeDeviceIdentity(mac, id);
  assert(strcmp(id.suffix, "979C") == 0);
  assert(strcmp(id.name, "HomeDeck Mini 979C") == 0);
  assert(strcmp(id.hostname, "homedeck-mini-979c") == 0);
  assert(strcmp(id.setupAp, "HomeDeck-Mini-979C") == 0);

  // Deterministic, zero-padded, from the last two bytes only.
  const uint8_t other[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x0A};
  makeDeviceIdentity(other, id);
  assert(strcmp(id.suffix, "000A") == 0 && strcmp(id.hostname, "homedeck-mini-000a") == 0);
  char suffix[DEVICE_SUFFIX_SIZE];
  formatDeviceSuffix(mac, suffix, sizeof(suffix));
  assert(strcmp(suffix, "979C") == 0);

  // The hostname is a valid DNS label: lower-case letters, digits, '-'.
  const uint8_t high[6] = {0, 0, 0, 0, 0xAB, 0xCD};
  makeDeviceIdentity(high, id);
  assert(strlen(id.hostname) <= 63);
  for (const char* p = id.hostname; *p; p++) assert((*p >= 'a' && *p <= 'z') || (*p >= '0' && *p <= '9') || *p == '-');
  assert(strlen(id.setupAp) <= 32); // Wi-Fi SSID limit.
  assert(strcmp(PRODUCT_SLUG, "homedeck-mini") == 0 && strcmp(PRODUCT_NAME, "HomeDeck Mini") == 0);
}

// --- Versions -------------------------------------------------------------------------

static void testVersions() {
  SemVer v;
  assert(parseSemVer("1.0.0", v) && v.major == 1 && v.minor == 0 && v.patch == 0);
  assert(parseSemVer("65535.65535.65535", v) && v.patch == 65535);
  assert(parseSemVer("0.0.0", v));
  const char* bad[] = {"", "1", "1.0", "1.0.0.0", "v1.0.0", "1.0.0-rc1", "01.0.0", "1.00.0", "1..0",
                       " 1.0.0", "1.0.0 ", "1.0.a", "65536.0.0", "123456.0.0", "-1.0.0", "1.0.", ".1.0"};
  for (const char* text : bad) assert(!parseSemVer(text, v));
  assert(!parseSemVer(nullptr, v));

  // Same -> no, older -> no, newer -> yes; numeric, not text, order.
  assert(!isNewerVersion("1.0.0", "1.0.0"));
  assert(!isNewerVersion("1.1.0", "1.0.9"));
  assert(!isNewerVersion("2.0.0", "1.99.99"));
  assert(isNewerVersion("1.0.0", "1.0.1"));
  assert(isNewerVersion("1.0.1", "1.1.0"));
  assert(isNewerVersion("1.1.0", "2.0.0"));
  assert(isNewerVersion("1.9.0", "1.10.0"));
  assert(!isNewerVersion("1.0.0", "garbage") && !isNewerVersion("garbage", "9.9.9"));
  assert(!isNewerVersion("1.0.0", "1.1.0-beta"));

  // The running firmware's version is a valid release version.
  assert(parseSemVer(DASHBOARD_FIRMWARE_VERSION, v));
}

// --- Offers and responses -------------------------------------------------------------------

static void testOfferValidation() {
  UpdateOffer offer;
  const char* url = "/firmware/homedeck-mini-1.1.0.bin";
  assert(validateUpdateOffer("1.0.0", "1.1.0", 1047421, SHA, url, "Notes", SLOT, offer) == OfferError::None);
  assert(strcmp(offer.version, "1.1.0") == 0 && offer.size == 1047421);
  assert(strcmp(offer.file, "homedeck-mini-1.1.0.bin") == 0 && strcmp(offer.notes, "Notes") == 0);
  assert(offer.sha256[0] == 0x6a && offer.sha256[31] == 0xff);

  assert(validateUpdateOffer("1.1.0", "1.1.0", 1, SHA, url, "", SLOT, offer) == OfferError::NotNewer);
  assert(validateUpdateOffer("1.2.0", "1.1.0", 1, SHA, url, "", SLOT, offer) == OfferError::NotNewer);
  assert(validateUpdateOffer("1.0.0", "1.1", 1, SHA, url, "", SLOT, offer) == OfferError::Version);
  assert(validateUpdateOffer("1.0.0", nullptr, 1, SHA, url, "", SLOT, offer) == OfferError::Version);
  assert(validateUpdateOffer("1.0.0", "1.1.0", 0, SHA, url, "", SLOT, offer) == OfferError::Size);
  assert(validateUpdateOffer("1.0.0", "1.1.0", SLOT + 1, SHA, url, "", SLOT, offer) == OfferError::TooLarge);
  assert(validateUpdateOffer("1.0.0", "1.1.0", SLOT, SHA, url, "", SLOT, offer) == OfferError::None);
  assert(validateUpdateOffer("1.0.0", "1.1.0", 1, "abc", url, "", SLOT, offer) == OfferError::Sha);
  std::string badHex(SHA);
  badHex[10] = 'g';
  assert(validateUpdateOffer("1.0.0", "1.1.0", 1, badHex.c_str(), url, "", SLOT, offer) == OfferError::Sha);
  // Only "/firmware/<plain name>" on the configured backend.
  const char* badUrls[] = {"http://evil.example/firmware/x.bin", "//evil.example/firmware/x.bin",
                           "/firmware/../x.bin", "/firmware/sub/x.bin", "/firmware/.x.bin", "/firmware/x.elf",
                           "/photos/x.bin", "/firmware/", "firmware/x.bin", "/firmware/x%2f.bin", nullptr};
  for (const char* bad : badUrls) {
    assert(validateUpdateOffer("1.0.0", "1.1.0", 1, SHA, bad, "", SLOT, offer) == OfferError::Url);
  }
  assert(validateUpdateOffer("1.0.0", "1.1.0", 1, SHA, url, nullptr, SLOT, offer) == OfferError::None);
  assert(offer.notes[0] == '\0');

  // Notes are signed: taken exactly (printable, at most 200) or refused,
  // never trimmed or rewritten.
  std::string notes(200, 'x');
  assert(validateUpdateOffer("1.0.0", "1.1.0", 1, SHA, url, notes.c_str(), SLOT, offer) == OfferError::None);
  assert(strcmp(offer.notes, notes.c_str()) == 0);
  notes += "x";
  assert(validateUpdateOffer("1.0.0", "1.1.0", 1, SHA, url, notes.c_str(), SLOT, offer) == OfferError::Notes);
  assert(validateUpdateOffer("1.0.0", "1.1.0", 1, SHA, url, "line\nbreak", SLOT, offer) == OfferError::Notes);
}

// Signature checks are covered by test_manifest_signature.cpp; here every
// signature "verifies" so the rest of the response handling is visible.
static const uint8_t TEST_KEY[65] = {0x04};
static const TrustedUpdateKey TEST_KEYS[] = {{"homedeck-test-a", TEST_KEY}};
static int acceptAll(const uint8_t*, const uint8_t*, size_t, const uint8_t*) {
  return 1;
}
static const char* SIGNED_FIELDS =
  "\"manifest_version\":2,\"product\":\"homedeck-mini\",\"filename\":\"homedeck-mini-1.1.0.bin\","
  "\"key_id\":\"homedeck-test-a\",\"signature\":\"AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA==\",";

static UpdateState parse(const char* body, UpdateOffer& offer, UpdateError& error, const char* current = "1.0.0") {
  SignatureCheck check;
  return parseUpdateResponse(body, strlen(body), current, SLOT, TEST_KEYS, 1, acceptAll, offer, error, check);
}

static void testResponses() {
  UpdateOffer offer;
  UpdateError error;
  assert(parse("{\"available\":false}", offer, error) == UpdateState::UpToDate && error == UpdateError::None);

  std::string ok = std::string("{\"available\":true,") + SIGNED_FIELDS + "\"version\":\"1.1.0\",\"size\":1047421,\"sha256\":\"" + SHA +
                   "\",\"notes\":\"Improved Wi-Fi setup and stability.\",\"url\":\"/firmware/homedeck-mini-1.1.0.bin\"}";
  assert(parse(ok.c_str(), offer, error) == UpdateState::Available && error == UpdateError::None);
  assert(strcmp(offer.version, "1.1.0") == 0 && strcmp(offer.notes, "Improved Wi-Fi setup and stability.") == 0);
  assert(strcmp(offer.keyId, "homedeck-test-a") == 0);

  // An older backend's unsigned answer is never an update.
  std::string unsignedBody = ok;
  unsignedBody.erase(unsignedBody.find(SIGNED_FIELDS), strlen(SIGNED_FIELDS));
  assert(parse(unsignedBody.c_str(), offer, error) == UpdateState::CheckFailed);
  assert(error == UpdateError::VerificationFailed && offer.version[0] == '\0');

  // A server offering the same or an older version is never believed.
  assert(parse(ok.c_str(), offer, error, "1.1.0") == UpdateState::UpToDate && offer.version[0] == '\0');
  assert(parse(ok.c_str(), offer, error, "2.0.0") == UpdateState::UpToDate);

  const char* invalid[] = {
    "", "not json", "[]", "{}", "{\"available\":\"yes\"}",
    "{\"available\":true}",
    "{\"available\":true,\"version\":\"1.1\",\"size\":10,\"sha256\":\"x\",\"url\":\"/firmware/a.bin\"}",
    "{\"available\":true,\"version\":\"1.1.0\",\"size\":-1,\"sha256\":\"x\",\"url\":\"/firmware/a.bin\"}",
    "{\"available\":true,\"version\":\"1.1.0\",\"size\":\"10\",\"sha256\":\"x\",\"url\":\"/firmware/a.bin\"}",
    "{\"available\":true,\"version\":\"1.1.0\",\"size\":10.5,\"sha256\":\"x\",\"url\":\"/firmware/a.bin\"}",
  };
  for (const char* body : invalid) {
    assert(parse(body, offer, error) == UpdateState::CheckFailed && error == UpdateError::BadResponse);
  }
  std::string badSha = ok;
  badSha.replace(badSha.find(SHA), 64, std::string(64, 'z'));
  assert(parse(badSha.c_str(), offer, error) == UpdateState::CheckFailed);
  std::string badUrl = ok;
  badUrl.replace(badUrl.find("/firmware/"), 10, "http://x/");
  assert(parse(badUrl.c_str(), offer, error) == UpdateState::CheckFailed);
  std::string tooLarge = ok;
  tooLarge.replace(tooLarge.find("1047421"), 7, "9999999");
  assert(parse(tooLarge.c_str(), offer, error) == UpdateState::CheckFailed && error == UpdateError::TooLarge);
  std::string numericNotes = ok;
  numericNotes.replace(numericNotes.find("\"Improved"), 36, "42");
  assert(parse(numericNotes.c_str(), offer, error) == UpdateState::CheckFailed);
  // Oversized answers are refused before parsing.
  std::string huge = "{\"available\":false,\"pad\":\"" + std::string(UPDATE_RESPONSE_MAX, 'x') + "\"}";
  assert(parse(huge.c_str(), offer, error) == UpdateState::CheckFailed);

  // HTTP status mapping.
  assert(checkStateForHttp(200, error) == UpdateState::Checking && error == UpdateError::None);
  assert(checkStateForHttp(404, error) == UpdateState::Unavailable && error == UpdateError::None);
  assert(checkStateForHttp(503, error) == UpdateState::CheckFailed && error == UpdateError::ServerError);
  assert(checkStateForHttp(-1, error) == UpdateState::CheckFailed && error == UpdateError::Unreachable);
}

// --- URLs -----------------------------------------------------------------------------------

static void testUrls() {
  char url[UPDATE_URL_MAX];
  assert(updateCheckUrl(url, sizeof(url), "http://192.0.2.10:8090", PRODUCT_SLUG, "1.0.0"));
  assert(strcmp(url, "http://192.0.2.10:8090/api/firmware/update?product=homedeck-mini&current=1.0.0") == 0);
  assert(firmwareDownloadUrl(url, sizeof(url), "http://server.local:8090", "homedeck-mini-1.1.0.bin"));
  assert(strcmp(url, "http://server.local:8090/firmware/homedeck-mini-1.1.0.bin") == 0);

  // No backend: no URL (nothing is ever compiled in).
  assert(!updateCheckUrl(url, sizeof(url), "", PRODUCT_SLUG, "1.0.0") && url[0] == '\0');
  assert(!firmwareDownloadUrl(url, sizeof(url), "", "a.bin") && url[0] == '\0');
  // Invalid parts are never sent.
  assert(!updateCheckUrl(url, sizeof(url), "http://h", PRODUCT_SLUG, "1.0") && url[0] == '\0');
  assert(!updateCheckUrl(url, sizeof(url), "http://h", "Home Deck", "1.0.0"));
  assert(!updateCheckUrl(url, sizeof(url), "http://h", "x&y=1", "1.0.0"));
  assert(!firmwareDownloadUrl(url, sizeof(url), "http://h", "../a.bin"));

  // The longest valid backend still fits both URLs.
  char longest[BACKEND_URL_MAX];
  memset(longest, 'a', sizeof(longest));
  memcpy(longest, "http://", 7);
  longest[BACKEND_URL_MAX - 1] = '\0';
  assert(updateCheckUrl(url, sizeof(url), longest, PRODUCT_SLUG, "65535.65535.65535"));
  std::string file = std::string(60, 'f') + ".bin";
  assert(validFirmwareFileName(file.c_str()));
  assert(firmwareDownloadUrl(url, sizeof(url), longest, file.c_str()));
  assert(!validFirmwareFileName((std::string(61, 'f') + ".bin").c_str()));
}

// --- Schedule, prompt and marker ------------------------------------------------------------

static void testSchedule() {
  UpdateSchedule s;
  updateScheduleBegin(s, 1000);
  assert(!updateCheckDue(s, 1000) && !updateCheckDue(s, 1000 + UPDATE_FIRST_CHECK_MS - 1));
  assert(updateCheckDue(s, 1000 + UPDATE_FIRST_CHECK_MS));
  updateCheckStarted(s, 40000);
  assert(!updateCheckDue(s, 40000 + UPDATE_CHECK_INTERVAL_MS - 1));
  assert(updateCheckDue(s, 40000 + UPDATE_CHECK_INTERVAL_MS));
  // A manual check restarts the interval.
  updateCheckStarted(s, 50000);
  assert(!updateCheckDue(s, 40000 + UPDATE_CHECK_INTERVAL_MS));
  // millis() wrap.
  updateCheckStarted(s, 0xFFFFFF00u);
  assert(!updateCheckDue(s, 0x100) && updateCheckDue(s, 0xFFFFFF00u + UPDATE_CHECK_INTERVAL_MS));

  // Never in Setup Mode, without Wi-Fi or a backend, or while busy.
  assert(updateCheckAllowed(false, true, true, UpdateState::Idle));
  assert(updateCheckAllowed(false, true, true, UpdateState::Failed));
  assert(!updateCheckAllowed(true, true, true, UpdateState::Idle));
  assert(!updateCheckAllowed(false, false, true, UpdateState::Idle));
  assert(!updateCheckAllowed(false, true, false, UpdateState::Idle));
  for (UpdateState busy : {UpdateState::Checking, UpdateState::Downloading, UpdateState::Installing,
                           UpdateState::Restarting}) {
    assert(updateBusy(busy) && !updateCheckAllowed(false, true, true, busy));
  }
  assert(!updateInstalling(UpdateState::Checking) && updateInstalling(UpdateState::Downloading));
  assert(updatePercent(0, 0) == 0 && updatePercent(50, 200) == 25 && updatePercent(300, 200) == 100);
  assert(updatePercent(1047420, 1047421) == 99 && updatePercent(1047421, 1047421) == 100);
}

static void testPrompt() {
  // Once per version per boot, only on calm pages, only when available.
  assert(updatePromptDue(UpdateState::Available, "1.1.0", "", true));
  assert(!updatePromptDue(UpdateState::Available, "1.1.0", "", false));
  assert(!updatePromptDue(UpdateState::Available, "1.1.0", "1.1.0", true)); // LATER (or shown) this boot.
  assert(updatePromptDue(UpdateState::Available, "1.2.0", "1.1.0", true));  // A newer release appears.
  assert(!updatePromptDue(UpdateState::UpToDate, "1.1.0", "", true));
  assert(!updatePromptDue(UpdateState::Failed, "1.1.0", "", true));
  assert(!updatePromptDue(UpdateState::Available, "", "", true));
  // Prompt buttons.
  assert(promptChoiceAt(80, 170) == PromptChoice::Install && promptChoiceAt(240, 170) == PromptChoice::Later);
  assert(promptChoiceAt(160, 170) == PromptChoice::None && promptChoiceAt(80, 120) == PromptChoice::None);
  assert(promptChoiceAt(80, 210) == PromptChoice::None); // The bottom bar is not a button here.
  assert(firmwareButtonAt(60, 170) == FirmwareButton::Check && firmwareButtonAt(230, 170) == FirmwareButton::Install);
  assert(firmwareButtonAt(160, 170) == FirmwareButton::None && firmwareButtonAt(60, 205) == FirmwareButton::None);
}

static void testPendingMarker() {
  assert(pendingUpdateOutcome(false, "", "1.0.0") == PendingUpdate::None);
  assert(pendingUpdateOutcome(true, "1.1.0", "1.1.0") == PendingUpdate::Succeeded);
  assert(pendingUpdateOutcome(true, "1.1.0", "1.0.0") == PendingUpdate::NotRunning); // Rolled back.
  assert(pendingUpdateOutcome(true, "", "1.0.0") == PendingUpdate::NotRunning);
}

static void testText() {
  char text[56];
  formatUpdateStatus(text, sizeof(text), UpdateState::UpToDate, "", UpdateError::None);
  assert(strcmp(text, "Up to date") == 0);
  formatUpdateStatus(text, sizeof(text), UpdateState::Available, "1.1.0", UpdateError::None);
  assert(strcmp(text, "Update available: 1.1.0") == 0);
  formatUpdateStatus(text, sizeof(text), UpdateState::Unavailable, "", UpdateError::None);
  assert(strcmp(text, "Updates unavailable") == 0);
  // Every message fits the Firmware page's status line (49 characters).
  for (int e = 0; e <= int(UpdateError::Busy); e++) {
    for (UpdateState s : {UpdateState::CheckFailed, UpdateState::Failed}) {
      formatUpdateStatus(text, sizeof(text), s, "", UpdateError(e));
      assert(strlen(text) <= 49);
    }
    if (e) assert(strlen(updateErrorText(UpdateError(e))) > 0);
  }
}

// --- Source checks ------------------------------------------------------------------------------

static std::string readFile(const char* name) {
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

static bool contains(const std::string& text, const char* needle) {
  return text.find(needle) != std::string::npos;
}

static void testSourceChecks() {
  // ArduinoOTA exists only inside the developer-only block.
  std::string ota = readFile("src/OtaManager.cpp");
  size_t guard = ota.find("#ifdef HOMEDECK_DEV_OTA");
  size_t elseBranch = ota.find("#else", guard);
  assert(guard != std::string::npos && elseBranch != std::string::npos);
  for (const char* needle : {"#include <ArduinoOTA.h>", "ArduinoOTA.begin()", "ArduinoOTA.handle()"}) {
    size_t at = ota.find(needle);
    assert(at != std::string::npos && at > guard && at < elseBranch);
    assert(ota.find(needle, elseBranch) == std::string::npos);
  }
  std::string publicBranch = ota.substr(elseBranch);
  assert(contains(publicBranch, "void setupOTA() {}") && contains(publicBranch, "return false;"));
  // No other file uses ArduinoOTA, and no shipped build config enables it.
  for (const char* name : {"src/main.cpp", "src/FirmwareUpdate.cpp", "src/UpdateScreen.cpp",
                           "src/SettingsScreen.cpp", "src/NetworkManager.cpp", "src/WifiProvisioning.cpp"}) {
    assert(!contains(readFile(name), "ArduinoOTA"));
  }
  assert(!contains(readFile("platformio.ini"), "HOMEDECK_DEV_OTA"));
  assert(contains(readFile("platformio_override.ini.example"), "-D HOMEDECK_DEV_OTA"));

  // Every update URL comes from the runtime backend (DeviceConfig), never a
  // compiled-in address.
  std::string update = readFile("src/FirmwareUpdate.cpp");
  assert(contains(update, "updateCheckUrl(url, sizeof(url), backendUrl(),"));
  assert(contains(update, "firmwareDownloadUrl(url, sizeof(url), backendUrl(),"));
  assert(!contains(update, "http://"));
  // The hash is checked before Update.end() activates the slot.
  size_t compare = update.find("memcmp(digest, offer.sha256");
  size_t activate = update.find("Update.end()");
  assert(compare != std::string::npos && activate != std::string::npos && compare < activate);
  assert(contains(update, "Update.abort()"));
  // Streamed: no buffer the size of the image.
  assert(contains(update, "uint8_t chunk[CHUNK_BYTES];") && !contains(update, "malloc(") &&
         !contains(update, "getString()"));

  // The device identity is used for the hotspot and the hostname.
  assert(contains(readFile("src/WifiProvisioning.cpp"), "deviceIdentity().setupAp"));
  assert(contains(readFile("src/NetworkManager.cpp"), "WiFi.setHostname(deviceIdentity().hostname)"));
  assert(!contains(readFile("src/WifiProvisioning.cpp"), "Homelab-Setup"));
  // The release tool's marker and the version source agree.
  assert(contains(readFile("src/BuildInfo.cpp"), "FIRMWARE_ID_PREFIX HOMEDECK_PRODUCT_SLUG \":\" DASHBOARD_FIRMWARE_VERSION"));
  assert(contains(readFile("../tools/prepare-firmware-release.py"), FIRMWARE_ID_PREFIX));
  // Installing owns the loop: nothing else runs.
  assert(contains(readFile("src/main.cpp"), "if (updateUpdateScreen()) return;"));
}

int main() {
  testIdentity();
  testVersions();
  testOfferValidation();
  testResponses();
  testUrls();
  testSchedule();
  testPrompt();
  testPendingMarker();
  testText();
  testSourceChecks();
  puts("update tests passed");
  return 0;
}
