"""Settings file text: UTF-8 save, UTF-8 and old Latin-1 load (ConfigText.h)."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


@unittest.skipUnless(shutil.which('g++'), 'C++ compiler required')
class ConfigText(unittest.TestCase):
    def test_round_trip_and_old_files(self):
        here = Path(__file__).resolve().parent
        with tempfile.TemporaryDirectory() as temp:
            exe = Path(temp) / 'config_text'
            subprocess.run(['g++', '-std=c++17', '-Wall', '-Werror', '-I', str(here.parent),
                            str(here / 'config_text.cpp'), '-o', str(exe)], check=True)
            self.assertEqual(subprocess.check_output([str(exe)], text=True).strip(), 'OK')


if __name__ == '__main__':
    unittest.main()
