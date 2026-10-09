#pragma once
#include <stdint.h>
#include "games/GameRandom.h"

// Pure Snake, shared with host tests. Fixed grid and a fixed ring buffer for
// the body; the caller drives step() from millis().

constexpr int SNAKE_COLS = 20;
constexpr int SNAKE_ROWS = 16;
constexpr int SNAKE_MAX_LEN = 128; // Reaching it wins the game.
constexpr int SNAKE_START_LEN = 3;

enum class SnakeDir : uint8_t { Up, Right, Down, Left };
enum class SnakeState : uint8_t { Ready, Running, Paused, Over, Won };
enum class SnakeStep : uint8_t { None, Moved, Ate, Died, Won };

struct SnakeCell {
  uint8_t x, y;
  bool operator==(const SnakeCell& other) const { return x == other.x && y == other.y; }
};

struct SnakeGame {
  SnakeCell body[SNAKE_MAX_LEN]; // body[(tail + i) % MAX], i = 0 is the tail.
  uint8_t tail = 0;
  uint8_t length = 0;
  SnakeDir dir = SnakeDir::Right;     // Direction of the last move.
  SnakeDir nextDir = SnakeDir::Right; // Applied on the next step.
  SnakeCell food = {0, 0};
  uint16_t score = 0;
  SnakeState state = SnakeState::Ready;
  // Set by step() for incremental drawing.
  bool tailVacated = false;
  SnakeCell vacated = {0, 0};
  GameRandom rng;
};

inline SnakeCell snakeSegment(const SnakeGame& game, int index) {
  return game.body[(game.tail + index) % SNAKE_MAX_LEN];
}

inline SnakeCell snakeHead(const SnakeGame& game) {
  return snakeSegment(game, game.length - 1);
}

inline bool snakeOccupies(const SnakeGame& game, SnakeCell cell) {
  for (int i = 0; i < game.length; i++) {
    if (snakeSegment(game, i) == cell) return true;
  }
  return false;
}

// Uniformly chooses a free cell. False when there is none.
inline bool snakePlaceFood(SnakeGame& game) {
  int freeCells = SNAKE_COLS * SNAKE_ROWS - game.length;
  if (freeCells <= 0) return false;
  int pick = (int)game.rng.below(freeCells);
  for (int y = 0; y < SNAKE_ROWS; y++) {
    for (int x = 0; x < SNAKE_COLS; x++) {
      SnakeCell cell = {(uint8_t)x, (uint8_t)y};
      if (snakeOccupies(game, cell)) continue;
      if (pick-- == 0) {
        game.food = cell;
        return true;
      }
    }
  }
  return false;
}

// New game: three cells heading right in the middle row, waiting to start.
inline void snakeReset(SnakeGame& game, uint32_t seed) {
  game = SnakeGame();
  game.rng.seed(seed);
  for (int i = 0; i < SNAKE_START_LEN; i++) game.body[i] = {(uint8_t)(3 + i), (uint8_t)(SNAKE_ROWS / 2)};
  game.length = SNAKE_START_LEN;
  snakePlaceFood(game);
}

inline bool snakeOpposite(SnakeDir a, SnakeDir b) {
  return ((uint8_t)a + 2) % 4 == (uint8_t)b;
}

// Queues a turn. Reversing onto the body is ignored. In Ready or Paused a
// valid direction also starts or resumes the game.
inline bool snakeTurn(SnakeGame& game, SnakeDir dir) {
  if (game.state == SnakeState::Over || game.state == SnakeState::Won) return false;
  if (snakeOpposite(game.dir, dir)) return false;
  game.nextDir = dir;
  if (game.state != SnakeState::Running) game.state = SnakeState::Running;
  return true;
}

// Interval between moves: 250 ms, 5 ms faster per food, never below 120 ms.
inline uint32_t snakeIntervalMs(uint16_t score) {
  return score >= 26 ? 120 : 250 - 5 * score;
}

inline SnakeStep snakeStep(SnakeGame& game) {
  game.tailVacated = false;
  if (game.state != SnakeState::Running) return SnakeStep::None;
  game.dir = game.nextDir;
  SnakeCell head = snakeHead(game);
  int x = head.x, y = head.y;
  switch (game.dir) {
    case SnakeDir::Up: y--; break;
    case SnakeDir::Down: y++; break;
    case SnakeDir::Left: x--; break;
    case SnakeDir::Right: x++; break;
  }
  if (x < 0 || y < 0 || x >= SNAKE_COLS || y >= SNAKE_ROWS) {
    game.state = SnakeState::Over;
    return SnakeStep::Died;
  }
  SnakeCell next = {(uint8_t)x, (uint8_t)y};
  bool eating = next == game.food;
  // The tail moves away this step unless the snake grows, so it is safe.
  for (int i = eating ? 0 : 1; i < game.length; i++) {
    if (snakeSegment(game, i) == next) {
      game.state = SnakeState::Over;
      return SnakeStep::Died;
    }
  }
  if (!eating) {
    game.vacated = game.body[game.tail];
    game.tailVacated = true;
    game.body[(game.tail + game.length) % SNAKE_MAX_LEN] = next;
    game.tail = (game.tail + 1) % SNAKE_MAX_LEN;
    return SnakeStep::Moved;
  }
  game.body[(game.tail + game.length) % SNAKE_MAX_LEN] = next;
  game.length++;
  game.score++;
  if (game.length >= SNAKE_MAX_LEN || !snakePlaceFood(game)) {
    game.state = SnakeState::Won;
    return SnakeStep::Won;
  }
  return SnakeStep::Ate;
}

// Leaving the page (or the screensaver) pauses a running game.
inline void snakePause(SnakeGame& game) {
  if (game.state == SnakeState::Running) game.state = SnakeState::Paused;
}

// --- Snake page layout --------------------------------------------------------------

constexpr int SNAKE_CELL = 10;
constexpr int SNAKE_BOARD_X = 4;
constexpr int SNAKE_BOARD_Y = 40;
constexpr int SNAKE_PAD_X = 212; // Direction pad: x 212..316.

enum class SnakeHit : uint8_t { None, Up, Down, Left, Right, Board, Restart, Back };

inline SnakeHit snakeHitAt(int x, int y) {
  if (y >= 205) return x < 107 ? SnakeHit::Restart : x < 214 ? SnakeHit::Back : SnakeHit::None;
  if (y < 36) return SnakeHit::None;
  if (x < SNAKE_PAD_X - 4) return SnakeHit::Board;
  if (y < 94) return SnakeHit::Up;
  if (y < 150) return x < 264 ? SnakeHit::Left : SnakeHit::Right;
  return SnakeHit::Down;
}
