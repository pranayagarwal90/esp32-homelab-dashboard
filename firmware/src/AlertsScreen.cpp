#include <Arduino.h>
#include "AlertsScreen.h"
#include "AlertManager.h"
#include "Display.h"
#include "PageRouter.h"
#include "UiHelpers.h"

static int alertsPage = 0;
static Page alertsReturn = PAGE_MORE;

static uint16_t severityColor(AlertSeverity severity) {
  switch (severity) {
    case AlertSeverity::Critical: return TFT_RED;
    case AlertSeverity::Warning: return TFT_ORANGE;
    case AlertSeverity::Info: return TFT_CYAN;
    default: return TFT_GREEN;
  }
}

static void printCentered(const char* text, int y) {
  tft.setCursor((320 - tft.textWidth(text)) / 2, y);
  tft.print(text);
}

static void printRight(const char* text, int right, int y) {
  tft.setCursor(right - tft.textWidth(text), y);
  tft.print(text);
}

static void drawAlertRow(const AlertTable& table, int slot, int row) {
  const Alert& alert = table.slots[slot];
  uint16_t color = severityColor(alert.severity);
  int y = ALERT_ROW_Y0 + row * ALERT_ROW_PITCH;
  char text[24];

  tft.fillRect(10, y, 5, ALERT_ROW_H, color);

  tft.setTextSize(1);
  tft.setTextColor(color, TFT_BLACK);
  tft.setCursor(24, y + 4);
  tft.print(alertSeverityLabel(alert.severity));
  char age[12];
  formatAlertAge(millis() - alert.sinceMs, age, sizeof(age));
  snprintf(text, sizeof(text), "for %s", age);
  tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  printRight(text, 306, y + 4);

  tft.setTextSize(2);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setCursor(24, y + 22);
  alertTitle(table, slot, text, sizeof(text));
  tft.print(text);
  alertValueText(table, slot, text, sizeof(text));
  tft.setTextColor(color, TFT_BLACK);
  printRight(text, 306, y + 22);
}

void openAlerts(Page returnTo) {
  alertsPage = 0;
  alertsReturn = returnTo;
  showPage(PAGE_ALERTS);
}

void drawAlertsPage() {
  const AlertTable& table = alerts();
  uint8_t order[ALERT_SLOT_COUNT];
  int count = alertsSorted(table, order);
  int pages = alertPageCount(count);
  alertsPage = clampAlertPage(alertsPage, count);

  app.currentPage = PAGE_ALERTS;
  tft.fillScreen(TFT_BLACK);
  char title[16];
  if (count) snprintf(title, sizeof(title), "ALERTS %d", count);
  else snprintf(title, sizeof(title), "ALERTS");
  drawHeader(title);

  if (count == 0) {
    tft.setTextSize(2);
    tft.setTextColor(table.haveStatus ? TFT_GREEN : TFT_LIGHTGREY, TFT_BLACK);
    printCentered(table.haveStatus ? "No active alerts" : "No status data", 88);
    tft.setTextSize(1);
    tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    printCentered(table.haveStatus ? "Everything looks healthy." : "Waiting for the first update.", 120);
  }

  int first = alertsPage * ALERTS_PER_PAGE;
  for (int row = 0; row < ALERTS_PER_PAGE && first + row < count; row++) {
    if (row > 0) tft.drawFastHLine(10, ALERT_ROW_Y0 + row * ALERT_ROW_PITCH - 3, 300, TFT_DARKGREY);
    drawAlertRow(table, order[first + row], row);
  }

  if (pages > 1) {
    char page[16];
    snprintf(page, sizeof(page), "PAGE %d/%d", alertsPage + 1, pages);
    tft.setTextSize(1);
    tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    printCentered(page, 197);
  }
  drawBackBar(alertsPage > 0 ? "PREV" : nullptr, "BACK", alertsPage < pages - 1 ? "NEXT" : nullptr);
}

void handleAlertsTouch(int x, int y) {
  int count = alertsActiveCount(alerts());
  switch (alertsBarAt(x, y)) {
    case AlertsBarHit::Prev:
      if (alertsPage > 0) {
        alertsPage--;
        drawAlertsPage();
      }
      break;
    case AlertsBarHit::Next:
      if (alertsPage < alertPageCount(count) - 1) {
        alertsPage++;
        drawAlertsPage();
      }
      break;
    case AlertsBarHit::Back:
      showPage(appBackTarget(PAGE_ALERTS, alertsReturn));
      break;
    case AlertsBarHit::None:
      break;
  }
}

void drawAlertBadge() {
  const AlertTable& table = alerts();
  char text[24];
  alertSummary(table, text, sizeof(text));
  AlertSeverity highest = alertsHighest(table);
  uint16_t color = !table.haveStatus && highest == AlertSeverity::None ? TFT_DARKGREY : severityColor(highest);

  tft.setTextSize(1);
  int textW = tft.textWidth(text);
  int textX = ALERT_BADGE_RIGHT - textW;
  tft.fillCircle(textX - 8, ALERT_BADGE_Y + 3, 3, color);
  tft.setTextColor(color, TFT_BLACK);
  tft.setCursor(textX, ALERT_BADGE_Y);
  tft.print(text);
}
