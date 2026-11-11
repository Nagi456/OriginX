#!/usr/bin/env python3
"""Find game functions of one game version in the image of another (client/GameImageDump.cpp images).

Byte patterns (tools/make_signatures.py) break when a version moves a struct member or a stack size. This
matches whole functions instead, by what changes least between versions:
  * the strings a function refers to (lea/mov [rip+...] into .rdata): a rare string (an error text, a file
    name) is referred to by a few functions only, in every version;
  * the shape of its instructions: mnemonics and registers, with every number (offsets, sizes, addresses)
    left out, compared as a sequence.
Every function start comes from .pdata (the exception table), so candidates are real function starts.

Usage:
  pip install capstone
  python tools/match_functions.py OLD.image NEW.image --functions OLD/hooked_functions.txt [--truth NEW/hooked_functions.txt] --out FILE
The functions file has "0xRVA <tab> label [<tab> symbol]" lines. With --truth (the same labels with their RVAs in
the new image, e.g. from LeviLamina's symbols), each answer is marked right or wrong, to measure the method.
"""
import argparse
import bisect
import difflib
import math
import re
import struct
import sys
from collections import defaultdict
from pathlib import Path

try:
    import capstone
    from capstone import x86
except ImportError:  # pragma: no cover
    sys.exit("match_functions.py needs capstone: pip install capstone")

sys.path.insert(0, str(Path(__file__).resolve().parent))
from analyze_game_image import Image  # noqa: E402

MAX_INSTRUCTIONS = 400   # instructions compared per function
SHAPE_CANDIDATES = 40    # functions compared by shape when no string narrows the search

_md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
_md.detail = True
_LEA_RIP = re.compile(rb"[\x48\x4c]\x8d[\x05\x0d\x15\x1d\x25\x2d\x35\x3d]", re.DOTALL)


class Game:
    """An image with its function list (.pdata) and every lea-[rip] reference into .rdata."""

    def __init__(self, path: Path):
        self.img = Image(path.read_bytes(), 0)
        self.data = self.img.data
        sec = {s[0]: s for s in self.img.sections}
        self.text, self.rdata, pdata = sec[".text"], sec[".rdata"], sec[".pdata"]
        ends = {}
        for at in range(pdata[1], pdata[1] + pdata[2], 12):
            begin, end, _ = struct.unpack_from("<III", self.data, at)
            if begin == 0:
                break
            ends[begin] = max(end, ends.get(begin, 0))
        # Leaf functions (no stack frame) and jump thunks have no .pdata entry. The compiler starts every
        # function on 16 bytes after int3 padding, so those starts are found from the padding.
        t0, t1 = self.text[1], self.text[1] + self.text[2]
        for m in re.finditer(rb"\xcc[^\xcc]", self.data[t0:t1]):
            at = t0 + m.start() + 1
            if at % 16 == 0 and at not in ends:
                ends[at] = 0
        self.starts = sorted(ends)
        self.ends = []
        for i, b in enumerate(self.starts):  # an end from .pdata, or the next start
            nxt = self.starts[i + 1] if i + 1 < len(self.starts) else t1
            self.ends.append(ends[b] if ends[b] else nxt)
        # string address -> places referring to it
        self.refs = defaultdict(list)
        t0, t1 = self.text[1], self.text[1] + self.text[2]
        r0, r1 = self.rdata[1], self.rdata[1] + self.rdata[2]
        for m in _LEA_RIP.finditer(self.data, t0, t1):
            at = m.start()
            target = at + 7 + struct.unpack_from("<i", self.data, at + 3)[0]
            if r0 <= target < r1:
                self.refs[target].append(at)
        self._strings = {}

    def function_at(self, rva):
        """(start, end) of the .pdata function holding rva."""
        i = bisect.bisect_right(self.starts, rva) - 1
        if i < 0:
            return None
        return self.starts[i], self.ends[i]

    def string_at(self, rva):
        if rva in self._strings:
            return self._strings[rva]
        raw = self.data[rva:rva + 160].split(b"\0")[0]
        text = raw.decode("ascii") if len(raw) >= 4 and all(32 <= c < 127 for c in raw) else None
        self._strings[rva] = text
        return text

    def string_addresses(self, text: str):
        """Every place in .rdata where text sits as a whole (zero-terminated, at a string start)."""
        needle = text.encode("ascii") + b"\0"
        out, start, end = [], self.rdata[1], self.rdata[1] + self.rdata[2]
        i = self.data.find(needle, start, end)
        while i != -1:
            if i == start or self.data[i - 1] == 0 or i in self.refs:
                out.append(i)
            i = self.data.find(needle, i + 1, end)
        return out

    def string_users(self):
        """text -> set of function starts referring to it (from the lea-[rip] references)."""
        users = defaultdict(set)
        for address, places in self.refs.items():
            text = self.string_at(address)
            if not text:
                continue
            for at in places:
                f = self.function_at(at)
                if f:
                    users[text].add(f[0])
        return users

    def describe(self, start, end):
        """(instruction shapes, strings referred to) of the function [start, end)."""
        shapes, strings = [], []
        code = self.data[start:min(end, start + 16384)]
        for n, ins in enumerate(_md.disasm(code, start)):
            if n >= MAX_INSTRUCTIONS or ins.mnemonic == "int3":  # int3 is the padding after a function
                break
            parts = []
            for op in ins.operands:
                if op.type == x86.X86_OP_REG:
                    parts.append(ins.reg_name(op.reg))
                elif op.type == x86.X86_OP_IMM:
                    parts.append("i")
                elif op.type == x86.X86_OP_MEM:
                    if op.mem.base == x86.X86_REG_RIP:
                        target = ins.address + ins.size + op.mem.disp
                        s = self.string_at(target) if self.rdata[1] <= target < self.rdata[1] + self.rdata[2] else None
                        if s:
                            strings.append(s)
                        parts.append("[rip]")
                    else:
                        base = ins.reg_name(op.mem.base) if op.mem.base else ""
                        index = ("+" + ins.reg_name(op.mem.index)) if op.mem.index else ""
                        parts.append(f"[{base}{index}]")
            shapes.append(ins.mnemonic + " " + ",".join(parts))
        return shapes, strings


