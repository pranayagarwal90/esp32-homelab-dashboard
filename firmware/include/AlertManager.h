#pragma once
#include "AlertLogic.h"

// Active homelab alerts (AlertLogic.h), fed from status results on the main
// task. Each returns true when the alerts (or what they display) changed.
bool alertsOnStatus();
bool alertsOnFetchFailed();
// Status fetching was intentionally suspended for pausedMs.
void alertsOnFetchPaused(uint32_t pausedMs);
const AlertTable& alerts();
