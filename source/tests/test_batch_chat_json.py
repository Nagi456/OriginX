"""Generated UI contract; not an execution of Bedrock's UI engine."""
import json
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


@unittest.skipUnless(shutil.which('g++'), 'C++ compiler required')
class BatchChatJson(unittest.TestCase):
    def test_hud_chat_panel_only_is_hidden(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            source = root / 'emit.cpp'
            source.write_text('#include "JavaChatPack.h"\n#include <iostream>\n'
                              'int main(){std::cout << JavaChatPack::HideJson();}\n')
            exe = root / 'emit'
            subprocess.run(['g++', '-std=c++17', '-I', str(Path(__file__).resolve().parents[1]),
                            str(source), '-o', str(exe)], check=True, capture_output=True)
            ui = json.loads(subprocess.check_output([str(exe)]))
        self.assertEqual(ui, {'namespace': 'hud', 'root_panel/chat_stack/chat_panel': {'visible': False, 'bindings': []}})
        # Both use this instance path. 26.60 adds an always-visible binding: replace it with an empty array.
        self.assertNotIn('chat_screen', ui)


if __name__ == '__main__':
    unittest.main()
