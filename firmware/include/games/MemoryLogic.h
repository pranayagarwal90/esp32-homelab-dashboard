#pragma once
#include <stdint.h>
#include "games/GameRandom.h"

// Pure Memory Match, shared with host tests: 4x4 cards, 8 numbered pairs.
// A mismatched pair stays face up for MEMORY_MISMATCH_MS (driven by the
// caller's millis()), during which card taps are ignored.

constexpr int MEMORY_COLS = 4;
constexpr int MEMORY_ROWS = 4;
constexpr int MEMORY_CARDS = MEMORY_COLS * MEMORY_ROWS;
constexpr int MEMORY_PAIRS = MEMORY_CARDS / 2;
constexpr uint32_t MEMORY_MISMATCH_MS = 800;

enum class MemoryPhase : uint8_t { WaitFirst, WaitSecond, ShowMismatch, Complete };
enum class MemoryCard : uint8_t { Hidden, Shown, Matched };
enum class MemoryTap : uint8_t { Ignored, First, Matched, Mismatch, Completed };

struct MemoryGame {
  uint8_t value[MEMORY_CARDS] = {}; // 1..8, each exactly twice.
  MemoryCard card[MEMORY_CARDS] = {};
  MemoryPhase phase = MemoryPhase::WaitFirst;
  int8_t first = -1;
  int8_t second = -1;
  uint16_t moves = 0;       // Pairs turned over.
  uint8_t matchedPairs = 0;
  uint32_t mismatchAt = 0;
};

// Fresh shuffled board (Fisher-Yates).
inline void memoryReset(MemoryGame& game, uint32_t seed) {
  game = MemoryGame();
  GameRandom rng;
  rng.seed(seed);
  for (int i = 0; i < MEMORY_CARDS; i++) game.value[i] = (uint8_t)(i / 2 + 1);
  for (int i = MEMORY_CARDS - 1; i > 0; i--) {
    int j = (int)rng.below(i + 1);
    uint8_t swap = game.value[i];
    game.value[i] = game.value[j];
    game.value[j] = swap;
  }
}

inline MemoryTap memoryTap(MemoryGame& game, int index, uint32_t now) {
  if (index < 0 || index >= MEMORY_CARDS) return MemoryTap::Ignored;
  if (game.phase == MemoryPhase::ShowMismatch || game.phase == MemoryPhase::Complete) return MemoryTap::Ignored;
  if (game.card[index] != MemoryCard::Hidden) return MemoryTap::Ignored; // Same or matched card.

  game.card[index] = MemoryCard::Shown;
  if (game.phase == MemoryPhase::WaitFirst) {
    game.first = (int8_t)index;
    game.phase = MemoryPhase::WaitSecond;
    return MemoryTap::First;
  }

  game.second = (int8_t)index;
  game.moves++;
  if (game.value[game.first] == game.value[index]) {
    game.card[game.first] = game.card[index] = MemoryCard::Matched;
    game.first = game.second = -1;
    game.matchedPairs++;
    if (game.matchedPairs == MEMORY_PAIRS) {
      game.phase = MemoryPhase::Complete;
      return MemoryTap::Completed;
    }
    game.phase = MemoryPhase::WaitFirst;
    return MemoryTap::Matched;
  }
  game.phase = MemoryPhase::ShowMismatch;
  game.mismatchAt = now;
  return MemoryTap::Mismatch;
}

// Turns a mismatched pair face down again immediately.
inline void memoryHideMismatch(MemoryGame& game) {
  if (game.phase != MemoryPhase::ShowMismatch) return;
  game.card[game.first] = game.card[game.second] = MemoryCard::Hidden;
  game.first = game.second = -1;
  game.phase = MemoryPhase::WaitFirst;
}

// Call every loop. True when a mismatched pair was just hidden (redraw it).
inline bool memoryUpdate(MemoryGame& game, uint32_t now) {
  if (game.phase != MemoryPhase::ShowMismatch || now - game.mismatchAt < MEMORY_MISMATCH_MS) return false;
  memoryHideMismatch(game);
  return true;
}

// --- Memory page layout -------------------------------------------------------------

constexpr int MEMORY_X0 = 12;
constexpr int MEMORY_Y0 = 40;
constexpr int MEMORY_CARD_W = 70;
constexpr int MEMORY_CARD_H = 38;
constexpr int MEMORY_PITCH_X = 76;
constexpr int MEMORY_PITCH_Y = 42;

// Card under a touch, using the whole cell pitch (gaps included) so that
// resistive touch error does not miss; -1 outside the grid.
inline int memoryCardAt(int x, int y) {
  int left = MEMORY_X0 - 3, top = MEMORY_Y0 - 2;
  if (x < left || y < top) return -1;
  int col = (x - left) / MEMORY_PITCH_X, row = (y - top) / MEMORY_PITCH_Y;
  if (col >= MEMORY_COLS || row >= MEMORY_ROWS) return -1;
  return row * MEMORY_COLS + col;
}
