// OriginXSigner.exe: the author's signing tool (docs/signing.md). Runs on the author's PC only.
//   Double-click, first time: makes K1 (kept in C:\originx-signing\k1.key, locked by a passphrase), K2 (put on the
//     clipboard for the password manager, never written to disk) and T1 (put on the clipboard for the Worker's
//     Secret TEST_MANIFEST_KEY), then shows the public keys to hand over.
//   Drop a list (catalog.json, launcher.json, versions.json) on it: shows what it is, asks the passphrase, writes
//     <list>.sig next to it. Without k1.key it asks for the K2 line from the password manager instead.
// Nothing secret is printed, logged or sent anywhere. Logic: SignerCore.h (tests/signer_core.cpp).
#include "SignerCore.h"

#include <windows.h>
#include <bcrypt.h>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>

namespace fs = std::filesystem;
using namespace OriginXSigner;

namespace {
const fs::path Folder = L"C:\\originx-signing";
const fs::path K1File = Folder / L"k1.key";
const fs::path PublicFile = Folder / L"public-keys.txt";

std::wstring Wide(const std::string& s)
{
    if (s.empty()) return {};
    std::wstring out(MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), out.data(), static_cast<int>(out.size()));
    return out;
}
std::string Narrow(const std::wstring& s)
{
    if (s.empty()) return {};
    std::string out(WideCharToMultiByte(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0, nullptr, nullptr), '\0');
    WideCharToMultiByte(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), out.data(), static_cast<int>(out.size()), nullptr, nullptr);
    return out;
}
void Say(const std::string& text) { std::fputs((text + "\n").c_str(), stdout); std::fflush(stdout); }

// A line typed by the author. hidden: nothing is shown while typing (passphrases, the K2 line).
std::string ReadLine(bool hidden)
{
    HANDLE in = GetStdHandle(STD_INPUT_HANDLE);
    DWORD mode = 0;
    const bool console = GetConsoleMode(in, &mode);
    if (hidden && console) SetConsoleMode(in, mode & ~ENABLE_ECHO_INPUT);
    std::wstring line;
    wchar_t buf[512];
    DWORD got = 0;
    while (console && ReadConsoleW(in, buf, 512, &got, nullptr) && got)
    {
        line.append(buf, got);
        if (line.find(L'\n') != std::wstring::npos) break;
    }
    if (hidden && console) { SetConsoleMode(in, mode); Say(""); }
    while (!line.empty() && (line.back() == L'\n' || line.back() == L'\r')) line.pop_back();
    std::string out = Narrow(line);
    SecureZeroMemory(line.data(), line.size() * sizeof(wchar_t));
    SecureZeroMemory(buf, sizeof buf);
    return out;
}
void Wipe(std::string& s) { if (!s.empty()) SecureZeroMemory(s.data(), s.size()); s.clear(); }

void WaitEnter(const char* prompt = "Enter キーを押すと閉じます。")
{
    Say(prompt);
    std::string ignored = ReadLine(false);
}

bool Random(uint8_t* out, size_t size)
{
    return BCryptGenRandom(nullptr, out, static_cast<ULONG>(size), BCRYPT_USE_SYSTEM_PREFERRED_RNG) == 0;
}

bool SetClipboard(const std::string& text)
{
    const std::wstring wide = Wide(text);
    if (!OpenClipboard(nullptr)) return false;
    EmptyClipboard();
    bool ok = false;
    if (!wide.empty())
    {
        HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, (wide.size() + 1) * sizeof(wchar_t));
        if (memory)
        {
            void* p = GlobalLock(memory);
            std::memcpy(p, wide.c_str(), (wide.size() + 1) * sizeof(wchar_t));
            GlobalUnlock(memory);
            ok = SetClipboardData(CF_UNICODETEXT, memory) != nullptr;
            if (!ok) GlobalFree(memory);
        }
    }
    else ok = true;
    CloseClipboard();
    return ok;
}

// A new passphrase, typed twice. Refuses short ones.
std::string NewPassphrase(const char* name)
{
    for (;;)
    {
        Say(std::string("  ") + name + " の合言葉を決めて入力し、Enter（入力中は何も表示されません。10 文字以上）:");
        std::string first = ReadLine(true);
        if (first.size() < 10) { Say("  短すぎます。10 文字以上にしてください。"); Wipe(first); continue; }
        Say("  もう一度同じものを入力:");
        std::string again = ReadLine(true);
        const bool same = first == again;
        Wipe(again);
        if (same) return first;
        Wipe(first);
        Say("  2 回の入力が違いました。もう一度。");
    }
}

