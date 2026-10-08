#pragma once

// Wi-Fi status page and the setup-hotspot page. Main task only.
void drawWifiSettings();
void handleWifiSettingsTouch(int x, int y);
// Periodic refresh of the visible Wi-Fi values; cheap when nothing changed.
void updateWifiSettings();
void drawWifiSetup();
void handleWifiSetupTouch(int x, int y);
void updateWifiSetupView();
