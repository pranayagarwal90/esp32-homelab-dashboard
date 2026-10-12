#pragma once
#include <stdint.h>
#include "UpdateLogic.h"

// HomeDeck Mini pull updates (not ArduinoOTA). The device asks its
// configured backend for a newer release (GET /api/firmware/update) and, only
// when the user chooses INSTALL, downloads /firmware/<file> from that same
// backend, streaming it into the inactive OTA slot while hashing it. The
// slot is activated only if the size and SHA-256 match the offer; anything
// else aborts and leaves the running firmware as it is.
//
// Checks and installs run one at a time on a short-lived task ("update"),
// created on demand and deleted when done; the main task only reads the
// status and draws. Nothing here touches the TFT.

struct UpdateStatus {
  UpdateState state;
  UpdateError error;
  uint8_t percent;     // Downloading only.
  bool hasOffer;       // offer is valid (Available, and kept after a failed install).
  uint32_t revision;   // Changes on every update of the fields above.
  UpdateOffer offer;
};

// setup(), normal boot only: starts the check schedule and consumes the
// post-update marker. True (version filled) once, after booting a newly
// installed update.
bool beginFirmwareUpdates(char* installedVersion, size_t size);
// loop(), main task: starts the automatic checks when due. Never blocks.
void serviceFirmwareUpdates();
// Main task. False (and nothing starts) when not allowed right now.
bool requestUpdateCheck();
bool requestUpdateInstall();
// Any task.
void firmwareUpdateStatus(UpdateStatus& out);
UpdateState firmwareUpdateState();
