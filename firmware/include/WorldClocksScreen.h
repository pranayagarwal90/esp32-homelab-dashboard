#pragma once
#include "SettingsLogic.h"

// Settings views for the local world clocks (LocalClockStore): the list,
// ADD WORLD CLOCK, COMMON CITIES and one clock's page with REMOVE. SEARCH ON
// PHONE goes through the usual confirmation and setup hotspot (WorldClock
// portal mode). No backend needed. Main task only; layout in WorldClockUi.h.

void drawWorldClockList();
// True when the touch was handled (its bottom bar has ADD CLOCK).
bool handleWorldClockListTouch(int x, int y);
void drawAddClock();
void handleAddClockTouch(int x, int y);
void drawCommonCities();
// True when handled (its bottom bar is PREV | CANCEL | NEXT).
bool handleCommonCitiesTouch(int x, int y);
void drawClockDetail();
void handleClockDetailTouch(int x, int y);
// Every loop while one of these views is shown: the times tick, offsets arrive.
void updateWorldClockViews(SettingsView view);
// ConfirmAction::RemoveClock.
void removeSelectedClock();
