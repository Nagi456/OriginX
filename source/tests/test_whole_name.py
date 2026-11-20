"""Own name in the name tag list: whole name only (client/WholeName.h)."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


@unittest.skipUnless(shutil.which('g++'), 'C++ compiler required')
class WholeName(unittest.TestCase):
    def test_cases(self):
        here = Path(__file__).resolve().parent
        with tempfile.TemporaryDirectory() as temp:
            exe = Path(temp) / 'whole_name'
            subprocess.run(['g++', '-std=c++17', '-Wall', '-Werror', '-I', str(here.parent),
                            str(here / 'whole_name.cpp'), '-o', str(exe)], check=True)
            self.assertEqual(subprocess.check_output([str(exe)], text=True).strip(), 'OK')


if __name__ == '__main__':
    unittest.main()
