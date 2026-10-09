#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include "PhotoClient.h"
#include "ApiConfig.h"

static constexpr uint32_t PHOTO_LIST_TIMEOUT_MS = 6000;    // Inactivity, as before.
static constexpr uint32_t PHOTO_LIST_DEADLINE_MS = 10000;  // Whole request.
static constexpr size_t PHOTO_LIST_MAX_BODY = 32768;
static constexpr uint32_t PHOTO_CONNECT_TIMEOUT_MS = 5000; // HTTPClient default.
static constexpr uint32_t PHOTO_IMAGE_TIMEOUT_MS = 10000;  // Inactivity, as before.
static constexpr uint32_t PHOTO_BODY_DEADLINE_MS = 10000;  // After headers, as before.
static constexpr uint32_t PHOTO_IMAGE_DEADLINE_MS = 20000; // Whole request.
static constexpr int PHOTO_MAX_BYTES = 130000;
static constexpr uint32_t PHOTO_STACK_BYTES = 8192;

static PhotoList photoListSlot;
static QueueHandle_t photoRequests = nullptr;
static QueueHandle_t photoResults = nullptr;

// HTTPClient's timeout is an inactivity timeout. Enforce an absolute deadline
// too, so a server that drips bytes cannot hold the worker indefinitely.
class DeadlineClient : public WiFiClient {
  unsigned long started = millis();
  uint32_t limit;
  bool expired() {
    if (millis() - started < limit) return false;
    WiFiClient::stop();
    return true;
  }
public:
  explicit DeadlineClient(uint32_t limitMs) : limit(limitMs) {}
  bool timedOut() const { return millis() - started >= limit; }
  int available() override { return expired() ? 0 : WiFiClient::available(); }
  int read() override { return expired() ? -1 : WiFiClient::read(); }
  int read(uint8_t* buffer, size_t size) override {
    return expired() ? -1 : WiFiClient::read(buffer, size);
  }
  uint8_t connected() override { return expired() ? 0 : WiFiClient::connected(); }
};

static bool readList() {
  if (WiFi.status() != WL_CONNECTED) return false;

  DeadlineClient client(PHOTO_LIST_DEADLINE_MS);
  HTTPClient http;
  http.setConnectTimeout(PHOTO_CONNECT_TIMEOUT_MS);
  http.setTimeout(PHOTO_LIST_TIMEOUT_MS);
  http.setReuse(false);
  // HTTP/1.0 requests an identity body, avoiding raw chunk framing.
  http.useHTTP10(true);
  if (!http.begin(client, API_PHOTOS)) return false;
  int code = http.GET();
  if (code != HTTP_CODE_OK) {
    Serial.printf("Photo list HTTP error: %d\n", code);
    http.end();
    return false;
  }

  int length = http.getSize();
  String body;
  bool valid = length != 0 && length <= (int)PHOTO_LIST_MAX_BODY;
  if (valid) valid = body.reserve(length > 0 ? length : 1024);
  while (valid && (length < 0 || body.length() < (size_t)length)) {
    int available = client.available();
    if (available > 0) {
      uint8_t buffer[256];
      size_t count = min((size_t)available, sizeof(buffer));
      if (length >= 0) count = min(count, (size_t)length - body.length());
      int received = client.read(buffer, count);
      if (received <= 0 || body.length() + received > PHOTO_LIST_MAX_BODY ||
          !body.concat((const char*)buffer, received)) { valid = false; break; }
    } else if (!client.connected()) {
      break;
    } else {
      vTaskDelay(pdMS_TO_TICKS(1));
    }
  }
  if (client.timedOut() || (length >= 0 && body.length() != (size_t)length)) valid = false;
  http.end();
  if (!valid) {
    Serial.println("Photo list body invalid");
    return false;
  }

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, body);
  if (err) {
    Serial.print("Photo list JSON error: ");
    Serial.println(err.c_str());
    return false;
  }

  photoListSlot.count = 0;
  for (JsonVariant item : doc["photos"].as<JsonArray>()) {
    if (photoListSlot.count >= PHOTO_LIST_MAX) break;
    photoListSlot.names[photoListSlot.count++] = item.as<String>();
  }
  return true;
}

