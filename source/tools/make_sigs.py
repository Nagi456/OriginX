#!/usr/bin/env python3
"""sigs: every game function and game-version-dependent offset MyClientData.dll uses, in one file.

The source of truth is tools/sigs.json (edited by hand / by an agent, see docs/version-update.md). This script
  * checks it (names, versions, patterns, RVAs, the fields every entry must have);
  * writes client/GameRvaTable.inc (the per-build address table the DLL hooks from, client/GameRva.h) and
    client/GameSigs.inc (patterns, purpose, "fatal", offsets, supported versions: client/GameSigs.h, used by the
    start-up self check that writes logs/sigcheck.log);
  * with --check BUILD IMAGE: proves every pattern of that build matches exactly once in the image's code, at its RVA;
  * with --scan IMAGE: for a game build not in sigs.json yet, where each signature's newest pattern is found now
    (OK = once, MULTI = several, NG = nowhere). tools/findsig.py looks for the NG ones.

tools/build_client.py runs the generation before compiling, so an edit of sigs.json is all a version update needs.

sigs.json:
  "supported": [{"version": "1.26.51.1", "timestamp": "0x6AA482FD", "imageSize": "0x12C0C000"}, ...]
  "signatures": [{"name", "purpose", "breaks", "fatal", "lastUpdated", "symbols": [...],
                  "builds": {"1.26.51.1": {"rva": "0x...", "pattern": "48 89 5C 24 ?? ...", "unique": true,
                                           "check": "16 bytes GameRva compares (optional)", "note": "..."}, ...}}]
  "offsets": [{"name", "purpose", "breaks", "manual", "builds": {"1.26.51.1": "0xC0", ...}}]
Usage:
  python tools/make_sigs.py                      # check + generate (what the build does)
  python tools/make_sigs.py --check 1.26.60.5 IMAGE
  python tools/make_sigs.py --scan IMAGE [--json OUT]
"""
import argparse
import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SIGS = ROOT / "tools" / "sigs.json"
RVA_TABLE = ROOT / "client" / "GameRvaTable.inc"
SIG_TABLE = ROOT / "client" / "GameSigs.inc"
CHECK_BYTES = 16
MIN_CHECKED = 8          # client/GameRva.h BytesMatch needs at least this many compared bytes (after a hook's 5)
VERSION = re.compile(r"1\.\d+\.\d+\.\d+")


class SigsError(Exception):
    pass


def load(path=SIGS):
    return json.loads(Path(path).read_text(encoding="utf-8"))


def parse_pattern(text):
    """'48 8B ?? 05' -> [0x48, 0x8B, None, 0x05]. Raises SigsError on anything else."""
    out = []
    for token in text.split():
        if token in ("??", "?"):
            out.append(None)
        elif re.fullmatch(r"[0-9A-Fa-f]{2}", token):
            out.append(int(token, 16))
        else:
            raise SigsError(f"bad pattern byte {token!r}")
    return out


