// Config sharing, stage A tasks 1-4 (docs/config-sharing-design.md §9).
// cd source && g++ -std=c++17 -Wall -Wextra -Werror -I. tests/config_share.cpp -o /tmp/config_share && /tmp/config_share
#include "ConfigShare/Code.h"
#include "ConfigShare/HudFraction.h"
#include "ConfigShare/Registry.h"
#include "ConfigShare/Snapshot.h"
#include "ConfigShare/Worker.h"

#include <algorithm>
#include <cassert>
#include <cstdlib>
#include <cstdio>
#include <fstream>
#include <set>
#include <sstream>
#include <string>
#include <chrono>
#include <filesystem>
#include <thread>

using namespace ConfigShare;

static int CountCategory(Category c)
{
    int n = 0;
    for (const auto& f : kFields) n += f.category == c;
    return n;
}

static Snapshot Defaults()
{
    Snapshot s;
    for (const auto& f : kFields) s.Set(f.id, f.min);
    return s;
}

static Snapshot Maxed()
{
    Snapshot s;
    for (const auto& f : kFields) s.Set(f.id, f.max);
    return s;
}

// Builds a code with raw items (bypassing the registry) for the rejection tests.
static std::string RawCode(const std::vector<std::pair<uint32_t, int32_t>>& items, uint8_t version = 1, uint32_t schema = 1,
                           uint8_t categories = AllCategories)
{
    std::vector<uint8_t> body{version};
    detail::PutVarint(body, schema);
    body.push_back(categories);
    body.push_back(0);
    body.push_back(0);
    detail::PutVarint(body, uint32_t(items.size()));
    for (const auto& [id, v] : items) {
        detail::PutVarint(body, id);
        detail::PutVarint(body, detail::ZigZag(v));
    }
    const uint32_t crc = detail::Crc32(body.data(), body.size());
    for (int i = 0; i < 4; ++i) body.push_back(uint8_t(crc >> (8 * i)));
    return "OXC." + detail::Base64UrlEncode(body);
}

static std::string RawBody(std::vector<uint8_t> body)
{
    const uint32_t crc = detail::Crc32(body.data(), body.size());
    for (int i = 0; i < 4; ++i) body.push_back(uint8_t(crc >> (8 * i)));
    return "OXC." + detail::Base64UrlEncode(body);
}

// ---- Task 1: Registry ----

static void registry_ids_unique()
{
    std::set<uint16_t> ids;
    std::set<std::string> keys;
    for (const auto& f : kFields) {
        assert(ids.insert(f.id).second);
        assert(keys.insert(f.key).second);
        for (const char* p = f.key; *p; ++p)
            assert((*p >= 'a' && *p <= 'z') || (*p >= '0' && *p <= '9') || *p == '.' || *p == '_');
        assert(FindById(f.id) == &f);
        assert(FindByKey(f.key) == &f);
    }
    assert(!FindById(6));
    assert(!FindById(9999));
    assert(!FindByKey("mod.party_auto_accept"));
}

static void registry_ids_in_category_range()
{
    for (const auto& f : kFields) {
        assert(f.id >= 1 && f.id <= 16383);
        assert(IdInCategoryRange(f.id, f.category));
    }
    for (const auto& f : kFields)
        if (f.category == Category::Hud) assert((f.id - 100) % 4 != 3);
}

static void registry_ranges_valid()
{
    for (const auto& f : kFields) {
        assert(f.min <= f.max);
        assert(f.step >= 1);
        assert((f.max - f.min) % f.step == 0);
        if (f.kind == Kind::Bool) assert(f.min == 0 && f.max == 1 && f.step == 1);
    }
}

static void registry_excludes_forbidden()
{
    const char* banned[] = {"command", "party_players", "nick_text", "client_name", "cape", "autorq",
                            "priority", "gpu", "throttle", "shortcut", "party_auto"};
    for (const auto& f : kFields)
        for (const char* b : banned) assert(std::string(f.key).find(b) == std::string::npos);
}

