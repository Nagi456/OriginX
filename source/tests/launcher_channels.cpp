// Release channels in the launcher (docs/superpowers/specs/2026-11-09-release-channels-design.md): the pure logic.
// g++ -std=c++17 -Wall -Wextra -Werror -I. -Ilauncher/vendor tests/launcher_channels.cpp launcher/vendor/tinyxml2/tinyxml2.cpp -o /tmp/myclient-channels && /tmp/myclient-channels
#include "launcher/Channels.h"
#include <cassert>
#include <iostream>
using namespace MyClientLauncher;

static const std::string Host = "https://originx.originx-client.workers.dev";
static const std::string Hash(64, 'a');

std::string Signed(const std::string& key) { return Host + "/file/" + key + "?exp=1790000000&sig=Ab-_9z"; }

Json Entry(const std::string& id, const std::string& channel, const std::string& tag, bool available = true, bool signedUrls = true)
{
    Json files = Json::object();
    for (const std::string name : {"runtime.lock.json", "mods/MyClientData/MyClientData.dll", "mods/MyClientData/manifest.json"})
    {
        std::string asset = name;
        for (auto& c : asset) if (c == '/') c = '_';
        const std::string key = channel + "/" + tag + "/" + asset;
        files[name] = {{"url", signedUrls ? Signed(key) : Host + "/dl/stable/" + key}, {"sha256", Hash}, {"bytes", 10}};
    }
    return {{"id", id}, {"available", available}, {"loader", "26.51.6"}, {"revision", 54}, {"launcherApi", 1},
            {"bridgeAbi", 2}, {"platform", "gdk-release-x64"}, {"files", files}};
}
// parsed from text like the Worker's answer is (a number built in code is signed, a parsed one unsigned: PositiveNumber wants that)
Json List(const Json& entry) { return Json::parse(Json{{"schemaVersion", 1}, {"sequence", 3}, {"versions", Json::array({entry})}}.dump()); }

Json Body(bool moderator, bool supporter, bool verified, const Json& early, const Json& test)
{
    return {{"user", {{"id", "42"}, {"name", "Nagi"}, {"avatarUrl", nullptr}}},
            {"roles", {{"verified", verified}, {"supporter", supporter}, {"moderator", moderator}}},
            {"channels", {{"early", early}, {"test", test}}}};
}

void Reject(const std::function<void()>& action)
{
    bool rejected = false;
    try { action(); } catch (const std::exception&) { rejected = true; }
    assert(rejected);
}

