// g++ -std=c++17 -O2 -Wall -Wextra -Werror -I. -Ilauncher/vendor tests/signer_core.cpp -o /tmp/signer-core
#include "../signer/SignerCore.h"
#include <cstdio>

using namespace OriginXSigner;

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #cond); ++failures; } } while (0)

int main()
{
    Seed seed{};
    for (int i = 0; i < 32; ++i) seed[i] = static_cast<uint8_t>(i);
    const PublicKey pub = PublicOf(seed);
    // the same key pair as tools/signing.py and tests/manifest_signature.cpp (seed 00..1f)
    CHECK(Hex(pub.data(), 32) == "03a107bff3ce10be1d70dd18e74bc09967e4d6309ba50d5f1ddc8664125531b8");
    CHECK(Fingerprint(pub) == "03a107bff3ce10be");
    CHECK(EmbedArgument(101, pub) == "101=03a107bff3ce10be1d70dd18e74bc09967e4d6309ba50d5f1ddc8664125531b8");
    CHECK(TableEntry(1, pub).rfind("{1, {0x03, 0xa1, 0x07,", 0) == 0 && TableEntry(1, pub).size() > 190);
    CHECK(TestKeyText(101, seed) == "101:AAECAwQFBgcICQoLDA0ODxAREhMUFRYXGBkaGxwdHh8=");

    // signatures: byte for byte tools/signing.py, and accepted by the launcher's check
    const std::string list = "{\"schemaVersion\":1,\"sequence\":7}\n";
    const std::string sig = SignList("catalog/stable", list, 1, seed);
    CHECK(sig == "{\"schemaVersion\": 1, \"keyId\": 1, \"signature\": \"vC/06qGUFoMLhmkenonD8Gd+LN9L9PfDZMQgi4ntzikkIchcMlxT+U/zdt2r/g4ZYjHEmF8PONR31Zx4BeOYAA==\"}\n");
    CHECK(SignList("catalog/test", "{\"schemaVersion\":1,\"sequence\":3,\"versions\":[]}\n", 101, seed) ==
          "{\"schemaVersion\": 1, \"keyId\": 101, \"signature\": \"ZFINddlPbjTt/PY+J0N3/n/t++EXqmU5/Ye/idLpj4dTnp0+U4u5dnPHgebbJLmTYdPsTjRNpHnjLvazlgVHDg==\"}\n");
    CHECK(VerifyManifest("catalog/stable", list, sig, {SigningKey{1, pub}}, 1).ok());

    // a locked key opens with its passphrase only, and keeps its id
    uint8_t salt[16], nonce[24];
    for (int i = 0; i < 16; ++i) salt[i] = static_cast<uint8_t>(0xa0 + i);
    for (int i = 0; i < 24; ++i) nonce[i] = static_cast<uint8_t>(0x50 + i);
    const std::string text = LockKey(2, seed, "パスフレーズ correct", salt, nonce);
    CHECK(text.rfind("originx-key-v1:2:", 0) == 0 && text.find('\n') == std::string::npos);
    uint32_t id = 0;
    Seed back{};
    CHECK(UnlockKey(text + "\r\n", "パスフレーズ correct", id, back) == Unlock::Ok && id == 2 && back == seed);
    CHECK(UnlockKey(text, "wrong", id, back) == Unlock::WrongPassphrase);
    std::string relabelled = text; relabelled.replace(0, 17, "originx-key-v1:1:");
    CHECK(UnlockKey(relabelled, "パスフレーズ correct", id, back) == Unlock::WrongPassphrase);
    std::string changed = text; changed[changed.size() - 5] = changed[changed.size() - 5] == 'A' ? 'B' : 'A';
    CHECK(UnlockKey(changed, "パスフレーズ correct", id, back) != Unlock::Ok);
    for (const char* bad : {"", "hello", "originx-key-v1:2:AA==:AA==:AA==", "originx-key-v1:101:x:y:z", "101:AAECAwQFBgcICQoLDA0ODxAREhMUFRYXGBkaGxwdHh8="})
        CHECK(UnlockKey(bad, "x", id, back) == Unlock::NotAKey);

    // which kind a list is signed as
    CHECK(DetectKind("out/catalog.json", "{\"schemaVersion\":1,\"sequence\":3,\"versions\":[]}") == "catalog/stable");
    CHECK(DetectKind("C:\\x\\early\\catalog.json", "{\"schemaVersion\":1,\"sequence\":3,\"versions\":[]}") == "catalog/early");
    CHECK(DetectKind("out\\test\\catalog.json", "{\"schemaVersion\":1,\"sequence\":3,\"versions\":[]}").empty());
    CHECK(DetectKind("launcher.json", "{\"schemaVersion\":1,\"build\":41,\"url\":\"u\",\"sha256\":\"aa\",\"bytes\":1}") == "launcher/stable");
    CHECK(DetectKind("launcher-test.json", "{\"schemaVersion\":1,\"channel\":\"test\",\"build\":41,\"key\":\"k\",\"sha256\":\"aa\",\"bytes\":1}") == "launcher/test");
    CHECK(DetectKind("versions.json", "{\"releaseVersions\":[]}") == "versions/stable");
    CHECK(DetectKind("x.json", "not json").empty() && DetectKind("x.json", "[]").empty() && DetectKind("x.json", "{\"a\":1}").empty());
    CHECK(Summary("catalog/stable", "{\"sequence\":3,\"versions\":[{\"id\":\"26.51.1\",\"revision\":58}]}").find("sequence 3, 26.51.1 r58") != std::string::npos);

    if (failures) return 1;
    std::printf("PASS: signer core (keys, lock/unlock, signatures as signing.py, kinds)\n");
}
