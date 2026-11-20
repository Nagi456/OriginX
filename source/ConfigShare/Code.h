// Share code "OXC." + base64url(body + CRC32) (docs/config-sharing-design.md §5.1, §6).
// Windows-independent. Everything a code can carry is checked against Registry.h:
// only listed integer fields are read, and only name/author are strings.
#pragma once

#include "Registry.h"
#include "Snapshot.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace ConfigShare {

constexpr uint8_t CodeVersion = 1;
constexpr size_t MaxInputBytes = 16 * 1024;
constexpr size_t MaxBodyBytes = 12 * 1024;
constexpr size_t MaxItems = 512;
constexpr size_t MaxTextBytes = 64;
constexpr size_t MaxVarintBytes = 5;

enum class DecodeError : uint8_t {
    None,
    TooLarge,          // input over 16KB, or decoded body over 12KB
    NotACode,          // no "OXC." prefix
    SiteCode,          // "OXS." — not supported yet (§6.5)
    BadBase64,
    BadCrc,            // cut short or altered
    NewerCodeVersion,
    NewerSchema,
    Malformed,         // truncated fields, long varint, bad lengths, trailing bytes
    TooManyItems,
    DuplicateId,
};

struct DecodeResult {
    DecodeError error = DecodeError::None;
    Snapshot snapshot;
    uint8_t categories = 0;
    std::string name;
    std::string author;
    int warnings = 0;         // skipped unknown ids, dropped bad bools, clamped ints
    bool verifiedAuthor = false;   // stage 2 extension point; always false in v1

    bool ok() const { return error == DecodeError::None; }
};

namespace detail {

inline uint32_t Crc32(const uint8_t* data, size_t size)
{
    static const std::array<uint32_t, 256> table = [] {
        std::array<uint32_t, 256> t{};
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t c = i;
            for (int k = 0; k < 8; ++k) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            t[i] = c;
        }
        return t;
    }();
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < size; ++i) crc = table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
    return crc ^ 0xFFFFFFFFu;
}

inline constexpr char kB64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";

inline std::string Base64UrlEncode(const std::vector<uint8_t>& in)
{
    std::string out;
    out.reserve((in.size() * 4 + 2) / 3);
    size_t i = 0;
    for (; i + 3 <= in.size(); i += 3) {
        const uint32_t v = (uint32_t(in[i]) << 16) | (uint32_t(in[i + 1]) << 8) | in[i + 2];
        out += kB64[(v >> 18) & 63];
        out += kB64[(v >> 12) & 63];
        out += kB64[(v >> 6) & 63];
        out += kB64[v & 63];
    }
    const size_t rest = in.size() - i;
    if (rest == 1) {
        const uint32_t v = uint32_t(in[i]) << 16;
        out += kB64[(v >> 18) & 63];
        out += kB64[(v >> 12) & 63];
    } else if (rest == 2) {
        const uint32_t v = (uint32_t(in[i]) << 16) | (uint32_t(in[i + 1]) << 8);
        out += kB64[(v >> 18) & 63];
        out += kB64[(v >> 12) & 63];
        out += kB64[(v >> 6) & 63];
    }
    return out;
}

inline int B64Value(char c)
{
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '-') return 62;
    if (c == '_') return 63;
    return -1;
}

// False on a character outside the alphabet, an impossible length, or non-zero padding bits.
inline bool Base64UrlDecode(std::string_view in, std::vector<uint8_t>& out)
{
    if (in.size() % 4 == 1) return false;
    out.clear();
    out.reserve(in.size() * 3 / 4);
    uint32_t acc = 0;
    int bits = 0;
    for (char c : in) {
        const int v = B64Value(c);
        if (v < 0) return false;
        acc = (acc << 6) | uint32_t(v);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out.push_back(uint8_t((acc >> bits) & 0xFF));
        }
    }
    return (acc & ((1u << bits) - 1)) == 0;
}

inline void PutVarint(std::vector<uint8_t>& out, uint32_t v)
{
    while (v >= 0x80) {
        out.push_back(uint8_t(v | 0x80));
        v >>= 7;
    }
    out.push_back(uint8_t(v));
}

inline uint32_t ZigZag(int32_t v) { return (uint32_t(v) << 1) ^ uint32_t(v >> 31); }
inline int32_t UnZigZag(uint32_t v) { return int32_t(v >> 1) ^ -int32_t(v & 1); }

struct Reader {
    const uint8_t* p;
    size_t size;
    size_t at = 0;

    bool Byte(uint8_t& v)
    {
        if (at >= size) return false;
        v = p[at++];
        return true;
    }

    bool Varint(uint32_t& v)
    {
        v = 0;
        for (size_t i = 0; i < MaxVarintBytes; ++i) {
            uint8_t b;
            if (!Byte(b)) return false;
            if (i == MaxVarintBytes - 1 && (b & 0xF0)) return false;   // over 32 bits or a 6th byte
            v |= uint32_t(b & 0x7F) << (7 * i);
            if (!(b & 0x80)) return true;
        }
        return false;
    }

    bool Text(std::string& s)
    {
        uint32_t len;
        if (!Varint(len) || len > MaxTextBytes || len > size - at) return false;
        s.assign(reinterpret_cast<const char*>(p + at), len);
        at += len;
        return true;
    }
};

} // namespace detail

