// g++ -std=c++17 -Wall -Wextra -Werror -I. -Ilauncher/vendor tests/manifest_signature.cpp -o /tmp/manifest-signature
#include "../launcher/Channels.h"
#include <cstdio>
#include <cstdlib>

using namespace MyClientLauncher;

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #cond); ++failures; } } while (0)

static std::string Hex(const char* hex)
{
    std::string out;
    for (size_t i = 0; hex[i] && hex[i + 1]; i += 2) out += static_cast<char>(std::strtoul(std::string(hex + i, 2).c_str(), nullptr, 16));
    return out;
}

int main()
{
    // RFC 8032 §7.1 test 1 (empty message): the primitive is the standard Ed25519 (SHA-512), not Monocypher's
    // BLAKE2b variant.
    {
        const std::string pub = Hex("d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a");
        const std::string sig = Hex("e5564300c360ac729086e2cc806e828a84877f1eb8e5d974d873e065224901555fb8821590a33bacc61e39701cf9b46bd25bf5f0595bbe24655141438e7a100b");
        CHECK(crypto_ed25519_check(reinterpret_cast<const uint8_t*>(sig.data()), reinterpret_cast<const uint8_t*>(pub.data()), nullptr, 0) == 0);
        std::string bad = sig; bad[0] ^= 1;
        CHECK(crypto_ed25519_check(reinterpret_cast<const uint8_t*>(bad.data()), reinterpret_cast<const uint8_t*>(pub.data()), nullptr, 0) != 0);
    }

    // A signature made by tools/signing.py (key seed 00..1f, key id 1) over catalog/stable.
    const SigningKey k1{1, {0x03, 0xa1, 0x07, 0xbf, 0xf3, 0xce, 0x10, 0xbe, 0x1d, 0x70, 0xdd, 0x18, 0xe7, 0x4b, 0xc0, 0x99,
                            0x67, 0xe4, 0xd6, 0x30, 0x9b, 0xa5, 0x0d, 0x5f, 0x1d, 0xdc, 0x86, 0x64, 0x12, 0x55, 0x31, 0xb8}};
    SigningKey k2 = k1; k2.id = 2; k2.publicKey[5] ^= 0x40;
    SigningKey t1 = k1; t1.id = 101;   // the same key pair as a test key: only the id says which table
    const std::vector<SigningKey> keys{k1, k2, t1};
    const std::string list = "{\"schemaVersion\":1,\"sequence\":7}\n";
    const std::string sig = "{\"schemaVersion\": 1, \"keyId\": 1, \"signature\": \"vC/06qGUFoMLhmkenonD8Gd+LN9L9PfDZMQgi4ntzikkIchcMlxT+U/zdt2r/g4ZYjHEmF8PONR31Zx4BeOYAA==\"}\n";

    auto r = VerifyManifest("catalog/stable", list, sig, keys, 1);
    CHECK(r.ok() && r.keyId == 1);
    // one byte of the list changed
    CHECK(VerifyManifest("catalog/stable", "{\"schemaVersion\":1,\"sequence\":8}\n", sig, keys, 1).check == SignatureCheck::Mismatch);
    // the same signature moved to another use
    // a stable key is not taken for catalog/test, a test key not for any other kind
    CHECK(VerifyManifest("catalog/test", list, sig, keys, 101).check == SignatureCheck::UnknownKey);
    {
        std::string tsig = sig; tsig.replace(tsig.find("\"keyId\": 1"), 10, "\"keyId\": 101");
        CHECK(VerifyManifest("catalog/stable", list, tsig, keys, 1).check == SignatureCheck::UnknownKey);
        CHECK(VerifyManifest("catalog/early", list, tsig, keys, 1).check == SignatureCheck::UnknownKey);
        CHECK(VerifyManifest("launcher/test", list, tsig, keys, 1).check == SignatureCheck::UnknownKey);
    }
    CHECK(VerifyManifest("launcher/stable", list, sig, keys, 1).check == SignatureCheck::Mismatch);
    CHECK(VerifyManifest("catalog/nothing", list, sig, keys, 1).check == SignatureCheck::Malformed);
    // no .sig
    CHECK(VerifyManifest("catalog/stable", list, "", keys, 1).check == SignatureCheck::Missing);
    // a key the launcher does not know
    CHECK(VerifyManifest("catalog/stable", list, sig, {k2}, 1).check == SignatureCheck::UnknownKey);
    CHECK(VerifyManifest("catalog/stable", list, sig, {}, 1).check == SignatureCheck::UnknownKey);
    // K1 refused once K2 has been seen (§5)
    CHECK(VerifyManifest("catalog/stable", list, sig, keys, 2).check == SignatureCheck::RetiredKey);
    // the right key id but another public key
    {
        SigningKey other = k2; other.id = 1;
        CHECK(VerifyManifest("catalog/stable", list, sig, {other}, 1).check == SignatureCheck::Mismatch);
    }
    // broken .sig files
    for (const char* broken : {"not json", "[]", "{\"schemaVersion\":2,\"keyId\":1,\"signature\":\"AA==\"}",
                               "{\"schemaVersion\":1,\"keyId\":0,\"signature\":\"AA==\"}",
                               "{\"schemaVersion\":1,\"keyId\":-1,\"signature\":\"AA==\"}",
                               "{\"schemaVersion\":1,\"keyId\":1,\"signature\":\"AA==\"}",
                               "{\"schemaVersion\":1,\"keyId\":1,\"signature\":\"vC/06qGUFoMLhmkenonD8Gd+LN9L9PfDZMQgi4ntzikkIchcMlxT+U/zdt2r/g4ZYjHEmF8PONR31Zx4BeOYAA=\"}",
                               "{\"schemaVersion\":1,\"keyId\":1,\"signature\":\"vC/06qGUFoMLhmkenonD8Gd+LN9L9PfDZMQgi4ntzikkIchcMlxT+U/zdt2r/g4ZYjHEmF8PONR31Zx4BeOYA*==\"}",
                               "{\"schemaVersion\":1,\"keyId\":\"1\",\"signature\":\"AA==\"}"})
        CHECK(VerifyManifest("catalog/stable", list, broken, keys, 1).check == SignatureCheck::Malformed);

    // A channel list from /launcher/status: read from the signed bytes, links taken from the rewritten list.
    {
        const std::string b64 = "eyJzY2hlbWFWZXJzaW9uIjoxLCJzZXF1ZW5jZSI6MywidmVyc2lvbnMiOlt7ImlkIjoiMjYuNTEuMSIsInJldmlzaW9uIjo1OCwiZmlsZXMiOnsiYS5kbGwiOnsidXJsIjoidGVzdC90L2EuZGxsIiwic2hhMjU2IjoiYWEiLCJieXRlcyI6MX19fV19";
        const std::string csig = "{\"schemaVersion\": 1, \"keyId\": 101, \"signature\": \"wzcFvn8qVpZRTNflAHM7ACgMML1rBv9W2WICBAIdF0L5EMfbka3dcunDDELv1ctLZMUc5fzpZSJL4OvW/KueCg==\"}";
        const Json rewritten = Json::parse(R"({"schemaVersion":1,"sequence":3,"versions":[{"id":"26.51.1","revision":58,"files":{"a.dll":{"url":"https://originx.originx-client.workers.dev/file/test/t/a.dll?exp=1&sig=x","sha256":"bb","bytes":9}}}]})");
        const Json entry{{"catalog", b64}, {"signature", csig}};
        std::string problem;
        Json got = VerifiedChannelCatalog(rewritten, &entry, Channel::Test, keys, 101, problem);
        CHECK(problem.empty() && got.is_object());
        if (got.is_object())
        {
            const auto& file = got["versions"][0]["files"]["a.dll"];
            CHECK(file["sha256"] == "aa" && file["bytes"] == 1);   // from the signed bytes, not the rewritten list
            CHECK(file["url"] == rewritten["versions"][0]["files"]["a.dll"]["url"]);
        }
        // the same signed list offered as early
        problem.clear();
        CHECK(VerifiedChannelCatalog(rewritten, &entry, Channel::Early, keys, 1, problem).is_null() && problem == "early: unknown key");
        // T1 revoked (revokedTestKeys raised the test mark to 102)
        problem.clear();
        CHECK(VerifiedChannelCatalog(rewritten, &entry, Channel::Test, keys, 102, problem).is_null() && problem == "test: retired key");
        // no signed copy next to the list
        problem.clear();
        CHECK(VerifiedChannelCatalog(rewritten, nullptr, Channel::Test, keys, 101, problem).is_null() && problem == "test: missing");
        // a signed file with no link of the same version and revision
        Json other = rewritten; other["versions"][0]["revision"] = 57;
        problem.clear();
        CHECK(VerifiedChannelCatalog(other, &entry, Channel::Test, keys, 101, problem).is_null() && !problem.empty());
        // not given to this account: stays null, no problem
        problem.clear();
        CHECK(VerifiedChannelCatalog(nullptr, &entry, Channel::Test, keys, 101, problem).is_null() && problem.empty());
        // ParseStatus drops a bad channel and says why; a launcher without keys takes it as before
        Json body = Json::parse(R"({"user":{"id":"1"},"roles":{"verified":true,"supporter":false,"moderator":true},"channels":{"early":null}})");
        body["channels"]["test"] = rewritten;
        body["signedChannels"] = Json{{"test", Json{{"catalog", b64}, {"signature", "{}"}}}};
        auto status = ParseStatus(body, keys);
        CHECK(status.test.is_null() && status.signatureProblem == "test: malformed");
        body["signedChannels"]["test"]["signature"] = csig;
        status = ParseStatus(body, keys);
        CHECK(status.test.is_object() && status.signatureProblem.empty());
        CHECK(ParseStatus(body, {}).test == rewritten);
    }

    // the mark of the newest key seen only goes up
    minSigningKeyId = 1;
    NoteSigningKey(2);
    NoteSigningKey(1);
    CHECK(minSigningKeyId.load() == 2);
    // the test table has its own mark
    NoteSigningKey(102);
    CHECK(minSigningKeyId.load() == 2 && minTestSigningKeyId.load() == 102 && MinKeyFor("catalog/test") == 102 && MinKeyFor("catalog/early") == 2);
    minSigningKeyId = 1; minTestSigningKeyId = 101;
    // test keys alone do not turn checking on
    CHECK(!SignaturesRequired({t1}) && SignaturesRequired({k1, t1}));

    std::string out;
    CHECK(DecodeBase64("TWFu", out) && out == "Man");
    CHECK(DecodeBase64("TWE=", out) && out == "Ma");
    CHECK(DecodeBase64("TQ==", out) && out == "M");
    CHECK(!DecodeBase64("TQ=", out) && !DecodeBase64("T===", out) && !DecodeBase64("TW=u", out));

    // The test channel's launcher (status testLauncher): signed as launcher/test by key 1, channel "test".
    {
        const std::string tlist = "{\"schemaVersion\":1,\"channel\":\"test\",\"build\":45,\"key\":\"test/launcher/45/MyClientLauncher.exe\",\"sha256\":\"" + std::string(64, 'a') + "\",\"bytes\":3}\n";
        const std::string tsig = "{\"schemaVersion\": 1, \"keyId\": 1, \"signature\": \"p/LL/rdKuiwo6GpKfiEw6+KoZKaNQRcXQv6L2B8eibXXe7gallprz5qjCjma3YO7rLwlboU3RXkNpC/MNXARAw==\"}\n";
        const std::string link = "https://originx.originx-client.workers.dev/file/test/launcher/45/MyClientLauncher.exe?exp=123&sig=abc_-";
        const auto entry = [&](const std::string& list, const std::string& url) {
            std::string encoded;
            static const char* abc = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
            size_t i = 0;
            for (; i + 2 < list.size(); i += 3)
            {
                const unsigned v = (static_cast<unsigned char>(list[i]) << 16) | (static_cast<unsigned char>(list[i + 1]) << 8) | static_cast<unsigned char>(list[i + 2]);
                encoded += {abc[v >> 18], abc[(v >> 12) & 63], abc[(v >> 6) & 63], abc[v & 63]};
            }
            if (list.size() - i == 1) { const unsigned v = static_cast<unsigned char>(list[i]) << 16; encoded += {abc[v >> 18], abc[(v >> 12) & 63], '=', '='}; }
            if (list.size() - i == 2) { const unsigned v = (static_cast<unsigned char>(list[i]) << 16) | (static_cast<unsigned char>(list[i + 1]) << 8); encoded += {abc[v >> 18], abc[(v >> 12) & 63], abc[(v >> 6) & 63], '='}; }
            return Json{{"list", encoded}, {"signature", tsig}, {"url", url}};
        };
        std::string problem;
        const auto info = VerifiedTestLauncher(entry(tlist, link), keys, 1, problem);
        CHECK(info && info->build == 45 && info->url == link && info->bytes == 3 && problem.empty());
        // the same bytes are not a stable launcher list (kind), a link to another file is refused, a changed list fails
        CHECK(!VerifyManifest("launcher/stable", tlist, tsig, keys, 1).ok());
        CHECK(!VerifiedTestLauncher(entry(tlist, "https://originx.originx-client.workers.dev/file/test/launcher/46/MyClientLauncher.exe?exp=1&sig=a"), keys, 1, problem));
        CHECK(problem == "test launcher: unreadable");
        std::string changed = tlist; changed[changed.find("45")] = '9';
        CHECK(!VerifiedTestLauncher(entry(changed, link), keys, 1, problem) && problem == "test launcher: mismatch");
        CHECK(!VerifiedTestLauncher(Json{{"list", 1}}, keys, 1, problem) && problem == "test launcher: malformed");
        CHECK(!VerifiedTestLauncher(nullptr, keys, 1, problem));
        // a list naming itself channel "test" in place of the stable one is refused by the stable parser
        bool refused = false;
        try { ParseLauncherInfo(Json::parse("{\"schemaVersion\":1,\"channel\":\"test\",\"build\":45,\"url\":\"https://originx.originx-client.workers.dev/dl/stable/launcher/45/MyClientLauncher.exe\",\"sha256\":\"" + std::string(64, 'a') + "\",\"bytes\":3}")); }
        catch (const std::exception&) { refused = true; }
        CHECK(refused);
    }

    if (failures) return 1;
    std::printf("PASS: manifest signatures (RFC 8032, kinds, keys, retired key, broken .sig)\n");
}
