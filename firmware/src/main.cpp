#include <Arduino.h>

#include <WiFi.h>

#include <HTTPClient.h>

#include <ArduinoJson.h>

#include <ArduinoOTA.h>

#include <TFT_eSPI.h>

#include <TJpg_Decoder.h>

#include <XPT2046_Touchscreen.h>

#include <SPI.h>

#include "secrets.h"



TFT_eSPI tft = TFT_eSPI();



#define TOUCH_CLK 25

#define TOUCH_MISO 39

#define TOUCH_MOSI 32

#define TOUCH_CS 33



SPIClass touchSPI(HSPI);

XPT2046_Touchscreen touch(TOUCH_CS);



#define RAW_X_MIN 328

#define RAW_X_MAX 3630

#define RAW_Y_MIN 526

#define RAW_Y_MAX 3613



const char* API_BASE = "http://192.168.1.13:8090";

const char* API_STATUS = "http://192.168.1.13:8090/api/status";

const char* API_PHOTOS = "http://192.168.1.13:8090/api/photos";



const unsigned long REFRESH_INTERVAL = 10000;

const unsigned long SCREENSAVER_TIMEOUT = 180000;

const unsigned long SCREENSAVER_ROTATE = 30000;



unsigned long lastRefresh = 0;

unsigned long lastTouchTime = 0;

unsigned long lastInteraction = 0;

unsigned long screensaverLastRotate = 0;



// --------------------------------------------------

// Pages

// --------------------------------------------------



enum Page {

  PAGE_HOME,

  PAGE_DOCKER,

  PAGE_SERVICES,

  PAGE_MORE,

  PAGE_TIME,

  PAGE_CALENDAR,

  PAGE_GAMES,

  PAGE_TTT,

  PAGE_REACTION,

  PAGE_PHOTOS,

  PAGE_SCREENSAVER

};



Page currentPage = PAGE_HOME;

Page pageBeforeScreensaver = PAGE_HOME;



// --------------------------------------------------

// System data

// --------------------------------------------------



float uptimeHours = 0;
float cpuPercent = 0;

float memUsed = 0;
float memTotal = 0;
float memPercent = 0;

#define MAX_DISKS 4
String diskNames[MAX_DISKS];
String diskLabels[MAX_DISKS];
float diskUsedGb[MAX_DISKS];
float diskTotalGb[MAX_DISKS];
float diskPercentages[MAX_DISKS];
int diskCount = 0;

float gpuPercent = 0;
String gpuName = "";

bool wifiAvailable = false;
float wifiLinkMbps = 0;
float wifiRxMbps = 0;
float wifiTxMbps = 0;
int wifiSignalPercent = -1;

bool serverOnline = false;



#define MAX_CONTAINERS 7

String containerNames[MAX_CONTAINERS];

bool containerRunning[MAX_CONTAINERS];

int displayedContainers = 0;

int totalRunningContainers = 0;



bool serviceJellyfin = false;

bool serviceNavidrome = false;

bool serviceNextcloud = false;

bool serviceImmich = false;

bool serviceOllama = false;

bool serviceCloudflare = false;

bool serviceTechnicalBlog = false;



// --------------------------------------------------

// Time/weather/calendar

// --------------------------------------------------



String localTime = "--:--";

String localDate = "---";

String indiaTime = "--:--";

String singaporeTime = "--:--";

String londonTime = "--:--";

float temperatureC = 0;

float highC = 0;

float lowC = 0;

String weatherCondition = "Unknown";

bool weatherAvailable = false;



int currentYear = 2026;

int currentMonth = 1;

int currentDay = 1;

int calendarYear = 2026;

int calendarMonth = 1;



// --------------------------------------------------

// Photos

// --------------------------------------------------



#define MAX_PHOTOS 20

String photoNames[MAX_PHOTOS];

int photoCount = 0;

int photoIndex = 0;

bool photoListLoaded = false;

bool screensaverShowingPhoto = false;



// --------------------------------------------------

// Tic-tac-toe

// --------------------------------------------------



char tttBoard[9] = {

  ' ', ' ', ' ',

  ' ', ' ', ' ',

  ' ', ' ', ' '

};

bool tttGameOver = false;

String tttMessage = "YOUR TURN";



// --------------------------------------------------

// Reaction game

// --------------------------------------------------



enum ReactionState {

  REACTION_IDLE,

  REACTION_WAITING,

  REACTION_GO,

  REACTION_RESULT,

  REACTION_TOO_SOON

};



ReactionState reactionState = REACTION_IDLE;

unsigned long reactionTargetTime = 0;

unsigned long reactionStartTime = 0;

unsigned long reactionResultMs = 0;



// --------------------------------------------------

// Forward declarations

// --------------------------------------------------



void drawCurrentPage();

void drawHomePage();

void drawDockerPage();

void drawServicesPage();

void drawMorePage();

void drawTimePage();

void drawCalendarPage();

void drawGamesPage();

void drawTTTPage();

void drawReactionPage();

void drawPhotosPage();

void drawScreensaverClock();

void enterScreensaver();

void exitScreensaver();

void fetchHomelabStatus();

void fetchPhotoList();

bool showPhoto(int index, bool overlayControls);

void updateReactionGame();

void handleTouch();



// --------------------------------------------------

// Helpers

// --------------------------------------------------



uint16_t metricColor(float percent) {

  if (percent >= 85) return TFT_RED;

  if (percent >= 70) return TFT_ORANGE;

  return TFT_GREEN;

}



void drawProgressBar(int x, int y, int w, int h, float percent, uint16_t color) {

  percent = constrain(percent, 0, 100);

  tft.drawRoundRect(x, y, w, h, 4, TFT_DARKGREY);

  int fillWidth = (int)((w - 4) * (percent / 100.0));

  tft.fillRoundRect(x + 2, y + 2, fillWidth, h - 4, 3, color);

  if (fillWidth < w - 4) {

    tft.fillRect(x + 2 + fillWidth, y + 2, (w - 4) - fillWidth, h - 4, TFT_BLACK);

  }

}