// Control characters (0x00-0x1F, 0x7F) become spaces, the ends are trimmed, and the
// text is cut to 64 bytes without splitting a UTF-8 character.
inline std::string CleanText(std::string_view in)
{
    std::string s(in);
    for (auto& c : s) {
        const unsigned char u = static_cast<unsigned char>(c);
        if (u < 0x20 || u == 0x7F) c = ' ';
    }
    while (!s.empty() && s.front() == ' ') s.erase(s.begin());
    while (!s.empty() && s.back() == ' ') s.pop_back();
    if (s.size() > MaxTextBytes) {
        size_t cut = MaxTextBytes;
        while (cut > 0 && (static_cast<unsigned char>(s[cut]) & 0xC0) == 0x80) --cut;
        s.resize(cut);
        while (!s.empty() && s.back() == ' ') s.pop_back();
    }
    return s;
}

// Writes every value of `snapshot` that is a registry field in one of `categories`, in id order.
inline std::string EncodeCode(const Snapshot& snapshot, uint8_t categories, std::string_view name, std::string_view author)
{
    categories &= AllCategories;
    std::vector<std::pair<uint16_t, int32_t>> items;
    for (const auto& [id, value] : snapshot.values) {
        const Field* f = FindById(id);
        if (f && (categories & static_cast<uint8_t>(f->category))) items.push_back({id, value});
    }
    const std::string n = CleanText(name), a = CleanText(author);
    std::vector<uint8_t> body;
    body.push_back(CodeVersion);
    detail::PutVarint(body, SchemaVersion);
    body.push_back(categories);
    detail::PutVarint(body, uint32_t(n.size()));
    body.insert(body.end(), n.begin(), n.end());
    detail::PutVarint(body, uint32_t(a.size()));
    body.insert(body.end(), a.begin(), a.end());
    detail::PutVarint(body, uint32_t(items.size()));
    for (const auto& [id, value] : items) {
        detail::PutVarint(body, id);
        detail::PutVarint(body, detail::ZigZag(value));
    }
    const uint32_t crc = detail::Crc32(body.data(), body.size());
    for (int i = 0; i < 4; ++i) body.push_back(uint8_t(crc >> (8 * i)));
    return "OXC." + detail::Base64UrlEncode(body);
}

inline DecodeResult DecodeCode(std::string_view text)
{
    DecodeResult r;
    auto fail = [&](DecodeError e) {
        DecodeResult f;
        f.error = e;
        return f;
    };
    if (text.size() > MaxInputBytes) return fail(DecodeError::TooLarge);
    auto isSpace = [](char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\v' || c == '\f'; };
    while (!text.empty() && isSpace(text.front())) text.remove_prefix(1);
    while (!text.empty() && isSpace(text.back())) text.remove_suffix(1);
    if (text.substr(0, 4) == "OXS.") return fail(DecodeError::SiteCode);
    if (text.substr(0, 4) != "OXC.") return fail(DecodeError::NotACode);
    text.remove_prefix(4);
    if (text.size() > (MaxBodyBytes + 4) * 4 / 3 + 4) return fail(DecodeError::TooLarge);

    std::vector<uint8_t> raw;
    if (!detail::Base64UrlDecode(text, raw)) return fail(DecodeError::BadBase64);
    if (raw.size() > MaxBodyBytes) return fail(DecodeError::TooLarge);
    if (raw.size() < 5) return fail(DecodeError::BadCrc);
    const size_t bodySize = raw.size() - 4;
    const uint32_t stored = uint32_t(raw[bodySize]) | (uint32_t(raw[bodySize + 1]) << 8) |
                            (uint32_t(raw[bodySize + 2]) << 16) | (uint32_t(raw[bodySize + 3]) << 24);
    if (detail::Crc32(raw.data(), bodySize) != stored) return fail(DecodeError::BadCrc);

    detail::Reader in{raw.data(), bodySize};
    uint8_t version;
    if (!in.Byte(version)) return fail(DecodeError::Malformed);
    if (version != CodeVersion) return fail(DecodeError::NewerCodeVersion);
    uint32_t schema;
    if (!in.Varint(schema)) return fail(DecodeError::Malformed);
    if (schema > uint32_t(SchemaVersion)) return fail(DecodeError::NewerSchema);
    // schema < SchemaVersion would run Migrate(N -> N+1) here; v1 has no older schema.
    if (schema < 1) return fail(DecodeError::Malformed);
    if (!in.Byte(r.categories)) return fail(DecodeError::Malformed);
    r.categories &= AllCategories;
    std::string name, author;
    if (!in.Text(name) || !in.Text(author)) return fail(DecodeError::Malformed);
    r.name = CleanText(name);
    r.author = CleanText(author);
    uint32_t count;
    if (!in.Varint(count)) return fail(DecodeError::Malformed);
    if (count > MaxItems) return fail(DecodeError::TooManyItems);

    std::vector<uint32_t> seen;
    seen.reserve(count);
    for (uint32_t i = 0; i < count; ++i) {
        uint32_t id, zz;
        if (!in.Varint(id) || !in.Varint(zz)) return fail(DecodeError::Malformed);
        for (uint32_t s : seen)
            if (s == id) return fail(DecodeError::DuplicateId);
        seen.push_back(id);
        const Field* f = id <= 0xFFFF ? FindById(uint16_t(id)) : nullptr;
        if (!f || !(r.categories & static_cast<uint8_t>(f->category))) {
            ++r.warnings;   // unknown id, or a field outside the categories the code declares
            continue;
        }
        int32_t value = detail::UnZigZag(zz);
        if (f->kind == Kind::Bool) {
            if (value != 0 && value != 1) {
                ++r.warnings;
                continue;
            }
        } else if (ClampValue(*f, value)) {
            ++r.warnings;
        }
        r.snapshot.Set(uint16_t(id), value);
    }
    if (in.at != in.size) return fail(DecodeError::Malformed);
    return r;
}

} // namespace ConfigShare
