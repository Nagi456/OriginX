// Signature dump (2026-11-03): MyClient is moving off LeviLamina's symbol table so it can follow new game
// versions on its own. Here, on a game version LeviLamina covers, every game function MyClient uses is
// looked up by name and the first 128 bytes at its address are written to
// %LOCALAPPDATA%\MyClient\signatures-<game>-<exe timestamp>.txt, with the shortest prefix that appears only
// once in the game's code. The file (tens of KB) is what byte-pattern signatures are made from; the game's
// executable itself is too big to send.
//
// The names come from three places: string literals in client/ (tools/list_game_symbols.py ->
// GameSymbolList.inc), names looked up at runtime (GameSymbols.h NoteLookup, for names built by macros) and
// the game functions this DLL imports from LeviLamina.dll (called directly through the SDK headers).
#include "SignatureDump.h"
#include "GameImageDump.h"
#include "GameSymbols.h"
#include "ll/api/memory/Hook.h"

#include <windows.h>
#include <tlhelp32.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <filesystem>
#include <fstream>
#include <functional>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

namespace {
const char* const kListed[] = {
#include "GameSymbolList.inc"
};

struct Section
{
    char name[9]{};
    uintptr_t start = 0, end = 0;
    bool code = false;
    bool readable = false; // every page committed and readable (checked once)
};

// True when [start, end) is committed memory that can be read.
bool Readable(uintptr_t start, uintptr_t end)
{
    for (uintptr_t at = start; at < end;)
    {
        MEMORY_BASIC_INFORMATION info{};
        if (!VirtualQuery(reinterpret_cast<const void*>(at), &info, sizeof(info))) return false;
        if (info.State != MEM_COMMIT || (info.Protect & (PAGE_GUARD | PAGE_NOACCESS))) return false;
        const DWORD readable = PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE |
                               PAGE_EXECUTE_WRITECOPY;
        if (!(info.Protect & readable)) return false;
        const uintptr_t next = reinterpret_cast<uintptr_t>(info.BaseAddress) + info.RegionSize;
        if (next <= at) return false;
        at = next;
    }
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

std::vector<Section> Sections(HMODULE module)
{
    std::vector<Section> out;
    const auto nt = Headers(module);
    if (!nt) return out;
    const auto base = reinterpret_cast<uintptr_t>(module);
    const IMAGE_SECTION_HEADER* section = IMAGE_FIRST_SECTION(nt);
    for (WORD i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++section)
    {
        Section s;
        std::memcpy(s.name, section->Name, 8);
        s.start = base + section->VirtualAddress;
        s.end = s.start + (section->Misc.VirtualSize ? section->Misc.VirtualSize : section->SizeOfRawData);
        s.code = (section->Characteristics & IMAGE_SCN_MEM_EXECUTE) != 0;
        s.readable = s.end > s.start && Readable(s.start, s.end);
        out.push_back(s);
    }
    return out;
}

// Names of the C++ functions a module imports from one DLL: this DLL's from LeviLamina.dll (called directly
// through the SDK headers) and, since 2026-11-05, LeviLamina.dll's own from bedrock_runtime.dll (PreLoader's
// table of game functions: the world tick, ClientInstance's constructor and the others LeviLamina hooks for its
// events and services, which MyClient has to hook itself to run without it).
std::vector<std::string> ImportedNames(HMODULE self, const char* from)
{
    std::vector<std::string> out;
    if (!self || !from) return out;
    const auto nt = Headers(self);
    if (!nt) return out;
    const auto base = reinterpret_cast<const uint8_t*>(self);
    const auto& dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (!dir.VirtualAddress || !dir.Size) return out;
    for (auto desc = reinterpret_cast<const IMAGE_IMPORT_DESCRIPTOR*>(base + dir.VirtualAddress); desc->Name; ++desc)
    {
        const char* dll = reinterpret_cast<const char*>(base + desc->Name);
        if (_stricmp(dll, from) != 0) continue;
        const DWORD table = desc->OriginalFirstThunk ? desc->OriginalFirstThunk : desc->FirstThunk;
        if (!table) continue;
        for (auto thunk = reinterpret_cast<const IMAGE_THUNK_DATA64*>(base + table); thunk->u1.AddressOfData; ++thunk)
        {
            if (IMAGE_SNAP_BY_ORDINAL64(thunk->u1.Ordinal)) continue;
            const auto byName = reinterpret_cast<const IMAGE_IMPORT_BY_NAME*>(base + thunk->u1.AddressOfData);
            const char* name = reinterpret_cast<const char*>(byName->Name);
            if (name[0] == '?') out.emplace_back(name);
        }
    }
    return out;
}

std::filesystem::path OutputFolder()
{
    wchar_t buffer[MAX_PATH]{};
    const DWORD length = GetEnvironmentVariableW(L"LOCALAPPDATA", buffer, MAX_PATH);
    if (!length || length >= MAX_PATH) return {};
    return std::filesystem::path(buffer) / L"MyClient";
}

struct Entry
{
    std::string name;
    uintptr_t at = 0;            // 0 = not found
    std::vector<uint8_t> bytes;  // empty when outside the game or unreadable
    const Section* section = nullptr;
    bool live = false;           // read after the hooks went in (looked up at runtime only), may be a jump
    bool atEnable = false;       // v5: found at the start of ll_mod_enable, before MyClient's hooks
    bool fromFile = false;       // bytes read from the executable on disk (never hooked)
    uintptr_t resolved = 0;      // what the lookup gave when it was outside the game (a thunk), 0 otherwise
    std::string where;           // module (and offset) of resolved, for names that never reach the game
    std::string outsideBytes;    // first bytes at resolved and what memory holds it, for names outside the game
    std::string chain;           // v4: each hop from resolved (memory, first bytes), for names outside the game
    uintptr_t preLoader = 0;     // v4: what PreLoader's own lookup gave (0 = nothing / not exported)
    int hops = 0;                // jumps followed from resolved to at
};

struct Snapshot
{
    std::mutex mutex;
    bool taken = false, skip = false;
    std::string version, stamp;
    std::filesystem::path path;
    HMODULE game = nullptr;
    uintptr_t base = 0, end = 0;
    std::vector<Section> sections;
    // The executable on disk, mapped read-only: original bytes even where a hook already patched memory.
    HANDLE file = INVALID_HANDLE_VALUE, mapping = nullptr;
    const uint8_t* fileView = nullptr;
    size_t fileSize = 0;
    std::vector<SignatureDump::FileSection> fileSections;
    std::vector<Entry> entries;
    std::set<std::string> names;
};

Snapshot& Shot()
{
    static Snapshot shot;
    return shot;
}

// "module+0xoffset" for an address outside the game (empty when no module holds it).
std::string ModuleOf(uintptr_t address)
{
    HMODULE module = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            reinterpret_cast<LPCWSTR>(address), &module) || !module)
    {
        char text[32];
        std::snprintf(text, sizeof(text), "0x%llX", static_cast<unsigned long long>(address));
        return std::string("no module @ ") + text;
    }
    wchar_t path[MAX_PATH]{};
    const DWORD length = GetModuleFileNameW(module, path, MAX_PATH);
    std::string name = "?";
    if (length && length < MAX_PATH)
    {
        name.clear();
        for (const wchar_t* c = std::wcsrchr(path, L'\\') ? std::wcsrchr(path, L'\\') + 1 : path; *c; ++c)
            name += *c < 128 ? static_cast<char>(*c) : '?';
    }
    char text[32];
    std::snprintf(text, sizeof(text), "+0x%llX", static_cast<unsigned long long>(address - reinterpret_cast<uintptr_t>(module)));
    return name + text;
}

