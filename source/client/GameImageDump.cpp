// Game image dump (2026-11-04): the first step of MyClient finding the game's functions on its own, the way
// Flarial does, instead of through LeviLamina's symbol table (whose new versions start with few functions).
//
// 1. At load (before MyClient's hooks), when the folder %LOCALAPPDATA%\MyClient\dumps exists, the game's executable as it sits in memory (PE headers and every section
//    at its RVA, unreadable pages as zeros) is written once per game build to
//    %LOCALAPPDATA%\MyClient\dumps\Minecraft.Windows-<game>-<exe timestamp>.image. The file on disk cannot be read
//    by other programs (the Xbox app protects its folder), the copy in memory can. It is analysed offline
//    (byte-pattern signatures, RTTI and vtables); it is not sent anywhere by MyClient.
// 2. 60 s after enable, the game's code is compared with that file: what differs are hook patches (LeviLamina's
//    and MyClient's). Each patch's jumps are followed to where they end (a module + offset), which, read with
//    MyClientData.map from the build, names the game functions MyClient hooks. Written next to the image as
//    patches-<game>-<exe timestamp>.txt.
#include "GameImageDump.h"
#include "SignatureDump.h"

#include <windows.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

namespace {
std::atomic<bool> started{false};
std::filesystem::path imagePath, patchesPath;

bool ReadablePage(uintptr_t at)
{
    MEMORY_BASIC_INFORMATION info{};
    if (!VirtualQuery(reinterpret_cast<const void*>(at), &info, sizeof(info))) return false;
    if (info.State != MEM_COMMIT || (info.Protect & (PAGE_GUARD | PAGE_NOACCESS))) return false;
    const DWORD readable = PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE |
                           PAGE_EXECUTE_WRITECOPY;
    return (info.Protect & readable) != 0;
}

bool Readable(uintptr_t start, uintptr_t end)
{
    for (uintptr_t page = start & ~uintptr_t(0xFFF); page < end; page += 0x1000)
        if (!ReadablePage(page)) return false;
    return true;
}

const IMAGE_NT_HEADERS64* Headers(HMODULE module)
{
    if (!module) return nullptr;
    const auto base = reinterpret_cast<const uint8_t*>(module);
    const auto dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return nullptr;
    const auto nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE || nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC) return nullptr;
    return nt;
}

std::filesystem::path DumpFolder()
{
    wchar_t buffer[MAX_PATH]{};
    const DWORD length = GetEnvironmentVariableW(L"LOCALAPPDATA", buffer, MAX_PATH);
    if (!length || length >= MAX_PATH) return {};
    return std::filesystem::path(buffer) / L"MyClient" / L"dumps";
}

std::string Hex64(uint64_t value)
{
    char text[24];
    std::snprintf(text, sizeof(text), "0x%llX", static_cast<unsigned long long>(value));
    return text;
}

// "module+0xoffset" (or "no module @ 0x..." for memory no module holds).
std::string Where(uintptr_t address)
{
    HMODULE module = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            reinterpret_cast<LPCWSTR>(address), &module) || !module)
        return "no module @ " + Hex64(address);
    wchar_t path[MAX_PATH]{};
    const DWORD length = GetModuleFileNameW(module, path, MAX_PATH);
    std::string name = "?";
    if (length && length < MAX_PATH)
    {
        name.clear();
        const wchar_t* slash = std::wcsrchr(path, L'\\');
        for (const wchar_t* c = slash ? slash + 1 : path; *c; ++c) name += *c < 128 ? static_cast<char>(*c) : '?';
    }
    return name + "+" + Hex64(address - reinterpret_cast<uintptr_t>(module));
}

uint64_t ReadSlot(uintptr_t address)
{
    if (!Readable(address, address + 8)) return 0;
    uint64_t value = 0;
    std::memcpy(&value, reinterpret_cast<const void*>(address), 8);
    return value;
}

