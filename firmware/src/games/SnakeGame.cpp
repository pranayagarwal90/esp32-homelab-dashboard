#include <Arduino.h>
#include "games/SnakeGame.h"
#include "games/SnakeLogic.h"
#include "AppState.h"
#include "Display.h"
#include "MenuScreens.h"
#include "PageRouter.h"
#include "UiHelpers.h"

static SnakeGame snake;
static bool snakeStarted = false;
static unsigned long lastStep = 0;

static void drawCell(SnakeCell cell, uint16_t color) {
  tft.fillRect(SNAKE_BOARD_X + cell.x * SNAKE_CELL + 1, SNAKE_BOARD_Y + cell.y * SNAKE_CELL + 1,
               SNAKE_CELL - 1, SNAKE_CELL - 1, color);
}

static void drawScore() {
  char text[12];
  snprintf(text, sizeof(text), "SCORE %u", (unsigned)snake.score);
  drawTitleBarValue(text);
}

static void drawMessage(const char* line1, const char* line2) {
  int cx = SNAKE_BOARD_X + SNAKE_COLS * SNAKE_CELL / 2;
  tft.setTextSize(2);
  tft.setTextColor(TFT_YELLOW, TFT_BLACK);
  tft.setCursor(cx - tft.textWidth(line1) / 2, 98);
  tft.print(line1);
  tft.setTextSize(1);
  tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  tft.setCursor(cx - tft.textWidth(line2) / 2, 124);
  tft.print(line2);
}

static void drawBoard() {
  tft.fillRect(SNAKE_BOARD_X, SNAKE_BOARD_Y, SNAKE_COLS * SNAKE_CELL + 1, SNAKE_ROWS * SNAKE_CELL + 1, TFT_BLACK);
  tft.drawRect(SNAKE_BOARD_X - 1, SNAKE_BOARD_Y - 1, SNAKE_COLS * SNAKE_CELL + 3, SNAKE_ROWS * SNAKE_CELL + 3,
               TFT_DARKGREY);
  for (int i = 0; i < snake.length; i++) drawCell(snakeSegment(snake, i), TFT_GREEN);
  drawCell(snakeHead(snake), TFT_GREENYELLOW);
  if (snake.state != SnakeState::Won) drawCell(snake.food, TFT_RED);

  switch (snake.state) {
    case SnakeState::Ready: drawMessage("SNAKE", "Tap an arrow to start"); break;
    case SnakeState::Paused: drawMessage("PAUSED", "Tap an arrow to resume"); break;
    case SnakeState::Over: drawMessage("GAME OVER", "Tap the board to play again"); break;
    case SnakeState::Won: drawMessage("YOU WIN!", "Tap the board to play again"); break;
    case SnakeState::Running: break;
  }
}

static void drawPad() {
  drawMenuButton(SNAKE_PAD_X, 40, 104, 50, "^");
  drawMenuButton(SNAKE_PAD_X, 96, 50, 50, "<");
  drawMenuButton(SNAKE_PAD_X + 54, 96, 50, 50, ">");
  drawMenuButton(SNAKE_PAD_X, 152, 104, 50, "v");
}

static void newGame() {
  snakeReset(snake, esp_random());
  snakeStarted = true;
}

void drawSnakePage() {
  app.currentPage = PAGE_SNAKE;
  if (!snakeStarted) newGame();
  snakePause(snake); // Never resume on its own after being away.
  tft.fillScreen(TFT_BLACK);
  drawTitleBar("SNAKE");
  drawScore();
  drawBoard();
  drawPad();
  drawBackBar("RESTART", "BACK", nullptr);
}

static void turn(SnakeDir dir) {
  SnakeState before = snake.state;
  if (!snakeTurn(snake, dir)) return;
  if (before != SnakeState::Running) {
    drawBoard(); // Clears the message.
    lastStep = millis();
  }
}

void handleSnakeTouch(int x, int y) {
  switch (snakeHitAt(x, y)) {
    case SnakeHit::Back:
      showMenuNode(menuParent(MenuNode::Snake));
      return;
    case SnakeHit::Restart:
      newGame();
      drawSnakePage();
      return;
    case SnakeHit::Up: turn(SnakeDir::Up); return;
    case SnakeHit::Down: turn(SnakeDir::Down); return;
    case SnakeHit::Left: turn(SnakeDir::Left); return;
    case SnakeHit::Right: turn(SnakeDir::Right); return;
    case SnakeHit::Board:
      // Tap the board to pause, resume, or start a new game after the end.
      if (snake.state == SnakeState::Running) {
        snakePause(snake);
        drawBoard();
      } else if (snake.state == SnakeState::Over || snake.state == SnakeState::Won) {
        newGame();
        drawSnakePage();
      } else {
        turn(snake.dir);
      }
      return;
    case SnakeHit::None:
      return;
  }
}

void updateSnakeGame() {
  if (app.currentPage != PAGE_SNAKE || snake.state != SnakeState::Running) return;
  if (millis() - lastStep < snakeIntervalMs(snake.score)) return;
  lastStep = millis();
  SnakeCell oldHead = snakeHead(snake);
  switch (snakeStep(snake)) {
    case SnakeStep::Moved:
      drawCell(snake.vacated, TFT_BLACK);
      drawCell(oldHead, TFT_GREEN);
      drawCell(snakeHead(snake), TFT_GREENYELLOW);
      break;
    case SnakeStep::Ate:
      drawCell(oldHead, TFT_GREEN);
      drawCell(snakeHead(snake), TFT_GREENYELLOW);
      drawCell(snake.food, TFT_RED);
      drawScore();
      break;
    case SnakeStep::Died:
    case SnakeStep::Won:
      drawScore();
      drawBoard();
      break;
    case SnakeStep::None:
      break;
  }
}
