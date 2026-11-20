#pragma once
// Monocypher 4.0.2 (monocypher.org, CC0 or BSD-2-Clause, LICENCE.md) built into the one source file that
// includes this, so the launcher and its tests need no extra file on the compile line. Only
// crypto_ed25519_check (RFC 8032 Ed25519, optional/monocypher-ed25519.c) is used, by ../../ManifestSignature.h.
// Include it from exactly one translation unit.
#if defined(__GNUC__)
#pragma GCC system_header
#endif
#include "monocypher.c"
#undef COPY
#undef FOR
#undef MIN
#undef WIPE_BUFFER
#undef WIPE_CTX
#undef ZERO
#include "monocypher-ed25519.c"
