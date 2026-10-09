#include "MenuLayout.h"
#include "games/MemoryLogic.h"
#include "games/SimonLogic.h"
#include "games/SnakeLogic.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// --- Snake ---------------------------------------------------------------------------

// Places the snake from a list of cells (tail first), heading `dir`.
static void setSnake(SnakeGame& game, const SnakeCell* cells, int count, SnakeDir dir) {
  game.tail = 0;
  for (int i = 0; i < count; i++) game.body[i] = cells[i];
  game.length = (uint8_t)count;
  game.dir = game.nextDir = dir;
  game.state = SnakeState::Running;
}

static void testSnakeMovement() {
  SnakeGame game;
  snakeReset(game, 1);
  assert(game.state == SnakeState::Ready && game.length == SNAKE_START_LEN && game.score == 0);
  assert(snakeStep(game) == SnakeStep::None); // Not started.
  SnakeCell head = snakeHead(game);
  assert(head.x == 5 && head.y == SNAKE_ROWS / 2);

  game.food = {0, 0}; // Out of the way.
  assert(snakeTurn(game, SnakeDir::Right) && game.state == SnakeState::Running);
  assert(snakeStep(game) == SnakeStep::Moved);
  assert(snakeHead(game).x == 6 && game.length == SNAKE_START_LEN);
  assert(game.tailVacated && game.vacated.x == 3);

  assert(snakeTurn(game, SnakeDir::Up));
  assert(snakeStep(game) == SnakeStep::Moved);
  assert(snakeHead(game).x == 6 && snakeHead(game).y == SNAKE_ROWS / 2 - 1);
  assert(snakeTurn(game, SnakeDir::Left));
  snakeStep(game);
  assert(snakeHead(game).x == 5);
}

static void testSnakeReversal() {
  SnakeGame game;
  snakeReset(game, 2);
  // Reversal into the body is refused, also from Ready.
  assert(!snakeTurn(game, SnakeDir::Left) && game.state == SnakeState::Ready);
  assert(snakeTurn(game, SnakeDir::Up));
  // Two quick taps between steps: still judged against the last move.
  assert(!snakeTurn(game, SnakeDir::Left));
  assert(snakeTurn(game, SnakeDir::Down));
  game.food = {0, 0};
  snakeStep(game);
  assert(game.dir == SnakeDir::Down);
  assert(!snakeTurn(game, SnakeDir::Up));
  assert(snakeOpposite(SnakeDir::Left, SnakeDir::Right) && !snakeOpposite(SnakeDir::Up, SnakeDir::Left));
}

static void testSnakeWalls() {
  const SnakeDir dirs[] = {SnakeDir::Up, SnakeDir::Down, SnakeDir::Left, SnakeDir::Right};
  const SnakeCell edge[] = {{5, 0}, {5, SNAKE_ROWS - 1}, {0, 5}, {SNAKE_COLS - 1, 5}};
  for (int i = 0; i < 4; i++) {
    SnakeGame game;
    snakeReset(game, 3);
    setSnake(game, &edge[i], 1, dirs[i]);
    game.food = {10, 10};
    assert(snakeStep(game) == SnakeStep::Died);
    assert(game.state == SnakeState::Over);
    assert(snakeStep(game) == SnakeStep::None);
    assert(!snakeTurn(game, SnakeDir::Up)); // Over: only restart.
  }
}

static void testSnakeSelfCollision() {
  // A 5-long snake curling right, then turning up into its own body.
  SnakeGame game;
  snakeReset(game, 4);
  const SnakeCell cells[] = {{6, 5}, {5, 5}, {4, 5}, {4, 6}, {5, 6}};
  setSnake(game, cells, 5, SnakeDir::Right);
  game.food = {15, 15};
  assert(snakeTurn(game, SnakeDir::Up));
  assert(snakeStep(game) == SnakeStep::Died); // (5,5) is body, not the tail.
  assert(game.state == SnakeState::Over);

  // Following the tail exactly is legal: the tail cell is vacated.
  SnakeGame loop;
  snakeReset(loop, 5);
  const SnakeCell ring[] = {{5, 6}, {5, 5}, {6, 5}, {6, 6}};
  setSnake(loop, ring, 4, SnakeDir::Down);
  loop.food = {15, 15};
  snakeTurn(loop, SnakeDir::Left);
  assert(snakeStep(loop) == SnakeStep::Moved); // Head into old tail (5,6).
  assert(loop.state == SnakeState::Running);

  // ...but not when growing: the tail stays.
  SnakeGame grow;
  snakeReset(grow, 6);
  setSnake(grow, ring, 4, SnakeDir::Down);
  grow.food = {5, 6}; // Food cannot really be on the snake; forces the case.
  snakeTurn(grow, SnakeDir::Left);
  assert(snakeStep(grow) == SnakeStep::Died);
}