uint64_t ReadSlot(uintptr_t address)
{
    if (!Readable(address, address + 8)) return 0;
    uint64_t value = 0;
    std::memcpy(&value, reinterpret_cast<const void*>(address), 8);
    return value;
}

// Follows thunks and hook jumps from address until it lands in the game's code (0 = it never does).
uintptr_t IntoGame(const Snapshot& shot, uintptr_t address, int& hops)
{
    hops = 0;
    for (uintptr_t at = address; at && hops < 8; ++hops)
    {
        if (at >= shot.base && at < shot.end && hops) return at;
        if (!Readable(at, at + 24)) return 0;
        const auto next = SignatureDump::JumpTarget(reinterpret_cast<const uint8_t*>(at), 24, at, ReadSlot);
        if (!next || next == at) return 0;
        at = next;
        if (at >= shot.base && at < shot.end) { ++hops; return at; }
    }
    return 0;
}

void OpenExecutable(Snapshot& shot)
{
    wchar_t path[MAX_PATH]{};
    const DWORD length = GetModuleFileNameW(shot.game, path, MAX_PATH);
    if (!length || length >= MAX_PATH) return;
    shot.file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                            FILE_ATTRIBUTE_NORMAL, nullptr);
    if (shot.file == INVALID_HANDLE_VALUE) return;
    LARGE_INTEGER size{};
    if (!GetFileSizeEx(shot.file, &size) || size.QuadPart <= 0) return;
    shot.mapping = CreateFileMappingW(shot.file, nullptr, PAGE_READONLY, 0, 0, nullptr);
    if (!shot.mapping) return;
    shot.fileView = static_cast<const uint8_t*>(MapViewOfFile(shot.mapping, FILE_MAP_READ, 0, 0, 0));
    if (!shot.fileView) return;
    shot.fileSize = static_cast<size_t>(size.QuadPart);
    {
        const auto dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(shot.fileView);
        if (shot.fileSize < sizeof(IMAGE_DOS_HEADER) || dos->e_magic != IMAGE_DOS_SIGNATURE) return;
        if (static_cast<size_t>(dos->e_lfanew) + sizeof(IMAGE_NT_HEADERS64) > shot.fileSize) return;
        const auto nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(shot.fileView + dos->e_lfanew);
        if (nt->Signature != IMAGE_NT_SIGNATURE) return;
        const IMAGE_SECTION_HEADER* section = IMAGE_FIRST_SECTION(nt);
        for (WORD i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++section)
        {
            SignatureDump::FileSection f;
            f.rva = section->VirtualAddress;
            f.virtualSize = section->Misc.VirtualSize;
            f.fileOffset = section->PointerToRawData;
            f.fileSize = section->SizeOfRawData;
            if (static_cast<uint64_t>(f.fileOffset) + f.fileSize > shot.fileSize) f.fileSize = 0;
            shot.fileSections.push_back(f);
        }
    }
}