def validate(doc):
    """Every rule the generated tables rely on. Returns a list of problems (empty = fine)."""
    problems = []
    versions = []
    for b in doc.get("supported", []):
        v = b.get("version", "")
        if not VERSION.fullmatch(v):
            problems.append(f"supported: bad version {v!r} (write all four parts, e.g. 1.26.60.5)")
        for key in ("timestamp", "imageSize"):
            if not re.fullmatch(r"0x[0-9A-Fa-f]{1,8}", str(b.get(key, ""))):
                problems.append(f"supported {v}: {key} must be a hex number like 0x6AD79EE4")
        versions.append(v)
    if len(set(versions)) != len(versions):
        problems.append("supported: a version is listed twice")
    names = set()
    symbols_per_build = {}
    for s in doc.get("signatures", []):
        n = s.get("name", "")
        if not n or n in names:
            problems.append(f"signature name missing or repeated: {n!r}")
        names.add(n)
        for key in ("purpose", "breaks", "lastUpdated"):
            if not s.get(key):
                problems.append(f"{n}: {key} is required")
        if not isinstance(s.get("fatal"), bool):
            problems.append(f"{n}: fatal must be true or false")
        if s.get("lastUpdated") and s["lastUpdated"] not in versions:
            problems.append(f"{n}: lastUpdated {s['lastUpdated']} is not a supported version")
        if not s.get("symbols") or len(s["symbols"]) > 4:
            problems.append(f"{n}: 1 to 4 symbols (the names the DLL looks it up by)")
        for v, at in s.get("builds", {}).items():
            if v not in versions:
                problems.append(f"{n}: build {v} is not in supported")
                continue
            if not re.fullmatch(r"0x[0-9A-Fa-f]{1,8}", at.get("rva", "")):
                problems.append(f"{n} {v}: rva must be hex")
            try:
                p = parse_pattern(at.get("pattern", ""))
            except SigsError as e:
                problems.append(f"{n} {v}: {e}")
                continue
            if len(p) < 12 and not at.get("note"):
                problems.append(f"{n} {v}: pattern shorter than 12 bytes (if it cannot be longer, say why in a \"note\")")
            try:
                head = parse_pattern(at["check"]) if at.get("check") else (p + [None] * CHECK_BYTES)[:CHECK_BYTES]
            except SigsError as e:
                problems.append(f"{n} {v}: check: {e}")
                continue
            if len(head) != CHECK_BYTES:
                problems.append(f"{n} {v}: check must be exactly {CHECK_BYTES} bytes")
            elif sum(b is not None for b in head[5:]) < MIN_CHECKED:
                problems.append(f"{n} {v}: fewer than {MIN_CHECKED} fixed bytes in bytes 5..15 of check/pattern (GameRva cannot check it)")
            for sym in s.get("symbols", []):
                key = (v, sym)
                if key in symbols_per_build:
                    problems.append(f"{sym} is in two signatures for {v}: {symbols_per_build[key]} and {n}")
                symbols_per_build[key] = n
    for o in doc.get("offsets", []):
        n = o.get("name", "")
        for key in ("purpose", "breaks", "manual"):
            if not o.get(key):
                problems.append(f"offset {n}: {key} is required")
        for v, value in o.get("builds", {}).items():
            if v not in versions or not re.fullmatch(r"0x[0-9A-Fa-f]+", str(value)):
                problems.append(f"offset {n}: build {v} unknown or value not hex")
    return problems


def cstr(text):
    return '"' + text.replace("\\", "\\\\").replace('"', '\\"') + '"'


def cbytes(values):
    return '"' + "".join(f"\\x{v:02x}" for v in values) + '"'


def generate(doc):
    versions = [b["version"] for b in doc["supported"]]
    rva = ["// Made by tools/make_sigs.py from tools/sigs.json. Do not edit: edit sigs.json and build.",
           "// MYCLIENT_RVA_EXE(exe, timestamp, image size); MYCLIENT_RVA_ROW(exe, label, (names...), RVA, first bytes, mask)",
           "// mask 0 = not compared (a byte the pattern leaves open: a relative target or a RIP displacement)"]
    sig = ["// Made by tools/make_sigs.py from tools/sigs.json. Do not edit: edit sigs.json and build.",
           "// MYCLIENT_SIG_BUILD(index, version, timestamp, image size)",
           "// MYCLIENT_SIG(index, name, fatal, purpose, breaks, last updated)",
           "// MYCLIENT_SIG_AT(signature index, build index, rva, pattern)",
           "// MYCLIENT_SIG_OFFSET(name, build index, value)"]
    for i, b in enumerate(doc["supported"]):
        rva.append(f"MYCLIENT_RVA_EXE({i}, {b['timestamp'].upper().replace('0X', '0x')}u, {b['imageSize'].upper().replace('0X', '0x')}u)")
        sig.append(f"MYCLIENT_SIG_BUILD({i}, {cstr(b['version'])}, {b['timestamp']}u, {b['imageSize']}u)")
    for i, b in enumerate(doc["supported"]):
        for s in doc["signatures"]:
            at = s["builds"].get(b["version"])
            if not at:
                continue
            # "check": the 16 bytes GameRva compares before handing the address out (default: the pattern's first 16)
            p = parse_pattern(at["check"]) if at.get("check") else (parse_pattern(at["pattern"]) + [None] * CHECK_BYTES)[:CHECK_BYTES]
            code = [0 if v is None else v for v in p]
            mask = [0 if v is None else 1 for v in p]
            names = ", ".join(cstr(n) for n in s["symbols"])
            rva.append(f"MYCLIENT_RVA_ROW({i}, {cstr(s['name'])}, ({names}), {at['rva'].upper().replace('0X', '0x')}u, {cbytes(code)}, {cbytes(mask)})")
    for k, s in enumerate(doc["signatures"]):
        sig.append(f"MYCLIENT_SIG({k}, {cstr(s['name'])}, {'true' if s['fatal'] else 'false'}, {cstr(s['purpose'])}, "
                   f"{cstr(s['breaks'])}, {cstr(s['lastUpdated'])})")
        for i, v in enumerate(versions):
            at = s["builds"].get(v)
            if at:
                sig.append(f"MYCLIENT_SIG_AT({k}, {i}, {at['rva']}u, {cstr(at['pattern'])})")
    for o in doc.get("offsets", []):
        for i, v in enumerate(versions):
            if v in o["builds"]:
                sig.append(f"MYCLIENT_SIG_OFFSET({cstr(o['name'])}, {i}, {o['builds'][v]}u)")
    return "\n".join(rva) + "\n", "\n".join(sig) + "\n"


