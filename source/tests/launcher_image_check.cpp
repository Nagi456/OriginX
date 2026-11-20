// g++ -std=c++17 -Wall -Wextra -Werror -I. -Ilauncher/vendor tests/launcher_image_check.cpp launcher/vendor/tinyxml2/tinyxml2.cpp -o /tmp/image-check
#include "../launcher/GameImageCheck.h"
#include "../launcher/Updates.h"
#include <cstdio>

using namespace MyClientLauncher;
static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #cond); ++failures; } } while (0)

template <class T> static void Put(std::vector<unsigned char>& image, size_t at, T value) { std::memcpy(image.data() + at, &value, sizeof value); }

// A small x64 image as the loader maps it: headers, a resource directory RT_VERSION/1/1033 and a VS_VERSIONINFO.
static std::vector<unsigned char> Image(uint32_t stamp, uint32_t size, uint16_t a, uint16_t b, uint16_t c, uint16_t d, bool version = true)
{
    std::vector<unsigned char> m(0x3000, 0);
    Put<uint16_t>(m, 0, 0x5A4D);
    Put<uint32_t>(m, 0x3C, 0x80);
    const size_t pe = 0x80;
    Put<uint32_t>(m, pe, 0x00004550);
    Put<uint16_t>(m, pe + 4, 0x8664);
    Put<uint32_t>(m, pe + 8, stamp);
    Put<uint16_t>(m, pe + 24, 0x20B);
    Put<uint32_t>(m, pe + 24 + 56, size);
    Put<uint32_t>(m, pe + 24 + 108, 16);
    if (!version) return m;
    const uint32_t rsrc = 0x1000;
    Put<uint32_t>(m, pe + 24 + 112 + 16, rsrc);
    Put<uint32_t>(m, pe + 24 + 112 + 20, 0x400);
    // root: an unrelated id first (RT_ICON = 3), then RT_VERSION -> directory at +0x40
    Put<uint16_t>(m, rsrc + 14, 2);
    Put<uint32_t>(m, rsrc + 16, 3); Put<uint32_t>(m, rsrc + 20, 0x80000000u | 0x100);
    Put<uint32_t>(m, rsrc + 24, 16); Put<uint32_t>(m, rsrc + 28, 0x80000000u | 0x40);
    // name level: id 1 -> directory at +0x80
    Put<uint16_t>(m, rsrc + 0x40 + 14, 1);
    Put<uint32_t>(m, rsrc + 0x40 + 16, 1); Put<uint32_t>(m, rsrc + 0x40 + 20, 0x80000000u | 0x80);
    // language level: 1033 -> data entry at +0xC0
    Put<uint16_t>(m, rsrc + 0x80 + 14, 1);
    Put<uint32_t>(m, rsrc + 0x80 + 16, 1033); Put<uint32_t>(m, rsrc + 0x80 + 20, 0xC0);
    // data entry: rva 0x2000, 0x100 bytes
    Put<uint32_t>(m, rsrc + 0xC0, 0x2000); Put<uint32_t>(m, rsrc + 0xC4, 0x100);
    // VS_VERSIONINFO header (length, value length, type, "VS_VERSION_INFO", padding) then VS_FIXEDFILEINFO at +40
    Put<uint16_t>(m, 0x2000, 0x100); Put<uint16_t>(m, 0x2002, 52);
    const char* key = "VS_VERSION_INFO";
    for (int i = 0; key[i]; ++i) Put<uint16_t>(m, 0x2006 + i * 2, static_cast<uint16_t>(key[i]));
    Put<uint32_t>(m, 0x2000 + 40, 0xFEEF04BDu);
    Put<uint32_t>(m, 0x2000 + 48, (uint32_t(a) << 16) | b);
    Put<uint32_t>(m, 0x2000 + 52, (uint32_t(c) << 16) | d);
    return m;
}

static ImageReader Reader(const std::vector<unsigned char>& m)
{
    return [&m](uint32_t rva, void* out, uint32_t size) {
        if (uint64_t(rva) + size > m.size()) return false;
        std::memcpy(out, m.data() + rva, size);
        return true;
    };
}

