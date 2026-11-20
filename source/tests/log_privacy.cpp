// g++ -std=c++17 -Wall -Wextra -Werror -I. tests/log_privacy.cpp -o /tmp/log-privacy
#include "../LogPrivacy.h"
#include <cstdio>

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #cond); ++failures; } } while (0)

int main()
{
    using LogPrivacy::RedactPaths;
    const LogPrivacy::Folders folders{{"C:\\Users\\Ann Smith", "%USERPROFILE%"},
                                      {"C:\\Users\\Ann Smith\\AppData\\Local", "%LOCALAPPDATA%"},
                                      {"C:\\Users\\Ann Smith\\AppData\\Roaming", "%APPDATA%"}};
    CHECK(RedactPaths("saved C:\\Users\\Ann Smith\\AppData\\Local\\MyClient\\closet.json", folders) == "saved %LOCALAPPDATA%\\MyClient\\closet.json");
    CHECK(RedactPaths("c:/users/ann smith/AppData/Roaming/Minecraft Bedrock", folders) == "%APPDATA%/Minecraft Bedrock");
    CHECK(RedactPaths("replay to C:\\Users\\Ann Smith\\Videos\\a.mcrec", folders) == "replay to %USERPROFILE%\\Videos\\a.mcrec");
    CHECK(RedactPaths("\"C:\\Users\\Ann Smith\"", folders) == "\"%USERPROFILE%\"");
    // a longer name that only starts the same is not the profile, but still a user folder
    CHECK(RedactPaths("C:\\Users\\Ann Smithers\\x", folders) == "C:\\Users\\%USERNAME%\\x");
    // another drive, a UTF-8 name, the shared folders kept
    CHECK(RedactPaths("D:\\Users\\\xe3\x81\x82\xe3\x81\x84\\game", folders) == "D:\\Users\\%USERNAME%\\game");
    CHECK(RedactPaths("C:\\Users\\Public\\x and C:\\Users\\Default\\y", folders) == "C:\\Users\\Public\\x and C:\\Users\\Default\\y");
    // text without paths, and the end of the text
    CHECK(RedactPaths("Hive判定: true (play.example)", folders) == "Hive判定: true (play.example)");
    CHECK(RedactPaths("C:\\Users\\Ann Smith", folders) == "%USERPROFILE%");
    CHECK(RedactPaths("C:\\Users\\", folders) == "C:\\Users\\");
    // no folders known (environment missing)
    CHECK(RedactPaths("C:\\Users\\bob\\AppData", {}) == "C:\\Users\\%USERNAME%\\AppData");
    if (failures) return 1;
    std::printf("PASS: log paths without the user name\n");
}