def write_tables(doc):
    rva, sig = generate(doc)
    for path, text in ((RVA_TABLE, rva), (SIG_TABLE, sig)):
        if not path.exists() or path.read_text(encoding="utf-8") != text:
            path.write_text(text, encoding="utf-8", newline="\n")
    return rva, sig


# ---- images (memory images from client/GameImageDump.cpp, or Minecraft.Windows.exe itself) ----
def open_image(path):
    sys.path.insert(0, str(Path(__file__).resolve().parent))
    from analyze_game_image import Image   # noqa: E402
    return Image(as_memory_image(Path(path).read_bytes()), 0)


def as_memory_image(data):
    """A PE file laid out as it sits in memory (sections at their RVAs). A memory image is returned as it is."""
    import struct
    pe = struct.unpack_from("<I", data, 0x3C)[0]
    count = struct.unpack_from("<H", data, pe + 6)[0]
    optional = struct.unpack_from("<H", data, pe + 20)[0]
    size_of_image = struct.unpack_from("<I", data, pe + 24 + 56)[0]
    if len(data) >= size_of_image:
        return data   # already a memory image
    out = bytearray(size_of_image)
    headers = struct.unpack_from("<I", data, pe + 24 + 60)[0]
    out[:headers] = data[:headers]
    for i in range(count):
        at = pe + 24 + optional + i * 40
        vsize, rva, rawsize, raw = struct.unpack_from("<IIII", data, at + 8)
        n = min(rawsize, vsize or rawsize, len(data) - raw)
        out[rva:rva + n] = data[raw:raw + n]
    return bytes(out)


def code_section(img):
    sections = img.code()
    _, start, size, _ = sections[0]
    return start, size


def find(img, pattern, limit=4):
    sys.path.insert(0, str(Path(__file__).resolve().parent))
    import make_signatures as ms   # noqa: E402
    start, size = code_section(img)
    hits = ms.find_all(img.data[start:start + size], pattern, limit)
    return [start + h for h in (hits or [])]


def check(doc, version, image):
    img = open_image(image)
    bad = 0
    for s in doc["signatures"]:
        at = s["builds"].get(version)
        if not at:
            continue
        pattern = parse_pattern(at["pattern"])
        rva = int(at["rva"], 16)
        here = matches_at(img, pattern, rva)
        hits = find(img, pattern)
        # unique: false = the pattern is known to match elsewhere too; only "it matches at its RVA" is required
        ok = here and (hits == [rva] or not at.get("unique", True) or here == "hooked")
        bad += not ok
        print(f"{'OK' if ok else 'NG'}\t{s['name']}\t{at['rva']}\tat rva: {here or 'no'}\t{len(hits)} hit(s) {[hex(h) for h in hits][:4]}")
    print(f"{bad} problem(s)")
    return bad == 0