// The function's first bytes from the executable on disk (false = not readable there).
bool CopyFromFile(const Snapshot& shot, Entry& entry)
{
    if (!shot.fileView || shot.fileSections.empty()) return false;
    const size_t offset = SignatureDump::FileOffset(shot.fileSections, entry.at - shot.base);
    if (offset == SIZE_MAX || offset >= shot.fileSize) return false;
    const size_t count = std::min<size_t>(SignatureDump::kMaxBytes, shot.fileSize - offset);
    // Bounds are checked above: a read-only view of the whole file never faults inside its size.
    entry.bytes.assign(shot.fileView + offset, shot.fileView + offset + count);
    entry.fromFile = true;
    return true;
}

// Fills at / section / bytes for an address (already known to be the game's or followed into it).
void Place(const Snapshot& shot, Entry& entry)
{
    entry.section = nullptr;
    entry.bytes.clear();
    entry.fromFile = false;
    for (const auto& s : shot.sections)
        if (entry.at >= s.start && entry.at < s.end) entry.section = &s;
    if (!entry.section) return;
    if (CopyFromFile(shot, entry)) return;
    if (entry.section->readable)
    {
        const size_t count = std::min<size_t>(SignatureDump::kMaxBytes, entry.section->end - entry.at);
        const auto bytes = reinterpret_cast<const uint8_t*>(entry.at);
        entry.bytes.assign(bytes, bytes + count);
    }
}


// What memory holds address: "Module.dll+0x..", or its kind and allocation base, its protection and first bytes.
std::string Describe(uintptr_t address, size_t bytes)
{
    std::string out = ModuleOf(address);
    MEMORY_BASIC_INFORMATION info{};
    if (VirtualQuery(reinterpret_cast<const void*>(address), &info, sizeof(info)))
    {
        char text[96];
        std::snprintf(text, sizeof(text), " [%s %s 0x%lX base 0x%llX]",
                      info.Type == MEM_IMAGE ? "image" : info.Type == MEM_MAPPED ? "mapped" : info.Type == MEM_PRIVATE ? "private" : "?",
                      info.State == MEM_COMMIT ? "commit" : "not-commit", static_cast<unsigned long>(info.Protect),
                      static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(info.AllocationBase)));
        out += text;
    }
    if (bytes && Readable(address, address + bytes)) out += " " + SignatureDump::Hex(reinterpret_cast<const uint8_t*>(address), bytes);
    else if (bytes) out += " unreadable";
    return out;
}

// PreLoader's own lookup by name (no LeviLamina stub in between), when PreLoader.dll exports it.
uintptr_t PreLoaderResolve(const std::string& name)
{
    using ResolveFn = void* (*)(const char*);
    static const HMODULE loader = GetModuleHandleW(L"PreLoader.dll");
    static const auto silent = loader ? reinterpret_cast<ResolveFn>(GetProcAddress(loader, "pl_resolve_symbol_silent")) : nullptr;
    static const auto loud = loader ? reinterpret_cast<ResolveFn>(GetProcAddress(loader, "pl_resolve_symbol")) : nullptr;
    const ResolveFn fn = silent ? silent : loud;
    if (!fn || name.empty()) return 0;
    try { return reinterpret_cast<uintptr_t>(fn(name.c_str())); } catch (...) { return 0; }
}

