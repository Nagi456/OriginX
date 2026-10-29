"""Checks the OriginX site: every page in both languages, hreflang links,
the required notices and internal links that resolve to files.

Usage: python3 tools/check_site.py site   (prints problems, exit 1 if any)
"""
import sys
from html.parser import HTMLParser
from pathlib import Path

PAGES = ["", "download/", "guide/", "support/", "terms/"]

NOTICE = {
    "ja": "OriginX は非公式のクライアントで、Mojang Studios・Microsoft とは関係ありません。",
    "en": "OriginX is an unofficial client and is not affiliated with Mojang Studios or Microsoft.",
}
FREE = {
    "ja": "先行版の機能も、しばらく後に全員へ無料で公開します。",
    "en": "Every early-access feature is released to everyone for free later.",
}


class _Page(HTMLParser):
    def __init__(self):
        super().__init__()
        self.lang = None
        self.alternates = {}
        self.links = []
        self.footer = []
        self.text = []
        self._in_footer = 0

    def handle_starttag(self, tag, attrs):
        a = dict(attrs)
        if tag == "html":
            self.lang = a.get("lang")
        if tag == "link" and a.get("rel") == "alternate" and a.get("hreflang"):
            self.alternates[a["hreflang"]] = a.get("href")
        if tag == "footer":
            self._in_footer += 1
        for key in ("href", "src"):
            v = a.get(key)
            if v and v.startswith("/") and not v.startswith("//"):
                self.links.append(v)

    def handle_endtag(self, tag):
        if tag == "footer" and self._in_footer:
            self._in_footer -= 1

    def handle_data(self, data):
        self.text.append(data)
        if self._in_footer:
            self.footer.append(data)


def _resolves(root: Path, link: str) -> bool:
    path = link.split("#", 1)[0].split("?", 1)[0].lstrip("/")
    target = root / path
    if path == "" or path.endswith("/"):
        target = target / "index.html"
    return target.is_file()


def check_site(root: Path) -> list[str]:
    root = Path(root)
    problems = []
    for lang, prefix in (("ja", ""), ("en", "en/")):
        for p in PAGES:
            rel = prefix + p
            name = rel or "(top)"
            file = root / rel / "index.html"
            if not file.is_file():
                problems.append(f"missing {rel}index.html")
                continue
            page = _Page()
            page.feed(file.read_text(encoding="utf-8"))
            if page.lang != lang:
                problems.append(f"{name}: lang should be {lang}")
            for alt_lang, href in (("ja", "/" + p), ("en", "/en/" + p)):
                if page.alternates.get(alt_lang) != href:
                    problems.append(f"{name}: no hreflang {alt_lang} link to {href}")
            if NOTICE[lang] not in " ".join(page.footer):
                problems.append(f"{name}: no unofficial notice")
            if p == "support/" and FREE[lang] not in " ".join(page.text):
                problems.append(f"{name}: no free release note")
            for link in page.links:
                if not _resolves(root, link):
                    problems.append(f"{name}: broken link {link}")
    return problems


if __name__ == "__main__":
    found = check_site(Path(sys.argv[1] if len(sys.argv) > 1 else "site"))
    for line in found:
        print(line)
    sys.exit(1 if found else 0)
