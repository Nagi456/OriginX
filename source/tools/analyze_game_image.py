"""Analyse a game image written by client/GameImageDump.cpp (the game's executable as it sat in memory).

Finds, without LeviLamina's symbol table:
  * every class with RTTI and its vftables (class name -> vftable RVA, entry count, entry RVAs), the way virtual
    game functions are found again in a new version (class + slot number);
  * how many of the byte patterns in a signature list (Flarial's SigInit.cpp ADD_SIG lines, or a plain
    "name|pattern" file) still match exactly once.

Usage:
  python tools/analyze_game_image.py IMAGE [--base 0x...] [--sigs SigInit.cpp ...] [--classes A,B] --out DIR
The base defaults to the one in IMAGE.txt (written next to the image). Outputs DIR/summary.txt,
DIR/vftables.txt (interesting classes, all entries) and DIR/all_classes.txt (every class, entry counts).
"""
import argparse
import re
import struct
import sys
from pathlib import Path


class Image:
    def __init__(self, data: bytes, base: int):
        self.data = data
        self.base = base
        if data[:2] != b'MZ':
            raise ValueError('not a PE image')
        pe = struct.unpack_from('<I', data, 0x3C)[0]
        if data[pe:pe + 4] != b'PE\0\0':
            raise ValueError('no PE header')
        count = struct.unpack_from('<H', data, pe + 6)[0]
        optional = struct.unpack_from('<H', data, pe + 20)[0]
        first = pe + 24 + optional
        self.sections = []
        for i in range(count):
            at = first + i * 40
            name = data[at:at + 8].rstrip(b'\0').decode('ascii', 'replace')
            vsize, rva, rawsize = struct.unpack_from('<III', data, at + 8)
            flags = struct.unpack_from('<I', data, at + 36)[0]
            self.sections.append((name, rva, max(vsize, rawsize), flags))

    def section(self, rva):
        for s in self.sections:
            if s[1] <= rva < s[1] + s[2]:
                return s
        return None

    def code(self):
        return [s for s in self.sections if s[3] & 0x20000000]

    def readonly(self):
        # .rdata-like: initialized data, readable, not writable, not code
        return [s for s in self.sections if (s[3] & 0x40000000) and not (s[3] & 0x80000000) and not (s[3] & 0x20000000)]

    def u32(self, rva):
        return struct.unpack_from('<I', self.data, rva)[0]

    def u64(self, rva):
        return struct.unpack_from('<Q', self.data, rva)[0]

    def is_code_va(self, va):
        rva = va - self.base
        return any(s[1] <= rva < s[1] + s[2] for s in self.code())


def demangle_type(name: str) -> str:
    # ".?AVLevelRenderer@@" / ".?AUFoo@ns@@" -> "LevelRenderer" / "ns::Foo" (templates are left as they are)
    body = name[4:]
    if body.endswith('@@'):
        body = body[:-2]
    if body.startswith('?$'):
        return body
    return '::'.join(reversed([p for p in body.split('@') if p]))


def find_classes(img: Image):
    """type descriptor RVA -> class name, for every '.?AV' / '.?AU' in the image."""
    out = {}
    for m in re.finditer(rb'\.\?A[VU][0-9A-Za-z_@?$<>]{1,400}?@@\0', img.data):
        name_at = m.start()
        td = name_at - 16
        if td < 0:
            continue
        out[td] = demangle_type(m.group(0)[:-1].decode('ascii', 'replace'))
    return out


def find_locators(img: Image, types):
    """CompleteObjectLocator RVA -> (class, offset of the subobject) for x64 locators (signature 1, pSelf)."""
    out = {}
    for s in img.readonly():
        start, end = s[1], s[1] + s[2] - 24
        data = img.data
        for at in range(start - start % 4, end, 4):
            if data[at] != 1 or data[at + 1] or data[at + 2] or data[at + 3]:
                continue
            sig, offset, cd, td, chd, self_rva = struct.unpack_from('<IIIIII', data, at)
            if self_rva == at and td in types:
                out[at] = (types[td], offset)
    return out


def find_vftables(img: Image, locators):
    """vftable RVA -> (class, subobject offset, entry RVAs). A vftable starts right after a pointer to its locator."""
    by_va = {img.base + rva: rva for rva in locators}
    out = {}
    for s in img.readonly():
        start, end = s[1], s[1] + s[2] - 16
        mv = memoryview(img.data)
        for at in range(start - start % 8, end, 8):
            va = struct.unpack_from('<Q', mv, at)[0]
            col = by_va.get(va)
            if col is None:
                continue
            table = at + 8
            entries = []
            k = table
            while k + 8 <= s[1] + s[2] and len(entries) < 4096:
                target = struct.unpack_from('<Q', mv, k)[0]
                if not img.is_code_va(target):
                    break
                entries.append(target - img.base)
                k += 8
            cls, offset = locators[col]
            out[table] = (cls, offset, entries)
    return out


