#pragma once

// Service health and Docker container pages. Main task only.
// MORE > SERVICES: the backend's monitored services (services_list), eight
// per page, green / red / grey (unknown); PREV / NEXT when there are more.
void drawServicesPage();
void handleServicesTouch(int x, int y);
// Not reachable from the current navigation; kept for PAGE_DOCKER.
void drawDockerPage();
