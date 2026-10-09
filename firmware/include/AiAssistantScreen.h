#pragma once
#include "AiLogic.h"
#include "AppState.h"

// AI ASSISTANT (MORE): a menu of fixed questions and one shared result page,
// also opened by ASK AI on ALERTS and HOMESERVER. Read-only explanations from
// the HomeServer's local Ollama. Requests run on the AiClient worker; all
// drawing happens here on the main task.
void drawAiMenuPage();
void handleAiMenuTouch(int x, int y);
void drawAiResultPage();
void handleAiResultTouch(int x, int y);
// Asks `mode` and shows the result page; BACK returns to `origin`.
void openAiResult(AiMode mode, Page origin);
// The ASK AI button drawn in a title bar (ALERTS).
void drawAskAiHeaderButton();
// Call every loop(): applies finished requests, submits waiting ones and
// animates the loading dots.
void updateAiAssistant();
