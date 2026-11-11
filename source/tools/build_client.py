"""Build the 26.51 LeviLamina mod with clang-cl and the MSVC C++ ABI."""
import argparse
import json
from pathlib import Path
import re
import shutil
import subprocess

from list_game_symbols import main as list_game_symbols
from make_sigs import main as make_sigs
from prepare_client_sdk import DEFAULT_GAME, REPO, default_sdk_root, load_lock, prepare


def build(args):
    lock = load_lock(args.game)
    sdk = (args.sdk_root or default_sdk_root(args.game)).resolve()
    prepare(sdk, args.game)
    standalone = getattr(args, "standalone", False)
    output = REPO / (("build/client" if args.game == DEFAULT_GAME else "build/client-" + args.game) +
                     ("-standalone" if standalone else ""))
    output.mkdir(parents=True, exist_ok=True)
    library = output / "LeviLamina.lib"
    if not standalone:
        runtime = sdk / "runtime/LeviLamina/LeviLamina.dll"
        exports = subprocess.check_output([args.readobj, "--coff-exports", str(runtime)], text=True)
        names = re.findall(r"^  Name: (.+)$", exports, re.MULTILINE)
        if len(names) < 200:
            raise RuntimeError("Unexpected LeviLamina export table")
        definitions = output / "LeviLamina.def"
        definitions.write_text("LIBRARY LeviLamina.dll\nEXPORTS\n" +
                               "".join("  " + name + "\n" for name in names))
        subprocess.run([args.dlltool, "-m", "i386:x86-64", "-d", str(definitions),
                        "-l", str(library)], check=True)

    flags = ["--target=x86_64-pc-windows-msvc", "/nologo", "/std:c++20", "/MD", "/EHsc",
             "/O2", "/DNDEBUG", "/W4", "/utf-8", "/D_HAS_CXX23=1", "/D_ITERATOR_DEBUG_LEVEL=0",
             "/DLL_PLAT_C", "/DENTT_PACKED_PAGE=128", "/DENTT_SPARSE_PAGE=2048", "/DENTT_NO_MIXIN",
             "/DNOMINMAX", "/DWIN32_LEAN_AND_MEAN", "/D_CRT_SECURE_NO_WARNINGS",
             "/DFMT_HEADER_ONLY", "/DMYCLIENT_BRIDGE_BUILD",
             # LeviLamina's own functions (LLAPI) as ordinary ones, not dllimport: the ones MyClient writes itself
             # (client/LeviLaminaParts.cpp) are linked from there, the rest from LeviLamina.lib until replaced too.
             "/DLLAPI=",
             '/DMYCLIENT_GAME_VERSION="' + lock["gameVersion"] + '"',
             '/DMYCLIENT_LOADER_VERSION="' + lock["loaderVersion"] + '"', "/I" + str(REPO)]
    if standalone:
        # Without LeviLamina at run time (client/Standalone.cpp): its headers are still the class layouts,
        # its DLL is not linked, and the import table is checked below.
        flags.append("/DMYCLIENT_STANDALONE=1")
    linkflags = []
    if args.msvc_root:
        msvc = args.msvc_root.resolve()
        for path in [msvc / "crt/include", *[msvc / "sdk/include" / n for n in ("ucrt", "um", "shared", "winrt")]]:
            flags += ["-imsvc", str(path)]
        for path in ("crt/lib/x86_64", "sdk/lib/um/x86_64", "sdk/lib/ucrt/x86_64"):
            linkflags += ["/libpath:" + str(msvc / path)]
    for path in [sdk / "levilamina/src-client", sdk / "levilamina/src", sdk / "aliases",
                 *[sdk / "headers" / h["name"] / h["include"] for h in lock["headers"]]]:
        # Third-party headers retain upstream compiler extensions/diagnostics.
        flags += ["-imsvc", str(path)]
    list_game_symbols([])  # client/GameSymbolList.inc for the signature dump
    # sigs: tools/sigs.json -> client/GameRvaTable.inc + client/GameSigs.inc (stops the build when sigs.json is wrong)
    if make_sigs([]) != 0:
        raise SystemExit("tools/sigs.json has problems (see above); fix it before building")
    sources = ["client/SignatureDump.cpp", "client/MyClientData.cpp", "client/FeatureAdapter.cpp", "client/PartyAdapter.cpp", "client/Compositor.cpp", "client/FrameGen.cpp", "client/MemoryOperators.cpp", "client/GameImageDump.cpp", "client/GameRva.cpp", "client/GameSigs.cpp", "client/OwnHook.cpp", "client/Log.cpp", "client/LeviLaminaParts.cpp", "MyClientBridge.cpp",
               # MinHook (client/OwnHook.h), C
               "client/vendor/minhook/src/buffer.c", "client/vendor/minhook/src/hook.c",
               "client/vendor/minhook/src/trampoline.c", "client/vendor/minhook/src/hde/hde64.c"]
    sources.append("client/JavaChatAdapter.cpp")
    if standalone:
        sources.append("client/Standalone.cpp")
    objects = []
    failed = []
    for source in sources:
        obj = output / (Path(source).stem + ".obj")
        # every file is compiled and every error shown, so one CI run reports all of them
        if source.endswith(".c"):
            # C: the C++ switches do not apply; upstream warnings are not MyClient's
            source_flags = [f for f in flags if f not in ("/std:c++20", "/EHsc", "/W4", "/D_HAS_CXX23=1")] + ["/TC", "/W0"]
        else:
            source_flags = flags
        if subprocess.run([args.compiler, *source_flags, "-ferror-limit=0", "/c", str(REPO / source), "/Fo" + str(obj)]).returncode:
            failed.append(source)
        objects.append(str(obj))
    if failed:
        raise SystemExit("Compile failed: " + ", ".join(failed))
    dll = output / "MyClientData.dll"
    subprocess.run([args.linker, "/nologo", "/dll", "/machine:x64", "/subsystem:windows",
                    "/dynamicbase", "/nxcompat", "/opt:ref", "/opt:icf", "/errorlimit:0",
                    "/out:" + str(dll), "/implib:" + str(output / "MyClientData.lib"), "/map:" + str(output / "MyClientData.map"),
                    *linkflags, *objects, *([] if standalone else [str(library)]), "kernel32.lib", "user32.lib"], check=True)
    if standalone:
        imports = subprocess.check_output([args.readobj, "--coff-imports", str(dll)], text=True)
        libraries = re.findall(r"^\s*Name: (\S+\.dll)\s*$", imports, re.MULTILINE | re.IGNORECASE)
        if not libraries or any(name.lower() in ("levilamina.dll", "preloader.dll") for name in libraries):
            raise SystemExit("The standalone DLL still imports LeviLamina: " + ", ".join(libraries))
        print("Standalone: no LeviLamina import (" + ", ".join(sorted(set(libraries))) + ")")
    manifest = json.loads((REPO / "client/manifest.json").read_text())
    manifest.pop("dependencies", None)  # LeviLamina is not in the mod registry; see launcher/Install.h
    (output / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    print("Built " + str(dll))
    print(f"Requires Minecraft {args.game} and LeviLamina Client {lock['loaderVersion']}. Runtime gameplay testing is separate.")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--game", default=DEFAULT_GAME, help="game version to build for (see prepare_client_sdk.py --list)")
    parser.add_argument("--sdk-root", type=Path, default=None)
    parser.add_argument("--msvc-root", type=Path, help="xwin splat root on Linux; omit in a VS developer prompt")
    parser.add_argument("--standalone", action="store_true",
                        help="build the DLL that runs without LeviLamina (client/Standalone.cpp), into build/client*-standalone")
    parser.add_argument("--compiler", default="clang-cl")
    parser.add_argument("--linker", default="lld-link")
    parser.add_argument("--readobj", default="llvm-readobj")
    parser.add_argument("--dlltool", default="llvm-dlltool")
    build(parser.parse_args())
