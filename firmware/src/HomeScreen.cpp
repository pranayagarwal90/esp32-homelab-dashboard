#include <Arduino.h>
#include "HomeScreen.h"
#include "AppState.h"
#include "AlertsScreen.h"
#include "Display.h"
#include "UiHelpers.h"

static uint16_t metricColor(float percent) {
  if (percent >= 85) return TFT_RED;
  if (percent >= 70) return TFT_ORANGE;
  return TFT_GREEN;
}

static void drawProgressBar(int x, int y, int w, int h, float percent, uint16_t color) {
  percent = constrain(percent, 0, 100);
  tft.drawRoundRect(x, y, w, h, 4, TFT_DARKGREY);
  int fillWidth = (int)((w - 4) * (percent / 100.0));
  tft.fillRoundRect(x + 2, y + 2, fillWidth, h - 4, 3, color);
  if (fillWidth < w - 4) {
    tft.fillRect(x + 2 + fillWidth, y + 2, (w - 4) - fillWidth, h - 4, TFT_BLACK);
  }
}

static void drawCompactMetric(const char* label, float percent, int x, int y, int w) {
  uint16_t color = metricColor(percent);

  tft.setTextSize(1);
  tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  tft.setCursor(x, y);
  tft.print(label);

  String value = String(percent, 1) + "%";
  int valueW = tft.textWidth(value);
  tft.setTextColor(color, TFT_BLACK);
  tft.setCursor(x + w - valueW, y);
  tft.print(value);

  drawProgressBar(x, y + 12, w, 9, percent, color);
}

static void drawDiskRow(int index, int y) {
  const SystemMetrics& metrics = app.metrics;
  if (index >= metrics.diskCount) return;

  uint16_t color = metricColor(metrics.diskPercentages[index]);

  tft.setTextSize(1);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setCursor(14, y);
  tft.print(metrics.diskNames[index]);

  if (metrics.diskLabels[index].length() > 0) {
    tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
    tft.setCursor(37, y);
    String label = metrics.diskLabels[index];
    if (label.length() > 9) label = label.substring(0, 9);
    tft.print(label);
  }

  tft.setTextColor(color, TFT_BLACK);
  String pct = String(metrics.diskPercentages[index], 1) + "%";
  int pctW = tft.textWidth(pct);
  tft.setCursor(302 - pctW, y);
  tft.print(pct);

  drawProgressBar(110, y - 2, 140, 9, metrics.diskPercentages[index], color);
}

static String formatUptime(float hours) {
  int totalHours = (int)hours;
  int days = totalHours / 24;
  int remainingHours = totalHours % 24;

  if (days > 0) return String(days) + "d " + String(remainingHours) + "h";
  return String(totalHours) + "h";
}

void drawHomePage() {
  const SystemMetrics& metrics = app.metrics;
  app.currentPage = PAGE_HOME;
  tft.fillScreen(TFT_BLACK);
  drawHeader("HOMESERVER STATUS");

  drawCompactMetric("CPU", metrics.cpuPercent, 14, 45, 138);
  drawCompactMetric("RAM", metrics.memPercent, 168, 45, 138);

  tft.setTextSize(1);
  tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
  tft.setCursor(14, 75);
  tft.printf("RAM %.1f / %.1f GB", metrics.memUsed, metrics.memTotal);
  drawAlertBadge();

  tft.drawFastHLine(10, 89, 300, TFT_DARKGREY);

  tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  tft.setCursor(14, 96);
  tft.print("DISKS");

  int diskY = 113;
  for (int i = 0; i < metrics.diskCount && i < 3; i++) {
    drawDiskRow(i, diskY);
    diskY += 18;
  }

  tft.drawFastHLine(10, 170, 300, TFT_DARKGREY);

  tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  tft.setCursor(14, 179);
  tft.print("GPU");
  tft.setTextColor(metricColor(metrics.gpuPercent), TFT_BLACK);
  tft.setCursor(45, 179);
  tft.printf("%.1f%%", metrics.gpuPercent);

  tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  tft.setCursor(105, 179);
  tft.print("WIFI");

  if (metrics.wifiAvailable) {
    tft.setTextColor(TFT_CYAN, TFT_BLACK);
    tft.setCursor(138, 179);
    if (metrics.wifiLinkMbps >= 1000) {
      tft.printf("%.1fG", metrics.wifiLinkMbps / 1000.0);
    } else {
      tft.printf("%.0fM", metrics.wifiLinkMbps);
    }

    if (metrics.wifiSignalPercent >= 0) {
      tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
      tft.setCursor(190, 179);
      tft.printf("%d%%", metrics.wifiSignalPercent);
    }
  } else {
    tft.setTextColor(TFT_RED, TFT_BLACK);
    tft.setCursor(138, 179);
    tft.print("OFF");
  }

  tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  tft.setCursor(231, 179);
  tft.print("UP");

  tft.setTextColor(TFT_GREEN, TFT_BLACK);
  String uptimeText = formatUptime(metrics.uptimeHours);
  int uptimeW = tft.textWidth(uptimeText);
  tft.setCursor(306 - uptimeW, 179);
  tft.print(uptimeText);

  tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
  tft.setCursor(105, 194);
  tft.printf("RX %.2f  TX %.2f Mbps", metrics.wifiRxMbps, metrics.wifiTxMbps);

  drawNavigation();
}
