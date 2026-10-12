#include <Arduino.h>
#include <esp_mac.h>
#include "DeviceIdentity.h"

static DeviceIdentity identity;

void initDeviceIdentity() {
  uint8_t mac[6] = {0};
  // The factory station MAC; works before Wi-Fi starts (same value as
  // WiFi.macAddress()).
  esp_read_mac(mac, ESP_MAC_WIFI_STA);
  makeDeviceIdentity(mac, identity);
  Serial.printf("%s (hostname %s)\n", identity.name, identity.hostname);
}

const DeviceIdentity& deviceIdentity() {
  return identity;
}