def flarial_sigs(path: Path):
    text = path.read_text(encoding='utf-8', errors='replace')
    sigs = []
    # Only the newest block (the first init function) unless the file is a plain list
    for m in re.finditer(r'ADD_SIG\(\s*"([^"]+)"\s*,\s*"([^"]+)"\s*\)', text):
        sigs.append((m.group(1), m.group(2)))
    if not sigs:
        for line in text.splitlines():
            if '|' in line and not line.startswith('#'):
                name, pattern = line.split('|', 1)
                sigs.append((name.strip(), pattern.strip()))
    seen, unique = set(), []
    for name, pattern in sigs:  # the first (newest) pattern of each name
        if name not in seen:
            seen.add(name)
            unique.append((name, pattern))
    return unique


def pattern_regex(pattern: str):
    parts = []
    for token in pattern.split():
        if token in ('?', '??'):
            parts.append(b'.')
        else:
            parts.append(re.escape(bytes([int(token, 16)])))
    return re.compile(b''.join(parts), re.DOTALL)


def scan(img: Image, sigs):
    results = []
    code = img.code()
    for name, pattern in sigs:
        try:
            rx = pattern_regex(pattern)
        except ValueError:
            results.append((name, -1, []))
            continue
        hits = []
        for s in code:
            for m in rx.finditer(img.data, s[1], s[1] + s[2]):
                hits.append(m.start())
                if len(hits) > 5:
                    break
        results.append((name, len(hits), hits[:3]))
    return results


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('image', type=Path)
    parser.add_argument('--base', type=lambda v: int(v, 0))
    parser.add_argument('--sigs', type=Path, action='append', default=[])
    parser.add_argument('--classes', default='')
    parser.add_argument('--classes-from', type=Path, help='take the classes from the "?name@Class@@" symbols in this file')
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args(argv)
    base = args.base
    side = Path(str(args.image) + '.txt')
    if base is None and side.exists():
        m = re.search(r'^base (0x[0-9A-Fa-f]+)', side.read_text(), re.MULTILINE)
        base = int(m.group(1), 16) if m else None
    if base is None:
        sys.exit('no base: pass --base or keep IMAGE.txt next to the image')
    img = Image(args.image.read_bytes(), base)
    args.out.mkdir(parents=True, exist_ok=True)
    types = find_classes(img)
    locators = find_locators(img, types)
    tables = find_vftables(img, locators)
    wanted = {c for c in args.classes.split(',') if c}
    if args.classes_from:
        for m in re.finditer(r'"\?+[0-9A-Za-z_$]*@([0-9A-Za-z_]+)@@', args.classes_from.read_text(errors='replace')):
            wanted.add(m.group(1))
    lines = [f'image {args.image.name} base {base:#x} size {len(img.data)} sections ' +
             ', '.join(f'{n}@{r:#x}+{z:#x}' for n, r, z, _ in img.sections),
             f'classes with RTTI {len(types)}, object locators {len(locators)}, vftables {len(tables)}']
    with (args.out / 'all_classes.txt').open('w') as f:
        for rva, (cls, off, entries) in sorted(tables.items(), key=lambda t: (t[1][0], t[1][1])):
            f.write(f'{cls}\t+{off:#x}\tvftable {rva:#x}\t{len(entries)} entries\n')
    with (args.out / 'vftables.txt').open('w') as f:
        for rva, (cls, off, entries) in sorted(tables.items(), key=lambda t: (t[1][0], t[1][1])):
            if wanted and cls not in wanted:
                continue
            f.write(f'{cls} +{off:#x} vftable {rva:#x} ({len(entries)})\n')
            for i, e in enumerate(entries):
                f.write(f'  [{i}] {e:#x}\n')
    for sigfile in args.sigs:
        sigs = flarial_sigs(sigfile)
        results = scan(img, sigs)
        good = sum(1 for _, n, _ in results if n == 1)
        lines.append(f'signatures from {sigfile.name}: {len(sigs)} names, {good} match exactly once')
        with (args.out / f'sigs_{sigfile.stem}.txt').open('w') as f:
            for name, n, hits in results:
                where = ' '.join(f'{h:#x}' for h in hits)
                f.write(f'{n}\t{name}\t{where}\n')
    (args.out / 'summary.txt').write_text('\n'.join(lines) + '\n')
    print('\n'.join(lines))


if __name__ == '__main__':
    main()