int main()
{
    const auto table = ParseSupportedImages(Json::parse(R"({"schemaVersion":1,"gameVersion":"26.51.1",
        "images":[{"fileVersion":"1.26.51.1","timestamp":"0x6AA482FD","imageSizes":["0x12C01000","0x12C0C000"]}]})"));
    CHECK(table.gameVersion == "26.51.1" && table.images.size() == 1 && table.images[0].timestamp == 0x6AA482FDu);

    std::string why;
    // the Store exe of 26.51.1 (file version 1.26.5101.0), and the one PeEditor edited (other SizeOfImage)
    {
        const auto m = Image(0x6AA482FD, 0x12C01000, 1, 26, 5101, 0);
        const auto image = ReadGameImage(Reader(m));
        CHECK(image && image->timestamp == 0x6AA482FDu && image->sizeOfImage == 0x12C01000u && image->fileVersion == "1.26.5101.0");
        CHECK(image && ImageSupported(*image, table, why));
        const auto edited = Image(0x6AA482FD, 0x12C0C000, 1, 26, 5101, 0);
        CHECK(ImageSupported(*ReadGameImage(Reader(edited)), table, why));
    }
    // another game version
    {
        const auto m = Image(0x6AD79EE4, 0x1383B000, 1, 26, 6005, 0);
        CHECK(!ImageSupported(*ReadGameImage(Reader(m)), table, why) && why == "file version 26.60.5");
    }
    // the same file version, but another build: the TimeDateStamp or the SizeOfImage differs
    {
        const auto stamp = Image(0x6AA48300, 0x12C01000, 1, 26, 5101, 0);
        CHECK(!ImageSupported(*ReadGameImage(Reader(stamp)), table, why) && why.find("TimeDateStamp") != std::string::npos);
        const auto size = Image(0x6AA482FD, 0x12C02000, 1, 26, 5101, 0);
        CHECK(!ImageSupported(*ReadGameImage(Reader(size)), table, why) && why.find("SizeOfImage") != std::string::npos);
    }
    // the right header but no version resource: not supported
    {
        const auto m = Image(0x6AA482FD, 0x12C01000, 0, 0, 0, 0, false);
        const auto image = ReadGameImage(Reader(m));
        CHECK(image && image->fileVersion.empty() && !ImageSupported(*image, table, why) && why == "no file version");
    }
    // not an x64 PE / unreadable memory
    {
        auto m = Image(0x6AA482FD, 0x12C01000, 1, 26, 5101, 0);
        Put<uint16_t>(m, 0x84, 0x014C);   // x86
        CHECK(!ReadGameImage(Reader(m)));
        std::vector<unsigned char> tiny(8, 0);
        CHECK(!ReadGameImage(Reader(tiny)));
        const ImageReader none = [](uint32_t, void*, uint32_t) { return false; };
        CHECK(!ReadGameImage(none));
    }
    // broken tables
    for (const char* bad : {R"({"schemaVersion":2,"gameVersion":"x","images":[]})", R"({"schemaVersion":1,"gameVersion":"26.51.1","images":[]})",
                            R"({"schemaVersion":1,"gameVersion":"26.51.1","images":[{"fileVersion":"1.26.51.1","timestamp":"6AA482FD","imageSizes":["0x1"]}]})",
                            R"({"schemaVersion":1,"gameVersion":"26.51.1","images":[{"fileVersion":"x","timestamp":"0x1","imageSizes":["0x1"]}]})",
                            R"({"schemaVersion":1,"gameVersion":"26.51.1","images":[{"fileVersion":"1.26.51.1","timestamp":"0x1","imageSizes":[]}]})"})
    {
        bool threw = false;
        try { ParseSupportedImages(Json::parse(bad)); } catch (const std::exception&) { threw = true; }
        CHECK(threw);
    }
    CHECK(UnsupportedGameMessage(nullptr, "26.51.1", "file version 26.60.5").find("26.60.5") != std::string::npos);
    // the table is a package file the launcher accepts (not a second DLL)
    CHECK(PackagePath(SupportedFile) && !PackagePath("mods/MyClientData/supported.dll"));
    if (failures) return 1;
    std::printf("PASS: running game image check (TimeDateStamp, SizeOfImage, file version, supported.json)\n");
}