void Resolve(const Snapshot& shot, Entry& entry)
{
    void* address = nullptr;
    try { address = ll::memory::SymbolView{entry.name.c_str()}.resolve(true); } catch (...) { address = nullptr; }
    const auto found = reinterpret_cast<uintptr_t>(address);
    entry.at = 0;
    entry.resolved = 0;
    entry.where.clear();
    entry.hops = 0;
    entry.chain.clear();
    // 2026-11-04 (v3 dump): LeviLamina's answer is a "jmp [rip+0]" stub whose target is a hook's detour or a
    // page of private memory, never the game. PreLoader's own lookup is asked as well.
    entry.preLoader = PreLoaderResolve(entry.name);
    if (entry.preLoader >= shot.base && entry.preLoader < shot.end && !(found >= shot.base && found < shot.end))
    {
        entry.at = entry.preLoader;
        entry.resolved = found;
        entry.where = found ? "PreLoader (LeviLamina gave " + ModuleOf(found) + ")" : "PreLoader";
        Place(shot, entry);
        return;
    }
    if (!found) return;
    if (found >= shot.base && found < shot.end)
    {
        entry.at = found;
        Place(shot, entry);
        return;
    }
    // Outside the game: LeviLamina's own API, or a thunk / hook that jumps into the game.
    entry.resolved = found;
    entry.where = ModuleOf(found);
    // 2026-11-04 (v2 dump): 63 names resolved to "no module" addresses 0x20 apart and no known jump led on, so
    // what that memory is and its first bytes are written, to see what the stubs do.
    {
        MEMORY_BASIC_INFORMATION info{};
        if (VirtualQuery(reinterpret_cast<const void*>(found), &info, sizeof(info)))
        {
            char text[96];
            std::snprintf(text, sizeof(text), "%s %s 0x%lX base 0x%llX",
                          info.Type == MEM_IMAGE ? "image" : info.Type == MEM_MAPPED ? "mapped" : info.Type == MEM_PRIVATE ? "private" : "?",
                          info.State == MEM_COMMIT ? "commit" : "not-commit", static_cast<unsigned long>(info.Protect),
                          static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(info.AllocationBase)));
            entry.outsideBytes = text;
        }
        if (Readable(found, found + 48))
            entry.outsideBytes += "\t" + SignatureDump::Hex(reinterpret_cast<const uint8_t*>(found), 48);
        else
            entry.outsideBytes += "\tunreadable";
    }
    // Every hop: where it is and its first bytes, until the game, a non-jump or 4 hops.
    {
        uintptr_t at = found;
        for (int hop = 0; at && hop < 8; ++hop)
        {
            entry.chain += (hop ? " -> " : "") + Describe(at, 32);
            if (at >= shot.base && at < shot.end) break;
            if (!Readable(at, at + 24)) break;
            const uintptr_t next = SignatureDump::JumpTarget(reinterpret_cast<const uint8_t*>(at), 24, at, ReadSlot);
            if (!next || next == at) break;
            at = next;
        }
        if (entry.preLoader) entry.chain += " | PreLoader gave " + Describe(entry.preLoader, 0);
    }
    int hops = 0;
    if (const uintptr_t inside = IntoGame(shot, found, hops))
    {
        entry.at = inside;
        entry.hops = hops;
        Place(shot, entry);
    }
}

// Resolves name and copies its first bytes (caller holds the snapshot's lock).
void Take(Snapshot& shot, const std::string& name, bool live)
{
    if (!shot.names.insert(name).second) return;
    Entry entry;
    entry.name = name;
    entry.live = live;
    Resolve(shot, entry);
    shot.entries.push_back(std::move(entry));
}