String shortName(String name) {

  if (name.length() > 24) return name.substring(0, 21) + "...";

  return name;

}



void drawHeader(const char* title) {

  tft.fillRect(0, 0, 320, 36, TFT_DARKGREY);

  tft.setTextSize(2);

  tft.setTextColor(TFT_WHITE, TFT_DARKGREY);

  tft.setCursor(10, 10);

  tft.print(title);



  if (serverOnline) {

    tft.setTextColor(TFT_GREEN, TFT_DARKGREY);

    tft.setCursor(248, 10);

    tft.print("LIVE");

  } else {

    tft.setTextColor(TFT_RED, TFT_DARKGREY);

    tft.setCursor(225, 10);

    tft.print("OFFLINE");

  }

}



void drawNavigation() {
  tft.drawFastHLine(0, 207, 320, TFT_DARKGREY);

  struct NavItem {
    int x;
    int w;
    const char* label;
    Page page;
  } items[] = {
    {0,   107, "HOME",     PAGE_HOME},
    {107, 107, "SERVICES", PAGE_SERVICES},
    {214, 106, "MORE",     PAGE_MORE}
  };

  for (auto &item : items) {
    uint16_t color = currentPage == item.page ? TFT_BLUE : TFT_DARKGREY;
    tft.fillRect(item.x, 208, item.w, 32, color);
    tft.setTextSize(1);
    tft.setTextColor(TFT_WHITE, color);
    int textW = tft.textWidth(item.label);
    tft.setCursor(item.x + (item.w - textW) / 2, 220);
    tft.print(item.label);
  }
}



void drawBackBar(const char* left = nullptr, const char* center = "BACK", const char* right = nullptr) {

  tft.fillRect(0, 208, 320, 32, TFT_DARKGREY);

  tft.setTextSize(1);

  tft.setTextColor(TFT_WHITE, TFT_DARKGREY);

  if (left) {

    tft.setCursor(35, 220);

    tft.print(left);

  }

  if (center) {

    int w = tft.textWidth(center);

    tft.setCursor((320 - w) / 2, 220);

    tft.print(center);

  }

  if (right) {

    int w = tft.textWidth(right);

    tft.setCursor(285 - w, 220);

    tft.print(right);

  }

}



void drawMenuButton(int x, int y, int w, int h, const char* label) {

  tft.fillRoundRect(x, y, w, h, 8, TFT_DARKGREY);

  tft.drawRoundRect(x, y, w, h, 8, TFT_LIGHTGREY);

  tft.setTextSize(1);

  tft.setTextColor(TFT_WHITE, TFT_DARKGREY);

  int textWidth = tft.textWidth(label);

  tft.setCursor(x + (w - textWidth) / 2, y + (h / 2) - 3);

  tft.print(label);

}



// --------------------------------------------------

// Home

// --------------------------------------------------



void drawCompactMetric(const char* label, float percent, int x, int y, int w) {
  uint16_t color = metricColor(percent);

  tft.setTextSize(1);
  tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  tft.setCursor(x, y);
  tft.print(label);

  String value = String(percent, 1) + "%";
  int valueW = tft.textWidth(value);
  tft.setTextColor(color, TFT_BLACK);
  tft.setCursor(x + w - valueW, y);
  tft.print(value);

  drawProgressBar(x, y + 12, w, 9, percent, color);
}


void drawDiskRow(int index, int y) {
  if (index >= diskCount) return;

  uint16_t color = metricColor(diskPercentages[index]);

  tft.setTextSize(1);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setCursor(14, y);
  tft.print(diskNames[index]);

  if (diskLabels[index].length() > 0) {
    tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
    tft.setCursor(37, y);
    String label = diskLabels[index];
    if (label.length() > 9) label = label.substring(0, 9);
    tft.print(label);
  }

  tft.setTextColor(color, TFT_BLACK);
  String pct = String(diskPercentages[index], 1) + "%";
  int pctW = tft.textWidth(pct);
  tft.setCursor(302 - pctW, y);
  tft.print(pct);

  drawProgressBar(110, y - 2, 140, 9, diskPercentages[index], color);
}


String formatUptime(float hours) {
  int totalHours = (int)hours;
  int days = totalHours / 24;
  int remainingHours = totalHours % 24;

  if (days > 0) return String(days) + "d " + String(remainingHours) + "h";
  return String(totalHours) + "h";
}


void drawHomePage() {
  currentPage = PAGE_HOME;
  tft.fillScreen(TFT_BLACK);
  drawHeader("HOMESERVER STATUS");

  drawCompactMetric("CPU", cpuPercent, 14, 45, 138);
  drawCompactMetric("RAM", memPercent, 168, 45, 138);

  tft.setTextSize(1);
  tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
  tft.setCursor(14, 75);
  tft.printf("RAM %.1f / %.1f GB", memUsed, memTotal);

  tft.drawFastHLine(10, 89, 300, TFT_DARKGREY);

  tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  tft.setCursor(14, 96);
  tft.print("DISKS");

  int diskY = 113;
  for (int i = 0; i < diskCount && i < 3; i++) {
    drawDiskRow(i, diskY);
    diskY += 18;
  }

  tft.drawFastHLine(10, 170, 300, TFT_DARKGREY);

  tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  tft.setCursor(14, 179);
  tft.print("GPU");
  tft.setTextColor(metricColor(gpuPercent), TFT_BLACK);
  tft.setCursor(45, 179);
  tft.printf("%.1f%%", gpuPercent);

  tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  tft.setCursor(105, 179);
  tft.print("WIFI");

  if (wifiAvailable) {
    tft.setTextColor(TFT_CYAN, TFT_BLACK);
    tft.setCursor(138, 179);
    if (wifiLinkMbps >= 1000) {
      tft.printf("%.1fG", wifiLinkMbps / 1000.0);
    } else {
      tft.printf("%.0fM", wifiLinkMbps);
    }

    if (wifiSignalPercent >= 0) {
      tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
      tft.setCursor(190, 179);
      tft.printf("%d%%", wifiSignalPercent);
    }
  } else {
    tft.setTextColor(TFT_RED, TFT_BLACK);
    tft.setCursor(138, 179);
    tft.print("OFF");
  }

  tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  tft.setCursor(231, 179);
  tft.print("UP");

  tft.setTextColor(TFT_GREEN, TFT_BLACK);
  String uptimeText = formatUptime(uptimeHours);
  int uptimeW = tft.textWidth(uptimeText);
  tft.setCursor(306 - uptimeW, 179);
  tft.print(uptimeText);

  tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
  tft.setCursor(105, 194);
  tft.printf("RX %.2f  TX %.2f Mbps", wifiRxMbps, wifiTxMbps);

  drawNavigation();
}



