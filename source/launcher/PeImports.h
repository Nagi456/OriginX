#pragma once
#include "../Lang.h"
#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace MyClientLauncher {
// Read only the import directory, bounded by actual raw section/file ranges.
// PeEditor performs modifications; this parser never changes the executable.
inline std::set<std::string> ImportedDlls(const std::filesystem::path& path)
{
    std::ifstream input(path, std::ios::binary);
    const uint64_t length = std::filesystem::file_size(path);
    auto read = [&](uint64_t offset, size_t size) {
        if (offset > length || size > length - offset) throw std::runtime_error(Lang::T("PEファイルが途中で切れています。", "The PE file is truncated."));
        std::vector<unsigned char> bytes(size);
        input.seekg(static_cast<std::streamoff>(offset));
        if (!input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(size)))
            throw std::runtime_error(Lang::T("PEファイルを読み込めません。", "Cannot read the PE file."));
        return bytes;
    };
    auto u16 = [](const auto& b, size_t i) -> uint16_t { return uint16_t(b.at(i)) | uint16_t(b.at(i + 1)) << 8; };
    auto u32 = [](const auto& b, size_t i) -> uint32_t {
        return uint32_t(b.at(i)) | uint32_t(b.at(i + 1)) << 8 | uint32_t(b.at(i + 2)) << 16 | uint32_t(b.at(i + 3)) << 24;
    };
    const auto dos = read(0, 64);
    if (dos[0] != 'M' || dos[1] != 'Z') throw std::runtime_error(Lang::T("PE形式ではありません。", "Not a PE file."));
    const uint32_t peOffset = u32(dos, 60);
    const auto pe = read(peOffset, 24);
    if (pe[0] != 'P' || pe[1] != 'E' || pe[2] || pe[3] || u16(pe, 4) != 0x8664)
        throw std::runtime_error(Lang::T("Windows x64のPEファイルが必要です。", "A Windows x64 PE file is required."));
    const auto count = u16(pe, 6), optionalSize = u16(pe, 20);
    if (peOffset < 64 || !count || count > 96 || optionalSize < 128 || optionalSize > 4096)
        throw std::runtime_error(Lang::T("PEヘッダーが不正です。", "Invalid PE header."));
    const auto optional = read(uint64_t(peOffset) + 24, optionalSize);
    if (u16(optional, 0) != 0x20b || u32(optional, 108) < 2)
        throw std::runtime_error(Lang::T("PE64のデータディレクトリが不正です。", "Invalid PE64 data directory."));
    const auto sections = read(uint64_t(peOffset) + 24 + optionalSize, count * 40);
    const uint32_t headersSize = u32(optional, 60);
    auto locate = [&](uint64_t rva, uint64_t size) -> uint64_t {
        if (rva < headersSize && size <= uint64_t(headersSize) - rva && rva + size <= length) return rva;
        for (size_t i = 0; i < count; ++i)
        {
            const uint64_t va = u32(sections, i * 40 + 12), rawSize = u32(sections, i * 40 + 16);
            const uint64_t offset = u32(sections, i * 40 + 20);
            if (rva >= va && rva - va < rawSize && size <= rawSize - (rva - va) &&
                offset <= length && rva - va <= length - offset && size <= length - offset - (rva - va))
                return offset + rva - va;
        }
        throw std::runtime_error(Lang::T("PEのRVAがファイル範囲外です。", "A PE RVA is outside the file."));
    };
    const uint32_t importRva = u32(optional, 120), importSize = u32(optional, 124);
    if (!importRva && !importSize) return {};
    if (!importRva || importSize < 20 || importSize > 1024 * 1024)
        throw std::runtime_error(Lang::T("PEのインポートディレクトリが不正です。", "Invalid PE import directory."));
    const uint64_t importsOffset = locate(importRva, importSize);
    std::set<std::string> result;
    for (size_t i = 0; i + 20 <= importSize && i / 20 < 1024; i += 20)
    {
        const auto descriptor = read(importsOffset + i, 20);
        if (std::all_of(descriptor.begin(), descriptor.end(), [](unsigned char c) { return c == 0; })) return result;
        const uint32_t nameRva = u32(descriptor, 12);
        if (!nameRva) throw std::runtime_error(Lang::T("PEのDLL名が不正です。", "Invalid DLL name in PE."));
        std::string name;
        bool terminated = false;
        for (size_t n = 0; n < 256; ++n)
        {
            const unsigned char c = read(locate(uint64_t(nameRva) + n, 1), 1)[0];
            if (!c) { terminated = true; break; }
            if (c < 32 || c > 126) throw std::runtime_error(Lang::T("PEのDLL名が不正です。", "Invalid DLL name in PE."));
            name += c >= 'A' && c <= 'Z' ? static_cast<char>(c + 'a' - 'A') : static_cast<char>(c);
        }
        if (!terminated || name.empty()) throw std::runtime_error(Lang::T("PEのDLL名が終了していません。", "A DLL name in the PE is not terminated."));
        result.insert(name);
    }
    throw std::runtime_error(Lang::T("PEのインポート一覧が終了していません。", "The PE import list is not terminated."));
}
}