// Writes the image to a temporary file and renames it, so a half-written file is never taken for a dump.
bool WriteImage(HMODULE game, const IMAGE_NT_HEADERS64* nt, const std::filesystem::path& path, std::string& error)
{
    const auto base = reinterpret_cast<uintptr_t>(game);
    const size_t size = nt->OptionalHeader.SizeOfImage;
    if (!size || size > (1ull << 31)) { error = "unexpected image size"; return false; }
    auto temporary = path;
    temporary += L".part";
    std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
    if (!out) { error = "cannot create the file"; return false; }
    std::vector<char> zeros(0x1000, 0);
    size_t unreadable = 0;
    for (size_t offset = 0; offset < size; offset += 0x1000)
    {
        const size_t count = std::min<size_t>(0x1000, size - offset);
        if (ReadablePage(base + offset))
            out.write(reinterpret_cast<const char*>(base + offset), static_cast<std::streamsize>(count));
        else
        {
            out.write(zeros.data(), static_cast<std::streamsize>(count));
            ++unreadable;
        }
        if (!out) { error = "writing failed (disk full?)"; return false; }
    }
    out.close();
    std::error_code ec;
    std::filesystem::rename(temporary, path, ec);
    if (ec) { error = "rename failed"; std::filesystem::remove(temporary, ec); return false; }
    error = std::to_string(unreadable) + " unreadable pages written as zeros";
    return true;
}

struct Patch
{
    uintptr_t start = 0;
    size_t length = 0;
};

// Byte runs where the game's code in memory differs from the image file (runs closer than 16 bytes are joined).
std::vector<Patch> DiffCode(HMODULE game, const IMAGE_NT_HEADERS64* nt, const std::filesystem::path& image)
{
    std::vector<Patch> out;
    std::ifstream in(image, std::ios::binary);
    if (!in) return out;
    const auto base = reinterpret_cast<uintptr_t>(game);
    const IMAGE_SECTION_HEADER* section = IMAGE_FIRST_SECTION(nt);
    std::vector<uint8_t> chunk(0x10000);
    for (WORD i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++section)
    {
        if (!(section->Characteristics & IMAGE_SCN_MEM_EXECUTE)) continue;
        const size_t size = section->Misc.VirtualSize ? section->Misc.VirtualSize : section->SizeOfRawData;
        for (size_t done = 0; done < size; done += chunk.size())
        {
            const size_t count = std::min(chunk.size(), size - done);
            const uintptr_t at = base + section->VirtualAddress + done;
            in.seekg(static_cast<std::streamoff>(section->VirtualAddress + done));
            if (!in.read(reinterpret_cast<char*>(chunk.data()), static_cast<std::streamsize>(count))) return out;
            if (!Readable(at, at + count)) continue;
            const auto* memory = reinterpret_cast<const uint8_t*>(at);
            if (std::memcmp(memory, chunk.data(), count) == 0) continue;
            for (size_t k = 0; k < count; ++k)
            {
                if (memory[k] == chunk[k]) continue;
                const uintptr_t where = at + k;
                if (!out.empty() && where <= out.back().start + out.back().length + 16)
                    out.back().length = where + 1 - out.back().start;
                else
                    out.push_back({where, 1});
                if (out.size() > 20000) return out;
            }
        }
    }
    return out;
}

void WritePatches(HMODULE game, const IMAGE_NT_HEADERS64* nt, const std::string& gameVersion,
                  const std::function<void(const std::string&)>& log)
{
    const auto patches = DiffCode(game, nt, imagePath);
    std::ifstream original(imagePath, std::ios::binary);
    const auto base = reinterpret_cast<uintptr_t>(game);
    std::ofstream out(patchesPath, std::ios::trunc);
    if (!out) { log("Game image dump: cannot write " + patchesPath.string()); return; }
    HMODULE self = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCWSTR>(&WritePatches), &self);
    out << "MyClient game patches v1\ngame " << gameVersion << "\nimage base " << Hex64(base) << "\nMyClientData.dll "
        << Hex64(reinterpret_cast<uintptr_t>(self)) << " built " __DATE__ " " __TIME__ "\npatches " << patches.size() << "\n";
    out << "# rva length | memory bytes | original bytes | jumps (module+offset, up to 8)\n";
    for (const auto& p : patches)
    {
        const size_t shown = std::min<size_t>(p.length, 32);
        std::vector<uint8_t> was(shown);
        original.seekg(static_cast<std::streamoff>(p.start - base));
        original.read(reinterpret_cast<char*>(was.data()), static_cast<std::streamsize>(shown));
        out << Hex64(p.start - base) << ' ' << p.length << " | "
            << SignatureDump::Hex(reinterpret_cast<const uint8_t*>(p.start), shown) << " | " << SignatureDump::Hex(was.data(), shown)
            << " |";
        uintptr_t at = p.start;
        for (int hop = 0; hop < 8; ++hop)
        {
            if (!Readable(at, at + 24)) break;
            const auto next = SignatureDump::JumpTarget(reinterpret_cast<const uint8_t*>(at), 24, at, ReadSlot);
            if (!next || next == at) break;
            out << ' ' << Where(next);
            at = next;
        }
        out << '\n';
    }
    out.close();
    log("Game image dump: " + std::to_string(patches.size()) + " patched places in the game's code written to " + patchesPath.string());
}
} // namespace