static void registry_has_no_string_fields()
{
    for (const auto& f : kFields) assert(f.kind == Kind::Bool || f.kind == Kind::Int);
}

static void registry_count_is_pinned()
{
    assert(CountCategory(Category::Modules) == 39);
    assert(CountCategory(Category::Hud) == 57);
    assert(CountCategory(Category::Theme) == 15);
    assert(CountCategory(Category::Lite) == 26);
    assert(kFieldCount == 137);
}

static void registry_matches_field_ids_md()
{
    std::ifstream in("ConfigShare/FieldIds.md");
    assert(in && "run from source/");
    std::string line;
    bool inTable = false;
    size_t rows = 0;
    while (std::getline(in, line)) {
        if (line.rfind("| 番号 | key | カテゴリ", 0) == 0) {
            inTable = true;
            continue;
        }
        if (!inTable || line.rfind("|---", 0) == 0) continue;
        if (line.empty() || line[0] != '|') break;
        // | id | `key` | Category | min | max | step |
        std::vector<std::string> cells;
        std::stringstream ss(line.substr(1));
        std::string cell;
        while (std::getline(ss, cell, '|')) {
            while (!cell.empty() && cell.front() == ' ') cell.erase(cell.begin());
            while (!cell.empty() && cell.back() == ' ') cell.pop_back();
            cells.push_back(cell);
        }
        assert(cells.size() >= 6);
        assert(rows < kFieldCount);
        const Field& f = kFields[rows];
        assert(std::stoi(cells[0]) == f.id);
        assert(cells[1] == "`" + std::string(f.key) + "`");
        const char* cat = f.category == Category::Modules ? "Modules"
                          : f.category == Category::Hud   ? "Hud"
                          : f.category == Category::Theme ? "Theme"
                                                          : "Lite";
        assert(cells[2] == cat);
        assert(std::stoi(cells[3]) == f.min);
        assert(std::stoi(cells[4]) == f.max);
        assert(std::stoi(cells[5]) == f.step);
        ++rows;
    }
    assert(rows == kFieldCount);
}

// ---- Task 2: Snapshot / Diff ----

static void diff_counts_per_category()
{
    const Snapshot now = Defaults();
    Snapshot in = now;
    in.Set(1, 1);                                   // module
    in.Set(2, 1);                                   // module
    in.Set(104, 5000);                              // hud.keystrokes.x
    in.Set(301, 3);                                 // theme.accent
    in.Set(402, 1);                                 // lite.fog
    in.Set(423, 20);                                // lite.render_distance
    const DiffResult d = Diff(now, in, AllCategories);
    assert(d.changed[CategoryIndex(Category::Modules)] == 2);
    assert(d.changed[CategoryIndex(Category::Hud)] == 1);
    assert(d.changed[CategoryIndex(Category::Theme)] == 1);
    assert(d.changed[CategoryIndex(Category::Lite)] == 2);
    assert(d.Total() == 6);
    assert(Diff(now, now, AllCategories).Total() == 0);
}

static void diff_lists_on_and_off_modules()
{
    Snapshot now = Defaults();
    now.Set(3, 1);   // armor on
    now.Set(5, 1);   // fps on, stays on
    Snapshot in = now;
    in.Set(1, 1);    // keystrokes off -> on
    in.Set(3, 0);    // armor on -> off
    const DiffResult d = Diff(now, in, AllCategories);
    assert(d.turnedOn == std::vector<uint16_t>{1});
    assert(d.turnedOff == std::vector<uint16_t>{3});
}

static void diff_respects_category_mask()
{
    const Snapshot now = Defaults();
    Snapshot in = now;
    in.Set(1, 1);
    in.Set(402, 1);
    const DiffResult d = Diff(now, in, static_cast<uint8_t>(Category::Lite));
    assert(d.changed[CategoryIndex(Category::Modules)] == 0);
    assert(d.changed[CategoryIndex(Category::Lite)] == 1);
    assert(d.turnedOn.empty() && d.turnedOff.empty());
}

// ---- Task 3: Code ----