int main()
{
    // --- channel names ---
    assert(std::string(ChannelKey(Channel::Stable)) == "stable" && std::string(ChannelKey(Channel::Early)) == "early" &&
           std::string(ChannelKey(Channel::Test)) == "test");
    assert(ParseChannel("test") == Channel::Test && ParseChannel("early") == Channel::Early && ParseChannel("stable") == Channel::Stable);
    assert(ParseChannel("") == Channel::Stable && ParseChannel("beta") == Channel::Stable && ParseChannel("TEST ") == Channel::Stable);

    // --- signed file links (the only non-public URLs the launcher downloads from) ---
    assert(SignedFileUrl(Signed("test/myclient-26.51.1-r54-test1/runtime.lock.json")));
    assert(SignedFileUrl(Host + "/file/early/x/y.dll?exp=1&sig=a"));
    for (const auto& bad : std::vector<std::string>{
             "http://originx.originx-client.workers.dev/file/test/x?exp=1&sig=a",           // not HTTPS
             "https://evil.example/file/test/x?exp=1&sig=a",                                 // another host
             "https://originx.originx-client.workers.dev.evil.example/file/test/x?exp=1&sig=a",
             Host + "/dl/stable/x?exp=1&sig=a",                                              // not a signed file link
             Host + "/file/test/../stable/x?exp=1&sig=a",                                    // path tricks
             Host + "/file/test/x?exp=abc&sig=a",                                            // exp is not a number
             Host + "/file/test/x?exp=1&sig=a%2F",                                           // sig with symbols
             Host + "/file/test/x?exp=1&sig=a&extra=1",
             Host + "/file/test/x",                                                          // unsigned
             Host + "/file/?exp=1&sig=a", ""})
        assert(!SignedFileUrl(bad));

    // --- avatar links ---
    const std::string hash(32, 'c');
    assert(AvatarUrl("https://cdn.discordapp.com/avatars/42/" + hash + ".png?size=64"));
    assert(AvatarUrl("https://cdn.discordapp.com/avatars/42/a_" + hash + ".png?size=64"));          // animated: still picture as .png
    assert(AvatarUrl("https://cdn.discordapp.com/guilds/100/users/42/avatars/" + hash + ".png?size=64"));
    for (const auto& bad : std::vector<std::string>{
             "http://cdn.discordapp.com/avatars/42/" + hash + ".png?size=64",
             "https://evil.example/avatars/42/" + hash + ".png?size=64",
             "https://cdn.discordapp.com.evil.example/avatars/42/" + hash + ".png?size=64",
             "https://cdn.discordapp.com/avatars/42/" + hash + ".png?size=128",
             "https://cdn.discordapp.com/avatars/42/" + hash + ".gif?size=64",
             "https://cdn.discordapp.com/avatars/42/" + hash + ".png",
             "https://cdn.discordapp.com/avatars/42/../x.png?size=64",
             "https://cdn.discordapp.com/attachments/1/2/x.png?size=64",
             "https://cdn.discordapp.com/avatars/42/" + std::string(31, 'c') + ".png?size=64", ""})
        assert(!AvatarUrl(bad));
    assert(MaxAvatarBytes == 128 * 1024);
    assert(LooksLikePng(std::string("\x89PNG\r\n\x1a\n", 8) + "rest"));
    assert(!LooksLikePng("GIF89a....") && !LooksLikePng("") && !LooksLikePng(std::string("\x89PNG", 4)));

    // --- the answer of /launcher/status ---
    const Json test = List(Entry("26.51.1", "test", "myclient-26.51.1-r54-test1"));
    const Json early = List(Entry("26.51.1", "early", "myclient-26.51.1-r54-test1"));
    {
        auto status = ParseStatus(Body(true, false, false, early, test));
        assert(status.id == "42" && status.name == "Nagi" && status.avatarUrl.empty() && status.token.empty());
        assert(status.moderator && !status.supporter && !status.verified);
        const auto allowed = AllowedChannels(status);
        assert((allowed == std::vector<Channel>{Channel::Stable, Channel::Early, Channel::Test}));
    }
    {
        auto status = ParseStatus(Body(false, true, true, early, nullptr));
        assert((AllowedChannels(status) == std::vector<Channel>{Channel::Stable, Channel::Early}));
        status = ParseStatus(Body(false, false, true, nullptr, nullptr));
        assert((AllowedChannels(status) == std::vector<Channel>{Channel::Stable}));
    }
    {
        Json body = Body(false, true, true, early, nullptr);
        body["user"]["avatarUrl"] = "https://cdn.discordapp.com/avatars/42/" + hash + ".png?size=64";
        body["token"] = "v1.payload.sig";
        const auto status = ParseStatus(body);
        assert(status.avatarUrl == body["user"]["avatarUrl"].get<std::string>() && status.token == "v1.payload.sig");
        body["user"]["avatarUrl"] = "https://evil.example/x.png";                    // an unsafe icon link is dropped, not trusted
        assert(ParseStatus(body).avatarUrl.empty());
        body["user"]["name"] = nullptr;
        assert(ParseStatus(body).name.empty());
    }
    Reject([] { ParseStatus(Json::object()); });
    Reject([] { ParseStatus(Json{{"user", {{"id", "42"}}}}); });
    Reject([] { ParseStatus(Json{{"error", "login"}}); });

    // --- a catalog with signed links is read only when the caller says so ---
    assert(ParseCatalog(test, SignedFileUrl).releases.size() == 1);
    Reject([&] { ParseCatalog(test); });                                       // the default accepts only public release urls
    assert(ParseCatalog(List(Entry("26.51.1", "stable", "x", true, false))).releases.size() == 1);   // ... and still does

    // --- what Launch does with the chosen channel ---
    {
        const auto status = ParseStatus(Body(true, false, false, early, test));
        auto plan = PlanLaunch(Channel::Stable, nullptr, "", "26.51.1");
        assert(plan.channel == Channel::Stable && plan.catalog == nullptr && plan.reason.empty() && !plan.release);
        plan = PlanLaunch(Channel::Test, &status, "", "26.51.1");
        assert(plan.channel == Channel::Test && plan.catalog == &status.test && plan.reason.empty());
        assert(plan.release && plan.release->target.id == "26.51.1" && plan.release->revision == 54);
        plan = PlanLaunch(Channel::Early, &status, "", "26.51.1");
        assert(plan.channel == Channel::Early && plan.catalog == &status.early && plan.release);
    }
    {   // not logged in, token expired, Discord down: the reason is shown and stable is used
        auto plan = PlanLaunch(Channel::Test, nullptr, "ログインの期限が切れました。もう一度 Discord でログインしてください。", "26.51.1");
        assert(plan.channel == Channel::Stable && !plan.catalog && !plan.release);
        assert(plan.reason == "テスト版を取得できなかったため、安定版で起動します。理由: ログインの期限が切れました。もう一度 Discord でログインしてください。");
        plan = PlanLaunch(Channel::Early, nullptr, "", "26.51.1");
        assert(plan.channel == Channel::Stable && plan.reason.find("先行版を取得できなかったため、安定版で起動します。") == 0);
        assert(plan.reason.find("Discord でログイン") != std::string::npos);
    }
    {   // the role was taken away (no catalog for the channel)
        const auto status = ParseStatus(Body(false, true, true, early, nullptr));
        const auto plan = PlanLaunch(Channel::Test, &status, "", "26.51.1");
        assert(plan.channel == Channel::Stable && !plan.catalog && plan.reason.find("権限") != std::string::npos);
    }
    {   // the channel has no build of the chosen game version
        const auto status = ParseStatus(Body(true, false, false, early, test));
        const auto plan = PlanLaunch(Channel::Test, &status, "", "26.60.5");
        assert(plan.channel == Channel::Stable && !plan.release && plan.reason.find("26.60.5") != std::string::npos);
    }
    {   // a catalog with urls that are not signed file links of this host is not used
        const Json bad = List(Entry("26.51.1", "test", "myclient-26.51.1-r54-test1", true, false));
        const auto status = ParseStatus(Body(true, false, false, nullptr, bad));
        const auto plan = PlanLaunch(Channel::Test, &status, "", "26.51.1");
        assert(plan.channel == Channel::Stable && !plan.release && !plan.reason.empty());
    }
    {   // an entry that is listed but not available is not a build
        const Json off = List(Entry("26.51.1", "test", "myclient-26.51.1-r54-test1", false));
        const auto status = ParseStatus(Body(true, false, false, nullptr, off));
        assert(PlanLaunch(Channel::Test, &status, "", "26.51.1").channel == Channel::Stable);
    }

    // --- the package cache keeps a channel's build apart from the stable release of the same revision ---
    {
        const auto status = ParseStatus(Body(true, false, false, early, test));
        const auto plan = PlanLaunch(Channel::Test, &status, "", "26.51.1");
        assert(PackageFolder(Channel::Stable, *plan.release).empty());
        const auto folder = PackageFolder(Channel::Test, *plan.release);
        assert(folder == "test-r54-" + Hash.substr(0, 12));
        assert(PackageFolder(Channel::Early, *plan.release) == "early-r54-" + Hash.substr(0, 12));
    }

    // --- the browser's return after the Discord login (http://127.0.0.1:<port>/callback?state=..&token=..) ---
    {
        const std::string state(32, 'a');
        const auto ok = LoginToken("GET /callback?state=" + state + "&token=v1.pay-load_9.sig HTTP/1.1", state);
        assert(ok && *ok == "v1.pay-load_9.sig");
        assert(LoginToken("GET /callback?token=v1.x.y&state=" + state + " HTTP/1.1", state));          // any order
        assert(LoginToken("GET /callback?state=" + state + "&token=v1.a%2Eb.c HTTP/1.1", state).value_or("") == "v1.a.b.c");
        assert(!LoginToken("GET /callback?state=" + std::string(32, 'b') + "&token=v1.x.y HTTP/1.1", state));   // another login's state
        assert(!LoginToken("GET /callback?token=v1.x.y HTTP/1.1", state));                                       // no state
        assert(!LoginToken("GET /callback?state=" + state + " HTTP/1.1", state));                                // no token
        assert(!LoginToken("GET /other?state=" + state + "&token=v1.x.y HTTP/1.1", state));                      // another path
        assert(!LoginToken("POST /callback?state=" + state + "&token=v1.x.y HTTP/1.1", state));
        assert(!LoginToken("GET /callback?state=" + state + "&token=v1.x%0D%0Ay HTTP/1.1", state));              // header-splitting bytes
        assert(!LoginToken("GET /callback?state=" + state + "&token=v1.x%ZZ HTTP/1.1", state));                  // bad escape
        assert(!LoginToken("GET /callback?state=" + state + "&token=plain HTTP/1.1", state));                    // not a v1 token
        assert(!LoginToken("GET /callback?state=" + state + "&token=v1." + std::string(3000, 'a') + " HTTP/1.1", state));
        assert(!LoginToken("", state) && !LoginToken("GET", state) && !LoginToken("GET /callback?state=" + state + "&token=v1.x.y", state));
        assert(!LoginToken("GET /callback?state=&token=v1.x.y HTTP/1.1", ""));                                   // an empty state never matches
    }

    // --- Final review fix 9: builds of one revision that differ only in the HUD must not share a package folder ---
    {
        const auto release = [](char dll, char hud) {
            Release r;
            r.revision = 54;
            r.files = Json::object();
            r.files["mods/MyClientData/MyClientData.dll"] = {{"sha256", std::string(64, dll)}};
            r.files["hud/MyClient.exe"] = {{"sha256", std::string(64, hud)}};
            return r;
        };
        const auto a = PackageFolder(Channel::Test, release('a', 'b'));
        assert(a == PackageFolder(Channel::Test, release('a', 'b')));                  // the same build, the same folder
        assert(a != PackageFolder(Channel::Test, release('a', 'c')));                  // only the HUD differs
        assert(a != PackageFolder(Channel::Test, release('d', 'b')));                  // only the DLL differs
        assert(a != PackageFolder(Channel::Early, release('a', 'b')));
        assert(a.size() < 40);
        assert(PackageFolder(Channel::Stable, release('a', 'b')).empty());
    }

    // --- Final review fix 2: a channel build the launcher cannot run with the stable target is not used ---
    {
        const auto stableRelease = ParseCatalog(List(Entry("26.51.1", "stable", "x", true, false)), [](const std::string&) { return true; }).releases[0];
        Json entry = Entry("26.51.1", "test", "myclient-26.51.1-r54-test1");
        const auto plan = [&](const Json& e) {
            const auto status = ParseStatus(Body(true, false, false, nullptr, List(e)));
            return PlanLaunch(Channel::Test, &status, "", "26.51.1", &stableRelease);
        };
        assert(plan(entry).channel == Channel::Test && plan(entry).release);                // the same loader: used
        entry["loader"] = "26.60.0";                                                          // another loader / runtime
        const auto other = plan(entry);
        assert(other.channel == Channel::Stable && !other.release && other.reason.find("ローダー") != std::string::npos);
        assert(other.reason.find("テスト版を取得できなかったため、安定版で起動します。") == 0);
    }

    // --- Final review fix 3: a result that started before a logout is not adopted ---
    {
        AccountEpoch epoch;
        const unsigned started = epoch.Now();
        assert(epoch.Current(started));
        epoch.Invalidate();                                       // the user logged out
        assert(!epoch.Current(started));
        assert(epoch.Current(epoch.Now()));                       // a login after that is current
    }

    // --- what is kept on this PC ---
    {
        StoredAccount account{"v1.payload.sig", "Nagi", "https://cdn.discordapp.com/avatars/42/" + hash + ".png?size=64"};
        const auto back = ParseAccount(SerializeAccount(account));
        assert(back.token == account.token && back.name == account.name && back.avatarUrl == account.avatarUrl);
        assert(ParseAccount("").token.empty() && ParseAccount("{").token.empty() && ParseAccount("[1]").token.empty());
        assert(ParseAccount("{\"token\": 5}").token.empty());
        assert(SerializeAccount(account).find("v1.payload.sig") != std::string::npos);   // (the file, not a log)
    }
    std::cout << "launcher channels: ok\n";
    return 0;
}