// --------------------------------------------------

// Docker

// --------------------------------------------------



void drawDockerPage() {

  currentPage = PAGE_DOCKER;

  tft.fillScreen(TFT_BLACK);

  drawHeader("DOCKER");



  tft.setTextSize(1);

  tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);

  tft.setCursor(15, 43);

  tft.print("Running: ");

  tft.setTextColor(TFT_GREEN, TFT_BLACK);

  tft.print(totalRunningContainers);



  int y = 60;

  for (int i = 0; i < displayedContainers; i++) {

    uint16_t color = containerRunning[i] ? TFT_GREEN : TFT_RED;

    tft.fillCircle(17, y + 5, 4, color);

    tft.setTextColor(TFT_WHITE, TFT_BLACK);

    tft.setCursor(30, y);

    tft.print(shortName(containerNames[i]));

    y += 20;

  }



  if (displayedContainers == 0) {

    tft.setTextColor(TFT_YELLOW, TFT_BLACK);

    tft.setCursor(25, 90);

    tft.print("No containers found");

  }



  drawNavigation();

}



// --------------------------------------------------

// Services

// --------------------------------------------------



void drawServiceRow(const char* name, bool running, int y) {

  uint16_t color = running ? TFT_GREEN : TFT_RED;

  tft.fillCircle(18, y + 5, 5, color);

  tft.setTextSize(1);

  tft.setTextColor(TFT_WHITE, TFT_BLACK);

  tft.setCursor(32, y);

  tft.print(name);

  tft.setCursor(250, y);

  tft.setTextColor(color, TFT_BLACK);

  tft.print(running ? "ONLINE" : "OFFLINE");

}



void drawServicesPage() {

  currentPage = PAGE_SERVICES;

  tft.fillScreen(TFT_BLACK);

  drawHeader("SERVICES");



  int y = 48;

  drawServiceRow("Jellyfin", serviceJellyfin, y); y += 22;

  drawServiceRow("Navidrome", serviceNavidrome, y); y += 22;

  drawServiceRow("Ollama", serviceOllama, y); y += 22;

  drawServiceRow("Cloudfare", serviceCloudflare, y); y += 22;

  drawServiceRow("NextCloud", serviceNextcloud, y); y += 22;

  drawServiceRow("Immich", serviceImmich, y); y += 22;

  drawServiceRow("Technical Blog", serviceTechnicalBlog, y);



  drawNavigation();

}



// --------------------------------------------------

// More menu

// --------------------------------------------------



void drawMorePage() {

  currentPage = PAGE_MORE;

  tft.fillScreen(TFT_BLACK);

  drawHeader("MORE");



  drawMenuButton(15, 50, 140, 55, "TIME / WEATHER");

  drawMenuButton(165, 50, 140, 55, "CALENDAR");

  drawMenuButton(15, 120, 140, 55, "GAMES");

  drawMenuButton(165, 120, 140, 55, "PHOTOS");



  drawNavigation();

}



// --------------------------------------------------

// Time/weather

// --------------------------------------------------



void drawTimeRow(const char* label, const String &timeValue, int y) {

  tft.setTextSize(1);

  tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);

  tft.setCursor(22, y);

  tft.print(label);

  tft.setTextColor(TFT_WHITE, TFT_BLACK);

  tft.setCursor(205, y);

  tft.print(timeValue);

}



void drawTimePage() {

  currentPage = PAGE_TIME;

  tft.fillScreen(TFT_BLACK);



  tft.setTextSize(1);

  tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);

  tft.setCursor(12, 8);

  tft.print(localDate);



  if (weatherAvailable) {

    tft.setTextColor(TFT_CYAN, TFT_BLACK);

    tft.setCursor(260, 8);

    tft.printf("%.1fC", temperatureC);

  }



  tft.setTextSize(4);

  tft.setTextColor(TFT_WHITE, TFT_BLACK);

  int timeWidth = tft.textWidth(localTime);

  tft.setCursor((320 - timeWidth) / 2, 35);

  tft.print(localTime);



  if (weatherAvailable) {

    tft.setTextSize(1);

    tft.setTextColor(TFT_CYAN, TFT_BLACK);

    tft.setCursor(15, 88);

    tft.print(weatherCondition);

    tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);

    tft.setCursor(15, 103);

    tft.printf("High %.1fC   Low %.1fC", highC, lowC);

  } else {

    tft.setTextSize(1);

    tft.setTextColor(TFT_RED, TFT_BLACK);

    tft.setCursor(15, 95);

    tft.print("Weather unavailable");

  }



  tft.drawFastHLine(10, 120, 300, TFT_DARKGREY);

  drawTimeRow("India", indiaTime, 135);

  drawTimeRow("Singapore", singaporeTime, 157);

  drawTimeRow("London", londonTime, 179);

  drawBackBar(nullptr, "BACK", nullptr);

}



// --------------------------------------------------

// Calendar

// --------------------------------------------------



const char* MONTH_NAMES[] = {

  "JANUARY", "FEBRUARY", "MARCH", "APRIL", "MAY", "JUNE",

  "JULY", "AUGUST", "SEPTEMBER", "OCTOBER", "NOVEMBER", "DECEMBER"

};



bool isLeapYear(int year) {

  return (year % 400 == 0) || ((year % 4 == 0) && (year % 100 != 0));

}



