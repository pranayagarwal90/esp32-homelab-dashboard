#pragma once
#include "ManifestSignature.h"

// The device's ECDSA P-256 verifier (mbedTLS), as an EcdsaP256Verify for
// checkManifestSignature(). Before its first real use each boot it runs a
// known-answer test (ManifestSignatureKat.h: a valid TEST signature must
// verify, a corrupted one must not); if that fails it returns -1 for every
// call, so no update can be offered. Any task; not reentrant (the single
// update task uses it).
int deviceEcdsaP256Verify(const uint8_t publicKey[UPDATE_PUBLIC_KEY_BYTES], const uint8_t* message, size_t length,
                          const uint8_t signature[SIGNATURE_RAW_BYTES]);
