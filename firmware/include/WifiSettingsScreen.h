#pragma once

// Wi-Fi status page, Dashboard Server page and the setup-hotspot page.
// Main task only.
void drawWifiSettings();
void handleWifiSettingsTouch(int x, int y);
// Periodic refresh of the visible Wi-Fi values; cheap when nothing changed.
void updateWifiSettings();
void drawServerSettings();
void handleServerSettingsTouch(int x, int y);
void updateServerSettings();
// Settings > Location: the saved place, local time, weather freshness.
void drawLocationSettings();
void handleLocationSettingsTouch(int x, int y);
void updateLocationSettings();
void drawWifiSetup();
void handleWifiSetupTouch(int x, int y);
void updateWifiSetupView();
// The setup hotspot's join instructions (SSID, password, address) from y, and
// its two-line status at statusY; shared with Setup Mode.
void drawSetupInstructions(int y);
void drawSetupStatus(int statusY);
