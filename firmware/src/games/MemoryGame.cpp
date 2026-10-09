#include <Arduino.h>
#include "games/MemoryGame.h"
#include "games/MemoryLogic.h"
#include "AppState.h"
#include "Display.h"
#include "MenuScreens.h"
#include "PageRouter.h"
#include "UiHelpers.h"

static MemoryGame memory;
static bool memoryStarted = false;

static void drawCard(int index) {
  int x = MEMORY_X0 + (index % MEMORY_COLS) * MEMORY_PITCH_X;
  int y = MEMORY_Y0 + (index / MEMORY_COLS) * MEMORY_PITCH_Y;
  MemoryCard state = memory.card[index];
  bool mismatched = memory.phase == MemoryPhase::ShowMismatch && state == MemoryCard::Shown;
  uint16_t fill = state == MemoryCard::Hidden ? TFT_DARKGREY : state == MemoryCard::Matched ? TFT_DARKGREEN : TFT_NAVY;
  uint16_t border = mismatched ? TFT_RED : state == MemoryCard::Shown ? TFT_YELLOW : TFT_LIGHTGREY;
  tft.fillRoundRect(x, y, MEMORY_CARD_W, MEMORY_CARD_H, 6, fill);
  tft.drawRoundRect(x, y, MEMORY_CARD_W, MEMORY_CARD_H, 6, border);
  if (state == MemoryCard::Hidden) return;
  char text[2] = {(char)('0' + memory.value[index]), '\0'};
  tft.setTextSize(3);
  tft.setTextColor(TFT_WHITE, fill);
  tft.setCursor(x + (MEMORY_CARD_W - tft.textWidth(text)) / 2, y + (MEMORY_CARD_H - 24) / 2);
  tft.print(text);
}

static void drawMoves() {
  char text[12];
  snprintf(text, sizeof(text), "MOVES %u", (unsigned)memory.moves);
  drawTitleBarValue(text);
}

static void drawComplete() {
  tft.fillRoundRect(40, 82, 240, 76, 8, TFT_BLACK);
  tft.drawRoundRect(40, 82, 240, 76, 8, TFT_GREEN);
  tft.setTextSize(2);
  tft.setTextColor(TFT_GREEN, TFT_BLACK);
  const char* title = "COMPLETE!";
  tft.setCursor(160 - tft.textWidth(title) / 2, 96);
  tft.print(title);
  char line[32];
  snprintf(line, sizeof(line), "%u moves - tap to play again", (unsigned)memory.moves);
  tft.setTextSize(1);
  tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  tft.setCursor(160 - tft.textWidth(line) / 2, 128);
  tft.print(line);
}

static void newGame() {
  memoryReset(memory, esp_random());
  memoryStarted = true;
}

void drawMemoryPage() {
  app.currentPage = PAGE_MEMORY;
  if (!memoryStarted) newGame();
  // A pair left face up while away is turned back over.
  memoryHideMismatch(memory);
  tft.fillScreen(TFT_BLACK);
  drawTitleBar("MEMORY");
  drawMoves();
  for (int i = 0; i < MEMORY_CARDS; i++) drawCard(i);
  if (memory.phase == MemoryPhase::Complete) drawComplete();
  drawBackBar("RESTART", "BACK", nullptr);
}

void handleMemoryTouch(int x, int y) {
  if (y >= 205) {
    if (x < 107) {
      newGame();
      drawMemoryPage();
    } else if (x < 214) {
      showMenuNode(menuParent(MenuNode::Memory));
    }
    return;
  }
  if (memory.phase == MemoryPhase::Complete) {
    newGame();
    drawMemoryPage();
    return;
  }
  int index = memoryCardAt(x, y);
  int first = memory.first;
  switch (memoryTap(memory, index, millis())) {
    case MemoryTap::First:
      drawCard(index);
      break;
    case MemoryTap::Matched:
      drawCard(first);
      drawCard(index);
      drawMoves();
      break;
    case MemoryTap::Mismatch:
      drawCard(memory.first);
      drawCard(index);
      drawMoves();
      break;
    case MemoryTap::Completed:
      drawCard(first);
      drawCard(index);
      drawMoves();
      drawComplete();
      break;
    case MemoryTap::Ignored:
      break;
  }
}

void updateMemoryGame() {
  if (app.currentPage != PAGE_MEMORY) return;
  int first = memory.first, second = memory.second;
  if (memoryUpdate(memory, millis())) {
    drawCard(first);
    drawCard(second);
  }
}
