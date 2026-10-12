#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <Update.h>
#include <esp_ota_ops.h>
#include <mbedtls/sha256.h>
#include "FirmwareUpdate.h"
#include "BuildInfo.h"
#include "DeviceConfig.h"
#include "NetworkManager.h"
#include "ProductIdentity.h"
#include "SettingsStore.h"
#include "SetupScreen.h"
#include "UpdateResponse.h"
#include "WifiProvisioning.h"

static const char* UPDATE_NAMESPACE = "update";
static const char* PENDING_KEY = "pending";

static constexpr uint32_t CHECK_TIMEOUT_MS = 8000;
static constexpr uint32_t DOWNLOAD_CONNECT_MS = 10000;
static constexpr uint32_t DOWNLOAD_STALL_MS = 15000;   // No bytes for this long: give up.
static constexpr uint32_t DOWNLOAD_TOTAL_MS = 300000;  // Whole image (about 1 MB on a LAN: seconds).
static constexpr uint32_t UPDATE_STACK_BYTES = 8192;
static constexpr size_t CHUNK_BYTES = 1024;

enum class Job : uint8_t { Check, Install };

// Shared with the update task; every access holds the lock.
static portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;
static UpdateStatus status = {UpdateState::Idle, UpdateError::None, 0, false, 0, {}};

static UpdateSchedule schedule; // Main task only.

static void publish(UpdateState state, UpdateError error, uint8_t percent = 0) {
  portENTER_CRITICAL(&lock);
  status.state = state;
  status.error = error;
  status.percent = percent;
  status.revision++;
  portEXIT_CRITICAL(&lock);
}

static void publishOffer(UpdateState state, UpdateError error, const UpdateOffer* offer) {
  portENTER_CRITICAL(&lock);
  status.state = state;
  status.error = error;
  status.percent = 0;
  status.hasOffer = offer != nullptr;
  if (offer) status.offer = *offer;
  else memset(&status.offer, 0, sizeof(status.offer));
  status.revision++;
  portEXIT_CRITICAL(&lock);
}

void firmwareUpdateStatus(UpdateStatus& out) {
  portENTER_CRITICAL(&lock);
  out = status;
  portEXIT_CRITICAL(&lock);
}

UpdateState firmwareUpdateState() {
  portENTER_CRITICAL(&lock);
  UpdateState state = status.state;
  portEXIT_CRITICAL(&lock);
  return state;
}

static uint32_t otaSlotSize() {
  const esp_partition_t* slot = esp_ota_get_next_update_partition(nullptr);
  return slot ? slot->size : 0;
}

// --- Check (update task) ------------------------------------------------------------

// Reads exactly length bytes (Content-Length) within the deadline.
static bool readBody(WiFiClient& client, char* body, int length, uint32_t deadlineMs) {
  int total = 0;
  unsigned long started = millis();
  while (total < length && millis() - started < deadlineMs) {
    int available = client.available();
    if (available > 0) {
      int read = client.read((uint8_t*)body + total, min(available, length - total));
      if (read > 0) total += read;
    } else if (!client.connected()) {
      break;
    } else {
      vTaskDelay(pdMS_TO_TICKS(5));
    }
  }
  body[total] = '\0';
  return total == length;
}

static void runCheck() {
  char url[UPDATE_URL_MAX];
  if (WiFi.status() != WL_CONNECTED) {
    publishOffer(UpdateState::CheckFailed, UpdateError::NoWifi, nullptr);
    return;
  }
  if (!updateCheckUrl(url, sizeof(url), backendUrl(), PRODUCT_SLUG, firmwareVersion())) {
    publishOffer(UpdateState::Unavailable, UpdateError::NoServer, nullptr);
    return;
  }
  Serial.printf("[UPDATE] Checking %s\n", url);
  WiFiClient client;
  HTTPClient http;
  http.setConnectTimeout(CHECK_TIMEOUT_MS);
  http.setTimeout(CHECK_TIMEOUT_MS);
  http.setReuse(false);
  http.useHTTP10(true); // Identity body with a Content-Length.
  if (!http.begin(client, url)) {
    publishOffer(UpdateState::CheckFailed, UpdateError::Unreachable, nullptr);
    return;
  }
  int code = http.GET();
  UpdateError error;
  UpdateState state = checkStateForHttp(code, error);
  if (state != UpdateState::Checking) {
    http.end();
    Serial.printf("[UPDATE] Check: HTTP %d -> %s\n", code,
                  state == UpdateState::Unavailable ? "updates unavailable on this server" : updateErrorText(error));
    publishOffer(state, error, nullptr);
    return;
  }
  int length = http.getSize();
  char body[UPDATE_RESPONSE_MAX + 1];
  bool complete = length > 0 && length <= (int)UPDATE_RESPONSE_MAX &&
                  readBody(client, body, length, CHECK_TIMEOUT_MS);
  http.end();
  UpdateOffer offer;
  state = complete ? parseUpdateResponse(body, length, firmwareVersion(), otaSlotSize(), offer, error)
                   : UpdateState::CheckFailed;
  if (!complete) error = UpdateError::BadResponse;
  if (state == UpdateState::Available) {
    Serial.printf("[UPDATE] Update available: %s (%u bytes; running %s)\n", offer.version,
                  unsigned(offer.size), firmwareVersion());
    publishOffer(state, error, &offer);
  } else {
    Serial.printf("[UPDATE] Check: %s\n", state == UpdateState::UpToDate ? "up to date" : updateErrorText(error));
    publishOffer(state, error, nullptr);
  }
}

