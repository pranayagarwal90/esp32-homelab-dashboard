#pragma once
#include <ArduinoJson.h>
#include "AiLogic.h"

// Parses /api/ai/explain and /api/ai/status bodies into fixed buffers.
// Shared with host tests (ArduinoJson is header-only).

inline void aiParseResponse(const char* body, size_t length, int httpCode, AiResponse& out) {
  out = AiResponse();
  JsonDocument doc;
  if (!body || deserializeJson(doc, body, length) || !doc.is<JsonObject>()) {
    aiSetError(out, httpCode == 200 ? "AI returned an unusable answer" : "AI request failed");
    return;
  }
  out.available = (doc["available"] | false) && httpCode == 200;
  out.cached = doc["cached"] | false;
  out.rules = strcmp(doc["source"] | "", "rules") == 0;
  out.engine = aiEngineFrom(doc["engine"] | "");
  aiCopyText(out.title, sizeof(out.title), doc["title"] | "");
  aiCopyText(out.summary, sizeof(out.summary), doc["summary"] | "");
  aiCopyText(out.action, sizeof(out.action), doc["action"] | "");
  aiCopyText(out.error, sizeof(out.error), doc["error"] | "");
  if (httpCode != 200 && !out.error[0]) aiCopyText(out.error, sizeof(out.error), "AI request failed");
  aiFinishResponse(out);
}

// GET /api/ai/status: {"available": bool, "error": "..."}; only those fields.
inline void aiParseAvailability(const char* body, size_t length, AiResponse& out) {
  out = AiResponse();
  JsonDocument doc;
  if (!body || deserializeJson(doc, body, length) || !doc.is<JsonObject>()) {
    aiSetError(out, "AI status unknown");
    return;
  }
  out.available = doc["available"] | false;
  aiCopyText(out.error, sizeof(out.error), out.available ? "" : (doc["error"] | "AI unavailable"));
}
