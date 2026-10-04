#pragma once

// Month calendar. Owns the displayed year/month; main task only.
void drawCalendarPage();
void handleCalendarTouch(int x, int y);
void setCalendarMonth(int year, int month);
// Moves the calendar to the given month only while it still shows its initial
// January 2026 placeholder (i.e. before the first status result arrived).
void syncCalendarIfUnset(int year, int month);