def matches_at(img, pattern, rva):
    """'yes', 'hooked' (a 5-byte jump of a hook in the image; the rest matches) or ''."""
    code = img.data[rva:rva + len(pattern)]
    if len(code) == len(pattern) and all(b is None or code[i] == b for i, b in enumerate(pattern)):
        return "yes"
    if len(code) == len(pattern) and code[:1] == b"\xe9" and all(b is None or i < 5 or code[i] == b for i, b in enumerate(pattern)):
        return "hooked"
    return ""


def newest_build(doc, s):
    order = [b["version"] for b in doc["supported"]]
    have = [v for v in order if v in s["builds"]]
    return have[-1] if have else None


def scan(doc, image, out=None):
    """For a new game build: where the newest known pattern of each signature is now."""
    img = open_image(image)
    rows = []
    for s in doc["signatures"]:
        v = newest_build(doc, s)
        if not v:
            continue
        hits = find(img, parse_pattern(s["builds"][v]["pattern"]))
        state = "OK" if len(hits) == 1 else ("MULTI" if hits else "NG")
        rows.append({"name": s["name"], "state": state, "fatal": s["fatal"], "from": v,
                     "rva": hex(hits[0]) if len(hits) == 1 else "", "candidates": [hex(h) for h in hits]})
        print(f"{state}\t{'FATAL' if s['fatal'] else '-'}\t{s['name']}\t{rows[-1]['rva'] or rows[-1]['candidates']}")
    print(f"{sum(r['state'] == 'OK' for r in rows)} OK, {sum(r['state'] == 'MULTI' for r in rows)} MULTI, "
          f"{sum(r['state'] == 'NG' for r in rows)} NG")
    if out:
        Path(out).write_text(json.dumps(rows, indent=1) + "\n", encoding="utf-8")
    return rows


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--sigs", default=str(SIGS))
    ap.add_argument("--check", nargs=2, metavar=("VERSION", "IMAGE"))
    ap.add_argument("--scan", metavar="IMAGE")
    ap.add_argument("--json", help="with --scan: write the result as JSON too")
    ap.add_argument("--add-build", nargs=2, metavar=("VERSION", "GAME"),
                    help="add a new supported build (its exe timestamp and image size are read from GAME)")
    args = ap.parse_args(argv)
    doc = load(args.sigs)
    problems = validate(doc)
    if problems:
        print("tools/sigs.json has problems:\n  " + "\n  ".join(problems), file=sys.stderr)
        return 1
    if args.add_build:
        version, game = args.add_build
        if not VERSION.fullmatch(version):
            print("the version has four parts, e.g. 1.26.70.1", file=sys.stderr)
            return 1
        img = open_image(game)
        import struct
        pe = struct.unpack_from("<I", img.data, 0x3C)[0]
        stamp, size = struct.unpack_from("<I", img.data, pe + 8)[0], struct.unpack_from("<I", img.data, pe + 24 + 56)[0]
        if any(b["version"] == version for b in doc["supported"]):
            print(f"{version} is already supported", file=sys.stderr)
            return 1
        fatal_missing = [s["name"] for s in doc["signatures"] if s["fatal"] and version not in s["builds"]]
        if fatal_missing:
            print("these fatal signatures have no entry for " + version + " yet: " + ", ".join(fatal_missing), file=sys.stderr)
            return 1
        doc["supported"].append({"version": version, "timestamp": f"0x{stamp:08X}", "imageSize": f"0x{size:X}"})
        problems = validate(doc)
        if problems:
            print("not added:\n  " + "\n  ".join(problems), file=sys.stderr)
            return 1
        Path(args.sigs).write_text(json.dumps(doc, ensure_ascii=False, indent=1) + "\n", encoding="utf-8")
        write_tables(doc)
        print(f"added {version} (exe 0x{stamp:08X}, image size 0x{size:X}); tables written")
        return 0
    if args.check:
        return 0 if check(doc, *args.check) else 1
    if args.scan:
        scan(doc, args.scan, args.json)
        return 0
    write_tables(doc)
    print(f"sigs: {len(doc['signatures'])} signatures, {len(doc.get('offsets', []))} offsets, "
          f"versions {', '.join(b['version'] for b in doc['supported'])} -> {RVA_TABLE.name}, {SIG_TABLE.name}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
