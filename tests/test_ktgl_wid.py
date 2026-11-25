"""Compile and run tests/test_ktgl_wid.c with strict C11 warnings and UBSan."""
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


@unittest.skipUnless(shutil.which("cc") or shutil.which("gcc") or shutil.which("clang"), "no C compiler")
class KtglWidTests(unittest.TestCase):
    def test_ktgl_wid_c(self):
        cc = shutil.which("cc") or shutil.which("gcc") or shutil.which("clang")
        with tempfile.TemporaryDirectory(prefix="ktgl-wid-") as tmp:
            exe = Path(tmp) / "test_ktgl_wid"
            cmd = [cc, "-std=c11", "-O1", "-g", "-Wall", "-Wextra", "-Werror",
                   "-fsanitize=undefined", "-fno-sanitize-recover=all",
                   "-I" + str(ROOT / "games/totk/runtime"),
                   str(ROOT / "tests/test_ktgl_wid.c"), "-o", str(exe)]
            c = subprocess.run(cmd, capture_output=True, text=True)
            self.assertEqual(c.returncode, 0, c.stderr)
            r = subprocess.run([str(exe)], capture_output=True, text=True)
            self.assertEqual(r.returncode, 0, r.stderr + r.stdout)
            self.assertIn("ktgl_wid_classify: PASS", r.stdout)


if __name__ == "__main__":
    unittest.main()
