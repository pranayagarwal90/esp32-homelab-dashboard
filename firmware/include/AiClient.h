#pragma once
#include <stdint.h>
#include "AiLogic.h"

// Background AI network I/O: one small FreeRTOS worker, created on first use,
// posts one request at a time to the HomeServer (/api/ai/explain or
// /api/ai/status) and returns fixed-size results. It never draws. The
// HomeServer talks to Ollama; the ESP32 never does.

enum class AiJobType : uint8_t { Status, Explain };

struct AiJob {
  uint32_t generation;
  AiJobType type;
  char body[AI_REQUEST_MAX];  // Explain only: JSON from aiRequestBody().
};

struct AiResult {
  uint32_t generation;
  AiJobType type;
  AiResponse response;
};

// Main task. False if the worker could not be started (treat as failed).
bool submitAiJob(const AiJob& job);
// Main task, nonblocking.
bool receiveAiResult(AiResult& result);
