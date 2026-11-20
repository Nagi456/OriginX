#pragma once
// Release channels (docs/superpowers/specs/2026-11-09-release-channels-design.md): stable (everyone), early
// (supporters, moderators) and test (moderators). The logic only: no Windows calls, so tests/launcher_channels.cpp
// runs it anywhere. The Windows side (login, status call, icon download) is AccountWin.h.
#include "../Lang.h"
#include "Updates.h"
#include "ManifestSignature.h"
#include "SelfUpdate.h"
#include <optional>

namespace MyClientLauncher {
enum class Channel { Stable, Early, Test };

inline const char* ChannelKey(Channel channel)
{
    switch (channel)
    {
    case Channel::Early: return "early";
    case Channel::Test: return "test";
    default: return "stable";
    }
}
inline Channel ParseChannel(const std::string& key)
{
    if (key == "early") return Channel::Early;
    if (key == "test") return Channel::Test;
    return Channel::Stable;   // anything else, including a hand-edited value, is the public channel
}
// The name shown to the user (UTF-8).
inline const char* ChannelName(Channel channel)
{
    switch (channel)
    {
    case Channel::Early: return Lang::T("先行版", "Early access");
    case Channel::Test: return Lang::T("テスト版", "Test");
    default: return Lang::T("安定版", "Stable");
    }
}

inline constexpr const char* OriginXHost = "https://originx.originx-client.workers.dev";

inline bool AllOf(const std::string& text, const char* allowed)
{
    return !text.empty() && text.find_first_not_of(allowed) == std::string::npos;
}
// A link the Worker signed for a file of a channel (valid 30 minutes): <host>/file/<key>?exp=<digits>&sig=<token>.
// The only URLs besides the public releases that the launcher downloads from; the key has plain names only.
inline bool SignedFileUrl(const std::string& url)
{
    const std::string prefix = std::string(OriginXHost) + "/file/";
    if (url.size() <= prefix.size() || url.compare(0, prefix.size(), prefix) != 0) return false;
    const auto query = url.find('?', prefix.size());
    if (query == std::string::npos) return false;
    const std::string key = url.substr(prefix.size(), query - prefix.size());
    if (!AllOf(key, "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-._/") || key.find("..") != std::string::npos ||
        key.front() == '/' || key.back() == '/' || key.find("//") != std::string::npos)
        return false;
    const std::string rest = url.substr(query + 1);
    const std::string exp = "exp=", sig = "&sig=";
    if (rest.compare(0, exp.size(), exp) != 0) return false;
    const auto split = rest.find(sig);
    if (split == std::string::npos) return false;
    return AllOf(rest.substr(exp.size(), split - exp.size()), "0123456789") &&
           AllOf(rest.substr(split + sig.size()), "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_");
}

inline constexpr size_t MaxAvatarBytes = 128 * 1024;
// The account's icon: a 64px PNG of Discord's CDN only (an animated icon, hash starting a_, is served as a still .png).
inline bool AvatarUrl(const std::string& url)
{
    const std::string prefix = "https://cdn.discordapp.com/", size = "?size=64", png = ".png";
    if (url.size() <= prefix.size() + size.size() || url.compare(0, prefix.size(), prefix) != 0) return false;
    if (url.compare(url.size() - size.size(), size.size(), size) != 0) return false;
    const std::string path = url.substr(prefix.size(), url.size() - prefix.size() - size.size());
    if (path.size() <= png.size() || path.compare(path.size() - png.size(), png.size(), png) != 0) return false;
    std::vector<std::string> parts;
    for (size_t at = 0; at <= path.size() - png.size();)
    {
        const auto slash = path.find('/', at);
        const auto end = slash == std::string::npos ? path.size() - png.size() : slash;
        parts.push_back(path.substr(at, end - at));
        if (slash == std::string::npos) break;
        at = slash + 1;
    }
    const auto digits = [](const std::string& p) { return AllOf(p, "0123456789"); };
    const auto icon = [](std::string hash) {
        if (hash.compare(0, 2, "a_") == 0) hash.erase(0, 2);
        return hash.size() == 32 && hash.find_first_not_of("0123456789abcdef") == std::string::npos;
    };
    // avatars/<user>/<hash>   or   guilds/<guild>/users/<user>/avatars/<hash>
    if (parts.size() == 3 && parts[0] == "avatars") return digits(parts[1]) && icon(parts[2]);
    return parts.size() == 6 && parts[0] == "guilds" && digits(parts[1]) && parts[2] == "users" && digits(parts[3]) &&
           parts[4] == "avatars" && icon(parts[5]);
}
inline bool LooksLikePng(const std::string& bytes)
{
    return bytes.size() > 8 && bytes.compare(0, 8, std::string("\x89PNG\r\n\x1a\n", 8)) == 0;
}

// The answer of GET /launcher/status. early / test are null (not given) when this account may not use the channel.
struct AccountStatus {
    std::string id, name, avatarUrl, token;
    bool verified = false, supporter = false, moderator = false;
    Json early = nullptr, test = nullptr;
    std::string signatureProblem;   // "test: mismatch" when a channel list was dropped for its signature (no secrets)
    std::optional<LauncherInfo> testLauncher;   // test accounts only, signature and channel checked (SelfUpdate.h)
};

// testLauncher of the status answer: {list: base64 of the stored bytes, signature, url}. Used only with signing keys
// (a launcher made without keys never takes a test launcher). Empty on anything wrong; `problem` names it.
inline std::optional<LauncherInfo> VerifiedTestLauncher(const Json& entry, const std::vector<SigningKey>& keys, uint32_t minKeyId, std::string& problem)
{
    if (entry.is_null()) return std::nullopt;
    std::string bytes;
    if (!entry.is_object() || !entry.contains("list") || !entry.at("list").is_string() || !entry.contains("signature") ||
        !entry.at("signature").is_string() || !entry.contains("url") || !entry.at("url").is_string() ||
        !SignedFileUrl(entry.at("url").get<std::string>()) || !DecodeBase64(entry.at("list").get<std::string>(), bytes))
    {
        problem = "test launcher: malformed";
        return std::nullopt;
    }
    const auto result = VerifyManifest("launcher/test", bytes, entry.at("signature").get<std::string>(), keys, minKeyId);
    if (!result.ok()) { problem = std::string("test launcher: ") + SignatureCheckName(result.check); return std::nullopt; }
    NoteSigningKey(result.keyId);
    try { return ParseTestLauncherInfo(Json::parse(bytes), entry.at("url").get<std::string>(), std::string(OriginXHost) + "/file/"); }
    catch (const std::exception&) { problem = "test launcher: unreadable"; return std::nullopt; }
}

// The Worker hands out a channel's list with every file URL swapped for a 30-minute link, which no longer matches
// the signature. Next to it, signedChannels.<name> carries the list's bytes as stored in R2 (base64) and its .sig
// (design 2026-11-20 §3). The list is read from those bytes; only the link of the same version and file is taken
// from the rewritten one. Null when the signature or the pairing fails.
inline Json VerifiedChannelCatalog(const Json& rewritten, const Json* signedEntry, Channel channel,
                                   const std::vector<SigningKey>& keys, uint32_t minKeyId, std::string& problem)
{
    if (rewritten.is_null()) return nullptr;
    const std::string kind = std::string("catalog/") + ChannelKey(channel);
    std::string bytes, sig;
    if (!signedEntry || !signedEntry->is_object() || !signedEntry->contains("catalog") || !signedEntry->at("catalog").is_string() ||
        !signedEntry->contains("signature") || !signedEntry->at("signature").is_string() ||
        !DecodeBase64(signedEntry->at("catalog").get<std::string>(), bytes))
    {
        problem = std::string(ChannelKey(channel)) + ": " + (signedEntry ? "malformed" : "missing");
        return nullptr;
    }
    const auto result = VerifyManifest(kind, bytes, signedEntry->at("signature").get<std::string>(), keys, minKeyId);
    if (!result.ok()) { problem = std::string(ChannelKey(channel)) + ": " + SignatureCheckName(result.check); return nullptr; }
    NoteSigningKey(result.keyId);
    Json list = Json::parse(bytes, nullptr, false);
    if (list.is_discarded() || !list.is_object() || !list.contains("versions") || !list.at("versions").is_array() ||
        !rewritten.contains("versions") || !rewritten.at("versions").is_array())
    {
        problem = std::string(ChannelKey(channel)) + ": unreadable";
        return nullptr;
    }
    for (auto& version : list.at("versions"))
    {
        if (!version.is_object() || !version.contains("files") || !version.at("files").is_object()) continue;
        const Json* links = nullptr;
        for (const auto& other : rewritten.at("versions"))
            if (other.is_object() && other.value("id", std::string{}) == version.value("id", std::string{}) &&
                other.value("revision", Json()) == version.value("revision", Json()) && other.contains("files") && other.at("files").is_object())
                links = &other.at("files");
        for (auto& file : version.at("files").items())
        {
            if (!links || !links->contains(file.key()) || !links->at(file.key()).is_object() ||
                !links->at(file.key()).contains("url") || !links->at(file.key()).at("url").is_string())
            {
                problem = std::string(ChannelKey(channel)) + ": no link for a signed file";
                return nullptr;
            }
            file.value()["url"] = links->at(file.key()).at("url");
        }
    }
    return list;
}
inline AccountStatus ParseStatus(const Json& body, const std::vector<SigningKey>& keys = EmbeddedSigningKeys())
{
    AccountStatus status;
    try
    {
        const auto& user = body.at("user");
        status.id = user.at("id").get<std::string>();
        if (status.id.empty() || status.id.size() > 32) throw std::runtime_error("");
        if (user.contains("name") && user.at("name").is_string()) status.name = user.at("name").get<std::string>();
        if (status.name.size() > 128) status.name.resize(128);
        if (user.contains("avatarUrl") && user.at("avatarUrl").is_string() && AvatarUrl(user.at("avatarUrl").get<std::string>()))
            status.avatarUrl = user.at("avatarUrl").get<std::string>();   // a link that is not Discord's icon is dropped
        const auto& roles = body.at("roles");
        status.verified = roles.at("verified").get<bool>();
        status.supporter = roles.at("supporter").get<bool>();
        status.moderator = roles.at("moderator").get<bool>();
        const auto& channels = body.at("channels");
        status.early = channels.at("early");
        status.test = channels.at("test");
        if ((!status.early.is_null() && !status.early.is_object()) || (!status.test.is_null() && !status.test.is_object()))
            throw std::runtime_error("");
        if (SignaturesRequired(keys))
        {
            const Json* signedChannels = body.contains("signedChannels") && body.at("signedChannels").is_object() ? &body.at("signedChannels") : nullptr;
            auto entry = [&](const char* name) -> const Json* {
                return signedChannels && signedChannels->contains(name) ? &signedChannels->at(name) : nullptr;
            };
            std::string problem;
            status.early = VerifiedChannelCatalog(status.early, entry("early"), Channel::Early, keys, MinKeyFor("catalog/early"), problem);
            status.test = VerifiedChannelCatalog(status.test, entry("test"), Channel::Test, keys, MinKeyFor("catalog/test"), problem);
            if (!status.test.is_null() && body.contains("testLauncher"))
                status.testLauncher = VerifiedTestLauncher(body.at("testLauncher"), keys, MinKeyFor("launcher/test"), problem);
            status.signatureProblem = problem;
        }
        if (body.contains("token") && body.at("token").is_string() && body.at("token").get<std::string>().size() <= 2048)
            status.token = body.at("token").get<std::string>();
    }
    catch (const std::exception&)
    {
        throw std::runtime_error(Lang::T("サーバーの応答を読み取れませんでした。", "Could not read the server's response."));
    }
    return status;
}
inline std::vector<Channel> AllowedChannels(const AccountStatus& status)
{
    std::vector<Channel> allowed{Channel::Stable};
    if (!status.early.is_null()) allowed.push_back(Channel::Early);
    if (!status.test.is_null()) allowed.push_back(Channel::Test);
    return allowed;
}

// What is kept on this PC (account.json): the login key, and the name and icon link last seen.
// verifiedAt: when the server last said this account has the member role (Unix seconds, 0 = never).
struct StoredAccount { std::string token, name, avatarUrl; long long verifiedAt = 0; };
inline std::string SerializeAccount(const StoredAccount& account)
{
    return Json{{"token", account.token}, {"name", account.name}, {"avatarUrl", account.avatarUrl},
                {"verifiedAt", account.verifiedAt}}.dump(2) + "\n";
}
inline StoredAccount ParseAccount(const std::string& text)
{
    StoredAccount account;
    const Json doc = Json::parse(text, nullptr, false);
    if (!doc.is_object()) return account;
    const auto field = [&](const char* key) { return doc.contains(key) && doc.at(key).is_string() ? doc.at(key).get<std::string>() : std::string{}; };
    account.token = field("token");
    account.name = field("name");
    account.avatarUrl = field("avatarUrl");
    if (doc.contains("verifiedAt") && doc.at("verifiedAt").is_number_integer()) account.verifiedAt = doc.at("verifiedAt").get<long long>();
    return account;
}

// Who may start the game (2026-11-12 policy): someone logged in with Discord who has the member role of the
// OriginX server, the same role the site's download needs. When the server cannot be asked right now (no network,
// an outage), a member check that succeeded within the last MemberGraceSeconds is enough; an expired login is not.
// Returns "" when the game may start, else the reason shown to the user.
inline constexpr long long MemberGraceSeconds = 7LL * 24 * 60 * 60;
inline std::string MemberGate(bool loggedIn, const AccountStatus* status, const std::string& error, long long verifiedAt, long long now)
{
    if (!loggedIn) return Lang::T("起動するには Discord でログインしてください（OriginX の Discord サーバーで認証した member の人だけが使えます）。", "Log in with Discord to launch (only members verified on the OriginX Discord server can use it).");
    if (status) return status->verified ? std::string() :
        Lang::T("このアカウントには member ロールがありません。OriginX の Discord サーバーで認証してから、もう一度起動してください。", "This account does not have the member role. Get verified on the OriginX Discord server, then launch again.");
    if (error.find(Lang::T("期限が切れました", "expired")) != std::string::npos)
        return Lang::T("ログインの期限が切れました。Discord でログインし直してください。", "Your login has expired. Log in with Discord again.");
    if (verifiedAt > 0 && now >= verifiedAt && now - verifiedAt <= MemberGraceSeconds) return {};
    return Lang::T("member ロールを確認できませんでした（", "Could not check the member role (") + (error.empty() ? std::string(Lang::T("サーバーにつながりません", "cannot reach the server")) : error) +
           Lang::T("）。インターネットにつながっているか確かめて、もう一度起動してください。", "). Check your internet connection and launch again.");
}

// The browser's return after the Discord login: the request line of
//   GET /callback?state=<S>&token=<T> HTTP/1.1  (sent to http://127.0.0.1:<port>, see AccountWin.h LoginInBrowser).
// Gives the token only when the state is the one this login started with (another tab's or a forged return is
// ignored) and the token has the plain v1.<payload>.<sig> shape; nothing else gets through to the saved file.
inline std::optional<std::string> LoginToken(const std::string& requestLine, const std::string& state)
{
    if (state.empty() || requestLine.compare(0, 4, "GET ") != 0) return std::nullopt;
    const auto space = requestLine.find(' ', 4);
    if (space == std::string::npos || requestLine.compare(space + 1, 5, "HTTP/") != 0) return std::nullopt;
    const std::string target = requestLine.substr(4, space - 4), path = "/callback?";
    if (target.compare(0, path.size(), path) != 0) return std::nullopt;
    const auto decode = [](const std::string& text) -> std::optional<std::string> {
        std::string out;
        for (size_t i = 0; i < text.size(); ++i)
        {
            if (text[i] != '%') { out += text[i]; continue; }
            if (i + 2 >= text.size()) return std::nullopt;
            const auto hex = [](char c) { return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1; };
            const int hi = hex(text[i + 1]), lo = hex(text[i + 2]);
            if (hi < 0 || lo < 0) return std::nullopt;
            out += static_cast<char>(hi * 16 + lo);
            i += 2;
        }
        return out;
    };
    std::optional<std::string> gotState, gotToken;
    const std::string query = target.substr(path.size());
    for (size_t at = 0; at <= query.size();)
    {
        const auto amp = query.find('&', at);
        const std::string pair = query.substr(at, amp == std::string::npos ? std::string::npos : amp - at);
        const auto eq = pair.find('=');
        if (eq != std::string::npos)
        {
            const std::string key = pair.substr(0, eq);
            const auto value = decode(pair.substr(eq + 1));
            if (key == "state") gotState = value;
            else if (key == "token") gotToken = value;
        }
        if (amp == std::string::npos) break;
        at = amp + 1;
    }
    if (!gotState || !gotToken || *gotState != state) return std::nullopt;
    const std::string& token = *gotToken;
    if (token.size() < 4 || token.size() > 2048 || token.compare(0, 3, "v1.") != 0 ||
        token.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-._") != std::string::npos)
        return std::nullopt;
    return token;
}

// What Launch does with the chosen channel: the channel to take the DLL / HUD from, and the reason when it had to
// be the stable one. Nothing here stops the launch.
struct LaunchPlan {
    Channel channel = Channel::Stable;
    const Json* catalog = nullptr;       // points into the AccountStatus given to PlanLaunch (keep it alive)
    std::optional<Release> release;      // the chosen game version's build in that channel
    std::string reason;
};
// `stable` is the stable release of the same game version: the launcher prepares the loader and runtime from it
// (RequireRuntime), so a channel build that needs another loader / runtime cannot be run by this launcher and
// is not used (Launch falls back to stable instead of stopping). A standalone build needs neither and is used.
inline LaunchPlan PlanLaunch(Channel wanted, const AccountStatus* status, const std::string& error, const std::string& gameId,
                             const Release* stable = nullptr)
{
    LaunchPlan plan;
    if (wanted == Channel::Stable) return plan;
    const std::string name = ChannelName(wanted);
    const auto stable_ = [&](const std::string& why) {
        LaunchPlan fallback;
        fallback.reason = name + Lang::T("を取得できなかったため、安定版で起動します。理由: ", " could not be fetched, so launching on Stable. Reason: ") + why;
        return fallback;
    };
    if (!status) return stable_(error.empty() ? Lang::T("ログインしていません。Discord でログインしてください。", "Not logged in. Log in with Discord.") : error);
    const Json* catalog = wanted == Channel::Early ? &status->early : &status->test;
    if (catalog->is_null()) return stable_(Lang::T("このアカウントには", "This account is not allowed to use ") + name + Lang::T("を使う権限がありません。", "."));
    Catalog parsed;
    try { parsed = ParseCatalog(*catalog, SignedFileUrl); }
    catch (const std::exception&) { return stable_(name + Lang::T("の一覧を読み取れませんでした。", ": could not read the list.")); }
    for (const auto& release : parsed.releases)
        if (release.target.id == gameId && release.available)
        {
            // A standalone build (launcherApi 2) needs no loader or runtime, so it never depends on the stable one's.
            if (stable && !release.target.standalone &&
                (release.target.loader != stable->target.loader || release.target.runtimeGame != stable->target.runtimeGame))
                return stable_(name + " " + gameId + Lang::T(" のローダー版が安定版と異なるため、このランチャーでは使えません。", " uses a different loader version from Stable, so this launcher cannot use it."));
            plan.channel = wanted;
            plan.catalog = catalog;
            plan.release = release;
            return plan;
        }
    return stable_(name + Lang::T("に ", " has no build for ") + gameId + Lang::T(" の配布がありません。", "."));
}

// The folder name inside the package cache (Updates.h AcquirePackage `folder`): stable keeps its revision folder,
// a channel's build gets its own, so a test build of revision 54 never meets the stable release 54.
// The name carries the first 4 hex digits of every file's SHA256 (files in name order), so two builds of one
// revision that differ in any file (a HUD-only change keeps the DLL's hash) never share a folder.
inline std::string PackageFolder(Channel channel, const Release& release)
{
    if (channel == Channel::Stable) return {};
    std::string digest;
    for (const auto& file : release.files.items())
        if (file.value().contains("sha256") && file.value().at("sha256").is_string())
            digest += file.value().at("sha256").get<std::string>().substr(0, 4);
    return std::string(ChannelKey(channel)) + "-r" + std::to_string(release.revision) + "-" + digest;
}

// A login or status result that started before the user logged out must not bring the account back: every
// background result carries the epoch it started in, and Logout() invalidates it.
struct AccountEpoch {
    unsigned value = 0;
    unsigned Now() const { return value; }
    void Invalidate() { ++value; }
    bool Current(unsigned started) const { return started == value; }
};
}