static void testSnakeFood() {
  SnakeGame game;
  snakeReset(game, 7);
  assert(!snakeOccupies(game, game.food));
  game.food = {6, (uint8_t)(SNAKE_ROWS / 2)};
  snakeTurn(game, SnakeDir::Right);
  assert(snakeStep(game) == SnakeStep::Ate);
  assert(game.length == SNAKE_START_LEN + 1 && game.score == 1 && !game.tailVacated);
  assert(snakeHead(game).x == 6);
  assert(!snakeOccupies(game, game.food));
  // Speed-up is gentle and bounded.
  assert(snakeIntervalMs(0) == 250 && snakeIntervalMs(1) == 245);
  assert(snakeIntervalMs(26) == 120 && snakeIntervalMs(500) == 120);

  // Food never lands on the snake, across many seeds and a long body.
  for (uint32_t seed = 1; seed < 300; seed++) {
    SnakeGame g;
    snakeReset(g, seed);
    g.tail = 0;
    g.length = 0;
    for (int y = 0; y < 4; y++) {
      for (int x = 0; x < SNAKE_COLS && g.length < 100; x++) g.body[g.length++] = {(uint8_t)x, (uint8_t)y};
    }
    assert(snakePlaceFood(g));
    assert(!snakeOccupies(g, g.food));
    assert(g.food.x < SNAKE_COLS && g.food.y < SNAKE_ROWS);
  }

  // Only one free cell: food goes exactly there. None free: refused.
  SnakeGame tight;
  snakeReset(tight, 9);
  tight.tail = 0;
  tight.length = 0;
  for (int y = 0; y < SNAKE_ROWS && tight.length < SNAKE_MAX_LEN; y++) {
    for (int x = 0; x < SNAKE_COLS && tight.length < SNAKE_MAX_LEN; x++) {
      if (!(x == 7 && y == 3)) tight.body[tight.length++] = {(uint8_t)x, (uint8_t)y};
    }
  }
  // Not a full board (MAX_LEN < cells), so just check the food avoids the body.
  assert(snakePlaceFood(tight) && !snakeOccupies(tight, tight.food));
}

static void testSnakeScoreAndWin() {
  SnakeGame game;
  snakeReset(game, 11);
  // Eat along a row repeatedly by placing food in front of the head.
  snakeTurn(game, SnakeDir::Right);
  for (int i = 0; i < 5; i++) {
    SnakeCell head = snakeHead(game);
    game.food = {(uint8_t)(head.x + 1), head.y};
    assert(snakeStep(game) == SnakeStep::Ate);
  }
  assert(game.score == 5 && game.length == SNAKE_START_LEN + 5);

  // Reaching the maximum length wins.
  SnakeGame win;
  snakeReset(win, 12);
  win.tail = 0;
  win.length = 0;
  for (int y = 0; y < SNAKE_ROWS && win.length < SNAKE_MAX_LEN - 1; y++) {
    for (int i = 0; i < SNAKE_COLS && win.length < SNAKE_MAX_LEN - 1; i++) {
      int x = y % 2 == 0 ? i : SNAKE_COLS - 1 - i; // Boustrophedon body.
      win.body[win.length++] = {(uint8_t)x, (uint8_t)y};
    }
  }
  SnakeCell head = snakeHead(win);
  win.state = SnakeState::Running;
  win.dir = win.nextDir = SnakeDir::Down;
  win.food = {head.x, (uint8_t)(head.y + 1)};
  assert(snakeStep(win) == SnakeStep::Won && win.state == SnakeState::Won);
  assert(win.length == SNAKE_MAX_LEN);
}