int daysInMonth(int year, int month) {

  static const int days[] = {31,28,31,30,31,30,31,31,30,31,30,31};

  if (month == 2 && isLeapYear(year)) return 29;

  return days[month - 1];

}



// 0=Sunday ... 6=Saturday

int dayOfWeek(int year, int month, int day) {

  static int t[] = {0,3,2,5,0,3,5,1,4,6,2,4};

  if (month < 3) year -= 1;

  return (year + year/4 - year/100 + year/400 + t[month-1] + day) % 7;

}



void changeCalendarMonth(int delta) {

  calendarMonth += delta;

  if (calendarMonth < 1) {

    calendarMonth = 12;

    calendarYear--;

  } else if (calendarMonth > 12) {

    calendarMonth = 1;

    calendarYear++;

  }

}



void drawCalendarPage() {

  currentPage = PAGE_CALENDAR;

  tft.fillScreen(TFT_BLACK);



  tft.setTextSize(2);

  tft.setTextColor(TFT_WHITE, TFT_BLACK);

  String title = String(MONTH_NAMES[calendarMonth - 1]) + " " + String(calendarYear);

  int titleWidth = tft.textWidth(title);

  tft.setCursor((320 - titleWidth) / 2, 8);

  tft.print(title);



  const char* weekdays[] = {"SU","MO","TU","WE","TH","FR","SA"};

  tft.setTextSize(1);

  tft.setTextColor(TFT_CYAN, TFT_BLACK);

  for (int i = 0; i < 7; i++) {

    tft.setCursor(15 + i * 43, 38);

    tft.print(weekdays[i]);

  }



  int first = dayOfWeek(calendarYear, calendarMonth, 1);

  int totalDays = daysInMonth(calendarYear, calendarMonth);

  int cellW = 43;

  int cellH = 23;

  int startX = 10;

  int startY = 55;



  for (int day = 1; day <= totalDays; day++) {

    int pos = first + day - 1;

    int col = pos % 7;

    int row = pos / 7;

    int x = startX + col * cellW;

    int y = startY + row * cellH;



    bool today = calendarYear == currentYear && calendarMonth == currentMonth && day == currentDay;

    if (today) {

      tft.fillCircle(x + 10, y + 5, 10, TFT_BLUE);

      tft.setTextColor(TFT_WHITE, TFT_BLUE);

    } else {

      tft.setTextColor(TFT_WHITE, TFT_BLACK);

    }



    tft.setCursor(x + (day < 10 ? 7 : 4), y + 2);

    tft.print(day);

  }



  tft.fillRect(0, 208, 106, 32, TFT_DARKGREY);

  tft.fillRect(106, 208, 108, 32, TFT_DARKGREY);

  tft.fillRect(214, 208, 106, 32, TFT_DARKGREY);

  tft.setTextSize(1);

  tft.setTextColor(TFT_WHITE, TFT_DARKGREY);

  tft.setCursor(33, 220); tft.print("< PREV");

  tft.setCursor(145, 220); tft.print("BACK");

  tft.setCursor(247, 220); tft.print("NEXT >");

}



// --------------------------------------------------

// Games menu

// --------------------------------------------------



void drawGamesPage() {

  currentPage = PAGE_GAMES;

  tft.fillScreen(TFT_BLACK);

  drawHeader("GAMES");

  drawMenuButton(25, 50, 270, 55, "TIC-TAC-TOE");

  drawMenuButton(25, 120, 270, 55, "REACTION TAP");

  drawBackBar(nullptr, "BACK", nullptr);

}



// --------------------------------------------------

// Tic-tac-toe

// --------------------------------------------------



void resetTTT() {

  for (int i = 0; i < 9; i++) tttBoard[i] = ' ';

  tttGameOver = false;

  tttMessage = "YOUR TURN";

}



char checkTTTWinner() {

  const int wins[8][3] = {

    {0,1,2},{3,4,5},{6,7,8},

    {0,3,6},{1,4,7},{2,5,8},

    {0,4,8},{2,4,6}

  };



  for (int i = 0; i < 8; i++) {

    int a = wins[i][0], b = wins[i][1], c = wins[i][2];

    if (tttBoard[a] != ' ' && tttBoard[a] == tttBoard[b] && tttBoard[b] == tttBoard[c]) {

      return tttBoard[a];

    }

  }



  for (int i = 0; i < 9; i++) if (tttBoard[i] == ' ') return ' ';

  return 'D';

}



int findWinningMove(char player) {

  for (int i = 0; i < 9; i++) {

    if (tttBoard[i] != ' ') continue;

    tttBoard[i] = player;

    char winner = checkTTTWinner();

    tttBoard[i] = ' ';

    if (winner == player) return i;

  }

  return -1;

}



void esp32Move() {

  if (tttGameOver) return;



  int move = findWinningMove('O');

  if (move == -1) move = findWinningMove('X');

  if (move == -1 && tttBoard[4] == ' ') move = 4;



  if (move == -1) {

    int corners[] = {0,2,6,8};

    for (int i = 0; i < 4; i++) {

      if (tttBoard[corners[i]] == ' ') {

        move = corners[i];

        break;

      }

    }

  }



  if (move == -1) {

    for (int i = 0; i < 9; i++) {

      if (tttBoard[i] == ' ') {

        move = i;

        break;

      }

    }

  }



  if (move >= 0) tttBoard[move] = 'O';



  char result = checkTTTWinner();

  if (result == 'O') {

    tttGameOver = true;

    tttMessage = "ESP32 WINS";

  } else if (result == 'D') {

    tttGameOver = true;

    tttMessage = "DRAW";

  } else {

    tttMessage = "YOUR TURN";

  }

}



