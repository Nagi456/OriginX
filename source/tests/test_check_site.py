import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from check_site import PAGES, check_site  # noqa: E402

NOTICE = {
    "ja": "OriginX は非公式のクライアントで、Mojang Studios・Microsoft とは関係ありません。",
    "en": "OriginX is an unofficial client and is not affiliated with Mojang Studios or Microsoft.",
}
FREE = {
    "ja": "先行版の機能も、しばらく後に全員へ無料で公開します。",
    "en": "Every early-access feature is released to everyone for free later.",
}


def page(lang, path, body=""):
    extra = FREE[lang] if path == "support/" else ""
    return (
        f'<!doctype html><html lang="{lang}"><head>'
        f'<link rel="alternate" hreflang="ja" href="/{path}">'
        f'<link rel="alternate" hreflang="en" href="/en/{path}">'
        f"</head><body>{body}<p>{extra}</p><footer>{NOTICE[lang]}</footer></body></html>"
    )


class CheckSiteTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name)
        for p in PAGES:
            self.write(p, page("ja", p))
            self.write("en/" + p, page("en", p))

    def tearDown(self):
        self.tmp.cleanup()

    def write(self, rel, text):
        f = self.root / rel / "index.html"
        f.parent.mkdir(parents=True, exist_ok=True)
        f.write_text(text, encoding="utf-8")

    def test_complete_site_passes(self):
        self.assertEqual(check_site(self.root), [])

    def test_missing_english_page(self):
        (self.root / "en/guide/index.html").unlink()
        self.assertIn("missing en/guide/index.html", check_site(self.root))

    def test_wrong_lang(self):
        self.write("en/", page("en", "").replace('lang="en"', 'lang="ja"'))
        self.assertIn("en/: lang should be en", check_site(self.root))

    def test_missing_disclaimer(self):
        self.write("download/", page("ja", "download/").replace(NOTICE["ja"], ""))
        self.assertIn("download/: no unofficial notice", check_site(self.root))

    def test_support_needs_free_release_note(self):
        self.write("support/", page("ja", "support/").replace(FREE["ja"], ""))
        self.assertIn("support/: no free release note", check_site(self.root))

    def test_missing_alternate(self):
        self.write("guide/", page("ja", "guide/").replace('hreflang="en" href="/en/guide/"', ""))
        self.assertIn("guide/: no hreflang en link to /en/guide/", check_site(self.root))

    def test_broken_internal_link(self):
        self.write("", page("ja", "", '<a href="/nope/">x</a>'))
        self.assertIn("(top): broken link /nope/", check_site(self.root))

    def test_query_and_hash_ignored(self):
        self.write("", page("ja", "", '<a href="/download/?need=verify#top">x</a><img src="/en/">'))
        self.assertEqual(check_site(self.root), [])


SITE = Path(__file__).resolve().parents[1] / "site"


class RealSiteTest(unittest.TestCase):
    def test_config_defaults(self):
        text = (SITE / "config.js").read_text(encoding="utf-8")
        self.assertIn('workerUrl: ""', text)
        self.assertIn('sourceUrl: "https://github.com/Nagi456/OriginX"', text)

    def test_download_buttons_use_worker(self):
        import re
        pages = list(SITE.rglob("*.html"))
        self.assertTrue(pages)
        for f in pages:
            for value in re.findall(r'data-href="(worker:[^"]*)"', f.read_text(encoding="utf-8")):
                self.assertEqual(value, "worker:/login", f)


if __name__ == "__main__":
    unittest.main()
