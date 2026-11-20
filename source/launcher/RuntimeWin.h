#pragma once
#include "../Lang.h"
#include <windows.h>
#include <bcrypt.h>
#include "Install.h"

namespace MyClientLauncher {
// 0 = readable. Otherwise the Win32 error (5 = protected GDK executable).
inline DWORD ProbeRead(const fs::path& path)
{
    HANDLE handle = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle == INVALID_HANDLE_VALUE) return GetLastError();
    CloseHandle(handle);
    return 0;
}

inline void RequireExecutableVersion(const fs::path& game, const Target& target = {})
{
    DWORD unused = 0;
    const DWORD size = GetFileVersionInfoSizeW(game.c_str(), &unused);
    if (!size || size > 4 * 1024 * 1024) throw std::runtime_error(Lang::T("ゲーム本体のバージョン情報を取得できません。", "Could not read the game's version information."));
    std::vector<unsigned char> bytes(size);
    VS_FIXEDFILEINFO* version = nullptr;
    UINT length = 0;
    if (!GetFileVersionInfoW(game.c_str(), 0, size, bytes.data()) ||
        !VerQueryValueW(bytes.data(), L"\\", reinterpret_cast<void**>(&version), &length) ||
        length < sizeof(VS_FIXEDFILEINFO) || version->dwSignature != 0xfeef04bd)
        throw std::runtime_error(Lang::T("ゲーム本体のバージョン情報が不正です。", "The game's version information is invalid."));
    const std::string actual = std::to_string(HIWORD(version->dwFileVersionMS)) + "." +
        std::to_string(LOWORD(version->dwFileVersionMS)) + "." + std::to_string(HIWORD(version->dwFileVersionLS)) + "." +
        std::to_string(LOWORD(version->dwFileVersionLS));
    if (!SupportedGameVersion(actual, target)) throw std::runtime_error("Minecraft " + target.id + Lang::T("が必要です。本体のファイルバージョン: ", " is required. Game file version: ") + actual);
}

inline std::string Sha256File(const fs::path& path)
{
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    struct Cleanup {
        BCRYPT_ALG_HANDLE& algorithm; BCRYPT_HASH_HANDLE& hash;
        std::vector<unsigned char> object;
        ~Cleanup() { if (hash) BCryptDestroyHash(hash); if (algorithm) BCryptCloseAlgorithmProvider(algorithm, 0); }
    } cleanup{algorithm, hash, {}};
    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0)
        throw std::runtime_error(Lang::T("SHA256を初期化できません。", "Could not initialize SHA256."));
    DWORD size = 0, returned = 0;
    if (BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&size), sizeof size, &returned, 0) < 0)
        throw std::runtime_error(Lang::T("SHA256の情報を取得できません。", "Could not get SHA256 information."));
    cleanup.object.resize(size);
    if (BCryptCreateHash(algorithm, &hash, cleanup.object.data(), size, nullptr, 0, 0) < 0)
        throw std::runtime_error(Lang::T("SHA256の計算を開始できません。", "Could not start the SHA256 calculation."));
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error(Lang::T("検査するファイルがありません: ", "No file to check: ") + path.u8string());
    std::vector<unsigned char> buffer(64 * 1024);
    while (input)
    {
        input.read(reinterpret_cast<char*>(buffer.data()), buffer.size());
        const auto count = input.gcount();
        if (count && BCryptHashData(hash, buffer.data(), static_cast<ULONG>(count), 0) < 0)
            throw std::runtime_error(Lang::T("SHA256の計算に失敗しました。", "SHA256 calculation failed."));
    }
    if (!input.eof()) throw std::runtime_error(Lang::T("検査中のファイル読み込みに失敗しました。", "Reading a file failed during the check."));
    unsigned char digest[32]{};
    if (BCryptFinishHash(hash, digest, sizeof digest, 0) < 0) throw std::runtime_error(Lang::T("SHA256を確定できません。", "Could not finish the SHA256."));
    constexpr char digits[] = "0123456789abcdef";
    std::string result;
    for (unsigned char byte : digest) { result += digits[byte >> 4]; result += digits[byte & 15]; }
    return result;
}

inline void RequireRuntime(const fs::path& directory, const fs::path& lockPath, const Target& target = {})
{
    const auto lock = ReadJson(lockPath);
    if (lock.at("schemaVersion") != 1 || lock.at("loaderVersion") != target.loader || lock.at("gameVersion") != target.runtimeGame)
        throw std::runtime_error(Lang::T("同梱のローダー設定の版が一致しません。", "The bundled loader settings version does not match."));
    RequireLoader(directory, target);
    RequireX64Pe(directory / "PreLoader.dll");
    const auto receipt = ReadJson(directory / "MyClient-runtime.json");
    if (receipt.value("manifestSha256", "") != Sha256File(lockPath) || receipt.value("ready", false) != true)
        throw std::runtime_error(Lang::T("ローダーの準備が完了していません。", "The loader is not ready yet."));
    for (const auto& asset : lock.at("assets"))
        for (const auto& file : asset.at("installedFiles").items())
            if (Sha256File(SafeChild(directory, file.key())) != file.value().get<std::string>())
                throw std::runtime_error(Lang::T("ローダーのファイルが変化しています: ", "A loader file has changed: ") + file.key());
    const auto game = directory / "Minecraft.Windows.exe";
    if (Sha256File(game) != receipt.at("gameExeSha256").get<std::string>())
        throw std::runtime_error(Lang::T("準備したゲーム本体が変化しています。", "The prepared game has changed."));
    const auto imports = ImportedDlls(game);
    if (!imports.count("preloader.dll") || imports.count("levilauncher.dll"))
        throw std::runtime_error(Lang::T("ゲームのDLL読み込み設定が一致しません。", "The game's DLL loading settings do not match."));
}

// Attach mode: the loader files live in a private directory and no game
// executable is copied or edited. Same lock/hash checks, minus the exe.
inline void RequireAttachRuntime(const fs::path& directory, const fs::path& lockPath, const Target& target = {})
{
    const auto lock = ReadJson(lockPath);
    if (lock.at("schemaVersion") != 1 || lock.at("loaderVersion") != target.loader || lock.at("gameVersion") != target.runtimeGame)
        throw std::runtime_error(Lang::T("同梱のローダー設定の版が一致しません。", "The bundled loader settings version does not match."));
    RequireLoader(directory, target);
    RequireX64Pe(directory / "PreLoader.dll");
    const auto receipt = ReadJson(directory / "MyClient-runtime.json");
    if (receipt.value("manifestSha256", "") != Sha256File(lockPath) || receipt.value("ready", false) != true ||
        receipt.value("attach", false) != true)
        throw std::runtime_error(Lang::T("ローダーの準備が完了していません。", "The loader is not ready yet."));
    for (const auto& asset : lock.at("assets"))
        for (const auto& file : asset.at("installedFiles").items())
            if (Sha256File(SafeChild(directory, file.key())) != file.value().get<std::string>())
                throw std::runtime_error(Lang::T("ローダーのファイルが変化しています: ", "A loader file has changed: ") + file.key());
}
}
