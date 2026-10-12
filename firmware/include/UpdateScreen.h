#pragma once
#include "UpdateLogic.h"

// PAGE_UPDATE (main task): the HomeDeck Mini update prompt (INSTALL NOW /
// LATER), install progress, the failure screen (BACK) and the one-time
// "updated successfully" screen (CONTINUE). The work itself runs in
// FirmwareUpdate; this module only draws and routes touches.

// INSTALL on Settings > Firmware: the prompt, returning there on LATER/BACK.
void openUpdatePromptFromSettings();
// setup(): the first screen after booting a newly installed update.
void showUpdateSuccess(const char* version);
void drawUpdatePage();
void handleUpdateTouch(int x, int y);
// Every loop() of the normal dashboard: automatic checks, the prompt,
// progress and the restart. True while an install owns the display (from the
// first byte written until the restart); loop() then skips touch and every
// other page, so nothing can navigate away or start another update.
bool updateUpdateScreen();
