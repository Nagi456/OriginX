#!/usr/bin/env python3
"""findsig: finds a broken signature again in a new Minecraft build (docs/version-update.md, step c).

Give it the game of the last supported build ("old") and the new one, and the names of the signatures that are NG
(from logs/sigcheck.log, tools/sigcheck_report.py prints this command for you). For each one it runs, in order, the
ways the addresses have been found by hand since 2026-11-05:

  1. pattern   the old pattern of sigs.json still matches exactly once in the new code (the function did not move
               or change at its start)
  2. strings   the strings only this function refers to, the order of the functions around it and the shape of its
               instructions (tools/match_functions.py)
  3. callers   functions known to be the same in both builds (a string of their own, then their calls followed both
               ways) call it from the same place: what the call at that place reaches now (tools/propagate_calls.py)
  4. vtable    it sits in a table of code pointers (a vtable): the neighbours that are known pairs say where the same
               slot is now (tools/propagate_calls.py)

A candidate is CONFIRMED when two different kinds of evidence name the same address and its instructions still look
like the old function (shape similarity >= 0.5 and not more than twice as long or short). With one kind of evidence
it is PROBABLE (look at it before using it: docs/version-update.md step d); otherwise UNKNOWN. For every candidate it
prints a new pattern and the 16 check bytes, and a sigs.json fragment. --apply writes the CONFIRMED ones into
tools/sigs.json under the new version (never the others, never over an existing entry).

The "game" files may be Minecraft.Windows.exe itself or a memory image written by client/GameImageDump.cpp
(<name>.image with <name>.image.txt beside it). Needs: pip install capstone numpy.

Usage:
  python tools/findsig.py --old-version 1.26.60.5 --old OLD.exe --new NEW.exe --new-version 1.26.70.1 --name FrameHook --name ...
  python tools/findsig.py ... --from-sigcheck logs/sigcheck.log     # every NG / MULTI signature in that log
  python tools/findsig.py ... --all                                  # every signature (to measure the method)
  add --apply to write the CONFIRMED results into tools/sigs.json, --out result.json for the details
"""
import argparse
import hashlib
import json
import struct
import sys
import tempfile
from collections import Counter
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import make_sigs  # noqa: E402

CACHE = Path(tempfile.gettempdir()) / "originx-findsig-cache"


def memory_image(path):
    """Path of a memory image for `path` (an exe is laid out at its RVAs once and cached) and its load base."""
    path = Path(path)
    data = path.read_bytes()
    info = path.with_name(path.name + ".txt")
    base = None
    if info.exists():
        for line in info.read_text(encoding="utf-8", errors="replace").splitlines():
            if line.startswith("base "):
                base = int(line.split()[1], 16)
    image = make_sigs.as_memory_image(data)
    pe = struct.unpack_from("<I", image, 0x3C)[0]
    if base is None:
        base = struct.unpack_from("<Q", image, pe + 24 + 24)[0]   # OptionalHeader.ImageBase (PE32+)
    if image is data:
        return path, base
    CACHE.mkdir(parents=True, exist_ok=True)
    out = CACHE / (hashlib.sha256(data).hexdigest()[:16] + ".image")
    if not out.exists():
        out.write_bytes(image)
    return out, base


def version_of(image_path):
    data = Path(image_path).read_bytes()[:4096]
    pe = struct.unpack_from("<I", data, 0x3C)[0]
    return struct.unpack_from("<I", data, pe + 8)[0], struct.unpack_from("<I", data, pe + 24 + 56)[0]


def pairs_for(old, new, old_path, new_path, log):
    """Functions known to be the same in both builds (cached per pair of games: it takes a few minutes)."""
    import match_functions as mf
    import propagate_calls as pc
    key = hashlib.sha256((str(old_path.stat().st_size) + old_path.name + str(new_path.stat().st_size) + new_path.name).encode()).hexdigest()[:16]
    cache = CACHE / f"pairs-{key}.json"
    if cache.exists():
        saved = json.loads(cache.read_text())
        return [tuple(p) for p in saved["anchors"]], {int(a): b for a, b in saved["pairs"].items()}
    log("  following calls between the two builds (a few minutes the first time; cached after)...")
    anchor_list = mf.anchors(old, new)
    pairs, _ = pc.grow(old, new, dict(anchor_list), log=lambda t: log("  " + t))
    CACHE.mkdir(parents=True, exist_ok=True)
    cache.write_text(json.dumps({"anchors": anchor_list, "pairs": {str(a): b for a, b in pairs.items()}}))
    return anchor_list, pairs