void Finish(const std::function<void(const std::string&)>& log)
{
    auto& shot = Shot();
    std::lock_guard lock(shot.mutex);
    if (!shot.taken)
    {
        log("Signature dump: nothing was copied at load (the game's executable headers could not be read).");
        return;
    }
    if (shot.skip)
    {
        log("Signature dump: already written for this game build: " + shot.path.string());
        return;
    }
    {
        std::lock_guard names(MyClientGame::LookedUpMutex());
        for (const auto& name : MyClientGame::LookedUpNames()) Take(shot, name, true);
    }
    // 2026-11-04 (first dump: 141 names, 8 in the game): names missed at load are tried again now that every
    // hook went in, and names that pointed outside the game are followed through their jumps.
    size_t retried = 0;
    for (auto& entry : shot.entries)
    {
        if (entry.at || entry.resolved) continue; // v5: an answer from before the hooks is kept
        Resolve(shot, entry);
        if (entry.at || entry.resolved)
        {
            entry.live = true;
            ++retried;
        }
    }
    std::string body;
    size_t inCode = 0, unique = 0, elsewhere = 0, missing = 0, live = 0, fromFile = 0, followed = 0;
    for (const auto& entry : shot.entries)
    {
        if (!entry.at && !entry.resolved)
        {
            ++missing;
            body += entry.name + "\t-\tnot found" + (entry.preLoader ? "\tPreLoader gave " + Describe(entry.preLoader, 16) : std::string()) + "\n";
            continue;
        }
        if (!entry.at)
        {
            ++elsewhere; // LeviLamina's own API or another module, never reaching the game
            body += entry.name + "\t-\toutside\t" + entry.chain + "\n";
            continue;
        }
        if (entry.resolved) ++followed;
        const std::string via = entry.resolved ? entry.where + " (" + std::to_string(entry.hops) + " jumps)" : "-";
        char rva[24];
        std::snprintf(rva, sizeof(rva), "0x%llX", static_cast<unsigned long long>(entry.at - shot.base));
        if (entry.bytes.empty())
        {
            body += entry.name + "\t" + rva + "\t" + (entry.section ? entry.section->name : "?") + "\tunreadable\t" + via + "\n";
            continue;
        }
        std::string prefix = "data";
        if (entry.section->code)
        {
            ++inCode;
            // Search the unhooked code in the file when it is there; memory otherwise.
            const uint8_t* text = reinterpret_cast<const uint8_t*>(entry.section->start);
            size_t textSize = entry.section->end - entry.section->start;
            if (entry.fromFile)
            {
                const size_t offset = SignatureDump::FileOffset(shot.fileSections, entry.section->start - shot.base);
                for (const auto& f : shot.fileSections)
                    if (f.rva == entry.section->start - shot.base && offset != SIZE_MAX && offset + f.fileSize <= shot.fileSize)
                    {
                        text = shot.fileView + offset;
                        textSize = f.fileSize;
                    }
            }
            const size_t length = SignatureDump::UniquePrefix(text, textSize, entry.at - entry.section->start, entry.bytes.data(),
                                                              entry.bytes.size());
            if (length) ++unique;
            prefix = std::to_string(length);
        }
        if (entry.live) ++live;
        if (entry.fromFile) ++fromFile;
        body += entry.name + "\t" + rva + "\t" + entry.section->name + "\t" + prefix + (entry.atEnable ? "\tenable" : entry.live ? "\tlive" : "\tload") +
                (entry.fromFile ? "\tfile\t" : "\tmemory\t") + via + "\t" + SignatureDump::Hex(entry.bytes.data(), entry.bytes.size()) + "\n";
        std::this_thread::yield();
    }

    const auto nt = Headers(shot.game);
    std::string head = "# MyClient signature dump\n# game " + shot.version + ", executable timestamp 0x" + shot.stamp + ", image size " +
                       std::to_string(nt ? nt->OptionalHeader.SizeOfImage : 0) + "\n";
    for (const auto& s : shot.sections)
    {
        char line[160];
        std::snprintf(line, sizeof(line), "# section %-8s rva 0x%llX size 0x%llX%s%s\n", s.name,
                      static_cast<unsigned long long>(s.start - shot.base), static_cast<unsigned long long>(s.end - s.start),
                      s.code ? " code" : "", s.readable ? "" : " unreadable");
        head += line;
    }
    head += std::string("# executable on disk: ") + (shot.fileView ? "read" : "not readable (bytes from memory)") + "\n";
    {
        char line[96];
        std::snprintf(line, sizeof(line), "# game loaded at 0x%llX\n", static_cast<unsigned long long>(shot.base));
        head += line;
        static const HMODULE loader = GetModuleHandleW(L"PreLoader.dll");
        head += std::string("# PreLoader lookup: ") +
                (loader && (GetProcAddress(loader, "pl_resolve_symbol_silent") || GetProcAddress(loader, "pl_resolve_symbol")) ? "exported" : "not exported") + "\n";
    }
    // Loaded modules, to name the addresses the hops land on.
    if (HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, GetCurrentProcessId()); snap != INVALID_HANDLE_VALUE)
    {
        MODULEENTRY32W module{};
        module.dwSize = sizeof(module);
        int listed = 0;
        for (BOOL more = Module32FirstW(snap, &module); more && listed < 200; more = Module32NextW(snap, &module), ++listed)
        {
            std::string name;
            for (const wchar_t* c = module.szModule; *c; ++c) name += *c < 128 ? static_cast<char>(*c) : '?';
            char line[160];
            std::snprintf(line, sizeof(line), "# module 0x%llX size 0x%lX %s\n",
                          static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(module.modBaseAddr)),
                          static_cast<unsigned long>(module.modBaseSize), name.c_str());
            head += line;
        }
        CloseHandle(snap);
    }
    head += "# columns: name, rva, section, shortest unique prefix in bytes (0 = not unique within 128, data = not code),\n"
            "#   when found (load = MOD load, enable = start of enable before MyClient's hooks, live = 60 s later), bytes from (file = the executable on disk, memory = may be hooked),\n"
            "#   via (the module the lookup gave and the jumps followed into the game, - = direct), bytes\n"
            "# other lines: name - not found | name - outside <each hop: module+offset or [kind protection base] and first bytes, joined by ->>\n";
    std::error_code ec;
    std::filesystem::create_directories(shot.path.parent_path(), ec);
    std::ofstream out(shot.path, std::ios::binary);
    out << head << body;
    out.close();
    if (!out)
    {
        log("Signature dump: could not write " + shot.path.string());
        return;
    }
    log("Signature dump: " + std::to_string(shot.entries.size()) + " names, " + std::to_string(inCode) + " in the game's code (" +
        std::to_string(unique) + " unique within 128 bytes, " + std::to_string(live) + " copied after the hooks), " +
        std::to_string(missing) + " not found, " + std::to_string(elsewhere) + " outside the game; " + std::to_string(followed) +
        " reached through jumps, " + std::to_string(fromFile) + " read from the executable, " + std::to_string(retried) +
        " found on the second try; written to " + shot.path.string());
}
} // namespace

