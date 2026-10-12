#pragma once

#include <stdint.h>
#include "ManifestSignature.h"

// HomeDeck Mini release keys this firmware trusts: PUBLIC keys only (ECDSA
// P-256, 04 || X || Y). The private halves stay offline with the project
// owner and never enter this repository, the backend or a device. An update
// is offered only if its manifest is signed by one of these (by key_id).
//
// Two slots, for rotation (README "Firmware authenticity"):
//   1. releases signed by key A, trusting A            {A, -}
//   2. a release still signed by A that also trusts B  {A, B}
//   3. later releases signed by B                       {A, B} or {B, -}
//   4. once devices have moved on, A is dropped         {B, -}
//
// A key compromised before devices trust its successor cannot be fixed by
// rotation alone (see README).
//
// IMPORTANT:
// - Only PUBLIC keys belong in this file.
// - The matching private signing key must never be committed.
// - The private key must never be stored on HomeDeck Mini.
// - The private key must never be placed in .env or on the backend.
//
// The throwaway keys used by the verifier self-test and test vectors must
// never appear here. Host tests enforce this.

constexpr size_t TRUSTED_UPDATE_KEY_SLOTS = 2;

// ---------------------------------------------------------------------------
// HomeDeck Mini release signing key — 2026
//
// key_id:
//   homedeck-release-2026
//
// Format:
//   ECDSA P-256 uncompressed public key
//   65 bytes: 04 || X(32 bytes) || Y(32 bytes)
//
// This key is PUBLIC and is safe to commit.
// ---------------------------------------------------------------------------

static const uint8_t RELEASE_KEY_HOMEDECK_RELEASE_2026[65] = {
  0x04, 0x6e, 0xf8, 0x24, 0x8a, 0x34, 0x24, 0xc4, 0x59, 0x7e, 0xac, 0xd2,
  0x97, 0x2b, 0x71, 0x34, 0x0d, 0x66, 0x03, 0xf8, 0x74, 0x0e, 0x70, 0x7d,
  0xd1, 0x84, 0xa9, 0x61, 0x96, 0x51, 0x51, 0x26, 0x4e, 0x14, 0xc5, 0xeb,
  0x12, 0xb2, 0x20, 0x61, 0xf9, 0x0f, 0xcd, 0x00, 0x7b, 0x2a, 0x66, 0x7d,
  0x3d, 0x08, 0x39, 0xca, 0x9b, 0xfc, 0xcc, 0x72, 0x50, 0x9c, 0xbb, 0x63,
  0xf0, 0xae, 0x96, 0x76, 0x1f,
};

// ---------------------------------------------------------------------------
// Trusted release keys
//
// Slot 1:
//   Current HomeDeck Mini production release key.
//
// Slot 2:
//   Reserved for future key rotation.
//
// Rotation example:
//
//   Current firmware:
//     { Key A, empty }
//
//   Transition firmware, still signed by Key A:
//     { Key A, Key B }
//
//   Future releases signed by Key B:
//     { Key A, Key B }
//
//   After migration:
//     { Key B, empty }
// ---------------------------------------------------------------------------

static const TrustedUpdateKey TRUSTED_UPDATE_KEYS[TRUSTED_UPDATE_KEY_SLOTS] = {
  {
    "homedeck-release-2026",
    RELEASE_KEY_HOMEDECK_RELEASE_2026,
  },
  {
    nullptr,
    nullptr,
  },
};
