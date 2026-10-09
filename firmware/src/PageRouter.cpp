#include "PageRouter.h"
#include "AlertsScreen.h"
#include "CalendarScreen.h"
#include "HomeScreen.h"
#include "MenuScreens.h"
#include "PhotoScreen.h"
#include "Screensaver.h"
#include "ServicesScreen.h"
#include "SettingsScreen.h"
#include "StopwatchScreen.h"
#include "TimeWeatherScreen.h"
#include "games/MemoryGame.h"
#include "games/ReactionGame.h"
#include "games/SimonGame.h"
#include "games/SnakeGame.h"
#include "games/TicTacToe.h"

void drawCurrentPage() {
  switch (app.currentPage) {
    case PAGE_HOME: drawHomePage(); break;
    case PAGE_DOCKER: drawDockerPage(); break;
    case PAGE_SERVICES: drawServicesPage(); break;
    case PAGE_MORE: drawMorePage(); break;
    case PAGE_TIME: drawTimePage(); break;
    case PAGE_CALENDAR: drawCalendarPage(); break;
    case PAGE_GAMES: drawGamesPage(); break;
    case PAGE_TTT: drawTTTPage(); break;
    case PAGE_REACTION: drawReactionPage(); break;
    case PAGE_PHOTOS: drawPhotosPage(); break;
    case PAGE_SCREENSAVER: drawScreensaverClock(); break;
    case PAGE_SETTINGS: drawSettingsPage(); break;
    case PAGE_ALERTS: drawAlertsPage(); break;
    case PAGE_TOOLS: drawToolsPage(); break;
    case PAGE_STOPWATCH: drawStopwatchPage(); break;
    case PAGE_SNAKE: drawSnakePage(); break;
    case PAGE_MEMORY: drawMemoryPage(); break;
    case PAGE_SIMON: drawSimonPage(); break;
  }
}

void showPage(Page page) {
  app.currentPage = page;
  drawCurrentPage();
}
