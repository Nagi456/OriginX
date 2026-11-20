#pragma once
// The public halves of the signing keys (docs/signing.md; designs 2026-11-20-manifest-signing-design.md §5 and
// 2026-11-20-test-signing-key-design.md). Public keys are not secret.
// Two tables, chosen by the kind a list is signed as (ManifestSignature.h):
//   StableSigningKeys (ids 1..100): K1 signs, K2 is the spare kept offline. Only on the author's PC. They sign
//     every kind except catalog/test.
//   TestSigningKeys (ids 101..): T1, held by the Worker (Secret TEST_MANIFEST_KEY). Signs catalog/test only.
// The author makes them with `python tools/signing.py keygen` / `keygen --test` and writes them in with
// `python tools/signing.py embed --pub 1=... --pub 2=... --test-pub 101=...`.
#include <array>
#include <cstdint>

namespace MyClientLauncher {
struct SigningKey { uint32_t id; std::array<uint8_t, 32> publicKey; };

inline constexpr uint32_t FirstTestKeyId = 101;
inline constexpr bool TestKeyId(uint32_t id) { return id >= FirstTestKeyId; }

// K1/K2 (stable) and T1 (test) public keys, made with OriginXSigner. Only public keys live here; the private
// keys stay on the author's PC (K1), in the password manager (K2) and in the Worker Secret (T1).
inline constexpr std::array<SigningKey, 2> StableSigningKeys{{
    {1, {0x49, 0x25, 0xe7, 0xd3, 0x87, 0xe9, 0x3e, 0x55, 0x51, 0x0e, 0xd1, 0x97, 0xdd, 0x7a, 0x55, 0x05, 0x57, 0xe2, 0x20, 0xc1, 0x47, 0x78, 0x70, 0xcf, 0x67, 0xcf, 0x7e, 0xfc, 0x6d, 0x83, 0xdc, 0xc0}},
    {2, {0x17, 0x2d, 0x84, 0x8f, 0x5e, 0x1b, 0x47, 0xdc, 0xc9, 0xa1, 0x78, 0x8a, 0xd9, 0x62, 0x76, 0xd9, 0x81, 0x2f, 0xde, 0x46, 0xdb, 0xf0, 0x90, 0x97, 0xf1, 0x7f, 0xf7, 0x68, 0x0f, 0x16, 0xa9, 0x3c}},
}};
inline constexpr std::array<SigningKey, 1> TestSigningKeys{{
    {101, {0xaf, 0x6c, 0xe6, 0xcb, 0x01, 0x91, 0x24, 0x5a, 0x1f, 0xe1, 0x0a, 0x6d, 0xe4, 0xf1, 0x5b, 0x5a, 0xa4, 0x34, 0x29, 0x02, 0xfd, 0x6b, 0x77, 0xa2, 0x07, 0x18, 0x71, 0x31, 0x05, 0xbb, 0x40, 0x86}},
}};

template <size_t N>
constexpr bool KeyIdsIn(const std::array<SigningKey, N>& keys, bool test)
{
    for (size_t i = 0; i < N; ++i)
        if (keys[i].id == 0 || keys[i].id > 0xFFFF || TestKeyId(keys[i].id) != test) return false;
    return true;
}
static_assert(KeyIdsIn(StableSigningKeys, false), "stable key ids are 1..100");
static_assert(KeyIdsIn(TestSigningKeys, true), "test key ids are 101 and up");
}

#if defined(MYCLIENT_LAUNCHER_BUILD) && MYCLIENT_LAUNCHER_BUILD >= 41
static_assert(MyClientLauncher::StableSigningKeys.size() >= 2, "build 41 and later need K1 and K2 in launcher/SigningKeys.h");
static_assert(MyClientLauncher::TestSigningKeys.size() >= 1, "build 41 and later need T1 in launcher/SigningKeys.h");
#endif