// --- Install (update task) ------------------------------------------------------------

static bool savePendingVersion(const char* version) {
  Preferences prefs;
  if (!prefs.begin(UPDATE_NAMESPACE, false)) return false;
  bool ok = prefs.putString(PENDING_KEY, version) == strlen(version);
  prefs.end();
  return ok;
}

// Streams the body into the OTA slot and the hash. Returns None on a
// complete body; the caller verifies the hash before activating anything.
static UpdateError streamImage(WiFiClient& stream, uint32_t size, mbedtls_sha256_context& sha) {
  uint8_t chunk[CHUNK_BYTES];
  uint32_t received = 0;
  unsigned long started = millis(), lastData = started;
  uint8_t shown = 0;
  while (received < size) {
    unsigned long now = millis();
    if (now - started >= DOWNLOAD_TOTAL_MS) return UpdateError::Timeout;
    int available = stream.available();
    if (available <= 0) {
      if (!stream.connected()) return UpdateError::Interrupted;
      if (now - lastData >= DOWNLOAD_STALL_MS) return UpdateError::Timeout;
      vTaskDelay(pdMS_TO_TICKS(5));
      continue;
    }
    size_t want = min<size_t>(min<size_t>(available, sizeof(chunk)), size - received);
    int read = stream.read(chunk, want);
    if (read <= 0) continue;
    lastData = millis();
    mbedtls_sha256_update_ret(&sha, chunk, read);
    if (Update.write(chunk, read) != size_t(read)) {
      Serial.printf("[UPDATE] Write failed: %s\n", Update.errorString());
      return UpdateError::WriteFailed;
    }
    received += read;
    uint8_t percent = updatePercent(received, size);
    if (percent != shown) {
      shown = percent;
      publish(UpdateState::Downloading, UpdateError::None, percent);
    }
  }
  return UpdateError::None;
}

static UpdateError install(const UpdateOffer& offer) {
  if (WiFi.status() != WL_CONNECTED) return UpdateError::NoWifi;
  uint32_t slot = otaSlotSize();
  if (offer.size == 0 || offer.size > slot) {
    Serial.printf("[UPDATE] %u bytes does not fit the %u-byte OTA slot\n", unsigned(offer.size), unsigned(slot));
    return UpdateError::TooLarge;
  }
  char url[UPDATE_URL_MAX];
  if (!firmwareDownloadUrl(url, sizeof(url), backendUrl(), offer.file)) return UpdateError::NoServer;

  Serial.printf("[UPDATE] Downloading %s (%u bytes) from %s\n", offer.version, unsigned(offer.size), url);
  WiFiClient client;
  HTTPClient http;
  http.setConnectTimeout(DOWNLOAD_CONNECT_MS);
  http.setTimeout(DOWNLOAD_STALL_MS);
  http.setReuse(false);
  http.useHTTP10(true); // A plain body with a Content-Length, never chunked.
  if (!http.begin(client, url)) return UpdateError::Unreachable;
  int code = http.GET();
  if (code != HTTP_CODE_OK) {
    http.end();
    Serial.printf("[UPDATE] Download: HTTP %d\n", code);
    return code == HTTP_CODE_NOT_FOUND ? UpdateError::FileMissing
           : code <= 0                 ? UpdateError::Unreachable
                                       : UpdateError::HttpError;
  }
  int length = http.getSize();
  if (length != int(offer.size)) {
    http.end();
    Serial.printf("[UPDATE] Content-Length %d, expected %u\n", length, unsigned(offer.size));
    return UpdateError::SizeMismatch;
  }
  if (!Update.begin(offer.size, U_FLASH)) {
    http.end();
    Serial.printf("[UPDATE] Update.begin failed: %s\n", Update.errorString());
    return UpdateError::BeginFailed;
  }

  mbedtls_sha256_context sha;
  mbedtls_sha256_init(&sha);
  mbedtls_sha256_starts_ret(&sha, 0);
  UpdateError error = streamImage(client, offer.size, sha);
  http.end();
  uint8_t digest[32];
  mbedtls_sha256_finish_ret(&sha, digest);
  mbedtls_sha256_free(&sha);
  if (error != UpdateError::None) {
    Update.abort();
    return error;
  }
  if (memcmp(digest, offer.sha256, sizeof(digest)) != 0) {
    Update.abort(); // The slot is never activated.
    Serial.println("[UPDATE] SHA-256 mismatch; update discarded");
    return UpdateError::HashMismatch;
  }
  Serial.println("[UPDATE] SHA-256 verified");

  publish(UpdateState::Installing, UpdateError::None, 100);
  // Checks the image and makes it the boot partition.
  if (!Update.end()) {
    Serial.printf("[UPDATE] Update.end failed: %s\n", Update.errorString());
    return UpdateError::EndFailed;
  }
  if (!savePendingVersion(offer.version)) Serial.println("[UPDATE] Could not save the update marker");
  Serial.printf("[UPDATE] Installed %s; restarting\n", offer.version);
  return UpdateError::None;
}

