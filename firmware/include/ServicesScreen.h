#pragma once

// Service health and Docker container pages. Main task only.
// MORE > SERVICES: one row per health check, green / red / grey (unreported).
void drawServicesPage();
void handleServicesTouch(int x, int y);
// Not reachable from the current navigation; kept for PAGE_DOCKER.
void drawDockerPage();
