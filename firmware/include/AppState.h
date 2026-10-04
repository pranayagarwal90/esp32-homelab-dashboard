#pragma once
#include <Arduino.h>

// Application state shared between screens. Main task only: the status worker
// fills a private snapshot that StatusClient copies in here.

enum Page {
  PAGE_HOME,
  PAGE_DOCKER,
  PAGE_SERVICES,
  PAGE_MORE,
  PAGE_TIME,
  PAGE_CALENDAR,
  PAGE_GAMES,
  PAGE_TTT,
  PAGE_REACTION,
  PAGE_PHOTOS,
  PAGE_SCREENSAVER
};

constexpr int MAX_DISKS = 4;
constexpr int MAX_CONTAINERS = 7;

struct SystemMetrics {
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
