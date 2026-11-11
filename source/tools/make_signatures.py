#!/usr/bin/env python3
"""Make byte-pattern signatures for game functions from a game image (client/GameImageDump.cpp).

For each function (RVA + name) the instructions from its start are read with capstone, and the parts that move
between game versions are written as ?? (wildcards):
  * relative call / jump / branch targets (E8, E9, EB, 7x, 0F 8x ...),
  * RIP-relative displacements (globals, strings, vftables: [rip+disp32]).
Instructions are added one at a time until the pattern matches exactly once in the image's .text (and at the
function's own RVA). The result is a "name|pattern" list, which tools/analyze_game_image.py --sigs reads, so the
same patterns can be checked against the image of a newer game version.

Usage:
  pip install capstone
  python tools/make_signatures.py IMAGE --functions hooked_functions.txt --out signatures.txt
The functions file has lines "0xRVA <tab> label <tab> symbol" (the third column is the name written out;
lines starting with # are skipped).
"""
import argparse
import re
import sys
from pathlib import Path

try:
    import capstone
    from capstone import x86
except ImportError:  # pragma: no cover
    sys.exit("make_signatures.py needs capstone: pip install capstone")

sys.path.insert(0, str(Path(__file__).resolve().parent))
from analyze_game_image import Image  # noqa: E402

MAX_BYTES = 192   # longest pattern tried
MIN_BYTES = 12    # shortest pattern written (a few extra bytes keep it unique in the next version too)


def instruction_mask(ins):
    """Bytes of one instruction with the version-dependent parts set to None."""
    raw = list(ins.bytes)
    size = len(raw)
    wild = [False] * size
    # [rip + disp32]: the displacement is the 4 bytes at disp_offset.
    if ins.disp_offset and any(op.type == x86.X86_OP_MEM and op.mem.base == x86.X86_REG_RIP for op in ins.operands):
        for k in range(ins.disp_offset, min(size, ins.disp_offset + ins.disp_size)):
            wild[k] = True
    # relative branches: the immediate is the target.
    if ins.group(capstone.CS_GRP_JUMP) or ins.group(capstone.CS_GRP_CALL):
        if any(op.type == x86.X86_OP_IMM for op in ins.operands) and ins.imm_offset:
            for k in range(ins.imm_offset, size):
                wild[k] = True
    return [None if w else b for b, w in zip(raw, wild)]


def function_pattern(code: bytes, start: int):
    """Yields the pattern growing one instruction at a time (list of int-or-None)."""
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
    md.detail = True
    pattern = []
    for ins in md.disasm(code, start):
        pattern += instruction_mask(ins)
        yield pattern[:]
        if len(pattern) >= MAX_BYTES:
            return
        if ins.mnemonic in ("ret", "int3", "jmp") and not ins.group(capstone.CS_GRP_CALL):
            # end of the function (a tail jump or return): nothing more is the same function.
            if ins.mnemonic != "jmp" or ins.bytes[0] in (0xE9, 0xEB):
                return


def anchor(pattern):
    """The longest run of concrete bytes (offset, bytes) for a first search."""
    best = (0, b"")
    run_start, run = None, bytearray()
    for i, b in enumerate(pattern + [None]):
        if b is None:
            if len(run) > len(best[1]):
                best = (run_start, bytes(run))
            run_start, run = None, bytearray()
        else:
            if run_start is None:
                run_start = i
            run.append(b)
    return best


def matches(text: bytes, pattern, at: int) -> bool:
    if at < 0 or at + len(pattern) > len(text):
        return False
    for k, b in enumerate(pattern):
        if b is not None and text[at + k] != b:
            return False
    return True


def find_all(text: bytes, pattern, limit=None):
    off, needle = anchor(pattern)
    if not needle:
        return None
    out = []
    i = text.find(needle)
    while i != -1:
        start = i - off
        if matches(text, pattern, start):
            out.append(start)
            if limit and len(out) >= limit:
                break
        i = text.find(needle, i + 1)
    return out


def short_name(symbol: str) -> str:
    """"?method@Class@@..." -> "Class::method"; other text as it is."""
    m = re.match(r"\?(\w+)@(\w+)@@", symbol)
    return f"{m.group(2)}::{m.group(1)}" if m else symbol


def to_text(pattern):
    return " ".join("??" if b is None else f"{b:02X}" for b in pattern)


def make(img: Image, rva: int):
    """(pattern, hits) for the function at rva: hits == 1 when it is unique."""
    sections = img.code()
    if not sections:
        return None, 0
    _, t_rva, t_size, _ = sections[0]
    text = img.data[t_rva:t_rva + t_size]
    code = img.data[rva:rva + MAX_BYTES + 16]
    candidates = None
    last = None
    for pattern in function_pattern(code, rva):
        last = pattern
        if candidates is None:
            found = find_all(text, pattern)
            if found is None:
                continue
            candidates = [t_rva + c for c in found]
        else:
            candidates = [c for c in candidates if matches(img.data, pattern, c)]
        if len(pattern) >= MIN_BYTES and candidates == [rva]:
            return pattern, 1
    if last is None:
        return None, 0
    if candidates is None:
        candidates = [t_rva + c for c in (find_all(text, last) or [])]
    return last, len(candidates)


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("image")
    ap.add_argument("--functions", required=True)
    ap.add_argument("--out", required=True)
    args = ap.parse_args(argv)
    img = Image(Path(args.image).read_bytes(), 0)  # patterns use RVAs only
    rows = []
    for line in Path(args.functions).read_text(encoding="utf-8").splitlines():
        if not line.strip() or line.startswith("#"):
            continue
        parts = line.split("\t")
        if len(parts) < 3 or parts[1].startswith("("):
            continue
        rva, label, symbol = int(parts[0], 16), parts[1], parts[2]
        pattern, hits = make(img, rva)
        rows.append((label, symbol, rva, pattern, hits))
        print(f"{label}: {'unique' if hits == 1 else f'{hits} hits'} ({len(pattern) if pattern else 0} bytes)", file=sys.stderr)
    out = [f"# made by tools/make_signatures.py from {Path(args.image).name}",
           "# name|pattern  (?? = moves between versions); '# not unique' lines did not get to one match"]
    for label, symbol, rva, pattern, hits in rows:
        name = f"{short_name(symbol)} [{label}]"  # the label keeps overloads apart (analyze_game_image.py keys on names)
        if pattern:  # a pattern that is not unique is kept as a comment (analyze_game_image.py skips # lines)
            prefix = "" if hits == 1 else f"# not unique ({hits} matches): "
            out.append(f"{prefix}{name}|{to_text(pattern)}")
    Path(args.out).write_text("\n".join(out) + "\n", encoding="utf-8")
    unique = sum(1 for r in rows if r[4] == 1)
    print(f"{unique} of {len(rows)} functions have a unique pattern -> {args.out}", file=sys.stderr)


if __name__ == "__main__":
    main()