namespace MyClientGame {
void SnapshotSignatures(const std::string& gameVersion)
{
    if (!DiagnosticsEnabled()) return;
    try
    {
        auto& shot = Shot();
        std::lock_guard lock(shot.mutex);
        if (shot.taken) return;
        shot.game = GetModuleHandleW(nullptr);
        const auto nt = Headers(shot.game);
        if (!nt) return;
        shot.base = reinterpret_cast<uintptr_t>(shot.game);
        shot.end = shot.base + nt->OptionalHeader.SizeOfImage;
        char stamp[16];
        std::snprintf(stamp, sizeof(stamp), "%08lX", static_cast<unsigned long>(nt->FileHeader.TimeDateStamp));
        shot.version = gameVersion;
        shot.stamp = stamp;
        shot.taken = true;
        const auto folder = OutputFolder();
        if (folder.empty())
        {
            shot.skip = true;
            return;
        }
        // v2..v5 (2026-11-04): a new name each time, so the dump runs again where an earlier version wrote its file.
        shot.path = folder / ("signatures-" + gameVersion + "-" + stamp + "-v6.txt");
        std::error_code ec;
        if (std::filesystem::exists(shot.path, ec))
        {
            shot.skip = true;
            return;
        }
        shot.sections = Sections(shot.game);
        OpenExecutable(shot);
        for (const char* name : kListed) Take(shot, name, false);
        HMODULE self = nullptr;
        if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                               reinterpret_cast<LPCWSTR>(&OutputFolder), &self))
            for (const auto& name : ImportedNames(self, "LeviLamina.dll")) Take(shot, name, false);
        for (const auto& name : ImportedNames(GetModuleHandleW(L"LeviLamina.dll"), "bedrock_runtime.dll")) Take(shot, name, false);
    }
    catch (...) {}
}

void ResolveSignaturesBeforeHooks()
{
    if (!DiagnosticsEnabled()) return;
    try
    {
        auto& shot = Shot();
        std::lock_guard lock(shot.mutex);
        if (!shot.taken || shot.skip) return;
        for (auto& entry : shot.entries)
        {
            if (entry.at || entry.resolved) continue;
            Resolve(shot, entry);
            if (entry.at || entry.resolved) entry.atEnable = true;
        }
    }
    catch (...) {}
}

void StartSignatureDump(std::string gameVersion, std::function<void(const std::string&)> log)
{
    if (!DiagnosticsEnabled()) return;
    static std::atomic<bool> started{false};
    if (started.exchange(true) || !log) return;
    SnapshotSignatures(gameVersion); // no-op when ll_mod_load already took it
    try
    {
        std::thread([log = std::move(log)] {
            SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_LOWEST);
            // Wait until hooks and lazy lookups ran, so the runtime list is full.
            std::this_thread::sleep_for(std::chrono::seconds(60));
            try { Finish(log); }
            catch (const std::exception& e) { log(std::string("Signature dump failed: ") + e.what()); }
            catch (...) { log("Signature dump failed."); }
        }).detach();
    }
    catch (...) {}
}
} // namespace MyClientGame

// ---- Real addresses (2026-11-05, step 2 of leaving LeviLamina) ----
// On 26.51 LeviLamina's symbol lookup gives PreLoader's jump stub, not the game's function, and the stub cannot
// be followed. LeviLamina's own hook can: it patches the real function. So, once, with the folder
// %LOCALAPPDATA%\MyClient\dumps present: every name MyClient uses (and the game functions LeviLamina.dll itself
// imports) is hooked through LeviLamina for a moment with a pass-through thunk, the function start in the game
// whose first bytes changed is noted, and the hook is taken off again. Written to
// dumps\addresses-<game>-<exe timestamp>.txt (name, how, RVA); tools/make_rva_table.py puts them in the table.
namespace {
struct FunctionStarts
{
    std::vector<uint32_t> rva;
    std::vector<uint64_t> first;   // the first 8 bytes of each, before the hook
};

FunctionStarts ReadStarts(HMODULE game)
{
    FunctionStarts out;
    const auto nt = Headers(game);
    if (!nt) return out;
    const auto base = reinterpret_cast<const uint8_t*>(game);
    const auto& dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXCEPTION];
    if (!dir.VirtualAddress || !dir.Size) return out;
    const auto functions = reinterpret_cast<const RUNTIME_FUNCTION*>(base + dir.VirtualAddress);
    const size_t count = dir.Size / sizeof(RUNTIME_FUNCTION);
    out.rva.reserve(count);
    for (size_t i = 0; i < count; ++i)
    {
        const uint32_t rva = functions[i].BeginAddress;
        if (!rva || rva + 8 > nt->OptionalHeader.SizeOfImage) continue;
        if (!out.rva.empty() && out.rva.back() == rva) continue;
        out.rva.push_back(rva);
    }
    out.first.resize(out.rva.size());
    for (size_t i = 0; i < out.rva.size(); ++i) std::memcpy(&out.first[i], base + out.rva[i], 8);
    return out;
}

// Starts whose first 5 bytes are no longer what they were.
std::vector<size_t> Changed(const FunctionStarts& starts, const uint8_t* base)
{
    std::vector<size_t> out;
    for (size_t i = 0; i < starts.rva.size(); ++i)
    {
        uint64_t now;
        std::memcpy(&now, base + starts.rva[i], 8);
        if ((now ^ starts.first[i]) & 0xFFFFFFFFFFull) out.push_back(i);
    }
    return out;
}

