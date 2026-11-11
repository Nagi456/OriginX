#pragma once
// Reading what switch-game.ps1 wrote (pure, no Windows calls; tests/launcher_switch_errors.cpp).
// - NormalIdFromLog: the Minecraft version Windows starts normally ("NormalId: 26.52.3", query mode).
// - InstallErrorHint: the first Windows install error code in the log, explained in Japanese.
//   Only codes whose meaning is fixed by Windows (winerror.h) are explained; others are named as they are.
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

inline std::string InstallErrorHint(const std::string& text)
{
    const std::string code = FirstErrorCode(text);
    if (code.empty()) return {};
    struct Known { const char* code; const char* meaning; };
    static const Known known[] = {
        {"0x80073CF0", "パッケージを開けません（ダウンロードしたファイルが壊れているか、途中までしかありません）。"},
        {"0x80073CF1", "パッケージが見つかりません。"},
        {"0x80073CF2", "パッケージの中身が正しくありません。"},
        {"0x80073CF3", "必要な別のパッケージ（依存関係）が足りないか、古いです。"},
        {"0x80073CF4", "インストール先のドライブの空き容量が足りません。"},
        {"0x80073CF5", "Windows がパッケージを取得中に通信に失敗しました。"},
        {"0x80073CF6", "Windows への登録に失敗しました。"},
        {"0x80073CF7", "前の版の登録を外せませんでした。"},
        {"0x80073CF8", "インストールが取り消されました。"},
        {"0x80073CF9", "インストールに失敗しました（詳しい理由は下の Windows のログ）。"},
        {"0x80073CFA", "前の版を削除できませんでした。"},
        {"0x80073CFB", "同じ版番号で中身の違うパッケージがすでに入っています。"},
        {"0x80073CFD", "インストールの前提条件を満たしていません。"},
        {"0x80073CFE", "Windows のパッケージ管理のデータが壊れています。"},
        {"0x80073CFF", "Windows の設定（ポリシー）でこの方法のインストールが許可されていません。"},
        {"0x80073D00", "依存関係の更新に失敗しました。"},
        {"0x80073D01", "Windows の設定（ポリシー）でブロックされました。"},
        {"0x80073D02", "Minecraft か関連するアプリが使用中です。終了してから、もう一度試してください。"},
        {"0x80073D05", "前の版のアプリデータを消せませんでした。"},
        {"0x80073D06", "もっと新しい版が入っているため、Windows が古い版への置き換えを断りました。"},
        {"0x80070005", "アクセスが拒否されました（権限が足りません）。"},
        {"0x80070070", "ドライブの空き容量が足りません。"},
        {"0x800B0100", "パッケージの署名がありません。"},
        {"0x800B0109", "パッケージの署名の証明書が信頼されていません。"},
    };
    for (const auto& item : known)
        if (code == item.code) return std::string("Windows のエラー ") + code + ": " + item.meaning;
    return "Windows のエラー " + code + "（意味は下の Windows のログを参照）";
}

} // namespace MyClientLauncher
