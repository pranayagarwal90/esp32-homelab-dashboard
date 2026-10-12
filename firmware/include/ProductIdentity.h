#pragma once
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

// HomeDeck Mini product identity, pure (host-tested). Every per-device name
// derives from the Wi-Fi station MAC (factory-programmed), so none of it is
// stored in NVS:
//   suffix     979C                  last two MAC bytes, upper-case hex
//   name       HomeDeck Mini 979C    shown on the display
//   hostname   homedeck-mini-979c    DHCP / developer ArduinoOTA (mDNS)
//   setup AP   HomeDeck-Mini-979C    first-run and Settings setup hotspot

#define HOMEDECK_PRODUCT_SLUG "homedeck-mini"
constexpr const char* PRODUCT_NAME = "HomeDeck Mini";
constexpr const char* PRODUCT_SLUG = HOMEDECK_PRODUCT_SLUG; // Update server product id.
constexpr const char* PRODUCT_TITLE = "HOMEDECK MINI";      // Screen title bars.

constexpr size_t DEVICE_SUFFIX_SIZE = 5;  // 4 hex digits.
constexpr size_t DEVICE_LABEL_SIZE = 24;  // Longest: 18 characters.

struct DeviceIdentity {
  char suffix[DEVICE_SUFFIX_SIZE];
  char name[DEVICE_LABEL_SIZE];
  char hostname[DEVICE_LABEL_SIZE];
  char setupAp[DEVICE_LABEL_SIZE];
};

inline void formatDeviceSuffix(const uint8_t mac[6], char* out, size_t size) {
  snprintf(out, size, "%02X%02X", mac[4], mac[5]);
}

inline void makeDeviceIdentity(const uint8_t mac[6], DeviceIdentity& id) {
  formatDeviceSuffix(mac, id.suffix, sizeof(id.suffix));
  char lower[DEVICE_SUFFIX_SIZE];
  snprintf(lower, sizeof(lower), "%02x%02x", mac[4], mac[5]);
  snprintf(id.name, sizeof(id.name), "%s %s", PRODUCT_NAME, id.suffix);
  snprintf(id.hostname, sizeof(id.hostname), "%s-%s", PRODUCT_SLUG, lower);
  snprintf(id.setupAp, sizeof(id.setupAp), "HomeDeck-Mini-%s", id.suffix);
}
