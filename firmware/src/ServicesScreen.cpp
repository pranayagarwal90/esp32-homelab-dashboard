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

static int servicesPage = 0; // Kept across status redraws; reset by BACK.

static void printCenteredLine(const char* text, int y, uint16_t color) {
  tft.setTextSize(1);
  tft.setTextColor(color, TFT_BLACK);
  tft.setCursor((320 - tft.textWidth(text)) / 2, y);
  tft.print(text);
}

void drawServicesPage() {
  const ServiceState& services = app.services;
  int pages = pageCountFor(services.count, SERVICES_PER_PAGE);
  servicesPage = clampPageIndex(servicesPage, services.count, SERVICES_PER_PAGE);
  app.currentPage = PAGE_SERVICES;
  tft.fillScreen(TFT_BLACK);
  char title[20];
  if (pages > 1) snprintf(title, sizeof(title), "SERVICES %d/%d", servicesPage + 1, pages);
  else snprintf(title, sizeof(title), "SERVICES");
  drawHeader(title);

  if (!services.received) {
    printCenteredLine("Waiting for the first status update.", 96, TFT_LIGHTGREY);
  } else if (services.count == 0) {
    tft.setTextSize(2);
    tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    tft.setCursor((320 - tft.textWidth("No services configured")) / 2, 80);
    tft.print("No services configured");
    printCenteredLine("Choose them with SERVICES= on the", 112, TFT_DARKGREY);
    printCenteredLine("HomeDeck Mini backend.", 126, TFT_DARKGREY);
  }

  int first = servicesPage * SERVICES_PER_PAGE;
  for (int row = 0; row < SERVICES_PER_PAGE && first + row < services.count; row++) {
    const ServiceStatus& service = services.items[first + row];
    int y = SERVICES_Y0 + row * SERVICES_PITCH;
    ServiceDot dot = serviceDot(service.status);
    tft.fillCircle(22, y + 7, 5, dotColor(dot));
    tft.setTextSize(2);
    tft.setTextColor(dot == ServiceDot::Unknown ? TFT_DARKGREY : TFT_WHITE, TFT_BLACK);
    tft.setCursor(38, y);
    tft.print(service.label);
  }
  drawBackBar(pages > 1 && servicesPage > 0 ? "PREV" : nullptr, "BACK",
              pages > 1 && servicesPage < pages - 1 ? "NEXT" : nullptr);
}

void handleServicesTouch(int x, int y) {
  int pages = pageCountFor(app.services.count, SERVICES_PER_PAGE);
  switch (pagerBarAt(x, y, pages)) {
    case PagerHit::Prev:
      if (servicesPage > 0) {
        servicesPage--;
        drawServicesPage();
      }
      break;
    case PagerHit::Next:
      if (servicesPage < pages - 1) {
        servicesPage++;
        drawServicesPage();
      }
      break;
    case PagerHit::Back:
      servicesPage = 0;
      showPage(appBackTarget(PAGE_SERVICES, PAGE_MORE));
      break;
    case PagerHit::None:
      break;
  }
}
