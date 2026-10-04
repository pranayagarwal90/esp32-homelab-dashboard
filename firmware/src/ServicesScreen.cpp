#include <Arduino.h>
#include "ServicesScreen.h"
#include "AppState.h"
#include "Display.h"
#include "UiHelpers.h"

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

static void drawServiceRow(const char* name, bool running, int y) {
  uint16_t color = running ? TFT_GREEN : TFT_RED;
  tft.fillCircle(18, y + 5, 5, color);
  tft.setTextSize(1);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setCursor(32, y);
  tft.print(name);
  tft.setCursor(250, y);
  tft.setTextColor(color, TFT_BLACK);
  tft.print(running ? "ONLINE" : "OFFLINE");
}

void drawServicesPage() {
  const ServiceState& services = app.services;
  app.currentPage = PAGE_SERVICES;
  tft.fillScreen(TFT_BLACK);
  drawHeader("SERVICES");

  int y = 48;
  drawServiceRow("Jellyfin", services.serviceJellyfin, y); y += 22;
  drawServiceRow("Navidrome", services.serviceNavidrome, y); y += 22;
  drawServiceRow("Ollama", services.serviceOllama, y); y += 22;
  drawServiceRow("Cloudfare", services.serviceCloudflare, y); y += 22;
  drawServiceRow("NextCloud", services.serviceNextcloud, y); y += 22;
  drawServiceRow("Immich", services.serviceImmich, y); y += 22;
  drawServiceRow("Technical Blog", services.serviceTechnicalBlog, y);

  drawNavigation();
}
