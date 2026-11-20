#pragma once
// Ed25519 signatures on the update lists (design: docs/superpowers/specs/2026-11-20-manifest-signing-design.md).
// Each list (catalog.json, launcher.json, versions.json) has a detached <name>.sig:
//   {"schemaVersion":1,"keyId":N,"signature":"<base64 of 64 bytes>"}
// over "OriginX-manifest-v1\n" + kind + "\n" + the list's exact bytes. kind ties a signature to one use:
// catalog/stable, catalog/test, catalog/early, launcher/stable, launcher/test, versions/stable.
// catalog/test is signed by the Worker with a test key (ids 101..); every other kind only by a stable key
// (2026-11-20-test-signing-key-design.md). A key of the other table is "unknown" for that kind.
// Pure logic, no Windows calls (tests/manifest_signature.cpp). Include from one translation unit only
// (it builds Monocypher in).
#include "../Lang.h"
#include "SigningKeys.h"
#include <nlohmann/json.hpp>
#include <monocypher/monocypher_unity.h>
#include <atomic>
#include <string>
#include <vector>

namespace MyClientLauncher {

enum class SignatureCheck { Ok, Missing, Malformed, UnknownKey, RetiredKey, Mismatch };

struct SignatureResult {
    SignatureCheck check = SignatureCheck::Missing;
    uint32_t keyId = 0;
    bool ok() const { return check == SignatureCheck::Ok; }
};

inline const char* SignatureCheckName(SignatureCheck check)
{
    switch (check)
    {
    case SignatureCheck::Ok: return "ok";
    case SignatureCheck::Missing: return "missing";
    case SignatureCheck::Malformed: return "malformed";
    case SignatureCheck::UnknownKey: return "unknown key";
    case SignatureCheck::RetiredKey: return "retired key";
    default: return "mismatch";
    }
}

inline bool TestSignedKind(const std::string& kind) { return kind == "catalog/test"; }

inline bool KnownManifestKind(const std::string& kind)
{
    for (const char* known : {"catalog/stable", "catalog/test", "catalog/early", "launcher/stable", "launcher/test", "versions/stable"})
        if (kind == known) return true;
    return false;
}

// Strict base64 (standard alphabet, padded). False on anything else.
inline bool DecodeBase64(const std::string& text, std::string& out)
{
    out.clear();
    if (text.size() % 4) return false;
    auto value = [](char c) -> int {
        if (c >= 'A' && c <= 'Z') return c - 'A';
        if (c >= 'a' && c <= 'z') return c - 'a' + 26;
        if (c >= '0' && c <= '9') return c - '0' + 52;
        if (c == '+') return 62;
        if (c == '/') return 63;
        return -1;
    };
    for (size_t i = 0; i < text.size(); i += 4)
    {
        const bool last = i + 4 == text.size();
        const int pad = last ? (text[i + 3] == '=') + (text[i + 2] == '=' && text[i + 3] == '=') : 0;
        int v[4];
        for (int k = 0; k < 4; ++k)
        {
            v[k] = k >= 4 - pad ? 0 : value(text[i + k]);
            if (v[k] < 0) return false;
        }
        const unsigned triple = (unsigned(v[0]) << 18) | (unsigned(v[1]) << 12) | (unsigned(v[2]) << 6) | unsigned(v[3]);
        out += static_cast<char>((triple >> 16) & 0xFF);
        if (pad < 2) out += static_cast<char>((triple >> 8) & 0xFF);
        if (pad < 1) out += static_cast<char>(triple & 0xFF);
    }
    return true;
}

inline std::string SignedMessage(const std::string& kind, const std::string& bytes)
{
    return "OriginX-manifest-v1\n" + kind + "\n" + bytes;
}

// minKeyId: keys below it are refused (a key that was replaced after a leak, §5). Pass MinKeyFor(kind).
inline SignatureResult VerifyManifest(const std::string& kind, const std::string& bytes, const std::string& sigText,
                                      const std::vector<SigningKey>& keys, uint32_t minKeyId)
{
    SignatureResult result;
    if (sigText.empty()) return result;
    result.check = SignatureCheck::Malformed;
    if (!KnownManifestKind(kind) || sigText.size() > 4096) return result;
    nlohmann::json sig;
    try { sig = nlohmann::json::parse(sigText); }
    catch (const std::exception&) { return result; }
    if (!sig.is_object() || !sig.contains("schemaVersion") || !sig.at("schemaVersion").is_number_unsigned() ||
        sig.at("schemaVersion").get<uint64_t>() != 1 || !sig.contains("keyId") || !sig.at("keyId").is_number_unsigned() ||
        !sig.contains("signature") || !sig.at("signature").is_string())
        return result;
    const uint64_t keyId = sig.at("keyId").get<uint64_t>();
    if (keyId == 0 || keyId > 0xFFFF) return result;
    result.keyId = static_cast<uint32_t>(keyId);
    std::string raw;
    if (!DecodeBase64(sig.at("signature").get<std::string>(), raw) || raw.size() != 64) return result;
    const SigningKey* key = nullptr;
    for (const auto& candidate : keys)
        if (candidate.id == keyId && TestKeyId(candidate.id) == TestSignedKind(kind)) key = &candidate;
    if (!key) { result.check = SignatureCheck::UnknownKey; return result; }
    if (keyId < minKeyId) { result.check = SignatureCheck::RetiredKey; return result; }
    const std::string message = SignedMessage(kind, bytes);
    result.check = crypto_ed25519_check(reinterpret_cast<const uint8_t*>(raw.data()), key->publicKey.data(),
                                        reinterpret_cast<const uint8_t*>(message.data()), message.size()) == 0
                       ? SignatureCheck::Ok : SignatureCheck::Mismatch;
    return result;
}

inline std::vector<SigningKey> EmbeddedSigningKeys()
{
    std::vector<SigningKey> keys(StableSigningKeys.begin(), StableSigningKeys.end());
    keys.insert(keys.end(), TestSigningKeys.begin(), TestSigningKeys.end());
    return keys;
}

// A launcher made without stable keys (a local build, or one before the keys existed) cannot check anything and
// takes the lists as before. A numbered build 41 or later cannot be made without keys (SigningKeys.h).
inline bool SignaturesRequired(const std::vector<SigningKey>& keys)
{
    for (const auto& key : keys) if (!TestKeyId(key.id)) return true;
    return false;
}

// The lowest key still accepted, one mark per table. Raised (never lowered) once a list signed by a newer key of
// that table has been seen, and kept in launcher.json "minSigningKey" / "minTestSigningKey" (§5: after a leak of
// K1, the first list signed by K2 retires K1 on that PC). The test mark is also raised by "revokedTestKeys" in the
// signed stable launcher list (RemoteStop.h), which retires a leaked test key without a new launcher.
inline std::atomic<uint32_t> minSigningKeyId{1};
inline std::atomic<uint32_t> minTestSigningKeyId{FirstTestKeyId};
inline void NoteSigningKey(uint32_t keyId)
{
    auto& mark = TestKeyId(keyId) ? minTestSigningKeyId : minSigningKeyId;
    uint32_t seen = mark.load();
    while (keyId > seen && !mark.compare_exchange_weak(seen, keyId)) {}
}
inline uint32_t MinKeyFor(const std::string& kind) { return TestSignedKind(kind) ? minTestSigningKeyId.load() : minSigningKeyId.load(); }
// Set once a list from the site failed its check in this run: updates, downloads and version switching stop.
inline std::atomic<bool> signatureStopped{false};

// What the launcher shows when a list from the site cannot be verified (design §4, author 2026-11-20).
inline const char* SignatureFailedMessage()
{
    return Lang::T("配布元から届いた更新情報の署名を確かめられませんでした。安全のため、更新と版の切り替えを止めています。"
                   "しばらくしてからもう一度開いてください。続く場合は Discord で知らせてください。",
                   "The signature of the update information from the site could not be verified. For safety, updates and version "
                   "switching are stopped. Open the launcher again later. If this continues, tell us on Discord.");
}
// Shown every time a saved, verified version is started while that is so (author 2026-11-20, question 4).
inline const char* SignatureStoppedNote()
{
    return Lang::T("署名を確認できないため更新・版切替は停止中", "Updates and version switching are stopped: the signature could not be verified");
}

} // namespace MyClientLauncher
