"""Resize retry policy only; real D3D12 resource lifetime needs Windows verification."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


@unittest.skipUnless(shutil.which("g++"), "g++ required for portable resize wait test")
class ResizeWaitTests(unittest.TestCase):
    def test_timeout_retries_and_device_removal(self):
        source = Path(__file__).resolve().parents[1]
        with tempfile.TemporaryDirectory() as temp:
            binary = Path(temp) / "resize_wait"
            subprocess.run(["g++", "-std=c++17", "-O2", "-Wall", "-Wextra", "-Werror",
                            "-I", str(source), str(source / "tests/resize_wait.cpp"),
                            "-o", str(binary)], check=True)
            subprocess.run([str(binary)], check=True, timeout=5)


if __name__ == "__main__":
    unittest.main()
