#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include "AiClient.h"
#include "AiResponse.h"
#include "ApiConfig.h"

static constexpr uint32_t AI_CONNECT_TIMEOUT_MS = 5000;
// The backend may try the GPU laptop and then the HomeServer: 50 s at most,
// plus reading the status (~1 s).
static constexpr uint16_t AI_EXPLAIN_TIMEOUT_MS = 55000;
static constexpr uint16_t AI_STATUS_TIMEOUT_MS = 5000;
static constexpr uint32_t AI_BODY_DEADLINE_MS = 5000; // After the headers.
// Body buffer, JSON document and HTTPClient all live on this stack, which
// only exists once the assistant has been used.
static constexpr uint32_t AI_STACK_BYTES = 8192;

static QueueHandle_t aiRequests = nullptr;
static QueueHandle_t aiResults = nullptr;
static bool aiWorkerFailed = false;

static void runJob(const AiJob& job, AiResult& result) {
  AiResponse& response = result.response;
  if (WiFi.status() != WL_CONNECTED) {
    aiSetError(response, "Wi-Fi is not connected");
    return;
  }
  bool explain = job.type == AiJobType::Explain;
  String url = String(API_BASE) + (explain ? "/api/ai/explain" : "/api/ai/status");
  WiFiClient client;
  HTTPClient http;
  http.setConnectTimeout(AI_CONNECT_TIMEOUT_MS);
  http.setTimeout(explain ? AI_EXPLAIN_TIMEOUT_MS : AI_STATUS_TIMEOUT_MS);
  http.setReuse(false);
  http.useHTTP10(true); // Identity body with a Content-Length.
  if (!http.begin(client, url)) {
    aiSetError(response, "HomeServer is not responding");
    return;
  }
  Serial.printf("AI: %s request, free heap %u\n", explain ? "explain" : "status", ESP.getFreeHeap());
  int code;
  if (explain) {
    http.addHeader("Content-Type", "application/json");
    code = http.POST((uint8_t*)job.body, strlen(job.body));
  } else {
    code = http.GET();
  }
  if (code <= 0) {
    Serial.printf("AI HTTP error: %d\n", code);
    aiSetError(response, code == HTTPC_ERROR_READ_TIMEOUT ? "AI response timed out" : "HomeServer is not responding");
    http.end();
    return;
  }

  int length = http.getSize();
  char body[AI_RESPONSE_MAX];
  int total = 0;
  if (length > 0 && length < (int)sizeof(body)) {
    unsigned long started = millis();
    while (total < length && millis() - started < AI_BODY_DEADLINE_MS) {
      int available = client.available();
      if (available > 0) {
        int read = client.read((uint8_t*)body + total, min(available, length - total));
        if (read > 0) total += read;
      } else if (!client.connected()) {
        break;
      } else {
        vTaskDelay(pdMS_TO_TICKS(2));
      }
    }
  }
  http.end();
  if (length <= 0 || total != length) {
    Serial.printf("AI response invalid: %d of %d bytes (HTTP %d)\n", total, length, code);
    aiSetError(response, "AI returned an unusable answer");
    return;
  }
  body[total] = '\0';
  if (explain) aiParseResponse(body, total, code, response);
  else aiParseAvailability(body, total, response);
  Serial.printf("AI: done (HTTP %d), free heap %u\n", code, ESP.getFreeHeap());
}

static void aiWorker(void*) {
  AiJob job;
  for (;;) {
    if (xQueueReceive(aiRequests, &job, portMAX_DELAY) != pdTRUE) continue;
    AiResult result = {job.generation, job.type, AiResponse()};
    runJob(job, result);
    xQueueSend(aiResults, &result, portMAX_DELAY);
  }
}

// Created on first use so the dashboard pays nothing until AI is opened.
static bool ensureWorker() {
  if (aiRequests) return true;
  if (aiWorkerFailed) return false;
  aiRequests = xQueueCreate(1, sizeof(AiJob));
  aiResults = xQueueCreate(1, sizeof(AiResult));
  if (aiRequests && aiResults &&
      xTaskCreate(aiWorker, "ai", AI_STACK_BYTES, nullptr, 1, nullptr) == pdPASS) return true;
  if (aiRequests) vQueueDelete(aiRequests);
  if (aiResults) vQueueDelete(aiResults);
  aiRequests = aiResults = nullptr;
  aiWorkerFailed = true;
  Serial.println("Could not create AI worker");
  return false;
}

bool submitAiJob(const AiJob& job) {
  return ensureWorker() && xQueueSend(aiRequests, &job, 0) == pdTRUE;
}

bool receiveAiResult(AiResult& result) {
  return aiResults && xQueueReceive(aiResults, &result, 0) == pdTRUE;
}