namespace MyClientGame {
bool DiagnosticsEnabled()
{
    static const bool enabled = [] {
        wchar_t value[4]{};
        return GetEnvironmentVariableW(L"ORIGINX_DIAGNOSTICS", value, 4) == 1 && value[0] == L'1';
    }();
    return enabled;
}
void DumpGameImage(const std::string& gameVersion, const std::function<void(const std::string&)>& log)
{
    if (!DiagnosticsEnabled()) return;
    try
    {
        HMODULE game = GetModuleHandleW(nullptr);
        const auto nt = Headers(game);
        const auto folder = DumpFolder();
        if (!nt || folder.empty()) return;
        char stamp[16];
        std::snprintf(stamp, sizeof(stamp), "%08lX", static_cast<unsigned long>(nt->FileHeader.TimeDateStamp));
        std::error_code ec;
        // Only for whoever asked for it by making the folder (the image is over 100 MB): other players get nothing.
        if (!std::filesystem::is_directory(folder, ec)) return;
        const std::string tag = gameVersion + "-" + stamp;
        imagePath = folder / ("Minecraft.Windows-" + tag + ".image");
        patchesPath = folder / ("patches-" + tag + ".txt");
        if (std::filesystem::exists(imagePath, ec))
        {
            log("Game image dump: already written (" + imagePath.string() + ")");
            return;
        }
        const auto began = std::chrono::steady_clock::now();
        std::string note;
        const bool ok = WriteImage(game, nt, imagePath, note);
        const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - began).count();
        if (ok)
        {
            // The image is relocated to where Windows loaded it; pointers in it are read against this base.
            auto info = imagePath;
            info += L".txt";
            std::ofstream side(info, std::ios::trunc);
            side << "MyClient game image v1\ngame " << gameVersion << "\nbase " << Hex64(reinterpret_cast<uintptr_t>(game))
                 << "\nsize " << nt->OptionalHeader.SizeOfImage << "\ntimestamp " << stamp << "\n";
        }
        if (ok)
            log("Game image dump: " + std::to_string(nt->OptionalHeader.SizeOfImage / (1024 * 1024)) + " MB written to " +
                imagePath.string() + " in " + std::to_string(ms) + " ms (" + note + ", image base " +
                Hex64(reinterpret_cast<uintptr_t>(game)) + ")");
        else
        {
            log("Game image dump failed: " + note);
            imagePath.clear();
        }
    }
    catch (...) { imagePath.clear(); }
}

void StartPatchScan(std::string gameVersion, std::function<void(const std::string&)> log)
{
    if (!DiagnosticsEnabled()) return;
    if (started.exchange(true)) return;
    std::thread([gameVersion = std::move(gameVersion), log = std::move(log)] {
        try
        {
            SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_LOWEST);
            std::this_thread::sleep_for(std::chrono::seconds(60));
            std::error_code ec;
            if (imagePath.empty() || !std::filesystem::exists(imagePath, ec) || std::filesystem::exists(patchesPath, ec)) return;
            HMODULE game = GetModuleHandleW(nullptr);
            const auto nt = Headers(game);
            if (!nt) return;
            WritePatches(game, nt, gameVersion, log);
        }
        catch (...) {}
    }).detach();
}
} // namespace MyClientGame