def shape_ok(old, new, a, b):
    import match_functions as mf
    f, g = old.function_at(a), new.function_at(b)
    if not f or not g or g[0] != b:
        return 0.0, "not a function start"
    x, _ = old.describe(a, f[1] if f[1] > a else a + 4096)
    y, _ = new.describe(b, g[1] if g[1] > b else b + 4096)
    sim = mf.similarity(x, y)
    if max(len(x), len(y)) > 2 * max(1, min(len(x), len(y))) + 4:
        return sim, f"length {len(x)} -> {len(y)} instructions"
    return sim, f"{len(x)} -> {len(y)} instructions"


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--old-version", required=True, help="a supported version in sigs.json (the build `--old` is)")
    ap.add_argument("--old", required=True, help="that build's Minecraft.Windows.exe or memory image")
    ap.add_argument("--new", required=True, help="the new build's Minecraft.Windows.exe or memory image")
    ap.add_argument("--new-version", help="the new build's version (four parts) for --apply and the fragments")
    ap.add_argument("--name", action="append", default=[], help="a signature name (repeat)")
    ap.add_argument("--from-sigcheck", help="take every NG / MULTI signature of this logs/sigcheck.log")
    ap.add_argument("--all", action="store_true", help="every signature that has an entry for --old-version")
    ap.add_argument("--apply", action="store_true", help="write the CONFIRMED results into tools/sigs.json")
    ap.add_argument("--out", help="write the details as JSON")
    args = ap.parse_args(argv)
    import make_signatures as ms
    import match_functions as mf
    import propagate_calls as pc
    log = lambda t: print(t, flush=True)  # noqa: E731

    doc = make_sigs.load()
    by_name = {s["name"]: s for s in doc["signatures"]}
    names = list(args.name)
    if args.from_sigcheck:
        for line in Path(args.from_sigcheck).read_text(encoding="utf-8", errors="replace").splitlines():
            cols = line.split("\t")
            head = cols[0].split(" ")[-1] if cols else ""
            if len(cols) > 2 and head in ("NG", "MULTI") and cols[1] not in names:
                names.append(cols[1])
    if args.all:
        names += [s["name"] for s in doc["signatures"] if args.old_version in s["builds"] and s["name"] not in names]
    unknown = [n for n in names if n not in by_name or args.old_version not in by_name[n]["builds"]]
    if unknown:
        log("not in sigs.json for " + args.old_version + ": " + ", ".join(unknown))
        names = [n for n in names if n not in unknown]
    if not names:
        log("nothing to look for (give --name, --from-sigcheck or --all)")
        return 1

    old_path, old_base = memory_image(args.old)
    new_path, new_base = memory_image(args.new)
    log(f"old {args.old_version}: {args.old}; new {args.new_version or '?'}: {args.new}")
    old, new = mf.Game(old_path), mf.Game(new_path)
    anchor_list, pairs = pairs_for(old, new, old_path, new_path, log)
    sites, targets = pc.direct_branches(old)
    old_places, new_places = pc.pointer_places(old, old_base), pc.pointer_places(new, new_base)
    pc.old_slot.clear()
    for fn, places in old_places.items():
        for p in places:
            pc.old_slot[p] = fn
    start, size = make_sigs.code_section(new.img)

    results = []
    for name in names:
        s = by_name[name]
        at = s["builds"][args.old_version]
        rva = int(at["rva"], 16)
        evidence = {}   # address -> list of evidence kinds
        notes = []
        # 1. the old pattern as it is
        hits = ms.find_all(new.data[start:start + size], make_sigs.parse_pattern(at["pattern"]), 3) or []
        if len(hits) == 1:
            evidence.setdefault(start + hits[0], []).append("pattern")
        notes.append(f"old pattern: {len(hits)} hit(s)" + (" (it matches several functions: not evidence)" if len(hits) > 1 else ""))
        # 2. strings, order, shape
        if rva in pairs:
            evidence.setdefault(pairs[rva], []).append("callgraph-pair")
            notes.append(f"a known pair through the call graph: 0x{pairs[rva]:x}")
        how, ranked, count = mf.match(old, new, rva, anchor_list)
        if ranked:
            best = ranked[0]
            second = ranked[1][0] if len(ranked) > 1 else 0.0
            if best[0] >= mf.SURE_SCORE and best[0] - second >= mf.SURE_MARGIN and count >= mf.SURE_INSTRUCTIONS:
                evidence.setdefault(best[3], []).append("strings/order/shape")
            notes.append(f"match_functions: 0x{best[3]:x} score {best[0]:.2f} (next {second:.2f}, {how})")
        # 3. callers
        votes, used = pc.by_matched_callers(old, new, pairs, anchor_list, sites, targets, rva)
        if votes:
            (top, n), *rest = votes.most_common(2)
            if n >= 2 and (rest[0][1] if rest else 0) * 5 <= n:
                evidence.setdefault(top, []).append("callers")
            notes.append(f"callers: {', '.join(f'0x{a:x} x{c}' for a, c in votes.most_common(3))} ({used} caller(s) lined up)")
        # 4. vtable neighbours
        tv = pc.by_table(old, new, pairs, old_places, new_places, new_base, rva)
        if tv:
            (top, n), *rest = tv.most_common(2)
            if (rest[0][1] if rest else 0) * 5 <= n:
                evidence.setdefault(top, []).append("vtable")
            notes.append(f"vtable: {', '.join(f'0x{a:x} x{c}' for a, c in tv.most_common(3))}")

        candidates = []
        for address, kinds in evidence.items():
            sim, shape = shape_ok(old, new, rva, address)
            kinds = sorted(set(kinds))
            strong = len(set(k for k in kinds if k != "callgraph-pair")) + ("callgraph-pair" in kinds)
            verdict = "CONFIRMED" if strong >= 2 and sim >= 0.5 and "length" not in shape else ("PROBABLE" if sim >= 0.5 else "WEAK")
            pattern, unique = ms.make(new.img, address)
            candidates.append({"rva": hex(address), "evidence": kinds, "shape": round(sim, 2), "shapeNote": shape,
                               "verdict": verdict, "pattern": ms.to_text(pattern) if pattern else "", "unique": unique == 1,
                               "check": " ".join(f"{b:02X}" for b in new.data[address:address + 16])})
        candidates.sort(key=lambda c: (c["verdict"] != "CONFIRMED", c["verdict"] != "PROBABLE", -len(c["evidence"]), -c["shape"]))
        verdict = candidates[0]["verdict"] if candidates else "UNKNOWN"
        if verdict == "CONFIRMED" and len([c for c in candidates if c["verdict"] == "CONFIRMED"]) > 1:
            verdict = "CONFLICT"
        results.append({"name": name, "fatal": s["fatal"], "oldRva": at["rva"], "verdict": verdict, "candidates": candidates, "notes": notes})
        log(f"\n== {name}  ({'FATAL' if s['fatal'] else 'not fatal'}; {s['purpose']})")
        log(f"   old {args.old_version} rva {at['rva']} -> {verdict}")
        for n in notes:
            log("   - " + n)
        for c in candidates[:4]:
            log(f"   {c['verdict']:9s} {c['rva']}  evidence: {', '.join(c['evidence'])}; shape {c['shape']} ({c['shapeNote']}); "
                f"new pattern {'unique' if c['unique'] else 'NOT unique'}")
        if candidates and verdict in ("CONFIRMED", "PROBABLE"):
            c = candidates[0]
            frag = {"rva": c["rva"].upper().replace("0X", "0x"), "pattern": c["pattern"], "unique": c["unique"], "check": c["check"]}
            log(f'   sigs.json "{args.new_version or "<new version>"}": ' + json.dumps(frag, ensure_ascii=False))

    counts = Counter(r["verdict"] for r in results)
    log("\nsummary: " + ", ".join(f"{k} {v}" for k, v in sorted(counts.items())))
    log("CONFIRMED: two kinds of evidence agree. PROBABLE / WEAK / CONFLICT / UNKNOWN: docs/version-update.md step d.")
    if args.out:
        Path(args.out).write_text(json.dumps(results, ensure_ascii=False, indent=1) + "\n", encoding="utf-8")
    if args.apply:
        if not args.new_version:
            log("--apply needs --new-version")
            return 1
        written = 0
        for r in results:
            if r["verdict"] != "CONFIRMED":
                continue
            c = r["candidates"][0]
            entry = by_name[r["name"]]["builds"]
            if args.new_version in entry:
                continue
            entry[args.new_version] = {"rva": c["rva"].upper().replace("0X", "0x"), "pattern": c["pattern"], "unique": c["unique"],
                                       "check": c["check"]}
            if not c["unique"]:
                entry[args.new_version]["note"] = "findsig: the new pattern also matches elsewhere; only the match at this RVA is checked"
            written += 1
        Path(make_sigs.SIGS).write_text(json.dumps(doc, ensure_ascii=False, indent=1) + "\n", encoding="utf-8")
        log(f"--apply: {written} CONFIRMED result(s) written into tools/sigs.json under {args.new_version} "
            "(add the version to \"supported\" with tools/make_sigs.py --add-build when every fatal one is there)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
