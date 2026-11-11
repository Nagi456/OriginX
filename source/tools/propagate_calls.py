#!/usr/bin/env python3
"""Find a game function of one version in another by who calls it (2026-11-10).

tools/match_functions.py compares a function's own shape; a small getter looks like a hundred others. This uses
its callers instead. A caller that refers to a string no other function refers to, in both images, is the same
function in both (match_functions.anchors). Inside such a pair the direct calls come in the same order, so the
call that reached the old function reaches the new one. Every such caller is one vote; a function nobody calls
directly (a virtual one) is found by its place in the table of code pointers it sits in, from the neighbours
of that table that are anchors.

Usage:
  python tools/propagate_calls.py OLD.image NEW.image --base-old 0x... --base-new 0x... --functions FILE --out FILE
FILE has "0xRVA <tab> label [<tab> symbol]" lines. Output: label, symbol, new RVA or '-', verdict, votes.
A verdict is evidence for a person to check, not permission to call the address (CLAUDE.md).
"""
import argparse
import bisect
import struct
import sys
from collections import Counter, defaultdict
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))
import match_functions as mf  # noqa: E402
from match_functions import Game, anchors, read_functions  # noqa: E402


def direct_branches(game: Game):
    """(site, target) arrays of every E8 (call) / E9 (jmp) rel32 in .text whose target is a function start."""
    t0, t1 = game.text[1], game.text[1] + game.text[2]
    data = np.frombuffer(game.data, dtype=np.uint8, count=t1 - t0 - 5, offset=t0)
    sites = np.flatnonzero((data == 0xE8) | (data == 0xE9)).astype(np.int64)
    raw = np.frombuffer(game.data, dtype=np.uint8)
    rel = (raw[t0 + sites + 1].astype(np.int64) | (raw[t0 + sites + 2].astype(np.int64) << 8) |
           (raw[t0 + sites + 3].astype(np.int64) << 16) | (raw[t0 + sites + 4].astype(np.int64) << 24))
    rel = np.where(rel >= 1 << 31, rel - (1 << 32), rel)
    targets = sites + t0 + 5 + rel
    ok = (targets >= t0) & (targets < t1)
    return sites[ok] + t0, targets[ok]


def calls_in(game: Game, start, end):
    """The direct call / jmp targets of the function, in order, as (address of the instruction, target, kind)."""
    out = []
    code = game.data[start:min(end, start + 65536)]
    for ins in mf._md.disasm(code, start):
        if ins.mnemonic == "int3":
            break
        if ins.mnemonic in ("call", "jmp") and ins.operands and ins.operands[0].type == mf.x86.X86_OP_IMM:
            target = ins.operands[0].imm
            if not (start <= target < end):   # a jump inside the function is not a call
                out.append((ins.address, target, ins.mnemonic))
    return out


def grow(old: Game, new: Game, pairs, rounds=8, log=print):
    """Spreads the anchors along the call graph: inside a pair of the same function the direct calls line up one
    to one, so each call names a pair of callees. A callee pair named by two or more callers, and by five times
    more of them than any other answer, becomes a pair itself, and its own calls are followed in the next round.
    Returns (pairs, votes): votes[old] = Counter of new addresses, from every pair's calls."""
    pairs = dict(pairs)
    votes = defaultdict(Counter)
    cache_old, cache_new = {}, {}
    todo = list(pairs.items())
    for n in range(rounds):
        for a0, b0 in todo:
            f, g = old.function_at(a0), new.function_at(b0)
            if not f or not g or f[0] != a0 or g[0] != b0:
                continue
            a = cache_old.setdefault(a0, calls_in(old, *f))
            b = cache_new.setdefault(b0, calls_in(new, *g))
            if len(a) != len(b) or [k for _, _, k in a] != [k for _, _, k in b]:
                continue   # the function changed: its calls cannot be lined up one to one
            for (_, t, _), (_, t2, _) in zip(a, b):
                votes[t][t2] += 1
        todo = []
        taken = set(pairs.values())
        for t, c in votes.items():
            if t in pairs:
                continue
            (answer, top), *rest = c.most_common(2)
            if top >= 2 and (rest[0][1] if rest else 0) * 5 <= top and answer not in taken:
                pairs[t] = answer
                taken.add(answer)
                todo.append((t, answer))
        log(f"round {n + 1}: {len(pairs)} pairs (+{len(todo)})")
        if not todo:
            break
    return pairs, votes


