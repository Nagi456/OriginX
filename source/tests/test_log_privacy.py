"""Log lines without the user's folders (LogPrivacy.h, tests/log_privacy.cpp)."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

HERE = Path(__file__).resolve().parent


@unittest.skipUnless(shutil.which('g++'), 'C++ compiler required')
class LogPrivacy(unittest.TestCase):
    def test_redact(self):
        with tempfile.TemporaryDirectory() as temp:
            exe = Path(temp) / 'log_privacy'
            subprocess.run(['g++', '-std=c++17', '-Wall', '-Wextra', '-Werror', '-I', str(HERE.parent),
                            str(HERE / 'log_privacy.cpp'), '-o', str(exe)], check=True)
            self.assertIn('PASS', subprocess.check_output([str(exe)], text=True))

    def test_no_scoreboard_text_at_info(self):
        # the game detection line keeps only its result at INFO; the raw names go to debug
        text = (HERE.parent / 'client' / 'MyClientData.cpp').read_text(encoding='utf-8')
        info = [line for line in text.splitlines() if 'info("ゲーム判定' in line]
        self.assertTrue(info)
        self.assertTrue(all('mDisplayName' not in line and 'mName' not in line for line in info))


if __name__ == '__main__':
    unittest.main()
