#pragma once
#include <stdint.h>
#include "games/GameRandom.h"

// Pure Simon Says, shared with host tests. Playback is a millis()-driven
// state machine: a lead-in gap, then pad on / gap for each step, then the
// player repeats the sequence. Each completed round adds one step.

constexpr int SIMON_PADS = 4;
constexpr int SIMON_MAX = 32; // Repeating all 32 wins.
constexpr uint32_t SIMON_LEAD_MS = 700;   // Before the first round's playback.
constexpr uint32_t SIMON_ROUND_MS = 900;  // After a correct round, before the next.

enum class SimonPhase : uint8_t { Idle, ShowPad, ShowGap, PlayerInput, GameOver, Won };
enum class SimonEvent : uint8_t { None, PadOn, PadOff, InputReady };
enum class SimonPress : uint8_t { Ignored, Correct, RoundComplete, Wrong, Won };

struct SimonGame {
  uint8_t sequence[SIMON_MAX] = {};
  uint8_t length = 0;
  uint8_t showIndex = 0;  // Next step to play back.
  uint8_t inputIndex = 0; // Next step the player must press.
  SimonPhase phase = SimonPhase::Idle;
  uint32_t phaseAt = 0;
  uint32_t waitMs = 0;    // Length of the current gap.
  GameRandom rng;
};

// Gentle speed-up: 400 ms on / 200 ms gap, down to 200 / 100.
inline uint32_t simonOnMs(int length) {
  int ms = 400 - 10 * (length - 1);
  return ms < 200 ? 200 : (uint32_t)ms;
}

inline uint32_t simonGapMs(int length) {
  int ms = 200 - 5 * (length - 1);
  return ms < 100 ? 100 : (uint32_t)ms;
}

inline void simonBeginPlayback(SimonGame& game, uint32_t now, uint32_t leadMs) {
  game.showIndex = 0;
  game.inputIndex = 0;
  game.phase = SimonPhase::ShowGap;
  game.phaseAt = now;
  game.waitMs = leadMs;
}

inline void simonStart(SimonGame& game, uint32_t seed, uint32_t now) {
  game = SimonGame();
  game.rng.seed(seed);
  game.sequence[0] = (uint8_t)game.rng.below(SIMON_PADS);
  game.length = 1;
  simonBeginPlayback(game, now, SIMON_LEAD_MS);
}

// Call every loop. `pad` is set for PadOn / PadOff.
inline SimonEvent simonUpdate(SimonGame& game, uint32_t now, uint8_t& pad) {
  if (game.phase == SimonPhase::ShowGap) {
    if (now - game.phaseAt < game.waitMs) return SimonEvent::None;
    game.phaseAt = now;
    if (game.showIndex < game.length) {
      game.phase = SimonPhase::ShowPad;
      pad = game.sequence[game.showIndex];
      return SimonEvent::PadOn;
    }
    game.phase = SimonPhase::PlayerInput;
    game.inputIndex = 0;
    return SimonEvent::InputReady;
  }
  if (game.phase == SimonPhase::ShowPad) {
    if (now - game.phaseAt < simonOnMs(game.length)) return SimonEvent::None;
    pad = game.sequence[game.showIndex++];
    game.phase = SimonPhase::ShowGap;
    game.phaseAt = now;
    game.waitMs = simonGapMs(game.length);
    return SimonEvent::PadOff;
  }
  return SimonEvent::None;
}

inline SimonPress simonPress(SimonGame& game, uint8_t pad, uint32_t now) {
  if (game.phase != SimonPhase::PlayerInput || pad >= SIMON_PADS) return SimonPress::Ignored;
  if (pad != game.sequence[game.inputIndex]) {
    game.phase = SimonPhase::GameOver;
    return SimonPress::Wrong;
  }
  if (++game.inputIndex < game.length) return SimonPress::Correct;
  if (game.length >= SIMON_MAX) {
    game.phase = SimonPhase::Won;
    return SimonPress::Won;
  }
  game.sequence[game.length++] = (uint8_t)game.rng.below(SIMON_PADS);
  simonBeginPlayback(game, now, SIMON_ROUND_MS);
  return SimonPress::RoundComplete;
}

// Leaving the page mid-playback: replay the current round from the start on
// return. Input in progress, game over and idle are kept as they are.
inline void simonSuspend(SimonGame& game, uint32_t now) {
  if (game.phase == SimonPhase::ShowPad || game.phase == SimonPhase::ShowGap) {
    simonBeginPlayback(game, now, SIMON_LEAD_MS);
  }
}

// Completed rounds.
inline int simonScore(const SimonGame& game) {
  if (game.phase == SimonPhase::Won) return game.length;
  return game.length > 0 ? game.length - 1 : 0;
}

// --- Simon page layout --------------------------------------------------------------

// 2x2 pads, 150x78 each: 0 top-left, 1 top-right, 2 bottom-left, 3 bottom-right.
constexpr int SIMON_PAD_W = 150;
constexpr int SIMON_PAD_H = 78;
constexpr int SIMON_PAD_X[2] = {8, 162};
constexpr int SIMON_PAD_Y[2] = {40, 122};

// The whole quadrant counts; -1 outside the pad area.
inline int simonPadAt(int x, int y) {
  if (y < 36 || y >= 205 || x < 0 || x >= 320) return -1;
  return (y < 120 ? 0 : 2) + (x < 160 ? 0 : 1);
}
