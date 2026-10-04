#pragma once
#include <stdint.h>

// Initial blocking connect; draws a "Connecting..." screen. Main task, setup only.
void connectWiFi();
// Reconnects if needed, waiting up to timeoutMs with vTaskDelay. Never draws;
// called from the status worker task.
bool ensureWiFiConnected(uint32_t timeoutMs);