def string_weights(game: Game, strings):
    """Rarer strings count more: log(functions in the game / functions referring to it)."""
    weights = {}
    for s in set(strings):
        users = set()
        for address in game.string_addresses(s):
            for at in game.refs.get(address, ()):
                f = game.function_at(at)
                if f:
                    users.add(f[0])
        weights[s] = math.log(len(game.starts) / max(1, len(users)))
    return weights


def candidates_by_strings(new: Game, strings, weights):
    """New-image functions referring to the old function's rarer strings, with summed weights."""
    score = defaultdict(float)
    for s in set(strings):
        if weights.get(s, 0) < 4.0:   # used by more than ~2 % of the functions: too common to narrow anything
            continue
        for address in new.string_addresses(s):
            for at in new.refs.get(address, ()):
                f = new.function_at(at)
                if f:
                    score[f] += weights[s]
    return sorted(score.items(), key=lambda kv: -kv[1])


def anchors(old: Game, new: Game):
    """(old start, new start) pairs from strings that exactly one function refers to in both images."""
    old_users, new_users = old.string_users(), new.string_users()
    pairs = {}
    for text, users in old_users.items():
        other = new_users.get(text)
        if len(users) == 1 and other and len(other) == 1:
            a, b = next(iter(users)), next(iter(other))
            if pairs.get(a, b) != b:
                pairs[a] = None   # one old function, two answers: unreliable
            elif a not in pairs:
                pairs[a] = b
    return sorted((a, b) for a, b in pairs.items() if b is not None)


NEAREST = 12          # anchors looked at around a function
SAME_PLACE = 0x40000
SURE_SCORE = 0.30     # a match is "sure" with at least this score ...
SURE_MARGIN = 0.12    # ... and this much ahead of the runner-up ...
SURE_INSTRUCTIONS = 8 # ... for a function of at least this many instructions  # offsets this close are the same move (one source file moved as a block)