void drawTTTPage() {

  currentPage = PAGE_TTT;

  tft.fillScreen(TFT_BLACK);

  tft.setTextSize(2);

  tft.setTextColor(TFT_WHITE, TFT_BLACK);

  tft.setCursor(8, 8);

  tft.print("TIC-TAC-TOE");



  tft.setTextSize(1);

  tft.setTextColor(TFT_CYAN, TFT_BLACK);

  tft.setCursor(220, 12);

  tft.print(tttMessage);



  int boardX = 85, boardY = 38, cell = 50;

  tft.drawFastVLine(boardX + 50, boardY, 150, TFT_WHITE);

  tft.drawFastVLine(boardX + 100, boardY, 150, TFT_WHITE);

  tft.drawFastHLine(boardX, boardY + 50, 150, TFT_WHITE);

  tft.drawFastHLine(boardX, boardY + 100, 150, TFT_WHITE);



  for (int i = 0; i < 9; i++) {

    int col = i % 3;

    int row = i / 3;

    int cx = boardX + col * cell + 25;

    int cy = boardY + row * cell + 25;



    if (tttBoard[i] == 'X') {

      tft.drawLine(cx - 13, cy - 13, cx + 13, cy + 13, TFT_CYAN);

      tft.drawLine(cx + 13, cy - 13, cx - 13, cy + 13, TFT_CYAN);

    } else if (tttBoard[i] == 'O') {

      tft.drawCircle(cx, cy, 15, TFT_ORANGE);

      tft.drawCircle(cx, cy, 14, TFT_ORANGE);

    }

  }



  tft.fillRect(0, 202, 160, 38, TFT_DARKGREY);

  tft.fillRect(160, 202, 160, 38, TFT_DARKGREY);

  tft.drawFastVLine(160, 202, 38, TFT_LIGHTGREY);

  tft.setTextSize(1);

  tft.setTextColor(TFT_WHITE, TFT_DARKGREY);

  tft.setCursor(63, 218); tft.print("RESET");

  tft.setCursor(225, 218); tft.print("BACK");

}



// --------------------------------------------------

// Reaction game

// --------------------------------------------------



void drawReactionBackButton() {

  drawBackBar(nullptr, "BACK", nullptr);

}



void startReactionGame() {

  reactionState = REACTION_WAITING;

  reactionTargetTime = millis() + random(1500, 4500);

  tft.fillScreen(TFT_BLACK);

  tft.setTextSize(3);

  tft.setTextColor(TFT_YELLOW, TFT_BLACK);

  tft.setCursor(75, 70);

  tft.print("WAIT...");

  tft.setTextSize(1);

  tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);

  tft.setCursor(67, 120);

  tft.print("Tap when screen turns GREEN");

  drawReactionBackButton();

}



void drawReactionPage() {

  currentPage = PAGE_REACTION;

  reactionState = REACTION_IDLE;

  tft.fillScreen(TFT_BLACK);

  tft.setTextSize(2);

  tft.setTextColor(TFT_WHITE, TFT_BLACK);

  tft.setCursor(82, 35);

  tft.print("REACTION TAP");

  tft.setTextSize(1);

  tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);

  tft.setCursor(58, 75);

  tft.print("How fast can you react?");

  tft.fillRoundRect(65, 105, 190, 55, 8, TFT_BLUE);

  tft.setTextSize(2);

  tft.setTextColor(TFT_WHITE, TFT_BLUE);

  tft.setCursor(128, 123);

  tft.print("START");

  drawReactionBackButton();

}



void updateReactionGame() {

  if (currentPage != PAGE_REACTION) return;

  if (reactionState == REACTION_WAITING && millis() >= reactionTargetTime) {

    reactionState = REACTION_GO;

    reactionStartTime = millis();

    tft.fillRect(0, 0, 320, 208, TFT_GREEN);

    tft.setTextSize(4);

    tft.setTextColor(TFT_BLACK, TFT_GREEN);

    tft.setCursor(92, 85);

    tft.print("TAP!");

    drawReactionBackButton();

  }

}



// --------------------------------------------------

// JPEG/photo support

// --------------------------------------------------



bool tftOutput(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t *bitmap) {

  if (y >= tft.height()) return 0;

  tft.pushImage(x, y, w, h, bitmap);

  return 1;

}



void fetchPhotoList() {

  if (WiFi.status() != WL_CONNECTED) return;



  HTTPClient http;

  http.setTimeout(6000);

  http.setReuse(false);

  http.begin(API_PHOTOS);

  int code = http.GET();



  if (code != HTTP_CODE_OK) {

    Serial.printf("Photo list HTTP error: %d\n", code);

    http.end();

    return;

  }



  JsonDocument doc;

  DeserializationError err = deserializeJson(doc, http.getString());

  if (err) {

    Serial.print("Photo list JSON error: ");

    Serial.println(err.c_str());

    http.end();

    return;

  }



  photoCount = 0;

  for (JsonVariant item : doc["photos"].as<JsonArray>()) {

    if (photoCount >= MAX_PHOTOS) break;

    photoNames[photoCount++] = item.as<String>();

  }

  photoListLoaded = true;

  if (photoIndex >= photoCount) photoIndex = 0;

  http.end();

}



bool showPhoto(int index, bool overlayControls) {

  if (!photoListLoaded) fetchPhotoList();

  if (photoCount == 0 || index < 0 || index >= photoCount) return false;



  String url = String(API_BASE) + "/photos/" + photoNames[index];

  HTTPClient http;

  http.setTimeout(10000);

  http.setReuse(false);

  http.begin(url);

  int code = http.GET();



  if (code != HTTP_CODE_OK) {

    Serial.printf("Photo HTTP error: %d\n", code);

    http.end();

    return false;

  }



  int len = http.getSize();

  if (len <= 0 || len > 130000) {

    Serial.printf("Photo size invalid: %d bytes\n", len);

    http.end();

    return false;

  }



  uint8_t* buffer = (uint8_t*)malloc(len);

  if (!buffer) {

    Serial.println("Not enough RAM for JPEG");

    http.end();

    return false;

  }



  WiFiClient* stream = http.getStreamPtr();

  int total = 0;

  unsigned long deadline = millis() + 10000;



  while (total < len && millis() < deadline) {

    int available = stream->available();

    if (available > 0) {

      int toRead = min(available, len - total);

      int read = stream->readBytes(buffer + total, toRead);

      if (read > 0) total += read;

    } else {

      delay(1);

    }

  }



  bool ok = false;

  if (total == len) {

    tft.fillScreen(TFT_BLACK);

    TJpgDec.drawJpg(0, 0, buffer, len);

    ok = true;

  }



  free(buffer);

  http.end();



  if (overlayControls) {

    tft.fillRect(0, 208, 106, 32, TFT_DARKGREY);

    tft.fillRect(106, 208, 108, 32, TFT_DARKGREY);

    tft.fillRect(214, 208, 106, 32, TFT_DARKGREY);

    tft.setTextSize(1);

    tft.setTextColor(TFT_WHITE, TFT_DARKGREY);

    tft.setCursor(36, 220); tft.print("< PREV");

    tft.setCursor(145, 220); tft.print("BACK");

    tft.setCursor(250, 220); tft.print("NEXT >");

  }



  return ok;

}