static void testSnakePauseAndRing() {
  SnakeGame game;
  snakeReset(game, 13);
  snakeTurn(game, SnakeDir::Right);
  snakePause(game);
  assert(game.state == SnakeState::Paused && snakeStep(game) == SnakeStep::None);
  assert(snakeTurn(game, SnakeDir::Up) && game.state == SnakeState::Running); // Resume.
  snakePause(game);
  snakePause(game);
  assert(game.state == SnakeState::Paused);

  // Ring buffer wrap: many moves keep the body contiguous.
  SnakeGame ring;
  snakeReset(ring, 14);
  ring.food = {0, 0};
  const SnakeDir square[] = {SnakeDir::Right, SnakeDir::Down, SnakeDir::Left, SnakeDir::Up};
  snakeTurn(ring, SnakeDir::Right);
  for (int step = 0; step < 1000; step++) {
    if (step % 4 == 0) snakeTurn(ring, square[(step / 4) % 4]);
    SnakeStep result = snakeStep(ring);
    assert(result == SnakeStep::Moved || result == SnakeStep::Ate);
    for (int i = 1; i < ring.length; i++) {
      SnakeCell a = snakeSegment(ring, i - 1), b = snakeSegment(ring, i);
      assert(abs(a.x - b.x) + abs(a.y - b.y) == 1);
    }
  }
}

static void testSnakeLayout() {
  assert(snakeHitAt(260, 60) == SnakeHit::Up);
  assert(snakeHitAt(230, 120) == SnakeHit::Left);
  assert(snakeHitAt(300, 120) == SnakeHit::Right);
  assert(snakeHitAt(260, 180) == SnakeHit::Down);
  assert(snakeHitAt(100, 120) == SnakeHit::Board);
  assert(snakeHitAt(50, 220) == SnakeHit::Restart);
  assert(snakeHitAt(160, 220) == SnakeHit::Back);
  assert(snakeHitAt(100, 20) == SnakeHit::None);
  // Board fits left of the pad and above the bottom bar.
  assert(SNAKE_BOARD_X + SNAKE_COLS * SNAKE_CELL < SNAKE_PAD_X - 4);
  assert(SNAKE_BOARD_Y + SNAKE_ROWS * SNAKE_CELL <= 205);
}

// --- Memory Match ----------------------------------------------------------------------

// Index of the card with `value`, skipping `not`.
static int findCard(const MemoryGame& game, int value, int skip = -1) {
  for (int i = 0; i < MEMORY_CARDS; i++) {
    if (i != skip && game.value[i] == value) return i;
  }
  return -1;
}

static void testMemoryShuffle() {
  bool differs = false;
  MemoryGame reference;
  memoryReset(reference, 1);
  for (uint32_t seed = 1; seed < 200; seed++) {
    MemoryGame game;
    memoryReset(game, seed);
    int counts[MEMORY_PAIRS + 1] = {};
    for (int i = 0; i < MEMORY_CARDS; i++) {
      assert(game.value[i] >= 1 && game.value[i] <= MEMORY_PAIRS);
      counts[game.value[i]]++;
      assert(game.card[i] == MemoryCard::Hidden);
      if (game.value[i] != reference.value[i]) differs = true;
    }
    for (int v = 1; v <= MEMORY_PAIRS; v++) assert(counts[v] == 2);
    assert(game.phase == MemoryPhase::WaitFirst && game.moves == 0);
  }
  assert(differs); // Seeds give different layouts.
  // Same seed, same board.
  MemoryGame again;
  memoryReset(again, 1);
  assert(memcmp(again.value, reference.value, sizeof(again.value)) == 0);
}

