"""The running game's image check before attach (launcher/GameImageCheck.h, tests/launcher_image_check.cpp)."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

HERE = Path(__file__).resolve().parent


@unittest.skipUnless(shutil.which('g++'), 'C++ compiler required')
class ImageCheck(unittest.TestCase):
    def test_image_check(self):
        with tempfile.TemporaryDirectory() as temp:
            exe = Path(temp) / 'image_check'
            root = HERE.parent
            subprocess.run(['g++', '-std=c++17', '-Wall', '-Wextra', '-Werror', '-I', str(root), '-I', str(root / 'launcher' / 'vendor'),
                            str(HERE / 'launcher_image_check.cpp'), str(root / 'launcher/vendor/tinyxml2/tinyxml2.cpp'), '-o', str(exe)], check=True)
            self.assertIn('PASS', subprocess.check_output([str(exe)], text=True))


if __name__ == '__main__':
    unittest.main()
