#include <Arduino.h>
#include "BluetoothControl.h"
#include "SettingsStore.h"

#if DASHBOARD_BLUETOOTH
#include <esp_bt.h>

void setupBluetooth() {
  if (!bluetoothReservedAtBoot()) return; // Memory released by the core: stay off.
  esp_bt_controller_config_t config = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
  if (esp_bt_controller_get_status() == ESP_BT_CONTROLLER_STATUS_IDLE &&
      esp_bt_controller_init(&config) != ESP_OK) {
    Serial.println("Bluetooth controller init failed");
    return;
  }
  if (esp_bt_controller_get_status() == ESP_BT_CONTROLLER_STATUS_INITED &&
      esp_bt_controller_enable(ESP_BT_MODE_BTDM) != ESP_OK) {
    Serial.println("Bluetooth controller enable failed");
    return;
  }
  Serial.println("Bluetooth controller enabled");
}

bool bluetoothControllerEnabled() {
  return esp_bt_controller_get_status() == ESP_BT_CONTROLLER_STATUS_ENABLED;
}

bool bluetoothRestartPending() {
  return settings().bluetoothEnabled != bluetoothReservedAtBoot();
}

void stopBluetoothForSleep() {
  if (bluetoothControllerEnabled()) esp_bt_controller_disable();
}

#else // Bluetooth not compiled in: nothing references the controller library.

void setupBluetooth() {}

bool bluetoothControllerEnabled() {
  return false;
}

bool bluetoothRestartPending() {
  return false;
}

void stopBluetoothForSleep() {}

#endif