static void testMemoryMatchAndMismatch() {
  MemoryGame game;
  memoryReset(game, 42);
  int a = findCard(game, 3), b = findCard(game, 3, a), c = findCard(game, 5);

  assert(memoryTap(game, a, 0) == MemoryTap::First);
  assert(game.phase == MemoryPhase::WaitSecond && game.card[a] == MemoryCard::Shown);
  assert(memoryTap(game, a, 10) == MemoryTap::Ignored); // Same card again.
  assert(game.moves == 0);
  assert(memoryTap(game, b, 20) == MemoryTap::Matched);
  assert(game.card[a] == MemoryCard::Matched && game.card[b] == MemoryCard::Matched);
  assert(game.moves == 1 && game.matchedPairs == 1 && game.phase == MemoryPhase::WaitFirst);
  assert(memoryTap(game, a, 30) == MemoryTap::Ignored); // Matched card.

  // Mismatch: both stay visible, further taps ignored, hidden after the delay.
  int d = findCard(game, 6);
  assert(memoryTap(game, c, 1000) == MemoryTap::First);
  assert(memoryTap(game, d, 1100) == MemoryTap::Mismatch);
  assert(game.phase == MemoryPhase::ShowMismatch && game.moves == 2);
  assert(game.card[c] == MemoryCard::Shown && game.card[d] == MemoryCard::Shown);
  int e = findCard(game, 7);
  assert(memoryTap(game, e, 1200) == MemoryTap::Ignored);
  assert(game.card[e] == MemoryCard::Hidden);
  assert(!memoryUpdate(game, 1100 + MEMORY_MISMATCH_MS - 1));
  assert(game.card[c] == MemoryCard::Shown);
  assert(memoryUpdate(game, 1100 + MEMORY_MISMATCH_MS));
  assert(game.card[c] == MemoryCard::Hidden && game.card[d] == MemoryCard::Hidden);
  assert(game.phase == MemoryPhase::WaitFirst && game.first == -1);
  assert(!memoryUpdate(game, 5000)); // Nothing pending.

  // Invalid taps.
  assert(memoryTap(game, -1, 0) == MemoryTap::Ignored);
  assert(memoryTap(game, MEMORY_CARDS, 0) == MemoryTap::Ignored);

  // Mismatch timing across millis() rollover.
  MemoryGame wrap;
  memoryReset(wrap, 7);
  int p = findCard(wrap, 1), q = findCard(wrap, 2);
  memoryTap(wrap, p, 0xFFFFFF00u);
  memoryTap(wrap, q, 0xFFFFFF00u);
  assert(!memoryUpdate(wrap, 0x00000100u)); // 512 ms later.
  assert(memoryUpdate(wrap, 0x00000300u));  // 1024 ms later.

  // Leaving the page mid-mismatch: hidden at once on return.
  MemoryGame away;
  memoryReset(away, 8);
  p = findCard(away, 1);
  q = findCard(away, 2);
  memoryTap(away, p, 0);
  memoryTap(away, q, 0);
  memoryHideMismatch(away);
  assert(away.phase == MemoryPhase::WaitFirst && away.card[p] == MemoryCard::Hidden);
  memoryHideMismatch(away); // No-op otherwise.
  assert(away.phase == MemoryPhase::WaitFirst);
}

static void testMemoryCompletion() {
  MemoryGame game;
  memoryReset(game, 99);
  MemoryTap last = MemoryTap::Ignored;
  for (int v = 1; v <= MEMORY_PAIRS; v++) {
    int a = findCard(game, v), b = findCard(game, v, a);
    assert(memoryTap(game, a, 0) == MemoryTap::First);
    last = memoryTap(game, b, 0);
  }
  assert(last == MemoryTap::Completed);
  assert(game.phase == MemoryPhase::Complete && game.matchedPairs == MEMORY_PAIRS && game.moves == MEMORY_PAIRS);
  for (int i = 0; i < MEMORY_CARDS; i++) {
    assert(game.card[i] == MemoryCard::Matched);
    assert(memoryTap(game, i, 0) == MemoryTap::Ignored);
  }
}