bool IsCode(const void* address);

// What this DLL's import table holds for each function it imports from one DLL (the loader filled it in).
std::vector<std::pair<std::string, void*>> ImportedAddresses(HMODULE self, const char* from)
{
    std::vector<std::pair<std::string, void*>> out;
    const auto nt = self ? Headers(self) : nullptr;
    if (!nt || !from) return out;
    const auto base = reinterpret_cast<const uint8_t*>(self);
    const auto& dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (!dir.VirtualAddress || !dir.Size) return out;
    for (auto desc = reinterpret_cast<const IMAGE_IMPORT_DESCRIPTOR*>(base + dir.VirtualAddress); desc->Name; ++desc)
    {
        if (_stricmp(reinterpret_cast<const char*>(base + desc->Name), from) != 0) continue;
        if (!desc->OriginalFirstThunk || !desc->FirstThunk) continue;
        auto names = reinterpret_cast<const IMAGE_THUNK_DATA64*>(base + desc->OriginalFirstThunk);
        auto slots = reinterpret_cast<const IMAGE_THUNK_DATA64*>(base + desc->FirstThunk);
        for (; names->u1.AddressOfData; ++names, ++slots)
        {
            if (IMAGE_SNAP_BY_ORDINAL64(names->u1.Ordinal)) continue;
            const auto byName = reinterpret_cast<const IMAGE_IMPORT_BY_NAME*>(base + names->u1.AddressOfData);
            const char* name = reinterpret_cast<const char*>(byName->Name);
            if (name[0] == '?') out.emplace_back(name, reinterpret_cast<void*>(slots->u1.Function));
        }
    }
    return out;
}

// The function a hook was put on, from the hook's way back (the trampoline): its first instructions copied and
// a jump to the rest of the function. The real start is a few bytes before that jump's target and now begins
// with the hook's own jump (E9). 0 = not found.
uintptr_t StartFromTrampoline(const uint8_t* trampoline, uintptr_t imageStart, uintptr_t imageEnd)
{
    if (!trampoline || !IsCode(trampoline)) return 0;
    for (size_t i = 0; i + 14 <= 96; ++i)
    {
        uintptr_t target = 0;
        if (trampoline[i] == 0xFF && trampoline[i + 1] == 0x25 && !trampoline[i + 2] && !trampoline[i + 3] && !trampoline[i + 4] &&
            !trampoline[i + 5])
            std::memcpy(&target, trampoline + i + 6, 8);
        else if (trampoline[i] == 0xE9)
        {
            int32_t rel;
            std::memcpy(&rel, trampoline + i + 1, 4);
            target = reinterpret_cast<uintptr_t>(trampoline + i + 5) + rel;
        }
        if (target < imageStart + 32 || target >= imageEnd) continue;
        for (size_t back = 5; back <= 32; ++back)
        {
            const auto start = reinterpret_cast<const uint8_t*>(target - back);
            if (back == i && start[0] == 0xE9) return target - back;   // copied exactly the bytes before the jump
        }
    }
    return 0;
}

bool InModule(const void* address, HMODULE module)
{
    HMODULE owner = nullptr;
    return module && GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                        static_cast<LPCWSTR>(address), &owner) && owner == module;
}

bool IsCode(const void* address)
{
    MEMORY_BASIC_INFORMATION info{};
    if (!VirtualQuery(address, &info, sizeof(info)) || info.State != MEM_COMMIT) return false;
    return (info.Protect & (PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY)) != 0;
}
} // namespace

