#pragma once
#include <Arduino.h>
#include "Pages.h"
#include "WeatherLogic.h"

// Application state shared between screens. Main task only: the status worker
// fills a private snapshot that StatusClient copies in here.

constexpr int MAX_DISKS = 4;
constexpr int MAX_CONTAINERS = 7;

struct SystemMetrics {
  // False when the backend could not read the host (cached or zero values).
  bool hostAvailable = true;
  float uptimeHours = 0;
  float cpuPercent = 0;
  float memUsed = 0;
  float memTotal = 0;
  float memPercent = 0;
  String diskNames[MAX_DISKS];
  String diskLabels[MAX_DISKS];
  float diskUsedGb[MAX_DISKS] = {};
  float diskTotalGb[MAX_DISKS] = {};
  float diskPercentages[MAX_DISKS] = {};
  int diskCount = 0;
  float gpuPercent = 0;
  String gpuName = "";
  bool wifiAvailable = false;
  float wifiLinkMbps = 0;
  float wifiRxMbps = 0;
  float wifiTxMbps = 0;
  int wifiSignalPercent = -1;
};

struct DockerData {
  String containerNames[MAX_CONTAINERS];
  bool containerRunning[MAX_CONTAINERS] = {};
  int displayedContainers = 0;
  int totalRunningContainers = 0;
};

struct ServiceState {
  bool serviceJellyfin = false;
  bool serviceNavidrome = false;
  bool serviceNextcloud = false;
  bool serviceImmich = false;
  bool serviceOllama = false;
  bool serviceCloudflare = false;
  bool serviceTechnicalBlog = false;
  bool serviceMetube = false;
  // Monitored health keys actually present in the response (AlertService
  // bits); a missing key is unknown, not offline.
  uint8_t alertReported = 0;
  // SERVICES list (ServiceId bits): key present as a bool / reported healthy.
  uint8_t reported = 0;
  uint8_t online = 0;
};

struct TimeData {
  String localTime = "--:--";
  String localDate = "---";
  String indiaTime = "--:--";
  String singaporeTime = "--:--";
  String londonTime = "--:--";
  int currentYear = 2026;
  int currentMonth = 1;
  int currentDay = 1;
};

struct WeatherData {
  float temperatureC = 0;
  float highC = 0;
  float lowC = 0;
  String weatherCondition = "Unknown";
  bool weatherAvailable = false;
  // Rich weather (optional backend fields; WEATHER_NO_VALUE / -1 if absent).
  int16_t weatherCode = -1;
  int16_t feelsLike10 = WEATHER_NO_VALUE;
  int16_t wind10 = WEATHER_NO_VALUE;      // km/h x10.
  int8_t humidity = -1;
  int8_t precipChance = -1;               // Current hour.
  int8_t precipChanceMax = -1;            // Today.
  uint8_t hourlyCount = 0;
  HourlyForecast hourly[WEATHER_HOURLY_MAX];
  // Day/night and sunrise/sunset display.
  uint32_t sunrise = 0;
  uint32_t sunset = 0;
  uint32_t localMidnight = 0;             // solar_day_start: gives the UTC offset.
  uint32_t observedAt = 0;                // Status timestamp (Unix).
  uint32_t observedMs = 0;                // millis() when applied.
};

struct AppState {
  Page currentPage = PAGE_HOME;
  unsigned long lastInteraction = 0;
  bool serverOnline = false;
  SystemMetrics metrics;
  DockerData docker;
  ServiceState services;
  TimeData time;
  WeatherData weather;
};

extern AppState app;
