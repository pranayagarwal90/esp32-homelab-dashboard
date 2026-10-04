#pragma once
#include "AppState.h"

// Redraws app.currentPage. The only place that knows every screen, so screens
// navigate through showPage() instead of including each other.
void drawCurrentPage();
void showPage(Page page);
