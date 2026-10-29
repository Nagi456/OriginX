"""Build the 26.51 LeviLamina mod with clang-cl and the MSVC C++ ABI."""
import argparse
import json
from pathlib import Path
import re
import shutil
import subprocess

from prepare_client_sdk import DEFAULT_GAME, REPO, default_sdk_root, load_lock, prepare


def build(args):
    lock = load_lock(args.game)
    sdk = (args.sdk_root or default_sdk_root(args.game)).resolve()
    prepare(sdk, args.game)
    output = REPO / ("build/client" if args.game == DEFAULT_GAME else "build/client-" + args.game)
    output.mkdir(parents=True, exist_ok=True)
    runtime = sdk / "runtime/LeviLamina/LeviLamina.dll"
    exports = subprocess.check_output([args.readobj, "--coff-exports", str(runtime)], text=True)
    names = re.findall(r"^  Name: (.+)$", exports, re.MULTILINE)
    if len(names) < 200:
        raise RuntimeError("Unexpected LeviLamina export table")
    definitions = output / "LeviLamina.def"
    definitions.write_text("LIBRARY LeviLamina.dll\nEXPORTS\n" +
                           "".join("  " + name + "\n" for name in names))
    library = output / "LeviLamina.lib"
    subprocess.run([args.dlltool, "-m", "i386:x86-64", "-d", str(definitions),
                    "-l", str(library)], check=True)

    flags = ["--target=x86_64-pc-windows-msvc", "/nologo", "/std:c++20", "/MD", "/EHsc",
             "/O2", "/DNDEBUG", "/W4", "/utf-8", "/D_HAS_CXX23=1", "/D_ITERATOR_DEBUG_LEVEL=0",
             "/DLL_PLAT_C", "/DENTT_PACKED_PAGE=128", "/DENTT_SPARSE_PAGE=2048", "/DENTT_NO_MIXIN",
             "/DNOMINMAX", "/DWIN32_LEAN_AND_MEAN", "/D_CRT_SECURE_NO_WARNINGS",
             "/DFMT_HEADER_ONLY", "/DMYCLIENT_BRIDGE_BUILD",
             '/DMYCLIENT_GAME_VERSION="' + lock["gameVersion"] + '"',
             '/DMYCLIENT_LOADER_VERSION="' + lock["loaderVersion"] + '"', "/I" + str(REPO)]
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
    sources = ["client/MyClientData.cpp", "client/FeatureAdapter.cpp", "client/PartyAdapter.cpp", "client/Compositor.cpp", "client/FrameGen.cpp", "client/MemoryOperators.cpp", "MyClientBridge.cpp"]
    objects = []
    for source in sources:
        obj = output / (Path(source).stem + ".obj")
        subprocess.run([args.compiler, *flags, "/c", str(REPO / source), "/Fo" + str(obj)], check=True)
        objects.append(str(obj))
    dll = output / "MyClientData.dll"
    subprocess.run([args.linker, "/nologo", "/dll", "/machine:x64", "/subsystem:windows",
                    "/dynamicbase", "/nxcompat", "/opt:ref", "/opt:icf",
                    "/out:" + str(dll), "/implib:" + str(output / "MyClientData.lib"),
                    *linkflags, *objects, str(library), "kernel32.lib", "user32.lib"], check=True)
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
    parser.add_argument("--compiler", default="clang-cl")
    parser.add_argument("--linker", default="lld-link")
    parser.add_argument("--readobj", default="llvm-readobj")
    parser.add_argument("--dlltool", default="llvm-dlltool")
    build(parser.parse_args())
