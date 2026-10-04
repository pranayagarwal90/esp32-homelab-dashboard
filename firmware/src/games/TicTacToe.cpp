#include <Arduino.h>
#include "games/TicTacToe.h"
#include "AppState.h"
#include "Display.h"
#include "PageRouter.h"

static char tttBoard[9] = {
  ' ', ' ', ' ',
  ' ', ' ', ' ',
  ' ', ' ', ' '
};
static bool tttGameOver = false;
static String tttMessage = "YOUR TURN";

void resetTTT() {
  for (int i = 0; i < 9; i++) tttBoard[i] = ' ';
  tttGameOver = false;
  tttMessage = "YOUR TURN";
}

static char checkTTTWinner() {
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

static int findWinningMove(char player) {
  for (int i = 0; i < 9; i++) {
    if (tttBoard[i] != ' ') continue;
    tttBoard[i] = player;
    char winner = checkTTTWinner();
    tttBoard[i] = ' ';
    if (winner == player) return i;
  }
  return -1;
}

static void esp32Move() {
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
  app.currentPage = PAGE_TTT;
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

void handleTTTTouch(int x, int y) {
  if (y >= 200 && x < 160) {
    resetTTT();
    drawTTTPage();
    return;
  }

  if (y >= 200 && x >= 160) {
    showPage(PAGE_GAMES);
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
}