def window(anchor_list, old_rva):
    """Where the function of old_rva should be in the new image. The linker moves a source file's functions as
    one block, so the anchors nearest to old_rva that moved by about the same offset give that offset; the
    window is the function's own position plus that offset, widened by how far the agreeing anchors are."""
    olds = [a for a, _ in anchor_list]
    i = bisect.bisect_left(olds, old_rva)
    near = anchor_list[max(0, i - NEAREST // 2):i + NEAREST // 2]
    if not near:
        return None
    # the group of agreeing offsets that weighs most; nearer anchors weigh more (a tie between two blocks goes
    # to the block the function sits in)
    best, best_weight = [], 0.0
    for _, (a0, b0) in enumerate(near):
        o = b0 - a0
        group = [(a, b) for a, b in near if abs((b - a) - o) <= SAME_PLACE]
        weight = sum(1.0 / (1.0 + abs(a - old_rva) / 0x1000) for a, _ in group)
        if weight > best_weight:
            best, best_weight = group, weight
    if len(best) < 2:
        return None
    offsets = sorted(b - a for a, b in best)
    offset = offsets[len(offsets) // 2]
    reach = max(abs(a - old_rva) for a, _ in best)
    reach = min(max(reach, 0x4000), 0x60000)
    centre = old_rva + offset
    return (centre - reach, centre + reach)


def similarity(a, b):
    m = difflib.SequenceMatcher(None, a, b, autojunk=False)
    return m.ratio()


def match(old: Game, new: Game, rva, anchor_list=None):
    f = old.function_at(rva)
    start = rva
    end = f[1] if f and f[1] > rva else rva + 4096
    old_shapes, old_strings = old.describe(start, end)
    weights = string_weights(old, old_strings)
    found = candidates_by_strings(new, old_strings, weights)
    how = "strings"
    candidates = [fn for fn, _ in found[:SHAPE_CANDIDATES]]
    span = window(anchor_list, rva) if anchor_list else None
    inside = set()
    if span:
        lo_i = bisect.bisect_left(new.starts, span[0])
        hi_i = bisect.bisect_right(new.starts, span[1])
        size = end - start
        for i in range(lo_i, min(hi_i, lo_i + 3000)):
            s0, e0 = new.starts[i], new.ends[i]
            if 0.5 * size <= e0 - s0 <= 2.0 * size or abs((e0 - s0) - size) < 32:
                inside.add((s0, e0))
        how = "strings+order" if candidates else f"order ({hi_i - lo_i} functions between anchors)"
        candidates = list(dict.fromkeys(candidates + sorted(inside)))
    if not candidates:
        how = "shape"
        first = old.data[start]
        size = end - start
        lo, hi = size * 0.6, size * 1.6
        head = [s.split(" ")[0] for s in old_shapes[:8]]
        pool = []
        for s, e in zip(new.starts, new.ends):
            if lo <= e - s <= hi and new.data[s] == first:
                pool.append((s, e))
        ranked = []
        for s, e in pool:
            got = [ins.mnemonic for _, ins in zip(range(8), _md.disasm(new.data[s:s + 120], s))]
            ranked.append((sum(1 for a, b in zip(head, got) if a == b), (s, e)))
        ranked.sort(key=lambda x: -x[0])
        candidates = [fn for _, fn in ranked[:SHAPE_CANDIDATES]]
    results = []
    old_set = set(old_strings)
    for s, e in candidates:
        shapes, strings = new.describe(s, e)
        shape = similarity(old_shapes, shapes)
        common = old_set & set(strings)
        text = sum(weights.get(x, 0) for x in common) / max(1e-9, sum(weights.get(x, 0) for x in old_set)) if old_set else 0
        score = 0.6 * shape + 0.4 * text if old_set else shape
        if inside and (s, e) not in inside:
            score -= 0.15   # a string match far from where the order says it should be
        results.append((score, shape, text, s))
    results.sort(key=lambda r: -r[0])
    return how, results, len(old_shapes)


def read_functions(path: Path):
    rows = []
    for line in path.read_text(encoding="utf-8").splitlines():
        if not line.startswith("0x"):
            continue
        parts = line.split("\t")
        if len(parts) < 2 or parts[1].startswith("("):
            continue
        rows.append((int(parts[0], 16), parts[1], parts[2] if len(parts) > 2 else ""))
    return rows


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("old")
    ap.add_argument("new")
    ap.add_argument("--functions", required=True)
    ap.add_argument("--truth")
    ap.add_argument("--only", help="comma-separated labels")
    ap.add_argument("--out", required=True)
    args = ap.parse_args(argv)
    old, new = Game(Path(args.old)), Game(Path(args.new))
    anchor_list = anchors(old, new)
    print(f"{len(anchor_list)} anchor functions (strings only one function uses, in both versions)", file=sys.stderr)
    truth = {label: rva for rva, label, _ in read_functions(Path(args.truth))} if args.truth else {}
    only = set(args.only.split(",")) if args.only else None
    lines = [f"# made by tools/match_functions.py: {Path(args.old).name} -> {Path(args.new).name}",
             "# label <tab> symbol <tab> new RVA <tab> sure/unsure <tab> score (shape, strings) <tab> runner-up <tab> how <tab> check"]
    right = wrong = 0
    sure_right = sure_wrong = 0
    for rva, label, symbol in read_functions(Path(args.functions)):
        if only and label not in only:
            continue
        how, results, size = match(old, new, rva, anchor_list)
        if not results:
            lines.append(f"{label}\t{symbol}\t-\tunsure\t-\t-\t{how}\tnot found")
            print(f"{label}: nothing", file=sys.stderr)
            continue
        best = results[0]
        second = results[1][0] if len(results) > 1 else 0.0
        # sure: clearly better than every other candidate (the wrong answers in the 26.51 -> 26.60 check all
        # won by a hair)
        # a few instructions (a jump thunk) look like hundreds of others, so those are never sure
        sure = best[0] >= SURE_SCORE and best[0] - second >= SURE_MARGIN and size >= SURE_INSTRUCTIONS
        check = ""
        if label in truth:
            ok = truth[label] == best[3]
            right += ok
            wrong += not ok
            sure_right += ok and sure
            sure_wrong += (not ok) and sure
            check = "right" if ok else f"WRONG (answer {truth[label]:#x})"
        verdict = "sure" if sure else "unsure"
        lines.append(f"{label}\t{symbol}\t{best[3]:#x}\t{verdict}\t{best[0]:.2f} ({best[1]:.2f}, {best[2]:.2f})\t{second:.2f}\t{how}\t{check}")
        print(f"{label}: {best[3]:#x} {verdict} {best[0]:.2f} vs {second:.2f} [{how}] {check}", file=sys.stderr)
    if truth:
        lines.append(f"# checked against --truth: {right} right, {wrong} wrong; marked sure: {sure_right} right, {sure_wrong} wrong")
    Path(args.out).write_text("\n".join(lines) + "\n", encoding="utf-8")
    print(f"-> {args.out}" + (f" ({right} right, {wrong} wrong; sure ones: {sure_right} right, {sure_wrong} wrong)" if truth else ""), file=sys.stderr)


if __name__ == "__main__":
    main()