void drawPhotosPage() {

  currentPage = PAGE_PHOTOS;

  if (!photoListLoaded) fetchPhotoList();



  if (photoCount == 0) {

    tft.fillScreen(TFT_BLACK);

    tft.setTextSize(2);

    tft.setTextColor(TFT_YELLOW, TFT_BLACK);

    tft.setCursor(60, 80);

    tft.print("NO PHOTOS");

    tft.setTextSize(1);

    tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);

    tft.setCursor(32, 120);

    tft.print("Add photos on HomeServer");

    drawBackBar(nullptr, "BACK", nullptr);

    return;

  }



  if (!showPhoto(photoIndex, true)) {

    tft.fillScreen(TFT_BLACK);

    tft.setTextSize(2);

    tft.setTextColor(TFT_RED, TFT_BLACK);

    tft.setCursor(45, 90);

    tft.print("PHOTO ERROR");

    drawBackBar(nullptr, "BACK", nullptr);

  }

}



// --------------------------------------------------

// Screensaver

// --------------------------------------------------



void drawScreensaverClock() {

  currentPage = PAGE_SCREENSAVER;

  screensaverShowingPhoto = false;

  tft.fillScreen(TFT_BLACK);



  tft.setTextSize(1);

  tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);

  int dateW = tft.textWidth(localDate);

  tft.setCursor((320 - dateW) / 2, 18);

  tft.print(localDate);



  tft.setTextSize(5);

  tft.setTextColor(TFT_WHITE, TFT_BLACK);

  int timeW = tft.textWidth(localTime);

  tft.setCursor((320 - timeW) / 2, 62);

  tft.print(localTime);



  if (weatherAvailable) {

    tft.setTextSize(2);

    tft.setTextColor(TFT_CYAN, TFT_BLACK);

    String temp = String(temperatureC, 1) + " C";

    int tempW = tft.textWidth(temp);

    tft.setCursor((320 - tempW) / 2, 130);

    tft.print(temp);



    tft.setTextSize(1);

    tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);

    int condW = tft.textWidth(weatherCondition);

    tft.setCursor((320 - condW) / 2, 162);

    tft.print(weatherCondition);

    String range = "H " + String(highC, 1) + "C  L " + String(lowC, 1) + "C";

    int rangeW = tft.textWidth(range);

    tft.setCursor((320 - rangeW) / 2, 182);

    tft.print(range);

  }



  tft.setTextSize(1);

  tft.setTextColor(TFT_DARKGREY, TFT_BLACK);

  const char* wakeText = "Touch to wake";

  int wakeW = tft.textWidth(wakeText);

  tft.setCursor((320 - wakeW) / 2, 225);

  tft.print(wakeText);

}



void enterScreensaver() {

  if (currentPage == PAGE_SCREENSAVER || currentPage == PAGE_TTT || currentPage == PAGE_REACTION) return;

  pageBeforeScreensaver = currentPage;

  screensaverLastRotate = millis();

  drawScreensaverClock();

}



void exitScreensaver() {

  currentPage = pageBeforeScreensaver;

  lastInteraction = millis();

  drawCurrentPage();

}



void updateScreensaver() {

  if (currentPage != PAGE_SCREENSAVER) {

    if (millis() - lastInteraction >= SCREENSAVER_TIMEOUT) enterScreensaver();

    return;

  }



  if (millis() - screensaverLastRotate < SCREENSAVER_ROTATE) return;

  screensaverLastRotate = millis();



  if (!photoListLoaded) fetchPhotoList();



  if (photoCount > 0 && !screensaverShowingPhoto) {

    if (showPhoto(photoIndex, false)) {

      screensaverShowingPhoto = true;

      photoIndex = (photoIndex + 1) % photoCount;

    } else {

      drawScreensaverClock();

    }

  } else {

    drawScreensaverClock();

  }

}



// --------------------------------------------------

// Current page

// --------------------------------------------------



void drawCurrentPage() {

  switch (currentPage) {

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

  }

}



// --------------------------------------------------

// API

// --------------------------------------------------



