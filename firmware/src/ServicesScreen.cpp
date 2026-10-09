#include <Arduino.h>
#include "ServicesScreen.h"
#include "AppState.h"
#include "Display.h"
#include "PageRouter.h"
#include "ServicesLogic.h"
#include "UiHelpers.h"
#include "UiTheme.h"

static String shortName(String name) {
  if (name.length() > 24) return name.substring(0, 21) + "...";
  return name;
}

void drawDockerPage() {
  const DockerData& docker = app.docker;
  app.currentPage = PAGE_DOCKER;
  tft.fillScreen(TFT_BLACK);
  drawHeader("DOCKER");

  tft.setTextSize(1);
  tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  tft.setCursor(15, 43);
  tft.print("Running: ");
  tft.setTextColor(TFT_GREEN, TFT_BLACK);
  tft.print(docker.totalRunningContainers);

  int y = 60;
  for (int i = 0; i < docker.displayedContainers; i++) {
    uint16_t color = docker.containerRunning[i] ? TFT_GREEN : TFT_RED;
    tft.fillCircle(17, y + 5, 4, color);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setCursor(30, y);
    tft.print(shortName(docker.containerNames[i]));
    y += 20;
  }

  if (docker.displayedContainers == 0) {
    tft.setTextColor(TFT_YELLOW, TFT_BLACK);
    tft.setCursor(25, 90);
    tft.print("No containers found");
  }

  drawNavigation();
}

static uint16_t dotColor(ServiceDot dot) {
  switch (dot) {
    case ServiceDot::Online: return UiColor::Green;
    case ServiceDot::Offline: return UiColor::Red;
    default: return UiColor::Grey;
  }
}

void drawServicesPage() {
  const ServiceState& services = app.services;
  app.currentPage = PAGE_SERVICES;
  tft.fillScreen(TFT_BLACK);
  drawHeader("SERVICES");
  for (int id = 0; id < SVC_COUNT; id++) {
    int y = SERVICES_Y0 + id * SERVICES_PITCH;
    ServiceDot dot = serviceDot(services.reported, services.online, id);
    tft.fillCircle(22, y + 7, 5, dotColor(dot));
    tft.setTextSize(2);
    tft.setTextColor(dot == ServiceDot::Unknown ? TFT_DARKGREY : TFT_WHITE, TFT_BLACK);
    tft.setCursor(38, y);
    tft.print(SERVICE_LIST[id].label);
  }
  drawBackBar(nullptr, "BACK", nullptr);
}

void handleServicesTouch(int, int y) {
  if (y >= NAV_Y) showPage(appBackTarget(PAGE_SERVICES, PAGE_MORE));
}