static void testMemoryLayout() {
  // Card centres map to themselves; the grid fits above the bottom bar.
  for (int i = 0; i < MEMORY_CARDS; i++) {
    int x = MEMORY_X0 + (i % MEMORY_COLS) * MEMORY_PITCH_X + MEMORY_CARD_W / 2;
    int y = MEMORY_Y0 + (i / MEMORY_COLS) * MEMORY_PITCH_Y + MEMORY_CARD_H / 2;
    assert(memoryCardAt(x, y) == i);
  }
  assert(memoryCardAt(5, 100) == -1);
  assert(memoryCardAt(160, 20) == -1);
  assert(memoryCardAt(315, 100) == -1);
  assert(memoryCardAt(160, 206) == -1);
  assert(MEMORY_X0 + 3 * MEMORY_PITCH_X + MEMORY_CARD_W <= 320);
  assert(MEMORY_Y0 + 3 * MEMORY_PITCH_Y + MEMORY_CARD_H <= 205);
  assert(MEMORY_CARD_W >= 60 && MEMORY_CARD_H >= 36); // Touch-friendly.
}

// --- Simon Says ----------------------------------------------------------------------

// Runs playback until input is ready; returns the pads shown, in order.
static int playBack(SimonGame& game, uint32_t& now, uint8_t* shown) {
  int count = 0;
  bool lit = false;
  for (int guard = 0; guard < 10000; guard++) {
    uint8_t pad = 0xFF;
    SimonEvent event = simonUpdate(game, now, pad);
    if (event == SimonEvent::PadOn) {
      assert(!lit && pad < SIMON_PADS);
      lit = true;
      shown[count++] = pad;
    } else if (event == SimonEvent::PadOff) {
      assert(lit && pad == shown[count - 1]);
      lit = false;
    } else if (event == SimonEvent::InputReady) {
      assert(!lit && game.phase == SimonPhase::PlayerInput);
      return count;
    }
    now += 10;
  }
  assert(false);
  return -1;
}

static void testSimonSequence() {
  SimonGame game;
  assert(game.phase == SimonPhase::Idle);
  uint8_t pad = 0;
  assert(simonUpdate(game, 1000, pad) == SimonEvent::None); // Idle does nothing.
  assert(simonPress(game, 0, 1000) == SimonPress::Ignored);

  uint32_t now = 1000;
  simonStart(game, 123, now);
  assert(game.length == 1 && game.sequence[0] < SIMON_PADS && game.phase == SimonPhase::ShowGap);
  // Pad sequences vary with the seed.
  bool varied = false;
  for (uint32_t seed = 1; seed < 50; seed++) {
    SimonGame other;
    simonStart(other, seed, 0);
    if (other.sequence[0] != game.sequence[0]) varied = true;
  }
  assert(varied);

  // Exact transitions: lead-in gap, pad on for 400 ms, gap 200 ms, input.
  SimonGame exact;
  simonStart(exact, 5, 0);
  assert(simonUpdate(exact, SIMON_LEAD_MS - 1, pad) == SimonEvent::None);
  assert(simonUpdate(exact, SIMON_LEAD_MS, pad) == SimonEvent::PadOn && pad == exact.sequence[0]);
  assert(exact.phase == SimonPhase::ShowPad);
  assert(simonPress(exact, pad, SIMON_LEAD_MS) == SimonPress::Ignored); // Not during playback.
  assert(simonUpdate(exact, SIMON_LEAD_MS + 399, pad) == SimonEvent::None);
  assert(simonUpdate(exact, SIMON_LEAD_MS + 400, pad) == SimonEvent::PadOff);
  assert(exact.phase == SimonPhase::ShowGap);
  assert(simonUpdate(exact, SIMON_LEAD_MS + 599, pad) == SimonEvent::None);
  assert(simonUpdate(exact, SIMON_LEAD_MS + 600, pad) == SimonEvent::InputReady);
  assert(simonUpdate(exact, SIMON_LEAD_MS + 5000, pad) == SimonEvent::None); // Waits for input.
}

