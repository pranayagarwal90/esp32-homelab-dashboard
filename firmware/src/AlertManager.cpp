#include <Arduino.h>
#include "AlertManager.h"
#include "AppState.h"

static AlertTable table; // Main task only.

bool alertsOnStatus() {
  const SystemMetrics& metrics = app.metrics;
  const ServiceState& services = app.services;
  AlertInput input;
  input.hostAvailable = metrics.hostAvailable;
  input.memPercent = metrics.memPercent;
  input.diskCount = min(metrics.diskCount, ALERT_MAX_DISKS);
  for (int i = 0; i < input.diskCount; i++) {
    input.diskNames[i] = metrics.diskNames[i].c_str();
    input.diskPercent[i] = metrics.diskPercentages[i];
  }
  input.servicesReported = services.alertReported;
  const bool online[ALERT_SERVICE_COUNT] = {
    services.serviceJellyfin, services.serviceNavidrome, services.serviceMetube,
    services.serviceOllama, services.serviceCloudflare,
  };
  for (int i = 0; i < ALERT_SERVICE_COUNT; i++) {
    if (online[i]) input.servicesOnline |= 1u << i;
  }
  bool changed = alertsApplyStatus(table, input, millis());
  if (changed) Serial.printf("Alerts: %d active\n", alertsActiveCount(table));
  return changed;
}

bool alertsOnFetchFailed() {
  bool changed = alertsFetchFailed(table, millis());
  if (changed) Serial.println("Alerts: status data stale");
  return changed;
}

void alertsOnFetchPaused(uint32_t pausedMs) {
  alertsExcludePause(table, pausedMs, millis());
}

const AlertTable& alerts() {
  return table;
}
