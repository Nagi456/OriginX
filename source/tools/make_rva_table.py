#!/usr/bin/env python3
"""Make client/GameRvaTable.inc: game functions LeviLamina has no symbol for, found by tools/match_functions.py.

For one game build (exe timestamp), each row gives the names the DLL looks the function up by (the mangled
names in client/*.cpp, all access-level variants), its RVA and its first bytes as they are in the dumped image.
At run time (client/GameSymbols.h Target) the RVA is used only when the symbol is missing, the exe has that
timestamp and the bytes at the RVA are the same: otherwise nothing is hooked there.

  python3 tools/make_rva_table.py --build <26.51 .image> <26.51 hooked_functions.txt> \\
                                  --build <26.60 .image> <26.60 hooked_functions.txt> <matched_from_26.51.txt>

hooked_functions.txt = where LeviLamina's hooks really were (the game's patches), matched_from_*.txt = what
tools/match_functions.py found (sure rows LeviLamina has no answer for). A hook's names come from the code
(tools/hook_names.py), else from NAMES below. Since 2026-11-05 the table is used for every hook, LeviLamina
or not: LeviLamina's symbol lookup gives PreLoader's jump stub, which MyClient's own hooks cannot patch.
"""
import argparse
import re
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from hook_names import collect  # noqa: E402

# hook label (column 1 of the match file) -> the names the DLL resolves it by
NAMES = {
    "PlayerUpdateSkinHook": ["?updateSkin@Player@@QEAAXAEBVSerializedSkinRef@@H@Z"],
    "ItemInHandPassHook": [
        "?_renderItemInHand@InGamePlayScreen@@UEAAXAEAVScreenContext@@AEAVPlayer@@@Z",
        "?_renderItemInHand@InGamePlayScreen@@MEAAXAEAVScreenContext@@AEAVPlayer@@@Z",
        "?_renderItemInHand@InGamePlayScreen@@EEAAXAEAVScreenContext@@AEAVPlayer@@@Z",
    ],
    "PacksChangedInnerHook": [
        "?_onActiveResourcePacksChanged@MinecraftGame@@AEAAXAEAVResourcePackManager@@_N@Z",
        "?_onActiveResourcePacksChanged@MinecraftGame@@IEAAXAEAVResourcePackManager@@_N@Z",
        "?_onActiveResourcePacksChanged@MinecraftGame@@QEAAXAEAVResourcePackManager@@_N@Z",
    ],
    "AtlasReloadedHook": [
        "?_onTerrainAtlasReloaded@MinecraftGame@@AEAAXXZ",
        "?_onTerrainAtlasReloaded@MinecraftGame@@IEAAXXZ",
        "?_onTerrainAtlasReloaded@MinecraftGame@@QEAAXXZ",
    ],
    "PacksChangedHook": ["?onActiveResourcePacksChanged@MinecraftGame@@UEAAXAEAVResourcePackManager@@@Z"],
    "TextHook": ["?handle@ClientNetworkHandler@@UEAAXAEBVNetworkIdentifier@@AEBVTextPacket@@@Z"],
    "HandlerStartGameHook": ["?handle@LegacyClientNetworkHandler@@UEAAXAEBVNetworkIdentifier@@AEBVStartGamePacket@@@Z"],
    "HandlerMoveDeltaHook": ["?handle@LegacyClientNetworkHandler@@UEAAXAEBVNetworkIdentifier@@AEBVMoveActorDeltaPacket@@@Z"],
    "ActorRenderHook": ["?render@ActorRenderDispatcher@@QEAAXAEAVBaseActorRenderContext@@AEAVActor@@_N@Z"],
    "QueueChunkHook": [
        "?queueChunk@LevelRendererCamera@@QEAAXAEAUChunkRenderObjectCollection@@AEBVRenderChunkInstanced@@MMAEBV"
        "TerrainMaterialVariationManager@@V?$optional_ref@$$CBVTerrainMaterialVariationManager@@@@@Z"
    ],
    "NameTagsHook": ["?extractNameTags@LevelRendererPlayer@@UEBA?AUNameTagRenderObjectCollection@@AEAVScreenContext@@@Z"],
    "LeftHandItemNewHook": [
        "?renderItemNew@ItemInHandRenderer@@QEAAXAEAVBaseActorRenderContext@@AEAVActor@@AEBVItemStack@@W4ItemContextFlags@@UBrightness@@@Z"
    ],
    "UiUpdateHook": ["?frameUpdate@UIScene@@UEAAXAEAVMinecraftUIFrameUpdateContext@@@Z"],
    "ItemUpdateHook": ["?update@InventoryItemRenderer@@UEAA_NAEAVIClientInstance@@AEAVUIControl@@AEBVUIScene@@@Z"],
    "DollBuildHook": [
        "?createOrUpdateSkin@PaperDollClientInstanceUpdateContext@@UEAAXAEAVAnimationComponent@@AEBVSerializedSkinRef@@AEBUSkinImages@@@Z"
    ],
    "DollUpdateHook": ["?_update@PaperDollRenderer@@QEAAXAEAVIPaperDollUpdateContext@@AEBUPaperDollUpdateOwnerData@@@Z"],
}
CHECK_BYTES = 16


