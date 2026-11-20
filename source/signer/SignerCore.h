#pragma once
// OriginXSigner (signer/OriginXSigner.cpp): makes the signing keys and signs the update lists on the author's PC,
// so no Python is needed (docs/signing.md). Pure logic, no Windows calls (tests/signer_core.cpp).
// The signatures are the launcher's (launcher/ManifestSignature.h) and byte for byte what tools/signing.py writes.
//
// A stable key (K1, K2) is kept as one line of text, so K2 can live in a password manager:
//   originx-key-v1:<id>:<salt>:<nonce>:<mac+box>        (base64 parts)
// box = the 32-byte Ed25519 seed under XChaCha20-Poly1305 with a key from Argon2id(passphrase, salt; 64 MiB, 3 passes).
// The id is in the authenticated data, so a key cannot be relabelled. A test key (T1) is "<id>:<seed>" for the Worker.
#include "../launcher/ManifestSignature.h"

#include <array>
#include <cctype>
#include <cstdio>
#include <string>
#include <vector>

namespace OriginXSigner {
using namespace MyClientLauncher;

using Seed = std::array<uint8_t, 32>;
using PublicKey = std::array<uint8_t, 32>;
using Json = nlohmann::json;

inline constexpr uint32_t ArgonBlocks = 65536;   // 64 MiB
inline constexpr uint32_t ArgonPasses = 3;

inline std::string Base64(const uint8_t* data, size_t size)
{
    static const char* abc = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    size_t i = 0;
    for (; i + 2 < size; i += 3)
    {
        const unsigned v = (data[i] << 16) | (data[i + 1] << 8) | data[i + 2];
        out += {abc[v >> 18], abc[(v >> 12) & 63], abc[(v >> 6) & 63], abc[v & 63]};
    }
    if (size - i == 1) { const unsigned v = data[i] << 16; out += {abc[v >> 18], abc[(v >> 12) & 63], '=', '='}; }
    if (size - i == 2) { const unsigned v = (data[i] << 16) | (data[i + 1] << 8); out += {abc[v >> 18], abc[(v >> 12) & 63], abc[(v >> 6) & 63], '='}; }
    return out;
}
inline std::string Base64(const std::string& s) { return Base64(reinterpret_cast<const uint8_t*>(s.data()), s.size()); }

inline std::string Hex(const uint8_t* data, size_t size)
{
    std::string out;
    char buf[3];
    for (size_t i = 0; i < size; ++i) { std::snprintf(buf, sizeof buf, "%02x", data[i]); out += buf; }
    return out;
}

inline PublicKey PublicOf(const Seed& seed)
{
    Seed copy = seed;   // crypto_ed25519_key_pair wipes the seed it is given
    uint8_t secret[64];
    PublicKey pub{};
    crypto_ed25519_key_pair(secret, pub.data(), copy.data());
    crypto_wipe(secret, sizeof secret);
    return pub;
}

// The first 8 bytes of a public key, as tools/signing.py fingerprint prints it.
inline std::string Fingerprint(const PublicKey& pub) { return Hex(pub.data(), 8); }

// The lines the author pastes: the launcher table entry ({1, {0x..}}) and the embed argument (1=hex).
inline std::string TableEntry(uint32_t id, const PublicKey& pub)
{
    std::string out = "{" + std::to_string(id) + ", {";
    char buf[8];
    for (size_t i = 0; i < pub.size(); ++i) { std::snprintf(buf, sizeof buf, "%s0x%02x", i ? ", " : "", pub[i]); out += buf; }
    return out + "}},";
}
inline std::string EmbedArgument(uint32_t id, const PublicKey& pub) { return std::to_string(id) + "=" + Hex(pub.data(), pub.size()); }

inline std::string TestKeyText(uint32_t id, const Seed& seed) { return std::to_string(id) + ":" + Base64(seed.data(), seed.size()); }

inline bool DeriveKey(const std::string& passphrase, const uint8_t salt[16], uint8_t key[32])
{
    std::vector<uint8_t> work(static_cast<size_t>(ArgonBlocks) * 1024);
    crypto_argon2_config config{CRYPTO_ARGON2_ID, ArgonBlocks, ArgonPasses, 1};
    crypto_argon2_inputs inputs{reinterpret_cast<const uint8_t*>(passphrase.data()), salt, static_cast<uint32_t>(passphrase.size()), 16};
    crypto_argon2(key, 32, work.data(), config, inputs, crypto_argon2_no_extras);
    crypto_wipe(work.data(), work.size());
    return true;
}

inline std::string KeyHeader(uint32_t id) { return "originx-key-v1:" + std::to_string(id); }

// salt (16) and nonce (24) come from the system's random source; the tests pass fixed ones.
inline std::string LockKey(uint32_t id, const Seed& seed, const std::string& passphrase, const uint8_t salt[16], const uint8_t nonce[24])
{
    uint8_t key[32];
    DeriveKey(passphrase, salt, key);
    const std::string ad = KeyHeader(id);
    uint8_t box[16 + 32];
    crypto_aead_lock(box + 16, box, key, nonce, reinterpret_cast<const uint8_t*>(ad.data()), ad.size(), seed.data(), seed.size());
    crypto_wipe(key, sizeof key);
    return ad + ":" + Base64(salt, 16) + ":" + Base64(nonce, 24) + ":" + Base64(box, sizeof box);
}

enum class Unlock { Ok, NotAKey, WrongPassphrase };

inline Unlock UnlockKey(std::string text, const std::string& passphrase, uint32_t& id, Seed& seed)
{
    while (!text.empty() && (text.back() == '\r' || text.back() == '\n' || text.back() == ' ')) text.pop_back();
    while (!text.empty() && (text.front() == ' ' || text.front() == '\xef' || text.front() == '\xbb' || text.front() == '\xbf')) text.erase(0, 1);
    std::vector<std::string> parts;
    size_t start = 0;
    for (size_t at; (at = text.find(':', start)) != std::string::npos; start = at + 1) parts.push_back(text.substr(start, at - start));
    parts.push_back(text.substr(start));
    if (parts.size() != 5 || parts[0] != "originx-key-v1" || parts[1].empty() || parts[1].size() > 5 ||
        parts[1].find_first_not_of("0123456789") != std::string::npos)
        return Unlock::NotAKey;
    const unsigned long parsed = std::stoul(parts[1]);
    std::string salt, nonce, box;
    if (parsed == 0 || parsed >= FirstTestKeyId || !DecodeBase64(parts[2], salt) || salt.size() != 16 ||
        !DecodeBase64(parts[3], nonce) || nonce.size() != 24 || !DecodeBase64(parts[4], box) || box.size() != 48)
        return Unlock::NotAKey;
    uint8_t key[32];
    DeriveKey(passphrase, reinterpret_cast<const uint8_t*>(salt.data()), key);
    const std::string ad = KeyHeader(static_cast<uint32_t>(parsed));
    const auto* raw = reinterpret_cast<const uint8_t*>(box.data());
    const int bad = crypto_aead_unlock(seed.data(), raw, key, reinterpret_cast<const uint8_t*>(nonce.data()),
                                       reinterpret_cast<const uint8_t*>(ad.data()), ad.size(), raw + 16, 32);
    crypto_wipe(key, sizeof key);
    if (bad) { crypto_wipe(seed.data(), seed.size()); return Unlock::WrongPassphrase; }
    id = static_cast<uint32_t>(parsed);
    return Unlock::Ok;
}

// The .sig text, the same bytes tools/signing.py sign_bytes writes.
inline std::string SignList(const std::string& kind, const std::string& bytes, uint32_t id, const Seed& seed)
{
    Seed copy = seed;
    uint8_t secret[64], pub[32], signature[64];
    crypto_ed25519_key_pair(secret, pub, copy.data());
    const std::string message = SignedMessage(kind, bytes);
    crypto_ed25519_sign(signature, secret, reinterpret_cast<const uint8_t*>(message.data()), message.size());
    crypto_wipe(secret, sizeof secret);
    return "{\"schemaVersion\": 1, \"keyId\": " + std::to_string(id) + ", \"signature\": \"" + Base64(signature, 64) + "\"}\n";
}

// Which kind a list is signed as, from its contents (and "early" in its path for the early channel).
// catalog/test is never offered: the Worker signs it. Empty when the file is not a list the author signs.
inline std::string DetectKind(const std::string& path, const std::string& bytes)
{
    const Json doc = Json::parse(bytes, nullptr, false);
    if (doc.is_discarded() || !doc.is_object()) return {};
    if (doc.contains("build") && doc.contains("sha256"))
    {
        const std::string channel = doc.value("channel", std::string("stable"));
        return channel == "test" ? "launcher/test" : channel == "stable" ? "launcher/stable" : std::string{};
    }
    if (doc.contains("releaseVersions")) return "versions/stable";
    if (doc.contains("sequence") && doc.contains("versions"))
    {
        std::string lower = path;
        for (auto& c : lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (lower.find("test") != std::string::npos) return {};   // a test list: the Worker signs it
        return lower.find("early") != std::string::npos ? "catalog/early" : "catalog/stable";
    }
    return {};
}

// What is about to be signed, so the author can stop before typing the passphrase.
inline std::string Summary(const std::string& kind, const std::string& bytes)
{
    std::string line = kind + ": " + std::to_string(bytes.size()) + " bytes";
    const Json doc = Json::parse(bytes, nullptr, false);
    try
    {
        if (kind.rfind("catalog/", 0) == 0)
        {
            line += ", sequence " + std::to_string(doc.at("sequence").get<uint64_t>());
            for (const auto& v : doc.at("versions")) line += ", " + v.value("id", std::string("?")) + " r" + (v.contains("revision") ? v.at("revision").dump() : "?");
        }
        else if (kind.rfind("launcher/", 0) == 0)
            line += ", channel " + doc.value("channel", std::string("stable")) + ", build " + doc.at("build").dump() + ", exe sha256 " + doc.at("sha256").get<std::string>();
        else if (kind == "versions/stable")
            line += ", " + std::to_string(doc.value("releaseVersions", Json::array()).size()) + " release versions";
    }
    catch (const std::exception&) { line += " (not the expected JSON)"; }
    return line;
}

} // namespace OriginXSigner
