#pragma once
#include "ProvisioningLogic.h"

// Setup Mode (PAGE_SETUP): entered at boot when the device has no usable
// configuration or cannot join its network. It opens the setup hotspot in
// first-run mode and shows how to reach it; the dashboard, status worker and
// OTA do not run. Saving on the setup page restarts the device. RESTART
// (bottom bar) retries; after a failed connect it also retries by itself
// once the hotspot has been idle (ProvisioningLogic). Main task only.

void enterSetupMode(SetupReason reason);
bool setupModeActive();
void drawSetupPage();
void handleSetupTouch(int x, int y);
// Every loop() while active: hotspot, redraws, retry.
void updateSetupMode();
