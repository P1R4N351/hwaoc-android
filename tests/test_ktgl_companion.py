"""Compile and run tests/test_ktgl_companion.c with C11 strict warnings and UBSan."""
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


@unittest.skipUnless(shutil.which('cc') or shutil.which('gcc') or shutil.which('clang'), 'no C compiler')
class KtglCompanionTests(unittest.TestCase):
    def test_ktgl_companion_c(self):
        cc = shutil.which('cc') or shutil.which('gcc') or shutil.which('clang')
        with tempfile.TemporaryDirectory(prefix='ktgl-c-') as tmp:
            exe = Path(tmp) / 'test_ktgl_companion'
            subprocess.run(
                [cc, '-std=c11', '-O1', '-g', '-Wall', '-Wextra', '-Werror',
                 '-fsanitize=undefined', '-fno-sanitize-recover=all',
                 '-I' + str(ROOT / 'games/totk/runtime'),
                 str(ROOT / 'tests/test_ktgl_companion.c'), '-o', str(exe), '-lm'],
                check=True, capture_output=True, text=True,
            )
            r = subprocess.run([str(exe)], check=True, capture_output=True, text=True)
            self.assertEqual(r.returncode, 0)
            self.assertIn('test_ktgl_companion: PASS', r.stdout)


if __name__ == '__main__':
    unittest.main()
