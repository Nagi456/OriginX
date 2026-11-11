// g++ -std=c++17 -Wall -Wextra -Werror -I. tests/launcher_switch_errors.cpp -o /tmp/myclient-switch-errors && /tmp/myclient-switch-errors
#include "launcher/SwitchErrors.h"
#include <cassert>
#include <iostream>

using namespace MyClientLauncher;

int main()
{
    // query mode output
    assert(NormalIdFromLog("Installed: 1.26.5203.0 (Store)\r\nNormalId: 26.52.3\r\n") == "26.52.3");
    assert(NormalIdFromLog("NormalId: 26.51.1") == "26.51.1");
    assert(NormalIdFromLog("Minecraft is not installed.\r\n").empty());
    assert(NormalIdFromLog("NormalId: \r\n").empty());
    assert(NormalIdFromLog("NormalId: 26\r\n").empty());

    // error codes: the first 0x8XXXXXXX, case-insensitive, not inside a longer word or number
    assert(FirstErrorCode("Deployment failed with HRESULT: 0x80073cf9, Install failed.") == "0x80073CF9");
    assert(FirstErrorCode("HRESULT: 0x80073D06\r\nHRESULT: 0x80073CF9") == "0x80073D06");
    assert(FirstErrorCode("id 10x80073CF9 and 0x80073CF9123").empty());
    assert(FirstErrorCode("0x00000000 0x1234").empty());
    assert(FirstErrorCode("").empty());

    // hints
    assert(InstallErrorHint("HRESULT: 0x80073D06").find("新しい版") != std::string::npos);
    assert(InstallErrorHint("0x80073D02").find("使用中") != std::string::npos);
    assert(InstallErrorHint("x 0x87E10BC6 y") == "Windows のエラー 0x87E10BC6（意味は下の Windows のログを参照）");
    assert(InstallErrorHint("no code here").empty());
    // Japanese Windows message around the code
    assert(InstallErrorHint("展開は HRESULT: 0x80073CFB で失敗しました").find("0x80073CFB") != std::string::npos);

    std::cout << "launcher switch errors: ok\n";
    return 0;
}