namespace MyClientGame {
void FindRealAddresses(const std::string& gameVersion, const std::function<void(const std::string&)>& log)
{
    if (!DiagnosticsEnabled()) return;
    try
    {
        const std::filesystem::path root = OutputFolder();
        if (root.empty()) return;
        const std::filesystem::path folder = root / L"dumps";
        std::error_code ec;
        if (!std::filesystem::is_directory(folder, ec)) return;   // only when asked for
        HMODULE game = GetModuleHandleW(nullptr);
        const auto nt = Headers(game);
        HMODULE levilamina = GetModuleHandleW(L"LeviLamina.dll");
        if (!nt || !levilamina) return;
        char stamp[16];
        std::snprintf(stamp, sizeof(stamp), "%08lX", static_cast<unsigned long>(nt->FileHeader.TimeDateStamp));
        const std::filesystem::path path = folder / ("addresses-" + gameVersion + "-" + stamp + "-v2.txt");
        if (std::filesystem::exists(path, ec))
        {
            if (log) log("Real addresses: already written (" + path.string() + ")");
            return;
        }
        const auto start = std::chrono::steady_clock::now();
        std::vector<std::string> names(std::begin(kListed), std::end(kListed));
        HMODULE self = nullptr;
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCWSTR>(&OutputFolder), &self);
        for (auto& name : ImportedNames(self, "LeviLamina.dll")) names.push_back(name);
        for (auto& name : ImportedNames(levilamina, "bedrock_runtime.dll")) names.push_back(name);
        // What LeviLamina hooks for its events and services (its import table is rewritten by the time MyClient runs).
        for (const char* name : {"?$_subTick@ClientLevel@@QEAAXXZ", "?$onLevelExit@ClientInstance@@QEAAXXZ",
                                 "?$ctor@ClientInstance@@QEAAPEAX$$QEAUClientInstanceArguments@@@Z",
                                 "?$onDestroy@WndProc_PC@MainGameCore@@QEAA?AV?$optional@_J@std@@UWndProcParams@WndProc@2@@Z"})
            names.emplace_back(name);
        const auto imported = ImportedAddresses(self, "LeviLamina.dll");   // the game functions called through the SDK headers
        std::sort(names.begin(), names.end());
        names.erase(std::unique(names.begin(), names.end()), names.end());

        const auto base = reinterpret_cast<const uint8_t*>(game);
        const uintptr_t imageStart = reinterpret_cast<uintptr_t>(game), imageEnd = imageStart + nt->OptionalHeader.SizeOfImage;
        FunctionStarts starts = ReadStarts(game);
        // One pass-through thunk for every hook: jmp [rip+0] to the trampoline LeviLamina writes into its slot.
        auto* thunk = static_cast<uint8_t*>(VirtualAlloc(nullptr, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
        if (!thunk || starts.rva.empty())
        {
            if (log) log("Real addresses: not written (no thunk memory or no function list)");
            return;
        }
        std::string body;
        size_t direct = 0, hooked = 0, own = 0, missing = 0, unclear = 0;
        for (const std::string& name : names)
        {
            void* address = ll::memory::SymbolView{name}.resolve(true);
            if (!address)
                for (const auto& [importName, slot] : imported)
                    if (importName == name) { address = slot; break; }
            const uintptr_t at = reinterpret_cast<uintptr_t>(address);
            char rva[24] = "-";
            const char* how = "none";
            if (!address) ++missing;
            else if (at >= imageStart && at < imageEnd)
            {
                std::snprintf(rva, sizeof(rva), "0x%llX", static_cast<unsigned long long>(at - imageStart));
                how = IsCode(address) ? "direct" : "data";
                ++direct;
            }
            else if (InModule(address, levilamina))
            {
                how = "levilamina";   // LeviLamina's own implementation, not a game function
                ++own;
            }
            else if (!IsCode(address) || name.rfind("??_7", 0) == 0) { how = "not code"; ++unclear; }
            else
            {
                std::memset(thunk, 0, 16);
                thunk[0] = 0xFF; thunk[1] = 0x25;   // jmp qword ptr [rip+0]; the 8 bytes after it are the slot
                auto slot = reinterpret_cast<void**>(thunk + 6);
                if (ll::memory::hook(address, thunk, slot, ll::memory::HookPriority::Normal, true) != 0)
                {
                    how = "hook failed";
                    ++unclear;
                }
                else
                {
                    const auto changed = Changed(starts, base);
                    const uintptr_t viaTrampoline = changed.empty() ? StartFromTrampoline(static_cast<const uint8_t*>(*slot), imageStart, imageEnd) : 0;
                    ll::memory::unhook(address, thunk, true);
                    if (changed.size() == 1)
                    {
                        std::snprintf(rva, sizeof(rva), "0x%X", starts.rva[changed[0]]);
                        how = "hooked";
                        ++hooked;
                    }
                    else if (viaTrampoline)
                    {
                        // not in the function list (.pdata: a leaf function), found from the hook's way back
                        std::snprintf(rva, sizeof(rva), "0x%llX", static_cast<unsigned long long>(viaTrampoline - imageStart));
                        how = "hooked";
                        ++hooked;
                    }
                    else
                    {
                        how = changed.empty() ? "no start changed" : "several starts changed";
                        ++unclear;
                    }
                    for (size_t i : changed) std::memcpy(&starts.first[i], base + starts.rva[i], 8);   // as now (after unhook)
                }
            }
            body += name + "\t" + how + "\t" + rva + "\n";
        }
        VirtualFree(thunk, 0, MEM_RELEASE);
        std::ofstream out(path, std::ios::binary);
        out << "# MyClient real addresses: game " << gameVersion << ", executable timestamp 0x" << stamp << "\n"
            << "# name <tab> how (direct = the lookup gave the game's code, hooked = found by LeviLamina's hook, data, "
               "levilamina = LeviLamina's own, none = not found) <tab> RVA\n"
            << body;
        const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
        if (log)
            log("Real addresses: " + std::to_string(names.size()) + " names, " + std::to_string(direct) + " direct, " +
                std::to_string(hooked) + " found through LeviLamina's hook, " + std::to_string(own) + " LeviLamina's own, " +
                std::to_string(missing) + " not found, " + std::to_string(unclear) + " unclear; " + std::to_string(ms) +
                " ms; written to " + path.string());
    }
    catch (...) {}
}
} // namespace MyClientGame
