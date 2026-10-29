#pragma once
// Replays panel (home screen): recordings with a title, favourite flag, date groups, sort and search.
// Titles and favourites live in replays.txt next to the recordings (UTF-8, one per line):
//   <file name> TAB <0|1 favourite> TAB <title, empty = default>
// Pure text / date handling, so it is unit-tested on the host.
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

namespace ReplayLibrary {

struct Meta { bool favorite = false; std::string title; };   // title UTF-8, empty = default
using MetaMap = std::map<std::string, Meta>;                  // by file name (UTF-8)

struct Entry {
    std::string file;          // replay-YYYYMMDD-HHMMSS.mcrec (UTF-8)
    uint64_t size = 0;
    uint32_t seconds = 0;      // 0 = unknown (not closed cleanly)
    int year = 0, month = 0, day = 0, hour = 0, minute = 0;   // from the name; 0 = not a standard name
    Meta meta;
};

enum class Sort { Date, Name, Length, Size };
constexpr int kSortCount = 4;
inline const char* SortName(Sort s)
{
    switch (s) {
    case Sort::Name: return "Name";
    case Sort::Length: return "Length";
    case Sort::Size: return "Size";
    default: return "Date";
    }
}

inline bool Digits(const std::string& s, size_t at, size_t n)
{
    if (at + n > s.size()) return false;
    for (size_t i = at; i < at + n; ++i) if (s[i] < '0' || s[i] > '9') return false;
    return true;
}

// Fills the date fields from "replay-YYYYMMDD-HHMMSS...". False for other names.
inline bool ParseName(Entry& e)
{
    const std::string& n = e.file;
    if (n.rfind("replay-", 0) != 0 || !Digits(n, 7, 8) || n.size() < 22 || n[15] != '-' || !Digits(n, 16, 6)) return false;
    e.year = std::stoi(n.substr(7, 4)); e.month = std::stoi(n.substr(11, 2)); e.day = std::stoi(n.substr(13, 2));
    e.hour = std::stoi(n.substr(16, 2)); e.minute = std::stoi(n.substr(18, 2));
    return true;
}

inline std::string Two(int v) { return std::string(1, char('0' + v / 10 % 10)) + char('0' + v % 10); }

inline std::string DefaultTitle(const Entry& e)
{
    if (!e.year) return e.file;
    return "Replay " + std::to_string(e.year) + "-" + Two(e.month) + "-" + Two(e.day) + " " + Two(e.hour) + ":" + Two(e.minute);
}
inline std::string Title(const Entry& e) { return e.meta.title.empty() ? DefaultTitle(e) : e.meta.title; }

// "7m 41s, 12.3 MB" (length unknown -> size only)
inline std::string Details(const Entry& e)
{
    char size[32];
    std::snprintf(size, sizeof size, "%.1f MB", e.size / 1048576.0);
    if (!e.seconds) return size;
    std::string len = e.seconds >= 60 ? std::to_string(e.seconds / 60) + "m " + std::to_string(e.seconds % 60) + "s"
                                      : std::to_string(e.seconds) + "s";
    return len + ", " + size;
}

// Days since 1970-01-01 (proleptic Gregorian), for "Today" / "Yesterday".
inline long DayNumber(int y, int m, int d)
{
    y -= m <= 2;
    const long era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = static_cast<unsigned>(y - era * 400);
    const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + static_cast<long>(doe) - 719468;
}

enum class Group { Favorites, Today, Yesterday, ThisWeek, Older };
constexpr int kGroupCount = 5;
inline const char* GroupName(Group g)
{
    switch (g) {
    case Group::Favorites: return "Favorites";
    case Group::Today: return "Today";
    case Group::Yesterday: return "Yesterday";
    case Group::ThisWeek: return "This week";
    default: return "Older";
    }
}
// Favourites are listed only under Favorites; the rest by how many days ago they were recorded.
inline Group GroupOf(const Entry& e, long today)
{
    if (e.meta.favorite) return Group::Favorites;
    if (!e.year) return Group::Older;
    const long ago = today - DayNumber(e.year, e.month, e.day);
    if (ago <= 0) return Group::Today;
    if (ago == 1) return Group::Yesterday;
    if (ago < 7) return Group::ThisWeek;
    return Group::Older;
}

inline std::string Lower(std::string s)
{
    for (auto& c : s) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return s;
}
inline bool Matches(const Entry& e, const std::string& search)
{
    return search.empty() || Lower(Title(e)).find(Lower(search)) != std::string::npos;
}

inline void SortEntries(std::vector<Entry>& list, Sort sort)
{
    std::stable_sort(list.begin(), list.end(), [sort](const Entry& a, const Entry& b) {
        switch (sort) {
        case Sort::Name: return Lower(Title(a)) < Lower(Title(b));
        case Sort::Length: return a.seconds > b.seconds;
        case Sort::Size: return a.size > b.size;
        default: return a.file > b.file;   // names start with the date: newest first
        }
    });
}

// One line of the list: a group header or a recording (index into the entries).
struct Row { bool header; Group group; int entry; };

// Group headers with their recordings (search applied, sorted within each group). A collapsed group
// keeps its header only. Empty groups are left out.
inline std::vector<Row> Layout(std::vector<Entry>& list, Sort sort, const std::string& search, long today,
                               const bool collapsed[kGroupCount])
{
    SortEntries(list, sort);
    std::vector<Row> rows;
    for (int g = 0; g < kGroupCount; ++g) {
        bool any = false;
        for (int i = 0; i < static_cast<int>(list.size()); ++i) {
            if (static_cast<int>(GroupOf(list[i], today)) != g || !Matches(list[i], search)) continue;
            if (!any) { rows.push_back({true, static_cast<Group>(g), -1}); any = true; }
            if (!collapsed[g]) rows.push_back({false, static_cast<Group>(g), i});
        }
    }
    return rows;
}

// ---- replays.txt ----
inline std::string CleanTitle(std::string t)
{
    for (auto& c : t) if (c == '\t' || c == '\r' || c == '\n') c = ' ';
    while (!t.empty() && t.front() == ' ') t.erase(t.begin());
    while (!t.empty() && t.back() == ' ') t.pop_back();
    return t;
}

inline MetaMap ParseMeta(const std::string& text)
{
    MetaMap map;
    size_t at = 0;
    while (at < text.size()) {
        size_t end = text.find('\n', at);
        if (end == std::string::npos) end = text.size();
        std::string line = text.substr(at, end - at);
        at = end + 1;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const size_t t1 = line.find('\t');
        if (t1 == std::string::npos || t1 == 0) continue;
        const size_t t2 = line.find('\t', t1 + 1);
        Meta m;
        m.favorite = line.compare(t1 + 1, 1, "1") == 0;
        if (t2 != std::string::npos) m.title = CleanTitle(line.substr(t2 + 1));
        map[line.substr(0, t1)] = m;
    }
    return map;
}

// Only recordings that still exist and have something to remember are written.
inline std::string SerializeMeta(const MetaMap& map)
{
    std::string out;
    for (const auto& [file, m] : map) {
        if (!m.favorite && m.title.empty()) continue;
        out += file + "\t" + (m.favorite ? "1" : "0") + "\t" + CleanTitle(m.title) + "\n";
    }
    return out;
}

}  // namespace ReplayLibrary