def by_matched_callers(old: Game, new: Game, pairs, anchor_list, sites, targets, rva, limit=40):
    """Votes from the callers of old function rva. A caller that is not a pair yet is looked for with
    match_functions.match and taken only when that answer is clear (score, margin over the runner-up, shape).
    The calls of the two are lined up as sequences (difflib), so a caller with a call added or removed elsewhere
    still votes; the call that reached rva votes for what the call at the same place reaches."""
    votes, used, seen = Counter(), 0, set()
    for site in sites[targets == rva]:
        f = old.function_at(int(site))
        if not f or f[0] in seen:
            continue
        seen.add(f[0])
        if len(seen) > limit:
            break
        there = pairs.get(f[0])
        if there is None:
            how, results, count = mf.match(old, new, f[0], anchor_list)
            if not results or count < 12:
                continue
            best = results[0]
            second = results[1][0] if len(results) > 1 else 0.0
            if best[0] < 0.55 or best[0] - second < 0.15 or best[1] < 0.6:
                continue
            there = best[3]
        g = new.function_at(there)
        if not g or g[0] != there:
            continue
        a, b = calls_in(old, *f), calls_in(new, *g)
        matcher = mf.difflib.SequenceMatcher(None, [k for _, _, k in a], [k for _, _, k in b], autojunk=False)
        # kinds alone line up loosely: also require the neighbours of the call to be known pairs where they are
        index = {}
        for block in matcher.get_matching_blocks():
            for i in range(block.size):
                index[block.a + i] = block.b + i
        if len(a) != len(b):
            # unequal: trust the alignment only if every call that is already a pair agrees with it
            agree = [pairs.get(a[i][1]) == b[j][1] for i, j in index.items() if a[i][1] in pairs]
            if not agree or not all(agree):
                continue
        mine = Counter()
        for i, (at, target, _) in enumerate(a):
            if target == rva and i in index:
                mine[b[index[i]][1]] += 1
        if len(mine) == 1:   # every call to it in this caller reaches one function in the new one
            votes[next(iter(mine))] += 1
            used += 1
    return votes, used


