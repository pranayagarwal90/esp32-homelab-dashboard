#pragma once

// CLOCKS: local time, weather and the world clocks: those added on the
// device (two per page, no backend needed), else the backend's (three per
// page), PREV / NEXT when there are more. EDIT > manages them. Main task only.
void drawTimePage();
void handleTimeTouch(int x, int y);
// Every loop: redraws when a local world clock's minute, offset or the
// clock list changes (needs no device location).
void updateTimePage();
