#pragma once
#include "AppState.h"

// PAGE_ALERTS: active homelab alerts, three per page. Main task only.

// Opens the first page; BACK returns to returnTo (MORE or HOME).
void openAlerts(Page returnTo);
void drawAlertsPage();
void handleAlertsTouch(int x, int y);
// HOME indicator: "ALL GOOD" / "2 WARNINGS" / "1 CRITICAL +2", right-aligned
// on the RAM line. Tapping it opens ALERTS (alertBadgeHit()).
void drawAlertBadge();
