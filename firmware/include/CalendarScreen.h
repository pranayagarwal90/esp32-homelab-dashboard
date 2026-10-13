#pragma once

// Month calendar. Owns the displayed year/month; main task only.
void drawCalendarPage();
void handleCalendarTouch(int x, int y);
// Shows that month (opening from More: today's month, from app.time).
void setCalendarMonth(int year, int month);
// Moves the calendar to today's month the first time a real date is known
// (local clock or /api/status), unless the user already moved it.
void syncCalendarIfUnset(int year, int month);
