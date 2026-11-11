"""package_update.py --channel test: a build for the moderators' test channel (docs/superpowers/specs/2026-11-09-release-channels-design.md)."""
import json
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import package_update  # noqa: E402


def pe(extra=b""):
    return b"MZ" + b"\0" * 58 + (64).to_bytes(4, "little") + b"PE\0\0\x64\x86" + extra + b"\0" * 32


def write_package(folder: Path, game="26.51.1", marker=b"1"):
    (folder / "mods/MyClientData").mkdir(parents=True)
    major, minor, patch = map(int, game.split("."))
    (folder / "runtime.lock.json").write_text(json.dumps({"gameVersion": f"1.{major}.{minor}.{patch:02d}", "loaderVersion": "26.51.6"}))
    (folder / "mods/MyClientData/manifest.json").write_text(json.dumps({"name": "MyClientData", "entry": "MyClientData.dll", "type": "native"}))
    (folder / "mods/MyClientData/MyClientData.dll").write_bytes(pe("Local\\MyClient.Bridge.v2.".encode("utf-16-le") + marker))


class ChannelPackageTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name)
        (self.root / "launcher").mkdir()
        self.stable_list = self.root / "launcher/catalog.json"
        self.stable_list.write_text("stable list, must stay as it is")
        package_update.ROOT = self.root  # keep the repository's catalog out of the test

    def tearDown(self):
        self.tmp.cleanup()

    def build(self, name, game="26.51.1", revision=54, test_number=1, previous=None, marker=b"1", channel="test"):
        pkg = self.root / f"pkg-{name}"
        write_package(pkg, game, marker)
        out = self.root / f"out-{name}"
        package_update.package(self.root, out, revision=revision, package_dir=pkg, version=game, channel=channel,
                               test_number=test_number, catalog_path=previous)
        return out

    def test_a_test_build_has_its_own_tag_key_urls_and_entry_json_and_leaves_the_stable_list_alone(self):
        out = self.build("a")
        tag = "myclient-26.51.1-r54-test1"
        catalog = json.loads((out / "catalog.json").read_text())
        self.assertEqual(catalog["sequence"], 1)
        self.assertEqual([v["id"] for v in catalog["versions"]], ["26.51.1"])
        urls = [f["url"] for f in catalog["versions"][0]["files"].values()]
        self.assertTrue(urls and all(u.startswith(f"test/{tag}/") and "https://" not in u for u in urls))
        entry = json.loads((out / tag / "entry.json").read_text())
        self.assertEqual(entry, catalog["versions"][0])
        self.assertEqual(self.stable_list.read_text(), "stable list, must stay as it is")

    def test_a_test_build_needs_a_test_number(self):
        for number in (None, 0, -1):
            with self.assertRaises(ValueError):
                self.build(f"n{number}", test_number=number)

    def test_a_second_game_version_joins_the_list_and_a_rebuild_replaces_its_own_entry(self):
        first = self.build("a")
        second = self.build("b", game="26.60.5", previous=first / "catalog.json")
        catalog = json.loads((second / "catalog.json").read_text())
        self.assertEqual(catalog["sequence"], 2)
        self.assertEqual([v["id"] for v in catalog["versions"]], ["26.51.1", "26.60.5"])
        third = self.build("c", test_number=2, previous=second / "catalog.json", marker=b"2")
        catalog = json.loads((third / "catalog.json").read_text())
        self.assertEqual(catalog["sequence"], 3)
        self.assertEqual([v["id"] for v in catalog["versions"]], ["26.51.1", "26.60.5"])
        self.assertTrue(all("test2" in f["url"] for f in catalog["versions"][0]["files"].values()))
        self.assertTrue(all("test1" in f["url"] for f in catalog["versions"][1]["files"].values()))

    def test_stable_is_unchanged(self):
        out = self.build("s", channel="stable", test_number=None, revision=54)
        catalog = json.loads((out / "catalog.json").read_text())
        stable = next(v for v in catalog["versions"] if v["id"] == "26.51.1")
        self.assertTrue(all(f["url"].startswith(package_update.BASE_URL + "/myclient-26.51.1-r54/") for f in stable["files"].values()))
        self.assertFalse((out / "myclient-26.51.1-r54" / "entry.json").exists())
        self.assertNotEqual(self.stable_list.read_text(), "stable list, must stay as it is")  # stable still writes launcher/catalog.json

    def test_an_unknown_channel_is_refused(self):
        with self.assertRaises(ValueError):
            self.build("x", channel="early")


class StandalonePackageTest(unittest.TestCase):
    """--standalone: the DLL that runs without LeviLamina (launcherApi 2, no runtime.lock.json)."""

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name)
        (self.root / "launcher").mkdir()
        package_update.ROOT = self.root

    def tearDown(self):
        self.tmp.cleanup()

    def test_the_entry_says_standalone_and_has_no_loader_lock(self):
        dll = (Path(__file__).resolve().parents[1] / "build/client-standalone/MyClientData.dll")
        if not dll.is_file():
            self.skipTest("needs a locally built standalone DLL (tools/build_client.py --standalone)")
        pkg = self.root / "pkg"
        write_package(pkg, "26.51.1", b"1")
        (pkg / "mods/MyClientData/MyClientData.dll").write_bytes(dll.read_bytes())
        out = self.root / "out"
        package_update.package(self.root, out, revision=60, package_dir=pkg, version="26.51.1", channel="test",
                               test_number=1, standalone=True)
        entry = json.loads((out / "myclient-26.51.1-r60-test1/entry.json").read_text())
        self.assertEqual((entry["launcherApi"], entry["loader"]), (2, "standalone"))
        self.assertNotIn("runtime.lock.json", entry["files"])
        self.assertFalse((out / "myclient-26.51.1-r60-test1/runtime.lock.json").exists())

    def test_a_dll_that_needs_levilamina_is_refused(self):
        dll = (Path(__file__).resolve().parents[1] / "build/client/MyClientData.dll")
        if not dll.is_file():
            self.skipTest("needs a locally built LeviLamina DLL (tools/build_client.py)")
        pkg = self.root / "pkg"
        write_package(pkg, "26.51.1", b"1")
        (pkg / "mods/MyClientData/MyClientData.dll").write_bytes(dll.read_bytes())
        with self.assertRaises(ValueError):
            package_update.package(self.root, self.root / "out", revision=60, package_dir=pkg, version="26.51.1",
                                   channel="test", test_number=1, standalone=True)


if __name__ == "__main__":
    unittest.main()
