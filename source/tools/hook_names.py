#!/usr/bin/env python3
"""The game-function names each MyClient hook (MYCLIENT_*_HOOK in client/*.cpp) is looked up by.

Reads the hook's target argument: a string literal, a constexpr const char* constant, MyClientGame::Target(...),
ll::memory::SymbolView{...}, or a function returning ResolveFirst({...}) (all its names). Used by
tools/make_rva_table.py to put the right names on each address. Prints label<TAB>name per line when run.
"""
import functools
import re
import sys
from pathlib import Path

CLIENT = Path(__file__).resolve().parent.parent / "client"
STRING = r'"(?:[^"\\]|\\.)*"'


def join_strings(text: str) -> str:
    return "".join(bytes(s[1:-1], "utf-8").decode("unicode_escape") for s in re.findall(STRING, text))


def split_args(text: str, start: int):
    """Arguments of the call whose '(' is at text[start-1]; returns (args, end)."""
    depth, cur, args, i = 1, "", [], start
    while depth:
        c = text[i]
        if c == '"':
            m = re.match(STRING, text[i:])
            cur += m.group(0)
            i += len(m.group(0))
            continue
        if c in "([{":
            depth += 1
        elif c in ")]}":
            depth -= 1
        if depth == 1 and c == ",":
            args.append(cur.strip())
            cur = ""
        elif depth:
            cur += c
        i += 1
    args.append(cur.strip())
    return args, i


def sources():
    text = ""
    for path in sorted(CLIENT.glob("*.cpp")) + sorted(CLIENT.glob("*.h")):
        text += path.read_text(encoding="utf-8", errors="replace") + "\n"
    return text


def expand_string_macros(text: str) -> str:
    """Expands the #define macros that build a name out of string pieces (MYCLIENT_RENDERMESH("QEBA") and so on)."""
    text = text.replace("\\\n", " ")
    macros = {}
    for m in re.finditer(r"^#define\s+(\w+)(\(([^)]*)\))?[ \t]+(.*)$", text, re.M):
        params = [p.strip() for p in m.group(3).split(",")] if m.group(2) else None
        body = m.group(4).strip()
        if '"' in body and re.fullmatch(r'(\s*(' + STRING + r'|\w+))+\s*', body):
            macros[m.group(1)] = (params, body)
    for _ in range(4):
        changed = False
        for name, (params, body) in macros.items():
            if params is None:
                new = re.sub(r"(?<![#\w])" + name + r"\b(?!\s*\()", body, text)
            else:
                def call(m, params=params, body=body):
                    args = [a.strip() for a in re.findall(STRING + "|[^,]+", m.group(1))]
                    if len(args) != len(params):
                        return m.group(0)
                    out = body
                    for p, a in zip(params, args):
                        out = re.sub(r"\b" + re.escape(p) + r"\b", a, out)
                    return out
                new = re.sub(r"(?<![#\w])" + name + r"\(((?:\s*" + STRING + r"\s*,?)+)\)", call, text)
            if new != text:
                text, changed = new, True
        if not changed:
            break
    return text


@functools.lru_cache(maxsize=None)
def collect():
    text = sources()
    text = re.sub(r"//[^\n]*", "", text)
    text = expand_string_macros(text)
    constants = {}
    for m in re.finditer(r"constexpr\s+const\s+char\s*\*\s*(\w+)\s*=\s*((?:\s*" + STRING + r")+)\s*;", text):
        constants[m.group(1)] = join_strings(m.group(2))
    functions = {}
    for m in re.finditer(r"inline\s+uintptr_t\s+(\w+)\s*\(\s*\)\s*\{(.*?)\n\}", text, re.S):
        body = m.group(2)
        # each element of the list: adjacent literals are one name
        names = []
        for item in re.findall(r"((?:\s*" + STRING + r")+)", body):
            names.append(join_strings(item))
        for word in re.findall(r"\b([A-Z]\w+Symbol\w*)\b", body):
            if word in constants:
                names.append(constants[word])
        if names:
            functions[m.group(1)] = names
    hooks = {}
    for m in re.finditer(r"\bMYCLIENT_(AUTO_TYPE_INSTANCE|TYPE_INSTANCE|INSTANCE|STATIC)_HOOK\(", text):
        if text[max(0, m.start() - 9):m.start()].strip().endswith("define"):
            continue
        args, _ = split_args(text, m.end())
        label = args[0]
        ident = args[3] if "TYPE" in m.group(1) else args[2]
        hooks[label] = resolve(ident, constants, functions)
    # MYCLIENT_HANDLER_HOOK(Name, Packet): the LegacyClientNetworkHandler handle overload for that packet
    for m in re.finditer(r"^MYCLIENT_HANDLER_HOOK\((\w+),\s*(\w+)\)", text, re.M):
        hooks[m.group(1)] = ["?handle@LegacyClientNetworkHandler@@UEAAXAEBVNetworkIdentifier@@AEBV" + m.group(2) + "@@@Z"]
    return hooks


def resolve(ident: str, constants, functions):
    if re.search(STRING, ident):
        return [join_strings(ident)]
    for word in re.findall(r"\b(\w+)\b", ident):
        if word in constants:
            return [constants[word]]
        if word in functions:
            return functions[word]
    return []


if __name__ == "__main__":
    for label, names in sorted(collect().items()):
        for name in names or ["-"]:
            print(f"{label}\t{name}")