void fetchHomelabStatus() {

  if (WiFi.status() != WL_CONNECTED) {

    WiFi.disconnect();

    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

    unsigned long start = millis();

    while (WiFi.status() != WL_CONNECTED && millis() - start < 8000) delay(250);

    if (WiFi.status() != WL_CONNECTED) {

      serverOnline = false;

      if (currentPage != PAGE_SCREENSAVER && currentPage != PAGE_TTT && currentPage != PAGE_REACTION) drawCurrentPage();

      return;

    }

  }



  HTTPClient http;

  http.setTimeout(8000);

  http.setReuse(false);

  http.begin(API_STATUS);

  int code = http.GET();



  if (code != HTTP_CODE_OK) {

    Serial.printf("HTTP error: %d\n", code);

    serverOnline = false;

    http.end();

    return;

  }



  JsonDocument doc;

  DeserializationError error = deserializeJson(doc, http.getString());

  if (error) {

    Serial.print("JSON error: ");

    Serial.println(error.c_str());

    http.end();

    return;

  }



  uptimeHours = doc["uptime_hours"] | 0.0;
  cpuPercent = doc["cpu"]["percent"] | 0.0;

  memUsed = doc["memory"]["used_gb"] | 0.0;
  memTotal = doc["memory"]["total_gb"] | 0.0;
  memPercent = doc["memory"]["percent"] | 0.0;

  diskCount = 0;
  for (JsonObject disk : doc["disks"].as<JsonArray>()) {
    if (diskCount >= MAX_DISKS) break;
    diskNames[diskCount] = disk["name"].as<String>();
    diskLabels[diskCount] = disk["label"].as<String>();
    diskUsedGb[diskCount] = disk["used_gb"] | 0.0;
    diskTotalGb[diskCount] = disk["total_gb"] | 0.0;
    diskPercentages[diskCount] = disk["percent"] | 0.0;
    diskCount++;
  }

  gpuPercent = doc["gpu"]["percent"] | 0.0;
  gpuName = doc["gpu"]["name"].as<String>();

  wifiAvailable = doc["wifi"]["available"] | false;
  wifiLinkMbps = doc["wifi"]["link_mbps"] | 0.0;
  wifiRxMbps = doc["wifi"]["receive_mbps"] | 0.0;
  wifiTxMbps = doc["wifi"]["send_mbps"] | 0.0;

  if (doc["wifi"]["signal_percent"].is<int>()) {
    wifiSignalPercent = doc["wifi"]["signal_percent"].as<int>();
  } else {
    wifiSignalPercent = -1;
  }

  totalRunningContainers = doc["docker"]["running"] | 0;

  displayedContainers = 0;

  for (JsonObject container : doc["docker"]["containers"].as<JsonArray>()) {

    if (displayedContainers >= MAX_CONTAINERS) break;

    containerNames[displayedContainers] = container["name"].as<String>();

    containerRunning[displayedContainers] = container["running"] | false;

    displayedContainers++;

  }



  serviceJellyfin = doc["services"]["jellyfin"] | false;

  serviceNavidrome = doc["services"]["navidrome"] | false;

  serviceNextcloud = doc["services"]["nextcloud"] | false;

  serviceImmich = doc["services"]["immich"] | false;

  serviceOllama = doc["services"]["ollama"] | false;

  serviceCloudflare = doc["services"]["cloudflare"] | false;

  serviceTechnicalBlog = doc["services"]["technical_blog"] | false;



  localTime = doc["timezones"]["local"]["time"].as<String>();

  localDate = doc["timezones"]["local"]["date"].as<String>();

  indiaTime = doc["timezones"]["india"]["time"].as<String>();

  singaporeTime = doc["timezones"]["singapore"]["time"].as<String>();

  londonTime = doc["timezones"]["london"]["time"].as<String>();



  currentYear = doc["timezones"]["local"]["year"] | currentYear;

  currentMonth = doc["timezones"]["local"]["month"] | currentMonth;

  currentDay = doc["timezones"]["local"]["day"] | currentDay;



  if (calendarYear == 2026 && calendarMonth == 1 && currentMonth != 1) {

    calendarYear = currentYear;

    calendarMonth = currentMonth;

  }



  weatherAvailable = doc["weather"]["available"] | false;

  if (weatherAvailable) {

    temperatureC = doc["weather"]["temperature_c"] | 0.0;

    highC = doc["weather"]["high_c"] | 0.0;

    lowC = doc["weather"]["low_c"] | 0.0;

    weatherCondition = doc["weather"]["condition"].as<String>();

  }



  serverOnline = true;

  http.end();



  if (currentPage != PAGE_TTT && currentPage != PAGE_REACTION && currentPage != PAGE_GAMES && currentPage != PAGE_PHOTOS && currentPage != PAGE_SCREENSAVER) {

    drawCurrentPage();

  }

}



// --------------------------------------------------

// OTA

// --------------------------------------------------



void setupOTA() {

  ArduinoOTA.setHostname("homelab-display");



  ArduinoOTA.onStart([]() {

    tft.fillScreen(TFT_BLACK);

    tft.setTextSize(2);

    tft.setTextColor(TFT_CYAN, TFT_BLACK);

    tft.setCursor(65, 70);

    tft.print("OTA UPDATE");

  });



  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {

    int percent = (progress * 100U) / total;

    tft.fillRect(30, 120, 260, 50, TFT_BLACK);

    tft.setTextSize(3);

    tft.setTextColor(TFT_WHITE, TFT_BLACK);

    tft.setCursor(115, 125);

    tft.printf("%d%%", percent);

  });



  ArduinoOTA.onEnd([]() {

    tft.fillScreen(TFT_BLACK);

    tft.setTextSize(2);

    tft.setTextColor(TFT_GREEN, TFT_BLACK);

    tft.setCursor(70, 100);

    tft.print("COMPLETE");

  });



  ArduinoOTA.onError([](ota_error_t error) {

    Serial.printf("OTA error: %u\n", error);

  });



  ArduinoOTA.begin();

  Serial.println("OTA ready: homelab-display");

}



// --------------------------------------------------

// Touch

// --------------------------------------------------