def pointer_places(game: Game, base):
    """function start -> places in the read-only data holding a pointer to it."""
    r0, r1 = game.rdata[1], game.rdata[1] + game.rdata[2]
    r0 += (-r0) % 8
    values = np.frombuffer(game.data, dtype="<u8", count=(r1 - r0) // 8, offset=r0)
    t0, t1 = game.text[1] + base, game.text[1] + game.text[2] + base
    index = np.flatnonzero((values >= t0) & (values < t1))
    places = defaultdict(list)
    for i in index:
        places[int(values[i]) - base].append(r0 + int(i) * 8)
    return places


def by_table(old: Game, new: Game, pairs, old_places, new_places, base_new, rva, reach=40):
    """Votes from the tables of code pointers (vtables) the old function sits in. A paired neighbour d slots away
    names where the same table is in the new image; a class and the classes derived from it share inherited
    entries, so for each old table only the new table named by the most neighbours counts (and by three at least).
    Returns Counter of new addresses -> number of old tables that gave it."""
    votes = Counter()
    for place in old_places.get(rva, ()):
        support = Counter()   # where this slot would be in the new image -> neighbours that say so
        for d in range(-reach, reach + 1):
            if not d:
                continue
            fn = old_slot.get(place + d * 8)
            if fn is None or fn not in pairs:
                continue
            for there in new_places.get(pairs[fn], ()):
                support[there - d * 8] += 1
        if not support:
            continue
        best = max(support.values())
        if best < 3:
            continue
        for q, n in support.items():
            if n == best:
                value = struct.unpack_from("<Q", new.data, q)[0] - base_new
                if new.text[1] <= value < new.text[1] + new.text[2]:
                    votes[value] += 1
    return votes


old_slot = {}


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("old")
    ap.add_argument("new")
    ap.add_argument("--base-old", type=lambda v: int(v, 0), required=True)
    ap.add_argument("--base-new", type=lambda v: int(v, 0), required=True)
    ap.add_argument("--functions", required=True)
    ap.add_argument("--out", required=True)
    args = ap.parse_args(argv)
    old, new = Game(Path(args.old)), Game(Path(args.new))
    anchor_list = anchors(old, new)
    first = dict(anchor_list)
    pairs, call_votes = grow(old, new, first, log=lambda t: print(t, file=sys.stderr, flush=True))
    sites, targets = direct_branches(old)
    old_places, new_places = pointer_places(old, args.base_old), pointer_places(new, args.base_new)
    for fn, places in old_places.items():
        for p in places:
            old_slot[p] = fn
    lines = ["# made by tools/propagate_calls.py: " + Path(args.old).name + " -> " + Path(args.new).name,
             f"# anchors (functions with a string of their own in both images): {len(first)}; pairs after following calls: {len(pairs)}",
             "# label <tab> symbol <tab> new RVA <tab> verdict <tab> caller votes <tab> table votes <tab> shape of the answer"]
    for rva, label, symbol in read_functions(Path(args.functions)):
        cv, _ = by_matched_callers(old, new, pairs, anchor_list, sites, targets, rva)
        cv = cv + Counter({k: v for k, v in call_votes.get(rva, Counter()).items() if k not in cv})
        tv = by_table(old, new, pairs, old_places, new_places, args.base_new, rva)
        if rva in first:
            answer, verdict = first[rva], "anchor"
        else:
            # callers and tables are two kinds of evidence: they must not disagree
            ca = cv.most_common(2)
            ta = tv.most_common(2)
            c_ok = bool(ca) and ca[0][1] >= 2 and (ca[1][1] if len(ca) > 1 else 0) * 5 <= ca[0][1]
            t_ok = bool(ta) and (ta[1][1] if len(ta) > 1 else 0) * 5 <= ta[0][1]
            if c_ok and t_ok:
                answer = ca[0][0]
                verdict = "agreed, callers+table" if ta[0][0] == answer else "CONFLICT callers/table"
            elif c_ok:
                answer, verdict = ca[0][0], ("agreed, callers" if not ta else "callers (table unclear)")
            elif t_ok:
                answer, verdict = ta[0][0], ("agreed, table" if not ca else "table (callers unclear)")
            elif ca or ta:
                answer, verdict = (ca or ta)[0][0], "weak"
            else:
                answer, verdict = None, "no evidence"
        shape = ""
        if answer is not None:
            f = old.function_at(rva)
            g = new.function_at(answer)
            if f and g and g[0] == answer:
                a, _ = old.describe(rva, f[1] if f[1] > rva else rva + 4096)
                b, _ = new.describe(answer, g[1] if g[1] > answer else answer + 4096)
                shape = f"{mf.similarity(a, b):.2f} ({len(a)}/{len(b)} instructions)"
            else:
                shape = "not a function start"
        if answer is not None and verdict not in ("anchor",):
            f = old.function_at(rva)
            g = new.function_at(answer)
            if not (f and g and g[0] == answer):
                verdict = "REJECTED (not a function start): " + verdict
            else:
                la, lb = len(a), len(b)
                if mf.similarity(a, b) < 0.5 or max(la, lb) > 2 * max(1, min(la, lb)) + 4:
                    verdict = "REJECTED (another shape): " + verdict
        show = lambda c: ", ".join(f"0x{k:x} x{v}" for k, v in c.most_common(3)) or "-"
        line = "\t".join([label, symbol, f"0x{answer:x}" if answer is not None else "-", verdict,
                          show(cv), show(tv), shape])
        lines.append(line)
        print(line, flush=True)
    Path(args.out).write_text("\n".join(lines) + "\n", encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
