#pragma once
// Texture pack switching, HUD side. Reads the installed resource packs and the game's own list of
// active "global resource packs" (global_resource_packs.json) and rewrites that list. The game is then
// asked (by the DLL) to re-read it. Everything here is plain file / text handling, so it is unit-tested.
#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <regex>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>

namespace TexturePacks {

namespace fs = std::filesystem;

// One subpack of a pack (manifest "subpacks"): the folder the game loads and the name it shows.
struct Subpack {
    std::string folder;    // "folder_name" (what global_resource_packs.json stores as "subpack")
    std::string name;      // UTF-8, may be empty (the folder is shown then)
};

struct Pack {
    std::string name;      // shown in the list (UTF-8)
    std::string uuid;      // lower-case, 36 characters
    int version[3] = {1, 0, 0};
    fs::path folder;
    std::vector<Subpack> subpacks;   // empty = the pack has none
};

// One entry of the active list (global_resource_packs.json), first = highest priority. `json` is the entry's
// object text as found in the file (kept verbatim so fields like "subpack" survive a rewrite).
struct Entry {
    std::string uuid;      // lower-case
    std::string json;
};

struct State {
    bool found = false;                 // a Minecraft data folder was located
    fs::path root;                      // ...\games\com.mojang that holds the active-pack list
    fs::path globalFile;                // root\minecraftpe\global_resource_packs.json
    std::vector<Pack> packs;
    std::vector<std::string> active;    // uuids currently active (lower-case), highest priority first
    std::vector<Entry> stack;           // the same list with each entry's text
    std::string note;                   // short human-readable status for the page
};

inline std::string Lower(std::string text)
{
    for (auto& c : text) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return text;
}

inline bool ValidUuid(const std::string& s)
{
    if (s.size() != 36) return false;
    for (size_t i = 0; i < s.size(); ++i) {
        const char c = s[i];
        if (i == 8 || i == 13 || i == 18 || i == 23) { if (c != '-') return false; }
        else if (!std::isxdigit(static_cast<unsigned char>(c))) return false;
    }
    return true;
}

// Decodes \uXXXX (and the common one-character escapes) of a JSON string into UTF-8.
inline std::string DecodeJsonString(const std::string& in)
{
    std::string out;
    for (size_t i = 0; i < in.size(); ++i) {
        if (in[i] != '\\' || i + 1 >= in.size()) { out += in[i]; continue; }
        const char e = in[++i];
        switch (e) {
        case 'n': out += ' '; break;
        case 't': out += ' '; break;
        case '"': out += '"'; break;
        case '\\': out += '\\'; break;
        case '/': out += '/'; break;
        case 'u':
            if (i + 4 < in.size()) {
                unsigned code = 0;
                bool ok = true;
                for (int k = 1; k <= 4; ++k) {
                    const char h = in[i + k];
                    code <<= 4;
                    if (h >= '0' && h <= '9') code |= static_cast<unsigned>(h - '0');
                    else if (h >= 'a' && h <= 'f') code |= static_cast<unsigned>(h - 'a' + 10);
                    else if (h >= 'A' && h <= 'F') code |= static_cast<unsigned>(h - 'A' + 10);
                    else { ok = false; break; }
                }
                if (ok) {
                    i += 4;
                    if (code < 0x80) out += static_cast<char>(code);
                    else if (code < 0x800) { out += static_cast<char>(0xC0 | (code >> 6)); out += static_cast<char>(0x80 | (code & 0x3F)); }
                    else { out += static_cast<char>(0xE0 | (code >> 12)); out += static_cast<char>(0x80 | ((code >> 6) & 0x3F)); out += static_cast<char>(0x80 | (code & 0x3F)); }
                    break;
                }
            }
            out += 'u';
            break;
        default: out += e; break;
        }
    }
    return out;
}

// The objects of the top-level-ish array that follows `key` (e.g. "subpacks"), as raw text.
inline std::vector<std::string> JsonArrayObjects(const std::string& text, const std::string& key)
{
    std::vector<std::string> objects;
    size_t at = text.find("\"" + key + "\"");
    if (at == std::string::npos) return objects;
    at = text.find('[', at);
    if (at == std::string::npos) return objects;
    int depth = 0;
    bool inString = false, escaped = false;
    size_t start = 0;
    for (size_t i = at + 1; i < text.size(); ++i) {
        const char c = text[i];
        if (inString) {
            if (escaped) escaped = false;
            else if (c == '\\') escaped = true;
            else if (c == '"') inString = false;
            continue;
        }
        if (c == '"') inString = true;
        else if (c == '{') { if (depth++ == 0) start = i; }
        else if (c == '}' && depth > 0 && --depth == 0) objects.push_back(text.substr(start, i + 1 - start));
        else if (c == ']' && depth == 0) break;
    }
    return objects;
}

inline std::vector<Subpack> ParseSubpacks(const std::string& manifest)
{
    std::vector<Subpack> out;
    static const std::regex folderRe("\"folder_name\"\\s*:\\s*\"((?:[^\"\\\\]|\\\\.)*)\"");
    static const std::regex nameRe("\"name\"\\s*:\\s*\"((?:[^\"\\\\]|\\\\.)*)\"");
    for (const auto& object : JsonArrayObjects(manifest, "subpacks")) {
        std::smatch m;
        if (!std::regex_search(object, m, folderRe)) continue;
        Subpack sp;
        sp.folder = DecodeJsonString(m[1].str());
        if (std::regex_search(object, m, nameRe)) sp.name = DecodeJsonString(m[1].str());
        if (!sp.folder.empty() && out.size() < 32) out.push_back(sp);
    }
    return out;
}

// manifest.json of a resource pack. The "header" fields and the subpack list are read; a manifest without a
// "resources" module (behaviour packs, broken files) is refused.
inline bool ParseManifest(const std::string& text, Pack& out)
{
    const size_t header = text.find("\"header\"");
    if (header == std::string::npos) return false;
    static const std::regex typeRe("\"type\"\\s*:\\s*\"resources\"");
    if (!std::regex_search(text, typeRe)) return false;

    const std::string tail = text.substr(header);
    static const std::regex uuidRe("\"uuid\"\\s*:\\s*\"([^\"]+)\"");
    static const std::regex nameRe("\"name\"\\s*:\\s*\"((?:[^\"\\\\]|\\\\.)*)\"");
    static const std::regex versionRe("\"version\"\\s*:\\s*\\[\\s*(\\d+)\\s*,\\s*(\\d+)\\s*,\\s*(\\d+)\\s*\\]");
    std::smatch match;
    if (!std::regex_search(tail, match, uuidRe)) return false;
    const std::string uuid = Lower(match[1].str());
    if (!ValidUuid(uuid)) return false;
    out.uuid = uuid;
    out.name.clear();
    if (std::regex_search(tail, match, nameRe)) out.name = DecodeJsonString(match[1].str());
    out.version[0] = 1; out.version[1] = 0; out.version[2] = 0;
    if (std::regex_search(tail, match, versionRe)) {
        for (int i = 0; i < 3; ++i) {
            try { out.version[i] = std::min(std::stoi(match[1 + i].str()), 1000000); } catch (...) { out.version[i] = 0; }
        }
    }
    out.subpacks = ParseSubpacks(text);
    return true;
}

// global_resource_packs.json: the uuids of the active packs, in file order.
inline std::vector<std::string> ParseActive(const std::string& text)
{
    std::vector<std::string> uuids;
    static const std::regex idRe("\"pack_id\"\\s*:\\s*\"([^\"]+)\"");
    for (auto it = std::sregex_iterator(text.begin(), text.end(), idRe); it != std::sregex_iterator(); ++it) {
        const std::string uuid = Lower((*it)[1].str());
        if (ValidUuid(uuid)) uuids.push_back(uuid);
    }
    return uuids;
}

// The active list's entries in file order. Each top-level {...} object with a valid "pack_id" is one entry.
inline std::vector<Entry> ParseEntries(const std::string& text)
{
    std::vector<Entry> entries;
    static const std::regex idRe("\"pack_id\"\\s*:\\s*\"([^\"]+)\"");
    int depth = 0;
    bool inString = false, escaped = false;
    size_t start = 0;
    for (size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        if (inString) {
            if (escaped) escaped = false;
            else if (c == '\\') escaped = true;
            else if (c == '"') inString = false;
            continue;
        }
        if (c == '"') inString = true;
        else if (c == '{') { if (depth++ == 0) start = i; }
        else if (c == '}' && depth > 0 && --depth == 0) {
            const std::string object = text.substr(start, i + 1 - start);
            std::smatch m;
            if (std::regex_search(object, m, idRe)) {
                const std::string uuid = Lower(m[1].str());
                if (ValidUuid(uuid)) entries.push_back({uuid, object});
            }
        }
    }
    return entries;
}

inline Entry EntryFor(const Pack& pack)
{
    std::ostringstream out;
    out << "{\n    \"pack_id\": \"" << pack.uuid << "\",\n    \"version\": ["
        << pack.version[0] << ", " << pack.version[1] << ", " << pack.version[2] << "]\n  }";
    return {pack.uuid, out.str()};
}

// The "subpack" an active-list entry selects ("" = none / the pack's default).
inline std::string SubpackOf(const Entry& e)
{
    static const std::regex re("\"subpack\"\\s*:\\s*\"((?:[^\"\\\\]|\\\\.)*)\"");
    std::smatch m;
    return std::regex_search(e.json, m, re) ? DecodeJsonString(m[1].str()) : std::string();
}

// The same entry selecting `folder` (other fields kept). Folder names are plain file names.
inline Entry WithSubpack(Entry e, const std::string& folder)
{
    std::string safe;
    for (char c : folder) if (c != '"' && c != '\\' && static_cast<unsigned char>(c) >= 32) safe += c;
    static const std::regex re("\"subpack\"\\s*:\\s*\"(?:[^\"\\\\]|\\\\.)*\"");
    if (std::regex_search(e.json, re)) {
        std::string replacement = "\"subpack\": \"";
        for (char c : safe) replacement += c == '$' ? std::string("$$") : std::string(1, c);   // '$' is special in a replacement
        e.json = std::regex_replace(e.json, re, replacement + "\"", std::regex_constants::format_first_only);
    }
    else if (const size_t close = e.json.rfind('}'); close != std::string::npos) {
        size_t end = close;
        while (end > 0 && (e.json[end - 1] == ' ' || e.json[end - 1] == '\n' || e.json[end - 1] == '\r' || e.json[end - 1] == '\t')) --end;
        e.json.insert(end, ",\n    \"subpack\": \"" + safe + "\"");
    }
    return e;
}

// The active list text: `top` (the generated Lite pack) first = highest priority, then `stack` in order.
inline std::string BuildStack(const std::vector<Entry>& stack, const Pack* top = nullptr)
{
    std::vector<Entry> list;
    if (top) list.push_back(EntryFor(*top));
    for (const auto& e : stack)
        if (!top || e.uuid != top->uuid) list.push_back(e);
    if (list.empty()) return "[]\n";
    std::string out = "[\n";
    for (size_t i = 0; i < list.size(); ++i) out += (i ? ",\n  " : "  ") + list[i].json;
    return out + "\n]\n";
}

// The chosen pack only, or an empty list for "no pack" (the default textures).
inline std::string BuildActive(const Pack* chosen, const Pack* top = nullptr)
{
    return BuildStack(chosen ? std::vector<Entry>{EntryFor(*chosen)} : std::vector<Entry>{}, top);
}

inline std::string ReadFile(const fs::path& path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in) return {};
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

// Minecraft data folders (...\games\com.mojang) that may exist on this PC.
inline std::vector<fs::path> CandidateRoots(const fs::path& appData, const fs::path& localAppData)
{
    std::vector<fs::path> roots;
    std::error_code ec;
    if (!appData.empty()) {
        const fs::path users = appData / "Minecraft Bedrock" / "Users";
        for (fs::directory_iterator it(users, ec), end; !ec && it != end; it.increment(ec)) {
            std::error_code dirEc;
            if (it->is_directory(dirEc)) roots.push_back(it->path() / "games" / "com.mojang");
        }
    }
    if (!localAppData.empty())
        roots.push_back(localAppData / "Packages" / "Microsoft.MinecraftUWP_8wekyb3d8bbwe" / "LocalState" / "games" / "com.mojang");
    return roots;
}

inline fs::path EnvPath(const char* narrow, const wchar_t* wide)
{
#ifdef _WIN32
    (void)narrow;
    const wchar_t* value = _wgetenv(wide);
    return value ? fs::path(value) : fs::path();
#else
    (void)wide;
    const char* value = std::getenv(narrow);
    return value ? fs::path(value) : fs::path();
#endif
}

inline std::vector<fs::path> CandidateRootsFromEnvironment()
{
    return CandidateRoots(EnvPath("APPDATA", L"APPDATA"), EnvPath("LOCALAPPDATA", L"LOCALAPPDATA"));
}

inline fs::file_time_type WriteTime(const fs::path& path)
{
    std::error_code ec;
    const auto t = fs::last_write_time(path, ec);
    return ec ? fs::file_time_type::min() : t;
}

// Finds the packs and the active list. The data folder the game uses is the one whose options.txt it wrote most
// recently (only the game writes that file). The newest global_resource_packs.json decided before, but MyClient
// writes that file itself: once it had written into another user's (or Shared's) folder, that folder stayed the
// newest and every later switch went to a list the game never reads (2026-10-28: nothing changed, OriginX Lite never
// listed). The newest list is used only when no folder has an options.txt. Packs are collected from every folder.
inline State Scan(const std::vector<fs::path>& roots)
{
    State state;
    std::error_code ec;
    fs::file_time_type bestTime = fs::file_time_type::min();
    bool haveBest = false, byOptions = false;

    for (const auto& root : roots) {
        if (!fs::is_directory(root, ec)) continue;
        if (state.root.empty()) { state.root = root; state.found = true; }
        const fs::path options = root / "minecraftpe" / "options.txt";
        const fs::path file = root / "minecraftpe" / "global_resource_packs.json";
        const bool hasOptions = fs::is_regular_file(options, ec);
        if (!hasOptions && (byOptions || !fs::is_regular_file(file, ec))) continue;
        const auto t = WriteTime(hasOptions ? options : file);
        if (hasOptions && !byOptions) { byOptions = true; haveBest = false; }
        if (!haveBest || t > bestTime) { bestTime = t; state.root = root; haveBest = true; }
    }
    if (!state.found) { state.note = "Minecraft's data folder was not found."; return state; }
    state.globalFile = state.root / "minecraftpe" / "global_resource_packs.json";
    state.stack = ParseEntries(ReadFile(state.globalFile));
    for (const auto& e : state.stack) state.active.push_back(e.uuid);

    for (const auto& root : roots) {
        for (const char* sub : {"resource_packs", "development_resource_packs"}) {
            const fs::path packs = root / sub;
            for (fs::directory_iterator it(packs, ec), end; !ec && it != end; it.increment(ec)) {
                std::error_code dirEc;
                if (!it->is_directory(dirEc)) continue;
                Pack pack;
                if (!ParseManifest(ReadFile(it->path() / "manifest.json"), pack)) continue;
                pack.folder = it->path();
                if (pack.name.empty() || pack.name.rfind("pack.", 0) == 0)       // a translation key, not a name
                    pack.name = it->path().filename().string();
                const bool duplicate = std::any_of(state.packs.begin(), state.packs.end(), [&](const Pack& other) {
                    return other.uuid == pack.uuid && other.version[0] == pack.version[0] &&
                           other.version[1] == pack.version[1] && other.version[2] == pack.version[2];
                });
                if (!duplicate) state.packs.push_back(pack);
            }
            ec.clear();
        }
    }
    std::sort(state.packs.begin(), state.packs.end(), [](const Pack& a, const Pack& b) { return Lower(a.name) < Lower(b.name); });
    state.note = state.packs.empty() ? "No resource packs were found in the data folder." : "";
    return state;
}

// Data folders a generated pack (OriginX Lite) is written to, the active list's folder first. The GDK game keeps
// the active list per user (Users\<id>\games\com.mojang) but finds resource packs in Users\Shared\games\com.mojang,
// so a pack written only next to the list never showed up in the game. Also every folder the player's own packs
// were found in, which the game evidently reads.
inline std::vector<fs::path> PackRoots(const State& state)
{
    std::vector<fs::path> roots;
    auto add = [&](const fs::path& root) {
        std::error_code ec;
        if (!root.empty() && fs::is_directory(root, ec) && std::find(roots.begin(), roots.end(), root) == roots.end())
            roots.push_back(root);
    };
    if (!state.found) return roots;
    add(state.root);
    const fs::path users = state.root.parent_path().parent_path().parent_path();   // ...\Users\<id>\games\com.mojang
    if (users.filename() == "Users") add(users / "Shared" / "games" / "com.mojang");
    for (const auto& pack : state.packs)
        if (pack.folder.parent_path().filename() == "resource_packs") add(pack.folder.parent_path().parent_path());
    return roots;
}

inline bool IsActive(const State& state, const Pack& pack)
{
    return std::find(state.active.begin(), state.active.end(), pack.uuid) != state.active.end();
}

// Writes the new active list (backing the original up once): `top` first, then `stack` in order.
inline bool WriteStack(State& state, const std::vector<Entry>& stack, std::string& error, const Pack* top = nullptr)
{
    if (!state.found || state.globalFile.empty()) { error = "No data folder."; return false; }
    std::error_code ec;
    fs::create_directories(state.globalFile.parent_path(), ec);
    const fs::path backup = fs::path(state.globalFile.string() + ".myclient-backup");
    if (fs::is_regular_file(state.globalFile, ec) && !fs::exists(backup, ec)) {
        const std::string original = ReadFile(state.globalFile);
        std::ofstream copy(backup, std::ios::binary | std::ios::trunc);
        copy << original;
    }
    const std::string text = BuildStack(stack, top);
    std::ofstream out(state.globalFile, std::ios::binary | std::ios::trunc);
    if (!out) { error = "Could not write global_resource_packs.json."; return false; }
    out << text;
    out.close();
    if (!out) { error = "Could not write global_resource_packs.json."; return false; }
    state.stack = ParseEntries(text);
    state.active.clear();
    for (const auto& e : state.stack) state.active.push_back(e.uuid);
    return true;
}

// Only `chosen` active (nullptr = default textures).
inline bool WriteActive(State& state, const Pack* chosen, std::string& error, const Pack* top = nullptr)
{
    return WriteStack(state, chosen ? std::vector<Entry>{EntryFor(*chosen)} : std::vector<Entry>{}, error, top);
}

}  // namespace TexturePacks