static void code_round_trip()
{
    Snapshot s = Defaults();
    s.Set(1, 1);
    s.Set(104, 9876);
    s.Set(106, 150);
    s.Set(306, 359);
    s.Set(409, 64);
    const std::string code = EncodeCode(s, AllCategories, "My setup", "someone");
    const DecodeResult r = DecodeCode(code);
    assert(r.ok());
    assert(r.snapshot.values == s.values);
    assert(r.categories == AllCategories);
    assert(r.name == "My setup" && r.author == "someone");
    assert(r.warnings == 0);
    assert(!r.verifiedAuthor);

    const uint8_t two = static_cast<uint8_t>(Category::Modules) | static_cast<uint8_t>(Category::Lite);
    const DecodeResult p = DecodeCode(EncodeCode(s, two, "", ""));
    assert(p.ok() && p.categories == two);
    for (const auto& [id, v] : p.snapshot.values) {
        (void)v;
        assert(FindById(id)->category == Category::Modules || FindById(id)->category == Category::Lite);
    }
    assert(p.snapshot.values.size() == 39 + 26);
}

static void code_worst_case_fits_discord()
{
    std::string jp;
    while (jp.size() + 3 <= MaxTextBytes) jp += "設";   // 21 × 3 = 63 bytes
    const std::string code = EncodeCode(Maxed(), AllCategories, jp, jp);
    std::printf("    worst case: %zu chars (estimate ~800)\n", code.size());
    assert(code.size() <= 2000);
    assert(DecodeCode(code).ok());
}

static void decode_rejects_bad_crc()
{
    std::string code = EncodeCode(Defaults(), AllCategories, "x", "y");
    code[10] = code[10] == 'A' ? 'B' : 'A';
    assert(DecodeCode(code).error == DecodeError::BadCrc);
}

static void decode_rejects_truncated()
{
    const std::string code = EncodeCode(Defaults(), AllCategories, "x", "y");
    for (size_t cut : {code.size() - 1, code.size() - 5, code.size() / 2, size_t(6), size_t(4)})
        assert(!DecodeCode(code.substr(0, cut)).ok());
}

static void decode_rejects_oversize()
{
    std::string big = "OXC." + std::string(MaxInputBytes, 'A');
    assert(DecodeCode(big).error == DecodeError::TooLarge);
    std::string body = "OXC." + std::string(13 * 1024 * 4 / 3, 'A');   // under 16KB text, over 12KB decoded
    assert(DecodeCode(body).error == DecodeError::TooLarge);
}

static void decode_rejects_long_varint()
{
    std::vector<uint8_t> body{1, 1, AllCategories, 0, 0, 1, 1, 0x80, 0x80, 0x80, 0x80, 0x80, 0x01};
    assert(DecodeCode(RawBody(body)).error == DecodeError::Malformed);
}

static void decode_rejects_duplicate_id()
{
    assert(DecodeCode(RawCode({{1, 1}, {2, 0}, {1, 0}})).error == DecodeError::DuplicateId);
    assert(DecodeCode(RawCode({{9999, 1}, {9999, 1}})).error == DecodeError::DuplicateId);
}

static void decode_rejects_newer_versions()
{
    assert(DecodeCode(RawCode({{1, 1}}, 2, 1)).error == DecodeError::NewerCodeVersion);
    assert(DecodeCode(RawCode({{1, 1}}, 1, 2)).error == DecodeError::NewerSchema);
}

static void decode_rejects_site_code()
{
    assert(DecodeCode("OXS.abc").error == DecodeError::SiteCode);
    assert(DecodeCode("hello").error == DecodeError::NotACode);
}

static void decode_ignores_unknown_ids()
{
    const DecodeResult a = DecodeCode(RawCode({{1, 1}, {9999, 7}}));
    assert(a.ok() && a.warnings == 1 && a.snapshot.values.size() == 1);
    const DecodeResult b = DecodeCode(RawCode({{1, 1}, {6, 1}}));
    assert(b.ok() && b.warnings == 1 && !b.snapshot.Find(6));
}

