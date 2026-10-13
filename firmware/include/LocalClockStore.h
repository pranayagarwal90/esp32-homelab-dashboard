#pragma once
#include <stdint.h>
#include "LocalClockLogic.h"

// World clocks added on the device, in NVS namespace "worldclocks" (key
// "clocks": one CRC-checked blob, LocalClockLogic.h). Independent of "wifi",
// "device" (backend) and "location": nothing here reads or writes them.
//
// One RAM copy, guarded by a lock: any task may read it. Changes come from
// the main task (Settings, offset refreshes) or the setup page task (SEARCH
// ON PHONE); they are serialized and saved before the RAM copy changes.

void loadLocalClocks();                    // setup(), before any task starts.
int localClockCount();
void localClocks(LocalClockList& out);     // A copy.
bool localClockAt(int index, WorldClockLocation& out);
bool localClockAdded(const WorldClockLocation& place);
ClockAddResult addLocalClockPlace(const WorldClockLocation& place);
bool removeLocalClockAt(int index);
// Main task, after a refresh. Saved when new or changed (otherwise daily).
bool applyLocalClockOffset(const WorldClockLocation& place, int32_t offset);
// Increments when a clock is added or removed.
uint32_t localClocksRevision();
// Increments on any change the CLOCKS page shows (also offsets).
uint32_t localClocksDisplayRevision();