// On success, *out is a malloc'd buffer of exactly outLength bytes.
static bool readBody(const String& url, int maxBytes, uint8_t** out, size_t* outLength) {
  if (WiFi.status() != WL_CONNECTED) return false;

  DeadlineClient client(PHOTO_IMAGE_DEADLINE_MS);
  HTTPClient http;
  http.setConnectTimeout(PHOTO_CONNECT_TIMEOUT_MS);
  http.setTimeout(PHOTO_IMAGE_TIMEOUT_MS);
  http.setReuse(false);
  if (!http.begin(client, url)) return false;
  int code = http.GET();

  if (code != HTTP_CODE_OK) {
    Serial.printf("Download HTTP error %d: %s\n", code, url.c_str());
    http.end();
    return false;
  }

  // Content-Length is required; a missing or oversized length is rejected
  // before allocating.
  int len = http.getSize();
  if (len <= 0 || len > maxBytes) {
    Serial.printf("Download size invalid: %d bytes\n", len);
    http.end();
    return false;
  }

  uint8_t* buffer = (uint8_t*)malloc(len);
  if (!buffer) {
    Serial.println("Not enough RAM for download");
    http.end();
    return false;
  }

  int total = 0;
  unsigned long bodyStart = millis();
  while (total < len && millis() - bodyStart < PHOTO_BODY_DEADLINE_MS) {
    int available = client.available();
    if (available > 0) {
      int read = client.read(buffer + total, min(available, len - total));
      if (read > 0) total += read;
    } else if (!client.connected()) {
      break;
    } else {
      vTaskDelay(pdMS_TO_TICKS(1));
    }
  }
  http.end();

  if (total != len) {
    Serial.printf("Download incomplete: %d of %d bytes\n", total, len);
    free(buffer);
    return false;
  }
  *out = buffer;
  *outLength = len;
  return true;
}

static void photoWorker(void*) {
  PhotoJob job;
  for (;;) {
    if (xQueueReceive(photoRequests, &job, portMAX_DELAY) != pdTRUE) continue;
    PhotoResult result = {job.generation, job.type, false, nullptr, nullptr, 0, job.client};
    if (job.type == PhotoJobType::List) {
      result.ok = readList();
      if (result.ok) result.list = &photoListSlot;
    } else if (job.type == PhotoJobType::Image) {
      result.ok = readBody(String(API_BASE) + "/photos/" + job.name, PHOTO_MAX_BYTES,
                           &result.jpeg, &result.jpegLength);
    } else {
      int limit = min((int)job.maxBytes, PHOTO_MAX_BYTES);
      result.ok = readBody(String(API_BASE) + job.name, limit, &result.jpeg, &result.jpegLength);
    }
    xQueueSend(photoResults, &result, portMAX_DELAY);
    // The list slot and JPEG buffer now belong to the main task until it
    // submits the next job.
  }
}

void setupPhotoWorker() {
  // Two slots: Photos and Radar may each have one job outstanding.
  photoRequests = xQueueCreate(2, sizeof(PhotoJob));
  photoResults = xQueueCreate(2, sizeof(PhotoResult));
  if (photoRequests && photoResults &&
      xTaskCreate(photoWorker, "photos", PHOTO_STACK_BYTES, nullptr, 1, nullptr) == pdPASS) return;
  if (photoRequests) vQueueDelete(photoRequests);
  if (photoResults) vQueueDelete(photoResults);
  photoRequests = photoResults = nullptr;
  Serial.println("Could not create photo worker");
}

bool submitPhotoJob(const PhotoJob& job) {
  return photoRequests && xQueueSend(photoRequests, &job, 0) == pdTRUE;
}

bool receivePhotoResult(WorkerClient client, PhotoResult& result) {
  // Only the main task receives, so a peeked result is still there to take.
  if (!photoResults || xQueuePeek(photoResults, &result, 0) != pdTRUE) return false;
  if (result.client != client) return false;
  return xQueueReceive(photoResults, &result, 0) == pdTRUE;
}