static void decode_clamps_out_of_range()
{
    const DecodeResult a = DecodeCode(RawCode({{423, 99}}));
    assert(a.ok() && a.warnings == 1 && *a.snapshot.Find(423) == 48);
    const DecodeResult b = DecodeCode(RawCode({{409, 13}}));
    assert(b.ok() && b.warnings == 1 && *b.snapshot.Find(409) % 8 == 0);
    const DecodeResult c = DecodeCode(RawCode({{305, -50}, {104, 20000}}));
    assert(c.ok() && c.warnings == 2 && *c.snapshot.Find(305) == 20 && *c.snapshot.Find(104) == 10000);
}

static void decode_rejects_bad_bool()
{
    const DecodeResult r = DecodeCode(RawCode({{1, 2}, {2, 1}}));
    assert(r.ok() && r.warnings == 1 && !r.snapshot.Find(1) && *r.snapshot.Find(2) == 1);
}

static void name_is_cleaned_and_cut()
{
    assert(CleanText("a\nb\tc\x01" "d\x7f") == "a b c d");
    std::string jp;
    for (int i = 0; i < 30; ++i) jp += "あ";   // 90 bytes
    const std::string cut = CleanText(jp);
    assert(cut.size() == 63);   // 21 whole characters
    assert(cut == jp.substr(0, 63));
    const DecodeResult r = DecodeCode(EncodeCode(Defaults(), AllCategories, "line1\r\nline2", jp));
    assert(r.ok() && r.name == "line1  line2" && r.author.size() == 63);
}

static void decode_accepts_whitespace()
{
    const std::string code = EncodeCode(Defaults(), AllCategories, "x", "y");
    assert(DecodeCode("  \r\n\t" + code + " \n").ok());
}

// ---- Task 4: HudFraction ----

static void hud_fraction_round_trip()
{
    for (int x = 0; x <= 1920 - 32; x += 7) assert(std::abs(FromFraction(ToFraction(x, 1920), 1920) - x) <= 1);
    for (int y = 0; y <= 1080 - 32; y += 5) assert(std::abs(FromFraction(ToFraction(y, 1080), 1080) - y) <= 1);
}

static void hud_fraction_stays_on_screen()
{
    const int32_t fx = ToFraction(1920 - 1, 1920), fy = ToFraction(1080 - 1, 1080);
    const int sizes[][2] = {{1024, 768}, {2560, 1080}, {1920, 1080}, {20, 10}};
    for (const auto& s : sizes) {
        const int x = FromFraction(fx, s[0]), y = FromFraction(fy, s[1]);
        assert(x >= 0 && x <= std::max(0, s[0] - 32));
        assert(y >= 0 && y <= std::max(0, s[1] - 32));
    }
    assert(FromFraction(-5, 1000) == 0 && FromFraction(50000, 1000) == 1000 - 32);
}

static void hud_fraction_zero_size()
{
    assert(ToFraction(100, 0) == 0);
    assert(FromFraction(5000, 0) == 0);
}


// ---- Task 5: Worker (portable part) ----

static void backup_keeps_newest_five()
{
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / "config_share_test";
    fs::remove_all(dir);
    fs::create_directories(dir);
    const BackupRequest req{dir / "myclient_config.txt", dir / "config_backups"};
    assert(BackupConfig(req));   // no config yet: nothing to back up, still a success
    assert(!fs::exists(req.backupDir) || fs::is_empty(req.backupDir));
    { std::ofstream(req.config) << "VERSION 4\n"; }
    for (int i = 0; i < 7; ++i) {
        fs::path made;
        assert(BackupConfig(req, &made, std::time_t(1700000000 + i)));
        assert(fs::exists(made));
    }
    size_t n = 0;
    for (const auto& e : fs::directory_iterator(req.backupDir)) { (void)e; ++n; }
    assert(n == KeepBackups);
    fs::path again;
    assert(BackupConfig(req, &again, std::time_t(1700000006)));   // same second: a second name, not an overwrite
    assert(again.filename().string().find("-02.txt") != std::string::npos);
    // The second copy of that second is the newest: pruning must drop an older one, not it.
    assert(fs::exists(again));
    size_t m = 0;
    for (const auto& e : fs::directory_iterator(req.backupDir)) { (void)e; ++m; }
    assert(m == KeepBackups);
    fs::path third;
    assert(BackupConfig(req, &third, std::time_t(1700000006)));
    assert(third.filename().string().find("-03.txt") != std::string::npos);
    assert(fs::exists(again) && fs::exists(third));
    fs::remove_all(dir);
}