bool NewSeed(Seed& seed) { return Random(seed.data(), seed.size()); }

std::string LockWithRandom(uint32_t id, const Seed& seed, const std::string& passphrase)
{
    uint8_t salt[16], nonce[24];
    if (!Random(salt, sizeof salt) || !Random(nonce, sizeof nonce)) throw std::runtime_error("乱数を作れませんでした");
    return LockKey(id, seed, passphrase, salt, nonce);
}

int Setup()
{
    Say("=== OriginX 署名ツール：鍵を作ります（最初の 1 回だけ） ===");
    Say("途中でやめても大丈夫です（最後まで行かないと何も保存されず、次はまた最初から始まります）。");
    Say("作るもの：K1（普段の鍵。この PC に保存）、K2（予備。パスワードマネージャーへ）、T1（test 用。Cloudflare へ）");
    Say("");
    std::error_code ec;
    fs::create_directories(Folder, ec);

    Seed k1{}, k2{}, t1{};
    if (!NewSeed(k1) || !NewSeed(k2) || !NewSeed(t1)) { Say("乱数を作れませんでした。"); WaitEnter(); return 1; }
    const PublicKey p1 = PublicOf(k1), p2 = PublicOf(k2), pt = PublicOf(t1);

    Say("[1/3] K1");
    std::string pass1 = NewPassphrase("K1");
    Say("  鍵を作っています（数秒）…");
    const std::string k1text = LockWithRandom(1, k1, pass1);
    Wipe(pass1);
    crypto_wipe(k1.data(), k1.size());
    Say("  K1 はできました（最後に C:\\originx-signing\\k1.key に保存します）。合言葉はパスワードマネージャーに入れてください。");
    Say("");

    Say("[2/3] K2（予備）。K1 とは別の合言葉にしてください。");
    std::string pass2 = NewPassphrase("K2");
    Say("  鍵を作っています（数秒）…");
    std::string k2text = LockWithRandom(2, k2, pass2);
    Wipe(pass2);
    crypto_wipe(k2.data(), k2.size());
    SetClipboard(k2text);
    Wipe(k2text);
    Say("  K2 をクリップボードに入れました。パスワードマネージャーに新しい項目「OriginX K2」を作って貼り付けてください。");
    Say("  （K2 の合言葉は、その項目には入れないでください。）");
    WaitEnter("  貼り付けたら Enter（クリップボードは消します）:");
    SetClipboard("");
    Say("");

    Say("[3/3] T1（test 用）");
    std::string t1text = TestKeyText(101, t1);
    crypto_wipe(t1.data(), t1.size());
    SetClipboard(t1text);
    Wipe(t1text);
    Say("  T1 をクリップボードに入れました。Cloudflare のダッシュボードで：");
    Say("    Workers & Pages → originx → 設定 → 変数とシークレット → 追加");
    Say("    種類「シークレット」、名前 TEST_MANIFEST_KEY、値に貼り付け → 保存（デプロイ）");
    WaitEnter("  保存したら Enter（クリップボードは消します）:");
    SetClipboard("");
    Say("");

    // K1 is written last: a setup stopped half way leaves nothing behind and simply starts again.
    {
        std::ofstream out(K1File, std::ios::binary);
        out << k1text << "\n";
        if (!out) { Say("C:\\originx-signing\\k1.key に書けませんでした。"); WaitEnter(); return 1; }
    }
    std::ostringstream pub;
    pub << "OriginX public keys (not secret)\n"
        << "K1 " << TableEntry(1, p1) << "  fingerprint " << Fingerprint(p1) << "\n"
        << "K2 " << TableEntry(2, p2) << "  fingerprint " << Fingerprint(p2) << "\n"
        << "T1 " << EmbedArgument(101, pt) << "  fingerprint " << Fingerprint(pt) << "\n";
    { std::ofstream out(PublicFile, std::ios::binary); out << pub.str(); }
    SetClipboard(pub.str());
    Say("=== できました ===");
    Say("K1 を C:\\originx-signing\\k1.key に保存しました。");
    Say("公開鍵（秘密ではありません）をクリップボードに入れました。そのまま Claude Code に貼ってください。");
    Say("C:\\originx-signing\\public-keys.txt にも保存してあります。");
    Say("fingerprint（下の 16 文字）を 3 本とも紙かメモに書いておいてください：");
    Say("  K1 " + Fingerprint(p1));
    Say("  K2 " + Fingerprint(p2));
    Say("  T1 " + Fingerprint(pt));
    WaitEnter();
    return 0;
}

