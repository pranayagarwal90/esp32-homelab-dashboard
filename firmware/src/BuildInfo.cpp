#include "BuildInfo.h"
#include "ProductIdentity.h"

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

// The version, behind a marker that tools/prepare-firmware-release.py looks
// for in a firmware .bin, so a release's manifest version always matches the
// version the image reports. Returned from here, so it is never discarded.
static const char FIRMWARE_ID[] = FIRMWARE_ID_PREFIX HOMEDECK_PRODUCT_SLUG ":" DASHBOARD_FIRMWARE_VERSION;

const char* firmwareVersion() {
  return FIRMWARE_ID + sizeof(FIRMWARE_ID_PREFIX HOMEDECK_PRODUCT_SLUG ":") - 1;
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
