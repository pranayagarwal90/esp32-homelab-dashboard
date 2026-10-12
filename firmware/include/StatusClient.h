#pragma once

// Background /api/status fetching. A FreeRTOS worker performs the blocking
// HTTP request and JSON parse into a private snapshot; the main task copies it
// into AppState and redraws. At most one request is in flight.
void setupStatusWorker();
// Main task: queue a request unless one is already in flight. Does nothing
// without a configured backend (Standalone Mode).
void fetchHomelabStatus();
// Main task: apply a finished result (if any), redraw, and release the slot.
void processStatusResult();
// True once REFRESH_INTERVAL has passed since the last request or result;
// never in Standalone Mode.
bool statusRefreshDue();
