"""Check the emitted native UI contract; this does not run Bedrock's UI engine."""
import ast
import json
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


@unittest.skipUnless(shutil.which("g++"), "native C++ compiler required")
class HomeBrandUi(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory()
        cls.addClassCleanup(cls.temp.cleanup)
        root = Path(cls.temp.name)
        source = root / "emit.cpp"
        source.write_text('#include "LitePack.h"\n#include <iostream>\n'
                          'int main() { std::cout << LitePack::HomeBrandJson(); }\n')
        exe = root / "emit"
        subprocess.run(["g++", "-std=c++17", "-I", str(Path(__file__).resolve().parents[1]),
                        str(source), "-o", str(exe)], check=True, capture_output=True)
        cls.ui = json.loads(subprocess.check_output([str(exe)], text=True))

    def test_only_existing_label_text_and_appended_binding_change(self):
        self.assertEqual(set(self.ui), {"namespace", "version/label"})
        self.assertEqual(self.ui["namespace"], "start")
        label = self.ui["version/label"]
        self.assertEqual(set(label), {"text", "modifications"})  # inherit native font, anchors, etc.
        self.assertEqual(len(label["modifications"]), 1)
        append = label["modifications"][0]
        self.assertEqual(append["array_name"], "bindings")
        self.assertEqual(append["operation"], "insert_back")  # retain the native #version binding
        self.assertEqual(len(append["value"]), 1)
        view = append["value"][0]
        self.assertEqual(view["binding_type"], "view")
        self.assertEqual(label["text"], view["target_property_name"])

    def test_expression_uses_current_native_version_for_both_supported_games(self):
        view = self.ui["version/label"]["modifications"][0]["value"][0]
        expression = view["source_property_name"]
        self.assertTrue(expression.startswith("(") and expression.endswith(")"))
        prefix, native_binding = expression[1:-1].split(" + ")
        self.assertEqual(native_binding, "#version")
        # Evaluate only the emitted concatenation contract, not a substitute UI renderer.
        for native, expected in [("v26.51", "OriginX Client v26.51"),
                                 ("v26.60", "OriginX Client v26.60")]:
            with self.subTest(native=native):
                self.assertEqual(ast.literal_eval(prefix) + native, expected)


if __name__ == "__main__":
    unittest.main()
