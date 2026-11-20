#pragma once
// Before the DLL is injected into a Minecraft that is already running (attach), the launcher checks that game
// against the versions the package's DLL was made for (release-checklist 2026-11-20, required item 3).
// The package carries mods/MyClientData/supported.json, written by tools/package_update.py from tools/sigs.json:
//   {"schemaVersion":1,"gameVersion":"26.51.1",
//    "images":[{"fileVersion":"1.26.51.1","timestamp":"0x6AA482FD","imageSizes":["0x12C01000","0x12C0C000"]}]}
// The running game's image is read from its memory (the Store exe on disk may not be readable): the PE header's
// TimeDateStamp and SizeOfImage, and the file version in its version resource (VS_FIXEDFILEINFO). All three must
// match one row; anything else is "not supported" and nothing is injected. The DLL's own check stays as it is.
// Pure logic (tests/launcher_image_check.cpp): memory is read through a callback.
#include "../Lang.h"
#include "GameVersions.h"
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace MyClientLauncher {

struct SupportedImage { std::string fileVersion; uint32_t timestamp = 0; std::vector<uint32_t> imageSizes; };
struct SupportedImages { std::string gameVersion; std::vector<SupportedImage> images; };

struct GameImage {
    uint32_t timestamp = 0, sizeOfImage = 0;
    std::string fileVersion;   // "1.26.5101.0" as the version resource spells it; empty when there is none
};

// Reads `size` bytes at `rva` from the start of the game's image; false when they cannot be read.
using ImageReader = std::function<bool(uint32_t rva, void* out, uint32_t size)>;

inline uint32_t ParseHex32(const Json& value)
{
    if (!value.is_string()) throw std::runtime_error("not a hex string");
    const std::string text = value.get<std::string>();
    if (text.size() < 3 || text.size() > 10 || text.compare(0, 2, "0x") != 0 ||
        text.find_first_not_of("0123456789abcdefABCDEF", 2) != std::string::npos)
        throw std::runtime_error("not a hex string");
    return static_cast<uint32_t>(std::stoul(text.substr(2), nullptr, 16));
}

inline SupportedImages ParseSupportedImages(const Json& doc)
{
    try
    {
        if (!doc.is_object() || doc.at("schemaVersion") != 1) throw std::runtime_error("");
        SupportedImages table;
        table.gameVersion = doc.at("gameVersion").get<std::string>();
        for (const auto& row : doc.at("images"))
        {
            SupportedImage image;
            image.fileVersion = row.at("fileVersion").get<std::string>();
            image.timestamp = ParseHex32(row.at("timestamp"));
            for (const auto& size : row.at("imageSizes")) image.imageSizes.push_back(ParseHex32(size));
            if (VersionId(image.fileVersion).empty() || image.imageSizes.empty()) throw std::runtime_error("");
            table.images.push_back(std::move(image));
        }
        if (table.images.empty()) throw std::runtime_error("");
        return table;
    }
    catch (const std::exception&)
    {
        throw std::runtime_error(Lang::T("対応版の表（supported.json）の形式が不正です。", "The supported version table (supported.json) is invalid."));
    }
}

namespace ImageDetail {
template <class T> bool Read(const ImageReader& read, uint32_t rva, T& out) { return read(rva, &out, sizeof out); }

// The first entry of a resource directory, or the one with this id (0 = any). Returns the entry's raw offset field.
inline bool DirectoryEntry(const ImageReader& read, uint32_t base, uint32_t directory, uint32_t id, uint32_t& offset)
{
    uint16_t counts[2];   // named, id entries (at +12 of IMAGE_RESOURCE_DIRECTORY)
    if (!read(base + directory + 12, counts, sizeof counts)) return false;
    const uint32_t total = uint32_t(counts[0]) + counts[1];
    if (total > 4096) return false;
    for (uint32_t i = 0; i < total; ++i)
    {
        uint32_t entry[2];
        if (!read(base + directory + 16 + i * 8, entry, sizeof entry)) return false;
        if (entry[0] & 0x80000000u) continue;   // a named entry
        if (id == 0 || entry[0] == id) { offset = entry[1]; return true; }
    }
    return false;
}
} // namespace ImageDetail

// Header fields and file version of the image. nullopt when the image is not a readable x64 PE.
inline std::optional<GameImage> ReadGameImage(const ImageReader& read)
{
    using namespace ImageDetail;
    uint16_t mz = 0;
    uint32_t pe = 0;
    if (!Read(read, 0, mz) || mz != 0x5A4D || !Read(read, 0x3C, pe) || pe > 4096) return std::nullopt;
    uint32_t signature = 0;
    uint16_t machine = 0, optionalMagic = 0;
    GameImage image;
    if (!Read(read, pe, signature) || signature != 0x00004550 || !Read(read, pe + 4, machine) || machine != 0x8664 ||
        !Read(read, pe + 8, image.timestamp) || !Read(read, pe + 24, optionalMagic) || optionalMagic != 0x20B ||
        !Read(read, pe + 24 + 56, image.sizeOfImage))
        return std::nullopt;
    // PE32+ optional header: data directories start at +112; [2] is the resource directory.
    uint32_t resource[2] = {0, 0};
    uint32_t directories = 0;
    if (!Read(read, pe + 24 + 108, directories) || directories < 3 || !read(pe + 24 + 112 + 2 * 8, resource, sizeof resource))
        return image;
    const uint32_t base = resource[0];
    if (!base || !resource[1] || base >= image.sizeOfImage) return image;
    uint32_t type = 0, name = 0, language = 0;
    if (!DirectoryEntry(read, base, 0, 16 /* RT_VERSION */, type) || !(type & 0x80000000u)) return image;
    if (!DirectoryEntry(read, base, type & 0x7FFFFFFFu, 0, name) || !(name & 0x80000000u)) return image;
    if (!DirectoryEntry(read, base, name & 0x7FFFFFFFu, 0, language) || (language & 0x80000000u)) return image;
    uint32_t data[2];   // IMAGE_RESOURCE_DATA_ENTRY: rva, size
    if (!read(base + language, data, sizeof data) || data[1] < 92 || data[1] > 64 * 1024) return image;
    std::vector<unsigned char> block(data[1]);
    if (!read(data[0], block.data(), data[1])) return image;
    // VS_VERSIONINFO: "VS_VERSION_INFO" then VS_FIXEDFILEINFO, found by its signature on a 4-byte boundary.
    for (size_t at = 0; at + 52 <= block.size(); at += 4)
    {
        uint32_t magic;
        std::memcpy(&magic, block.data() + at, 4);
        if (magic != 0xFEEF04BDu) continue;
        uint32_t ms, ls;
        std::memcpy(&ms, block.data() + at + 8, 4);
        std::memcpy(&ls, block.data() + at + 12, 4);
        image.fileVersion = std::to_string(ms >> 16) + "." + std::to_string(ms & 0xFFFF) + "." + std::to_string(ls >> 16) + "." +
                            std::to_string(ls & 0xFFFF);
        break;
    }
    return image;
}

// The row the running game matches, or the reason it does not (shown with "this version is not supported").
inline bool ImageSupported(const GameImage& image, const SupportedImages& table, std::string& why)
{
    const std::string id = VersionId(image.fileVersion);
    if (id.empty()) { why = "no file version"; return false; }
    for (const auto& row : table.images)
    {
        if (VersionId(row.fileVersion) != id) continue;
        if (row.timestamp != image.timestamp) { why = "file version " + id + ", other build (TimeDateStamp)"; return false; }
        for (const uint32_t size : row.imageSizes)
            if (size == image.sizeOfImage) return true;
        why = "file version " + id + ", other build (SizeOfImage)";
        return false;
    }
    why = "file version " + id;
    return false;
}

inline std::string Hex32(uint32_t value)
{
    char text[11];
    std::snprintf(text, sizeof text, "0x%08X", value);
    return text;
}

// The message shown instead of injecting (UTF-8).
inline std::string UnsupportedGameMessage(const GameImage* image, const std::string& gameVersion, const std::string& why)
{
    std::string detail = why;
    if (image) detail += " / TimeDateStamp " + Hex32(image->timestamp) + " / SizeOfImage " + Hex32(image->sizeOfImage);
    return Lang::T("この版は未対応です。起動中の Minecraft は、OriginX の DLL（", "This version is not supported. The running Minecraft is not the one the OriginX DLL (") +
           gameVersion + Lang::T(" 用）が対応している版ではないため、読み込みませんでした（", ") supports, so it was not loaded (") + detail + Lang::T("）", ")");
}

} // namespace MyClientLauncher