class Image:
    """A PE image dumped as it is in memory (offset = RVA)."""

    def __init__(self, data: bytes):
        self.data = data
        pe = struct.unpack_from("<I", data, 0x3C)[0]
        if data[pe:pe + 4] != b"PE\0\0":
            raise ValueError("not a PE image")
        self.timestamp = struct.unpack_from("<I", data, pe + 8)[0]
        opt = pe + 24
        if struct.unpack_from("<H", data, opt)[0] != 0x20B:
            raise ValueError("not a PE32+ image")
        self.size_of_image = struct.unpack_from("<I", data, opt + 56)[0]
        dirs = opt + 112
        self.pdata = struct.unpack_from("<II", data, dirs + 3 * 8)
        self.reloc = struct.unpack_from("<II", data, dirs + 5 * 8)

    def function_starts(self) -> set:
        rva, size = self.pdata
        return {struct.unpack_from("<I", self.data, rva + i)[0] for i in range(0, size - size % 12, 12)}

    def relocated(self) -> set:
        """RVAs of every byte a base relocation rewrites (those differ between runs)."""
        out = set()
        rva, size = self.reloc
        at = rva
        while at < rva + size:
            page, block = struct.unpack_from("<II", self.data, at)
            if block < 8:
                break
            for i in range(8, block, 2):
                entry = struct.unpack_from("<H", self.data, at + i)[0]
                kind, offset = entry >> 12, entry & 0xFFF
                if kind == 10:   # DIR64
                    out.update(range(page + offset, page + offset + 8))
                elif kind == 3:  # HIGHLOW
                    out.update(range(page + offset, page + offset + 4))
            at += block
        return out


def read_matches(path: Path):
    """matched_from_*.txt: label, symbol, RVA, sure/unsure, ..., check. Only sure rows LeviLamina has no answer for."""
    rows = []
    for line in path.read_text(encoding="utf-8").splitlines():
        if not line or line.startswith("#"):
            continue
        cols = line.split("\t") + [""] * 8
        if cols[3] == "sure" and not cols[7].strip():
            rows.append((cols[0], int(cols[2], 16)))
    return rows


def read_hooked(path: Path):
    """hooked_functions.txt: RVA, label (from the game's patches: where the hook really is)."""
    rows = []
    for line in path.read_text(encoding="utf-8").splitlines():
        if not line or line.startswith("#"):
            continue
        cols = line.split("\t")
        if len(cols) >= 2 and not cols[1].startswith("("):
            rows.append((cols[1], int(cols[0], 16)))
    return rows


def cpp_string(text: str) -> str:
    return '"' + text.replace("\\", "\\\\").replace('"', '\\"') + '"'


def read_addresses(path: Path):
    """addresses-*.txt (the DLL's diagnostic run): name, how, RVA. The name is its own label."""
    rows = []
    for line in path.read_text(encoding="utf-8").splitlines():
        if not line or line.startswith("#"):
            continue
        cols = line.split("\t")
        if len(cols) >= 3 and cols[0].startswith("?") and cols[1] in ("direct", "hooked") and cols[2].startswith("0x"):
            rows.append((cols[0], int(cols[2], 16)))
    return rows


def c_bytes(data: bytes) -> str:
    return '"' + "".join(f"\\x{b:02x}" for b in data) + '"'


def make(builds) -> str:
    """builds: [(image path, [(label, rva), ...]), ...] -> the .inc text."""
    hook_names = collect()
    lines = [
        "// Made by tools/make_rva_table.py. Do not edit.",
        "// MYCLIENT_RVA_EXE(exe, timestamp, image size); MYCLIENT_RVA_ROW(exe, label, (names...), RVA, first bytes, mask)",
        "// mask 0 = not compared (a relocated byte, or a hook's jump when the image was dumped)",
    ]
    for index, (image_path, rows) in enumerate(builds):
        image = Image(Path(image_path).read_bytes())
        starts = image.function_starts()
        relocated = image.relocated()
        lines.append(f"MYCLIENT_RVA_EXE({index}, 0x{image.timestamp:08X}u, 0x{image.size_of_image:X}u)")
        seen = set()
        seen_names = set()
        for label, rva in rows:
            names = hook_names.get(label) or NAMES.get(label) or ([label] if label.startswith("?") else None)
            if not names or label in seen:
                continue
            names = [n for n in names if n not in seen_names]   # a name already in this build's table (from a hook row)
            if not names:
                continue
            seen.add(label)
            seen_names.update(names)
            if rva not in starts:
                # a leaf function has no unwind entry; the patches still show a hook was put there
                print(f"note: {label} 0x{rva:x} is not in .pdata of {Path(image_path).name}", file=sys.stderr)
            code = image.data[rva:rva + CHECK_BYTES]
            hooked = code[0] == 0xE9   # already hooked when dumped (a jmp to a trampoline): its first 5 bytes vary
            mask = bytes(0 if rva + i in relocated or (hooked and i < 5) else 1 for i in range(CHECK_BYTES))
            lines.append(f"MYCLIENT_RVA_ROW({index}, {cpp_string(label)}, ({', '.join(cpp_string(n) for n in names)}), "
                         f"0x{rva:X}u, {c_bytes(code)}, {c_bytes(mask)})")
    return "\n".join(lines) + "\n"


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--build", nargs="+", action="append", metavar=("IMAGE", "TABLE"), required=True,
                        help="a game image and its tables (hooked_functions.txt and/or matched_from_*.txt)")
    parser.add_argument("--out", type=Path, default=Path(__file__).resolve().parent.parent / "client" / "GameRvaTable.inc")
    args = parser.parse_args(argv)
    builds = []
    for image, *tables in args.build:
        rows = []
        for table in tables:
            path = Path(table)
            if path.name.startswith("hooked_functions"):
                rows += read_hooked(path)
            elif path.name.startswith("addresses-"):
                rows += read_addresses(path)
            else:
                rows += read_matches(path)
        builds.append((image, rows))
    text = make(builds)
    args.out.write_text(text, encoding="utf-8", newline="\n")
    print(f"wrote {args.out} ({sum(l.startswith('MYCLIENT_RVA_ROW(') for l in text.splitlines())} functions in {len(builds)} game builds)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