// Opens K1 (or, without k1.key, the K2 line pasted from the password manager).
bool OpenKey(uint32_t& id, Seed& seed)
{
    std::error_code ec;
    std::string text;
    if (fs::is_regular_file(K1File, ec))
    {
        std::ifstream in(K1File, std::ios::binary);
        std::getline(in, text);
    }
    else
    {
        Say("k1.key が見つかりません。パスワードマネージャーの「OriginX K2」の中身を貼り付けて Enter:");
        text = ReadLine(true);
    }
    for (int attempt = 0; attempt < 3; ++attempt)
    {
        Say("合言葉を入力して Enter（入力中は何も表示されません）:");
        std::string pass = ReadLine(true);
        Say("確かめています（数秒）…");
        const Unlock result = UnlockKey(text, pass, id, seed);
        Wipe(pass);
        if (result == Unlock::Ok) { Wipe(text); return true; }
        if (result == Unlock::NotAKey) { Say("鍵の形ではありません（貼り付け漏れか、別のものです）。"); Wipe(text); return false; }
        Say("合言葉が違います。");
    }
    Wipe(text);
    return false;
}

int SignFile(const fs::path& path)
{
    Say("=== OriginX 署名ツール：署名 ===");
    std::string bytes;
    {
        std::ifstream in(path, std::ios::binary);
        if (!in) { Say("ファイルを開けません: " + Narrow(path.wstring())); WaitEnter(); return 1; }
        std::ostringstream s; s << in.rdbuf(); bytes = s.str();
    }
    const std::string kind = DetectKind(Narrow(path.wstring()), bytes);
    if (kind.empty())
    {
        Say("署名する一覧ではありません（test の一覧は Worker が署名するので、ここでは署名しません）: " + Narrow(path.wstring()));
        WaitEnter();
        return 1;
    }
    Say("ファイル: " + Narrow(path.wstring()));
    Say("中身    : " + Summary(kind, bytes));
    Say("違っていたら、ここでウィンドウを閉じてください。");
    uint32_t id = 0;
    Seed seed{};
    if (!OpenKey(id, seed)) { Say("署名しませんでした。"); WaitEnter(); return 1; }
    const std::string sig = SignList(kind, bytes, id, seed);
    const PublicKey pub = PublicOf(seed);
    crypto_wipe(seed.data(), seed.size());
    if (!VerifyManifest(kind, bytes, sig, {SigningKey{id, pub}}, 1).ok()) { Say("署名の確認に失敗しました。"); WaitEnter(); return 1; }
    const fs::path out = path.wstring() + L".sig";
    {
        std::ofstream file(out, std::ios::binary);
        file << sig;
        if (!file) { Say("書けませんでした: " + Narrow(out.wstring())); WaitEnter(); return 1; }
    }
    Say("署名しました（鍵 K" + std::to_string(id) + "）: " + Narrow(out.wstring()));
    Say("Claude Code に「署名した」と伝えてください。");
    WaitEnter();
    return 0;
}

int Menu()
{
    Say("=== OriginX 署名ツール ===");
    Say("鍵はもう作ってあります（C:\\originx-signing\\k1.key）。");
    Say("署名するときは、一覧のファイル（catalog.json など）をこの exe のアイコンの上にドラッグして離してください。");
    Say("");
    Say("合言葉が開くか確かめるときは 1、閉じるときはそのまま Enter:");
    if (ReadLine(false) != "1") return 0;
    uint32_t id = 0;
    Seed seed{};
    if (OpenKey(id, seed))
    {
        const PublicKey pub = PublicOf(seed);
        crypto_wipe(seed.data(), seed.size());
        Say("開きました。鍵 K" + std::to_string(id) + "、fingerprint " + Fingerprint(pub) + "（紙と同じか見比べてください）");
    }
    WaitEnter();
    return 0;
}
}

int wmain(int argc, wchar_t** argv)
{
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
    try
    {
        if (argc >= 2) return SignFile(argv[1]);
        std::error_code ec;
        return fs::is_regular_file(K1File, ec) ? Menu() : Setup();
    }
    catch (const std::exception& error)
    {
        Say(std::string("エラー: ") + error.what());
        WaitEnter();
        return 1;
    }
}
