#include "BuildInfo.h"

// Recompiled on every build (scripts/build_info.py), so __DATE__/__TIME__
// always describe the current image.
#ifndef DASHBOARD_GIT_SHA
#define DASHBOARD_GIT_SHA ""
#endif
#ifndef DASHBOARD_BUILD_ENV
#define DASHBOARD_BUILD_ENV ""
#endif
#ifndef DASHBOARD_BUILD_BOARD
#define DASHBOARD_BUILD_BOARD ""
#endif

const char* firmwareVersion() {
  return DASHBOARD_FIRMWARE_VERSION;
}

const char* firmwareBuildTime() {
  static char text[24] = "";
  if (!text[0]) formatBuildTimestamp(__DATE__, __TIME__, text, sizeof(text));
  return text;
}

const char* firmwareGitSha() {
  return buildValueOr(DASHBOARD_GIT_SHA);
}

const char* firmwareBuildEnv() {
  return buildValueOr(DASHBOARD_BUILD_ENV);
}

const char* firmwareBuildBoard() {
  return buildValueOr(DASHBOARD_BUILD_BOARD);
}
