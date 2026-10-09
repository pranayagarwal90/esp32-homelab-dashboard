#pragma once
#include <stdint.h>

// Tiny deterministic PRNG (xorshift32) for game layouts. Seeded from the
// hardware RNG on the device and from fixed values in host tests.
struct GameRandom {
  uint32_t state = 0x9E3779B9u;

  void seed(uint32_t value) {
    state = value ? value : 0x9E3779B9u; // Zero would stick at zero.
  }

  uint32_t next() {
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return state;
  }

  // Uniform enough for games: 0 <= result < bound (bound > 0).
  uint32_t below(uint32_t bound) {
    return next() % bound;
  }
};
