"""Static DLL contract verification; this is not a Minecraft runtime test."""
import argparse
import json
from pathlib import Path
import re
import subprocess

from prepare_client_sdk import REPO


def verify(readobj, sdk):
    dll = REPO / "build/client/MyClientData.dll"
    result = subprocess.check_output([readobj, "--file-headers", "--coff-imports", "--coff-exports", str(dll)], text=True)
    assert "COFF-x86-64" in result and "IMAGE_FILE_DLL" in result
    exports = set(re.findall(r"Export \{.*?\n  Name: (.+?)\n", result, re.DOTALL))
    assert exports == {"MyClientBridgeInitialize", "MyClientBridgePublish", "MyClientBridgeShutdown",
                       "ll_mod_load", "ll_mod_enable", "ll_mod_disable", "ll_memory_operator_overrided"}, exports
    imports = {}
    for name, body in re.findall(r"Import \{\n  Name: (.+?)\n(.*?)\n\}", result, re.DOTALL):
        imports[name] = set(re.findall(r"  Symbol: (.+?) \(\d+\)", body))
    required = ["getInventory@Player", "getTypeName@ItemStackBase", "getOffhandSlot@Actor",
                "getClientInstance@bedrock", "getMultiPlayerLevel@bedrock",
                "getDefaultAllocator@memory", "getGameVersion@ll", "addListener@EventBus",
                "resolve@SymbolView", "getArmor@Actor", "hookEx@memory"]
    for name in required:
        assert any(name in symbol for symbol in imports["LeviLamina.dll"]), name
    official = subprocess.check_output([readobj, "--coff-exports",
                                        str(sdk / "runtime/LeviLamina/LeviLamina.dll")], text=True)
    official_exports = set(re.findall(r"^  Name: (.+)$", official, re.MULTILINE))
    assert imports["LeviLamina.dll"] <= official_exports
    assert all(name in {"LeviLamina.dll", "KERNEL32.dll", "USER32.dll", "MSVCP140.dll", "VCRUNTIME140.dll"}
               or name.startswith("api-ms-win-crt-") for name in imports), imports.keys()
    hud = REPO / "build/MyClient.exe"
    hud_info = subprocess.check_output([readobj, "--file-headers", "--coff-imports", str(hud)], text=True)
    assert "COFF-x86-64" in hud_info and "IMAGE_SUBSYSTEM_WINDOWS_GUI" in hud_info
    assert "LeviLamina.dll" not in hud_info
    launcher = REPO / "build/MyClientLauncher.exe"
    launcher_info = subprocess.check_output([readobj, "--file-headers", "--coff-imports", str(launcher)], text=True)
    assert "COFF-x86-64" in launcher_info and "IMAGE_SUBSYSTEM_WINDOWS_GUI" in launcher_info
    assert "LeviLamina.dll" not in launcher_info
    for dependency in ("COMDLG32.dll", "SHELL32.dll", "ole32.dll", "bcrypt.dll", "VERSION.dll", "WINHTTP.dll"):
        assert dependency.lower() in launcher_info.lower(), dependency
    # The game is started directly; Windows PowerShell prepares pinned deps.
    assert "CreateProcessW" in launcher_info and "BCryptHashData" in launcher_info
    assert "Minecraft.Windows.exe".encode("utf-16-le") in launcher.read_bytes()
    assert b"PrepareRuntime.ps1" in launcher.read_bytes()  # filesystem path uses a narrow UTF-8 literal
    assert "--launch=".encode("utf-16-le") not in launcher.read_bytes()
    assert "LeviLauncher.exe".encode("utf-16-le") not in launcher.read_bytes()
    assert "--minecraft-pid=".encode("utf-16-le") in launcher.read_bytes()
    # Optimizers may split the HUD's ASCII prefix into machine immediates.
    # Its parser is covered by native tests; verify integration imports here.
    assert "CommandLineToArgvW" in hud_info and "EnumWindows" in hud_info
    assert "Local\\MyClient.Hud.".encode("utf-16-le") in hud.read_bytes()
    assert "Local\\MyClient.Launcher".encode("utf-16-le") in launcher.read_bytes()
    # Both processes must address the same IPC revision.
    for binary in (dll, hud, launcher):
        assert "Local\\MyClient.Bridge.v2.".encode("utf-16-le") in binary.read_bytes(), binary
    for binary in (dll, hud):
        assert "Local\\MyClient.Features.v1.".encode("utf-16-le") in binary.read_bytes(), binary
    for binary in (dll, hud):
        assert "Local\\MyClient.Party.v1.".encode("utf-16-le") in binary.read_bytes(), binary
    for symbol in (b"?handle@ClientNetworkHandler@@UEAAXAEBVNetworkIdentifier@@AEBVTextPacket@@@Z",
                   b"?createPacket@MinecraftPackets@@SA?AV?$shared_ptr@VPacket@@@std@@W4MinecraftPacketIds@@@Z",
                   b"??1GameConnectionInfo@Social@@QEAA@XZ"):
        assert symbol in dll.read_bytes(), symbol
    assert "GetAsyncKeyState" in result and "GetForegroundWindow" in result
    for symbol in (b"?renderCurrentFrame@GameRenderer@@QEAAXM@Z", b"?getFov@LevelRendererPlayer@@QEBAMM_N@Z",
                   b"?getDamageValue@ItemStackBase@@QEBAFXZ"):
        assert symbol in dll.read_bytes(), symbol
    manifest = json.loads((REPO / "build/client/manifest.json").read_text())
    assert manifest["entry"] == dll.name and manifest["name"] == "MyClientData"
    assert not manifest.get("dependencies")  # a LeviLamina dependency makes the loader reject the mod
    print("PASS: Windows x64 mod/HUD/standalone launcher, SDK exports/imports, ABI v2, pinned mod manifest, direct process/hash imports, startup helper and PID request.")
    print("Minecraft execution was not tested by this command.")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sdk-root", type=Path, default=REPO / "build/client-sdk")
    parser.add_argument("--readobj", default="llvm-readobj")
    args = parser.parse_args()
    verify(args.readobj, args.sdk_root.resolve())