static void worker_decodes_off_thread()
{
    Worker w;
    Snapshot now = Defaults();
    Snapshot in = now;
    in.Set(1, 1);
    assert(!w.TryTakeResult());
    assert(w.Decode({EncodeCode(in, AllCategories, "n", "a"), now}));
    std::optional<WorkerResult> r;
    for (int i = 0; i < 500 && !r; ++i) {
        r = w.TryTakeResult();
        if (!r) std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    assert(r && r->type == WorkerResult::Type::Decode && r->decoded.ok());
    assert(r->diff.turnedOn == std::vector<uint16_t>{1} && r->diff.Total() == 1);
}

static void worker_keeps_every_result()
{
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / "config_share_test2";
    fs::remove_all(dir);
    fs::create_directories(dir);
    { std::ofstream(dir / "myclient_config.txt") << "VERSION 4\n"; }
    Worker w;
    assert(w.Backup({dir / "myclient_config.txt", dir / "config_backups"}));
    for (int i = 0; i < 500 && w.Busy(); ++i) std::this_thread::sleep_for(std::chrono::milliseconds(2));
    // The backup result is not taken yet when a paste comes in: both must come out, in order.
    assert(w.Decode({EncodeCode(Defaults(), AllCategories, "", ""), Defaults()}));
    for (int i = 0; i < 500 && w.Busy(); ++i) std::this_thread::sleep_for(std::chrono::milliseconds(2));
    std::optional<WorkerResult> a, b;
    for (int i = 0; i < 500 && !a; ++i) a = w.TryTakeResult();
    for (int i = 0; i < 500 && !b; ++i) b = w.TryTakeResult();
    assert(a && a->type == WorkerResult::Type::Backup && a->backupOk);
    assert(b && b->type == WorkerResult::Type::Decode && b->decoded.ok());
    assert(!w.TryTakeResult());
    fs::remove_all(dir);
}

#define RUN(t) (std::printf("%s\n", #t), t())

int main()
{
    RUN(registry_ids_unique);
    RUN(registry_ids_in_category_range);
    RUN(registry_ranges_valid);
    RUN(registry_excludes_forbidden);
    RUN(registry_has_no_string_fields);
    RUN(registry_count_is_pinned);
    RUN(registry_matches_field_ids_md);
    RUN(diff_counts_per_category);
    RUN(diff_lists_on_and_off_modules);
    RUN(diff_respects_category_mask);
    RUN(code_round_trip);
    RUN(code_worst_case_fits_discord);
    RUN(decode_rejects_bad_crc);
    RUN(decode_rejects_truncated);
    RUN(decode_rejects_oversize);
    RUN(decode_rejects_long_varint);
    RUN(decode_rejects_duplicate_id);
    RUN(decode_rejects_newer_versions);
    RUN(decode_rejects_site_code);
    RUN(decode_ignores_unknown_ids);
    RUN(decode_clamps_out_of_range);
    RUN(decode_rejects_bad_bool);
    RUN(name_is_cleaned_and_cut);
    RUN(decode_accepts_whitespace);
    RUN(hud_fraction_round_trip);
    RUN(hud_fraction_stays_on_screen);
    RUN(hud_fraction_zero_size);
    RUN(backup_keeps_newest_five);
    RUN(worker_decodes_off_thread);
    RUN(worker_keeps_every_result);
    std::printf("config_share: all passed\n");
    return 0;
}