static void testSimonRounds() {
  SimonGame game;
  uint32_t now = 0;
  simonStart(game, 77, now);
  uint8_t shown[SIMON_MAX];
  for (int round = 1; round <= 5; round++) {
    assert(game.length == round);
    assert(playBack(game, now, shown) == round);
    for (int i = 0; i < round; i++) assert(shown[i] == game.sequence[i]);
    // Correct input; the last press completes the round and adds a step.
    for (int i = 0; i < round - 1; i++) assert(simonPress(game, shown[i], now) == SimonPress::Correct);
    uint8_t before[SIMON_MAX];
    memcpy(before, game.sequence, sizeof(before));
    assert(simonPress(game, shown[round - 1], now) == SimonPress::RoundComplete);
    assert(game.length == round + 1 && game.phase == SimonPhase::ShowGap && game.waitMs == SIMON_ROUND_MS);
    assert(memcmp(before, game.sequence, round) == 0); // Earlier steps unchanged.
    assert(simonScore(game) == round);
  }

  // Wrong input ends the game.
  playBack(game, now, shown);
  assert(simonPress(game, shown[0], now) == SimonPress::Correct);
  uint8_t wrong = (uint8_t)((shown[1] + 1) % SIMON_PADS);
  assert(simonPress(game, wrong, now) == SimonPress::Wrong);
  assert(game.phase == SimonPhase::GameOver && simonScore(game) == 5);
  assert(simonPress(game, shown[1], now) == SimonPress::Ignored);
  uint8_t pad = 0;
  assert(simonUpdate(game, now + 10000, pad) == SimonEvent::None);
  assert(simonPress(game, 9, now) == SimonPress::Ignored);
}

static void testSimonMaxAndSpeed() {
  SimonGame game;
  uint32_t now = 0;
  simonStart(game, 31, now);
  uint8_t shown[SIMON_MAX];
  SimonPress last = SimonPress::Ignored;
  while (game.phase != SimonPhase::Won) {
    int count = playBack(game, now, shown);
    for (int i = 0; i < count; i++) last = simonPress(game, shown[i], now);
  }
  assert(last == SimonPress::Won);
  assert(game.length == SIMON_MAX && simonScore(game) == SIMON_MAX);

  assert(simonOnMs(1) == 400 && simonGapMs(1) == 200);
  assert(simonOnMs(10) == 310 && simonGapMs(10) == 155);
  assert(simonOnMs(SIMON_MAX) == 200 && simonGapMs(SIMON_MAX) == 100);
}

static void testSimonSuspendAndRollover() {
  // Leaving mid-playback replays the round from its first step.
  SimonGame game;
  simonStart(game, 9, 0);
  uint32_t now = 0;
  uint8_t shown[SIMON_MAX];
  playBack(game, now, shown);
  simonPress(game, shown[0], now); // Round 2 playback begins.
  uint8_t pad = 0;
  now += SIMON_ROUND_MS;
  assert(simonUpdate(game, now, pad) == SimonEvent::PadOn);
  simonSuspend(game, 50000);
  assert(game.phase == SimonPhase::ShowGap && game.showIndex == 0 && game.waitMs == SIMON_LEAD_MS);
  now = 50000;
  assert(playBack(game, now, shown) == 2);
  // Input in progress is kept.
  assert(simonPress(game, shown[0], now) == SimonPress::Correct);
  simonSuspend(game, now + 100000);
  assert(game.phase == SimonPhase::PlayerInput && game.inputIndex == 1);

  // millis() rollover during playback.
  SimonGame wrap;
  simonStart(wrap, 3, 0xFFFFFE00u);
  assert(simonUpdate(wrap, 0xFFFFFFFFu, pad) == SimonEvent::None); // 511 ms < lead.
  assert(simonUpdate(wrap, 0x000000C0u, pad) == SimonEvent::PadOn); // 704 ms.
}

static void testSimonLayout() {
  assert(simonPadAt(80, 80) == 0);
  assert(simonPadAt(240, 80) == 1);
  assert(simonPadAt(80, 160) == 2);
  assert(simonPadAt(240, 160) == 3);
  assert(simonPadAt(160, 20) == -1);
  assert(simonPadAt(160, 220) == -1);
  // Pads fit and sit inside their own quadrant.
  assert(SIMON_PAD_X[1] + SIMON_PAD_W <= 320 && SIMON_PAD_Y[1] + SIMON_PAD_H <= 205);
  for (int pad = 0; pad < SIMON_PADS; pad++) {
    int cx = SIMON_PAD_X[pad % 2] + SIMON_PAD_W / 2, cy = SIMON_PAD_Y[pad / 2] + SIMON_PAD_H / 2;
    assert(simonPadAt(cx, cy) == pad);
  }
}

