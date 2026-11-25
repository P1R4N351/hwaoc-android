"""Compile and run tests/test_vk_minimap.c with C11 strict warnings and UBSan."""
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


@unittest.skipUnless(shutil.which("cc") or shutil.which("gcc") or shutil.which("clang"), "no C compiler")
class VkMinimapTests(unittest.TestCase):
    def test_vk_minimap_c(self):
        cc = shutil.which("cc") or shutil.which("gcc") or shutil.which("clang")
        with tempfile.TemporaryDirectory(prefix="vk-mm-c-") as tmp:
            exe = Path(tmp) / "test_vk_minimap"
            c = subprocess.run(
                [cc, "-std=c11", "-O1", "-g", "-Wall", "-Wextra", "-Werror",
                 "-fsanitize=undefined", "-fno-sanitize-recover=all",
                 "-I" + str(ROOT / "games/totk/android/gfx"),
                 str(ROOT / "tests/test_vk_minimap.c"), "-o", str(exe), "-lm"],
                capture_output=True, text=True,
            )
            self.assertEqual(c.returncode, 0, c.stderr)
            r = subprocess.run([str(exe)], capture_output=True, text=True)
            self.assertEqual(r.returncode, 0, r.stderr + r.stdout)
            self.assertIn("vk_minimap_c_regression: PASS", r.stdout)


if __name__ == "__main__":
    unittest.main()