void handleTouch() {

  if (!touch.touched()) return;

  if (millis() - lastTouchTime < 250) return;

  lastTouchTime = millis();



  TS_Point p = touch.getPoint();

  if (p.z < 200) return;



  int x = constrain(map(p.x, RAW_X_MIN, RAW_X_MAX, 0, 319), 0, 319);

  int y = constrain(map(p.y, RAW_Y_MIN, RAW_Y_MAX, 0, 239), 0, 239);



  lastInteraction = millis();

  Serial.printf("Touch X=%d Y=%d\n", x, y);



  if (currentPage == PAGE_SCREENSAVER) {

    exitScreensaver();

    return;

  }



  if (currentPage == PAGE_TTT) {

    if (y >= 200 && x < 160) {

      resetTTT();

      drawTTTPage();

      return;

    }

    if (y >= 200 && x >= 160) {

      drawGamesPage();

      return;

    }

    if (x >= 85 && x < 235 && y >= 38 && y < 188 && !tttGameOver) {

      int col = (x - 85) / 50;

      int row = (y - 38) / 50;

      int index = row * 3 + col;

      if (tttBoard[index] == ' ') {

        tttBoard[index] = 'X';

        char result = checkTTTWinner();

        if (result == 'X') {

          tttGameOver = true;

          tttMessage = "YOU WIN!";

        } else if (result == 'D') {

          tttGameOver = true;

          tttMessage = "DRAW";

        } else {

          tttMessage = "ESP32 TURN";

          drawTTTPage();

          delay(350);

          esp32Move();

        }

        drawTTTPage();

      }

      return;

    }

    return;

  }



  if (currentPage == PAGE_REACTION) {

    if (y >= 205) {

      reactionState = REACTION_IDLE;

      drawGamesPage();

      return;

    }



    if (reactionState == REACTION_IDLE) {

      if (x >= 55 && x <= 265 && y >= 95 && y <= 170) {

        startReactionGame();

        return;

      }

    } else if (reactionState == REACTION_WAITING) {

      reactionState = REACTION_TOO_SOON;

      tft.fillRect(0, 0, 320, 208, TFT_RED);

      tft.setTextSize(3);

      tft.setTextColor(TFT_WHITE, TFT_RED);

      tft.setCursor(60, 65);

      tft.print("TOO SOON!");

      tft.setTextSize(1);

      tft.setCursor(91, 120);

      tft.print("Tap to try again");

      drawReactionBackButton();

      return;

    } else if (reactionState == REACTION_GO) {

      reactionResultMs = millis() - reactionStartTime;

      reactionState = REACTION_RESULT;

      tft.fillRect(0, 0, 320, 208, TFT_BLACK);

      tft.setTextSize(2);

      tft.setTextColor(TFT_GREEN, TFT_BLACK);

      tft.setCursor(75, 55);

      tft.print("REACTION");

      tft.setTextSize(4);

      tft.setTextColor(TFT_WHITE, TFT_BLACK);

      String result = String(reactionResultMs) + " ms";

      int width = tft.textWidth(result);

      tft.setCursor((320 - width) / 2, 95);

      tft.print(result);

      tft.setTextSize(1);

      tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);

      tft.setCursor(90, 160);

      tft.print("Tap to play again");

      drawReactionBackButton();

      return;

    } else {

      startReactionGame();

      return;

    }

    return;

  }



  if (currentPage == PAGE_CALENDAR) {

    if (y >= 205) {

      if (x < 106) {

        changeCalendarMonth(-1);

        drawCalendarPage();

      } else if (x < 214) {

        drawMorePage();

      } else {

        changeCalendarMonth(1);

        drawCalendarPage();

      }

    }

    return;

  }



  if (currentPage == PAGE_PHOTOS) {

    if (y >= 205) {

      if (x < 106 && photoCount > 0) {

        photoIndex = (photoIndex - 1 + photoCount) % photoCount;

        drawPhotosPage();

      } else if (x < 214) {

        drawMorePage();

      } else if (photoCount > 0) {

        photoIndex = (photoIndex + 1) % photoCount;

        drawPhotosPage();

      }

    }

    return;

  }



  if (currentPage == PAGE_GAMES) {

    if (y >= 45 && y <= 110) {

      resetTTT();

      drawTTTPage();

      return;

    }

    if (y >= 115 && y <= 185) {

      drawReactionPage();

      return;

    }

    if (y >= 205) {

      drawMorePage();

      return;

    }

    return;

  }



  if (currentPage == PAGE_TIME) {

    if (y >= 205) drawMorePage();

    return;

  }



  if (currentPage == PAGE_MORE) {

    if (y >= 45 && y <= 110) {

      if (x < 160) drawTimePage();

      else {

        calendarYear = currentYear;

        calendarMonth = currentMonth;

        drawCalendarPage();

      }

      return;

    }

    if (y >= 115 && y <= 190) {

      if (x < 160) drawGamesPage();

      else drawPhotosPage();

      return;

    }

  }



  if (y >= 205) {
    if (x < 107) drawHomePage();
    else if (x < 214) drawServicesPage();
    else drawMorePage();
  }

}



// --------------------------------------------------

// Wi-Fi/setup/loop

// --------------------------------------------------



void connectWiFi() {

  tft.fillScreen(TFT_BLACK);

  tft.setTextSize(2);

  tft.setTextColor(TFT_CYAN, TFT_BLACK);

  tft.setCursor(55, 95);

  tft.print("Connecting...");



  WiFi.mode(WIFI_STA);

  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  while (WiFi.status() != WL_CONNECTED) {

    delay(500);

    Serial.print(".");

  }

  WiFi.setSleep(false);

  Serial.println();

  Serial.print("ESP32 IP: ");

  Serial.println(WiFi.localIP());

}



void setup() {

  Serial.begin(115200);

  delay(500);



  pinMode(21, OUTPUT);

  digitalWrite(21, HIGH);



  tft.init();

  tft.setRotation(1);



  touchSPI.begin(TOUCH_CLK, TOUCH_MISO, TOUCH_MOSI, TOUCH_CS);

  touch.begin(touchSPI);

  touch.setRotation(1);



  TJpgDec.setJpgScale(1);

  TJpgDec.setSwapBytes(true);

  TJpgDec.setCallback(tftOutput);



  connectWiFi();

  setupOTA();

  randomSeed(micros());

  fetchHomelabStatus();

  fetchPhotoList();



  calendarYear = currentYear;

  calendarMonth = currentMonth;

  lastRefresh = millis();

  lastInteraction = millis();

}



void loop() {

  ArduinoOTA.handle();

  handleTouch();

  updateReactionGame();

  updateScreensaver();



  if (millis() - lastRefresh >= REFRESH_INTERVAL) {

    lastRefresh = millis();

    if (currentPage != PAGE_TTT && currentPage != PAGE_REACTION && currentPage != PAGE_GAMES && currentPage != PAGE_PHOTOS) {

      fetchHomelabStatus();

    }

  }

}