static void runInstall() {
  UpdateStatus snapshot;
  firmwareUpdateStatus(snapshot);
  UpdateError error = snapshot.hasOffer ? install(snapshot.offer) : UpdateError::NoUpdate;
  if (error == UpdateError::None) {
    publish(UpdateState::Restarting, UpdateError::None, 100);
  } else {
    Serial.printf("[UPDATE] Install failed: %s; running firmware unchanged\n", updateErrorText(error));
    publish(UpdateState::Failed, error);
  }
}

// A task exists exactly while the state is Checking, Downloading or
// Installing (updateBusy): the main task publishes that state before
// creating it, and the task's last action is publishing a final state.
static void updateTask(void* arg) {
  if (Job(uintptr_t(arg)) == Job::Check) runCheck();
  else runInstall();
  vTaskDelete(nullptr);
}

static bool startJob(Job job) {
  if (xTaskCreate(updateTask, "update", UPDATE_STACK_BYTES, (void*)uintptr_t(job), 1, nullptr) == pdPASS) {
    return true;
  }
  Serial.println("[UPDATE] Could not start the update task");
  return false;
}

// --- Main task API ----------------------------------------------------------------------

static bool networkReady() {
  WifiSwitchState wifi = wifiSwitchState();
  return WiFi.status() == WL_CONNECTED && !wifiSetupRunning() &&
         wifi != WifiSwitchState::Pending && wifi != WifiSwitchState::Connecting;
}

bool requestUpdateCheck() {
  if (!updateCheckAllowed(setupModeActive(), networkReady(), hasBackendUrl(), firmwareUpdateState())) return false;
  updateCheckStarted(schedule, millis());
  publish(UpdateState::Checking, UpdateError::None);
  if (startJob(Job::Check)) return true;
  publishOffer(UpdateState::CheckFailed, UpdateError::Busy, nullptr);
  return false;
}

bool requestUpdateInstall() {
  UpdateStatus snapshot;
  firmwareUpdateStatus(snapshot);
  if (setupModeActive() || updateBusy(snapshot.state) || !snapshot.hasOffer) return false;
  if (!networkReady()) {
    publish(UpdateState::Failed, UpdateError::NoWifi);
    return true; // The failure screen explains it.
  }
  publish(UpdateState::Downloading, UpdateError::None, 0);
  if (startJob(Job::Install)) return true;
  publish(UpdateState::Failed, UpdateError::Busy);
  return true;
}

void serviceFirmwareUpdates() {
  if (!updateCheckDue(schedule, millis())) return;
  if (!updateCheckAllowed(setupModeActive(), networkReady(), hasBackendUrl(), firmwareUpdateState())) return;
  requestUpdateCheck();
}

bool beginFirmwareUpdates(char* installedVersion, size_t size) {
  updateScheduleBegin(schedule, millis());
  installedVersion[0] = '\0';
  char pending[UPDATE_VERSION_MAX] = "";
  bool stored = false;
  Preferences prefs;
  if (nvsNamespaceExists(UPDATE_NAMESPACE) && prefs.begin(UPDATE_NAMESPACE, true)) {
    stored = prefs.isKey(PENDING_KEY);
    if (stored) prefs.getString(PENDING_KEY, pending, sizeof(pending));
    prefs.end();
  }
  PendingUpdate outcome = pendingUpdateOutcome(stored, pending, firmwareVersion());
  if (outcome == PendingUpdate::None) return false;
  // Cleared either way, so the success screen shows at most once.
  if (prefs.begin(UPDATE_NAMESPACE, false)) {
    prefs.remove(PENDING_KEY);
    prefs.end();
  }
  if (outcome == PendingUpdate::NotRunning) {
    Serial.printf("[UPDATE] Installed %s did not start (running %s); marker cleared\n", pending, firmwareVersion());
    return false;
  }
  Serial.printf("[UPDATE] Now running %s\n", pending);
  snprintf(installedVersion, size, "%s", pending);
  return true;
}
