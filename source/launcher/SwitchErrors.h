#pragma once
// Reading what switch-game.ps1 wrote (pure, no Windows calls; tests/launcher_switch_errors.cpp).
// - NormalIdFromLog: the Minecraft version Windows starts normally ("NormalId: 26.52.3", query mode).
// - InstallErrorHint: the first Windows install error code in the log, explained in Japanese.
//   Only codes whose meaning is fixed by Windows (winerror.h) are explained; others are named as they are.
#include "../Lang.h"
#include <cctype>
#include <cstdio>
#include <string>

namespace MyClientLauncher {

inline std::string NormalIdFromLog(const std::string& text)
{
    const std::string key = "NormalId: ";
    const size_t at = text.rfind(key);
    if (at == std::string::npos) return {};
    size_t end = at + key.size();
    while (end < text.size() && (std::isdigit(static_cast<unsigned char>(text[end])) || text[end] == '.')) ++end;
    std::string id = text.substr(at + key.size(), end - at - key.size());
    while (!id.empty() && id.back() == '.') id.pop_back();
    if (id.empty() || id.find('.') == std::string::npos) return {};
    return id;
}

// The first "0x8XXXXXXX" in the text, upper-cased ("0x80073CF9"), or empty.
inline std::string FirstErrorCode(const std::string& text)
{
    for (size_t i = 0; i + 10 <= text.size(); ++i)
    {
        if (text[i] != '0' || (text[i + 1] != 'x' && text[i + 1] != 'X')) continue;
        if (i > 0 && std::isalnum(static_cast<unsigned char>(text[i - 1]))) continue;
        bool hex = true;
        for (size_t k = i + 2; k < i + 10 && hex; ++k) hex = std::isxdigit(static_cast<unsigned char>(text[k])) != 0;
        if (!hex || (text[i + 2] != '8')) continue;
        if (i + 10 < text.size() && std::isxdigit(static_cast<unsigned char>(text[i + 10]))) continue;
        std::string code = "0x";
        for (size_t k = i + 2; k < i + 10; ++k) code += static_cast<char>(std::toupper(static_cast<unsigned char>(text[k])));
        return code;
    }
    return {};
}

// Windows refuses to replace a Minecraft that was registered from an unpacked folder (another launcher's way of
// installing, or Add-AppxPackage -Register): 0x80073CFB with "unpackaged version" in its install log (2026-11-11,
// the user's PC with 26.63.1). Only reinstalling Minecraft from the Xbox app / Store makes the switch possible.
inline bool UnpackagedInstall(const std::string& text)
{
    return text.find("unpackaged version") != std::string::npos ||
           text.find("\xE3\x83\x91\xE3\x83\x83\xE3\x82\xB1\xE3\x83\xBC\xE3\x82\xB8\xE5\x8C\x96\xE3\x81\x95\xE3\x82\x8C\xE3\x81\xA6\xE3\x81\x84\xE3\x81\xAA\xE3\x81\x84") != std::string::npos;   // "パッケージ化されていない" (UTF-8)
}

inline std::string InstallErrorHint(const std::string& text)
{
    const std::string code = FirstErrorCode(text);
    if (code.empty()) return {};
    if (code == "0x80073CFB" && UnpackagedInstall(text))
        return Lang::T("Windows のエラー 0x80073CFB: 今の Minecraft は、別のランチャーなどで展開済みのフォルダーとして登録された形で入っているため、"
               "Windows が正規のパッケージに置き換えられません。Xbox アプリ（Microsoft Store）から入れ直した Minecraft なら切り替えられます"
                       "（入れ直す前にワールドのバックアップを推奨）。OriginX は今のまま、選んだ版で起動できます。",
                       "Windows error 0x80073CFB: the current Minecraft is registered as a folder unpacked by another launcher or similar, so "
                       "Windows cannot replace it with the official package. A Minecraft reinstalled from the Xbox app (Microsoft Store) can be switched "
                       "(back up your worlds before reinstalling). OriginX can still launch the chosen version as it is.");
    struct Known { const char* code; const char* meaning; };
    const Known known[] = {
        {"0x80073CF0", Lang::T("パッケージを開けません（ダウンロードしたファイルが壊れているか、途中までしかありません）。", "Cannot open the package (the downloaded file is broken or incomplete).")},
        {"0x80073CF1", Lang::T("パッケージが見つかりません。", "Package not found.")},
        {"0x80073CF2", Lang::T("パッケージの中身が正しくありません。", "The package contents are invalid.")},
        {"0x80073CF3", Lang::T("必要な別のパッケージ（依存関係）が足りないか、古いです。", "A required package (dependency) is missing or outdated.")},
        {"0x80073CF4", Lang::T("インストール先のドライブの空き容量が足りません。", "Not enough free space on the install drive.")},
        {"0x80073CF5", Lang::T("Windows がパッケージを取得中に通信に失敗しました。", "Windows failed to download the package.")},
        {"0x80073CF6", Lang::T("Windows への登録に失敗しました。", "Registering with Windows failed.")},
        {"0x80073CF7", Lang::T("前の版の登録を外せませんでした。", "Could not unregister the previous version.")},
        {"0x80073CF8", Lang::T("インストールが取り消されました。", "The installation was cancelled.")},
        {"0x80073CF9", Lang::T("インストールに失敗しました（詳しい理由は下の Windows のログ）。", "Installation failed (see the Windows log below for details).")},
        {"0x80073CFA", Lang::T("前の版を削除できませんでした。", "Could not remove the previous version.")},
        {"0x80073CFB", Lang::T("同じパッケージがすでに入っていて、Windows が置き換えを断りました（同じ版番号で中身が違う、または別の方法で入っている）。", "The same package is already installed and Windows refused to replace it (same version number with different contents, or installed another way).")},
        {"0x80073CFD", Lang::T("インストールの前提条件を満たしていません。", "The installation prerequisites are not met.")},
        {"0x80073CFE", Lang::T("Windows のパッケージ管理のデータが壊れています。", "Windows' package management data is corrupted.")},
        {"0x80073CFF", Lang::T("Windows の設定（ポリシー）でこの方法のインストールが許可されていません。", "Windows settings (policy) do not allow this kind of installation.")},
        {"0x80073D00", Lang::T("依存関係の更新に失敗しました。", "Updating dependencies failed.")},
        {"0x80073D01", Lang::T("Windows の設定（ポリシー）でブロックされました。", "Blocked by Windows settings (policy).")},
        {"0x80073D02", Lang::T("Minecraft か関連するアプリが使用中です。終了してから、もう一度試してください。", "Minecraft or a related app is in use. Close it and try again.")},
        {"0x80073D05", Lang::T("前の版のアプリデータを消せませんでした。", "Could not delete the previous version's app data.")},
        {"0x80073D06", Lang::T("もっと新しい版が入っているため、Windows が古い版への置き換えを断りました。", "A newer version is installed, so Windows refused to replace it with an older one.")},
        {"0x80070005", Lang::T("アクセスが拒否されました（権限が足りません）。", "Access denied (insufficient permissions).")},
        {"0x80070070", Lang::T("ドライブの空き容量が足りません。", "Not enough free space on the drive.")},
        {"0x800B0100", Lang::T("パッケージの署名がありません。", "The package is not signed.")},
        {"0x800B0109", Lang::T("パッケージの署名の証明書が信頼されていません。", "The package's signing certificate is not trusted.")},
    };
    for (const auto& item : known)
        if (code == item.code) return std::string(Lang::T("Windows のエラー ", "Windows error ")) + code + ": " + item.meaning;
    return Lang::T("Windows のエラー ", "Windows error ") + code + Lang::T("（意味は下の Windows のログを参照）", " (see the Windows log below for the meaning)");
}

} // namespace MyClientLauncher
