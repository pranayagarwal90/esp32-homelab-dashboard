#include <Arduino.h>
#include "CalendarScreen.h"
#include "AppState.h"
#include "Display.h"
#include "PageRouter.h"
#include "TimeLogic.h"

// The shown month (TimeLogic.h): today's month once a real date is known
// (the device clock, or the backend's), unless the user already moved it.
static CalendarCursor cursor;

static const char* MONTH_NAMES[] = {
  "JANUARY", "FEBRUARY", "MARCH", "APRIL", "MAY", "JUNE",
  "JULY", "AUGUST", "SEPTEMBER", "OCTOBER", "NOVEMBER", "DECEMBER"
};

static bool isLeapYear(int year) {
  return (year % 400 == 0) || ((year % 4 == 0) && (year % 100 != 0));
}

static int daysInMonth(int year, int month) {
  static const int days[] = {31,28,31,30,31,30,31,31,30,31,30,31};
  if (month == 2 && isLeapYear(year)) return 29;
  return days[month - 1];
}

// 0=Sunday ... 6=Saturday
static int dayOfWeek(int year, int month, int day) {
  static int t[] = {0,3,2,5,0,3,5,1,4,6,2,4};
  if (month < 3) year -= 1;
  return (year + year/4 - year/100 + year/400 + t[month-1] + day) % 7;
}

void setCalendarMonth(int year, int month) {
  if (month < 1 || month > 12) return;
  cursor.year = year;
  cursor.month = month;
}

void syncCalendarIfUnset(int year, int month) {
  calendarSyncToday(cursor, year, month);
}

void drawCalendarPage() {
  app.currentPage = PAGE_CALENDAR;
  tft.fillScreen(TFT_BLACK);

  tft.setTextSize(2);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  String title = String(MONTH_NAMES[cursor.month - 1]) + " " + String(cursor.year);
  int titleWidth = tft.textWidth(title);
  tft.setCursor((320 - titleWidth) / 2, 8);
  tft.print(title);

  const char* weekdays[] = {"SU","MO","TU","WE","TH","FR","SA"};
  tft.setTextSize(1);
  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  for (int i = 0; i < 7; i++) {
    tft.setCursor(15 + i * 43, 38);
    tft.print(weekdays[i]);
  }

  int first = dayOfWeek(cursor.year, cursor.month, 1);
  int totalDays = daysInMonth(cursor.year, cursor.month);
  int cellW = 43;
  int cellH = 23;
  int startX = 10;
  int startY = 55;

  for (int day = 1; day <= totalDays; day++) {
    int pos = first + day - 1;
    int col = pos % 7;
    int row = pos / 7;
    int x = startX + col * cellW;
    int y = startY + row * cellH;

    bool today = cursor.year == app.time.currentYear && cursor.month == app.time.currentMonth && day == app.time.currentDay;
    if (today) {
      tft.fillCircle(x + 10, y + 5, 10, TFT_BLUE);
      tft.setTextColor(TFT_WHITE, TFT_BLUE);
    } else {
      tft.setTextColor(TFT_WHITE, TFT_BLACK);
    }

    tft.setCursor(x + (day < 10 ? 7 : 4), y + 2);
    tft.print(day);
  }

  tft.fillRect(0, 208, 106, 32, TFT_DARKGREY);
  tft.fillRect(106, 208, 108, 32, TFT_DARKGREY);
  tft.fillRect(214, 208, 106, 32, TFT_DARKGREY);
  tft.setTextSize(1);
  tft.setTextColor(TFT_WHITE, TFT_DARKGREY);
  tft.setCursor(33, 220); tft.print("< PREV");
  tft.setCursor(145, 220); tft.print("BACK");
  tft.setCursor(247, 220); tft.print("NEXT >");
}

void handleCalendarTouch(int x, int y) {
  if (y >= 205) {
    if (x < 106) {
      calendarStep(cursor, -1);
      drawCalendarPage();
    } else if (x < 214) {
      showPage(PAGE_MORE);
    } else {
      calendarStep(cursor, 1);
      drawCalendarPage();
    }
  }
}