// --- Navigation ------------------------------------------------------------------------

static void testMoreMenu() {
  // Each drawn button's centre maps to its item.
  struct { int x, y; MoreItem item; } cases[] = {
    {85, 60, MoreItem::Time}, {235, 60, MoreItem::Calendar},
    {85, 100, MoreItem::Games}, {235, 100, MoreItem::Photos},
    {85, 140, MoreItem::Alerts}, {235, 140, MoreItem::Settings},
    {85, 180, MoreItem::Tools}, {235, 180, MoreItem::Tools},
  };
  for (const auto& c : cases) assert(moreItemAt(c.x, c.y) == c.item);
  // Band edges: contiguous rows, header and nav bar excluded.
  assert(moreItemAt(85, 39) == MoreItem::None);
  assert(moreItemAt(85, 40) == MoreItem::Time && moreItemAt(85, 79) == MoreItem::Time);
  assert(moreItemAt(85, 80) == MoreItem::Games);
  assert(moreItemAt(85, 200) == MoreItem::Tools);
  assert(moreItemAt(85, 205) == MoreItem::None && moreItemAt(85, 230) == MoreItem::None);
  // Buttons fit above the nav bar with a gap between rows.
  assert(MORE_Y0 + 3 * MORE_PITCH + MORE_H <= 205 && MORE_PITCH > MORE_H);
}

static void testGamesAndToolsMenus() {
  assert(gamesItemAt(85, 66) == MenuNode::TicTacToe);
  assert(gamesItemAt(235, 66) == MenuNode::Reaction);
  assert(gamesItemAt(85, 118) == MenuNode::Snake);
  assert(gamesItemAt(235, 118) == MenuNode::Memory);
  assert(gamesItemAt(85, 170) == MenuNode::Simon && gamesItemAt(235, 170) == MenuNode::Simon);
  assert(gamesItemAt(160, 20) == MenuNode::None && gamesItemAt(160, 220) == MenuNode::None);
  // All five games on one page with the same 44 px buttons as MORE had: no paging.
  assert(GAMES_Y[2] + GAMES_H <= 205 && GAMES_H >= 44);

  assert(toolsItemAt(160, TOOLS_Y + TOOLS_H / 2) == MenuNode::Stopwatch);
  assert(toolsItemAt(160, 150) == MenuNode::None && toolsItemAt(160, 20) == MenuNode::None);
}

static void testBackNavigation() {
  assert(menuParent(MenuNode::Stopwatch) == MenuNode::Tools);
  assert(menuParent(MenuNode::Tools) == MenuNode::More);
  assert(menuParent(MenuNode::Games) == MenuNode::More);
  const MenuNode games[] = {MenuNode::TicTacToe, MenuNode::Reaction, MenuNode::Snake, MenuNode::Memory,
                            MenuNode::Simon};
  for (MenuNode game : games) {
    assert(menuParent(game) == MenuNode::Games);
    assert(menuParent(menuParent(game)) == MenuNode::More); // Game -> GAMES -> MORE.
  }
  assert(menuParent(menuParent(MenuNode::Stopwatch)) == MenuNode::More);
  assert(menuParent(MenuNode::More) == MenuNode::More);
}

int main() {
  testSnakeMovement();
  testSnakeReversal();
  testSnakeWalls();
  testSnakeSelfCollision();
  testSnakeFood();
  testSnakeScoreAndWin();
  testSnakePauseAndRing();
  testSnakeLayout();
  testMemoryShuffle();
  testMemoryMatchAndMismatch();
  testMemoryCompletion();
  testMemoryLayout();
  testSimonSequence();
  testSimonRounds();
  testSimonMaxAndSpeed();
  testSimonSuspendAndRollover();
  testSimonLayout();
  testMoreMenu();
  testGamesAndToolsMenus();
  testBackNavigation();
  puts("Game tests passed: snake, memory, simon, navigation");
}
