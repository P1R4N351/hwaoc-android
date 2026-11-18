"""End-to-end regression test for patch_nso.py jump-table index resolution when Capstone aliases x30/x29 as lr/fp."""
import os
import struct
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
PATCH_NSO = ROOT / 'games/totk/third_party/d2r/patch_nso.py'


def _synth_dispatch(reg, wide=False):
    """Build 4-instruction AArch64 jump-table dispatch ending with `br x18` (x<reg> index)."""
    assert 0 <= reg <= 31, f'invalid register {reg}'
    ldr = (0x78606800 if wide else 0x38606800) | (12 << 16) | (14 << 5) | reg
    adr = 0x10000092   # adr x18, .+0x10 -> lands at offset +4 after br x18
    add = 0x8B000A52 | (reg << 16)
    br = 0xD61F0240
    ret = 0xD65F03C0
    return struct.pack('<5I', ldr, adr, add, br, ret)


def _build_synthetic_nso(text_code):
    """Wrap AArch64 instructions in a minimal uncompressed NSO0 with valid MOD0, DT_HASH, and eh_frame_hdr."""
    assert len(text_code) <= 0xE0, 'synthetic text exceeds minimal page'
    text = bytearray(0x1000)
    struct.pack_into('<II', text, 0, 0xD503201F, 0x1000)
    text[0x20:0x20 + len(text_code)] = text_code
    ro = bytearray(0x1000)
    struct.pack_into('<4s6i', ro, 0, b'MOD0', 0x20, 0x1000, 0x1000, 0x60, 0x78, 0)
    struct.pack_into('<QqQqQq', ro, 0x20, 4, 0x1050, 6, 0x1058, 0, 0)
    struct.pack_into('<II', ro, 0x50, 1, 0)
    struct.pack_into('<4BIIII', ro, 0x60, 1, 0x03, 0x03, 0x03, 0, 1, 0x20, 0x1078)
    data = bytearray(0x1000)
    hdr = bytearray(0x100)
    struct.pack_into('<4sIII', hdr, 0, b'NSO0', 0, 0, 0)
    struct.pack_into('<3I', hdr, 0x10, 0x100, 0x0000, len(text))
    struct.pack_into('<3I', hdr, 0x20, 0x1100, 0x1000, len(ro))
    struct.pack_into('<3I', hdr, 0x30, 0x2100, 0x2000, len(data))
    struct.pack_into('<3I', hdr, 0x60, len(text), len(ro), len(data))
    return bytes(hdr) + bytes(text) + bytes(ro) + bytes(data)


def _run_patch_nso(tmp_dir, text_code):
    """Run patch_nso.py in a subprocess against synthetic NSO0 bytes and return (CompletedProcess, sites_text)."""
    assert Path(tmp_dir).is_dir(), f'missing temp dir {tmp_dir}'
    nso_path = Path(tmp_dir) / 'mini'
    out_dir = Path(tmp_dir) / 'out'
    out_dir.mkdir(exist_ok=True)
    nso_path.write_bytes(_build_synthetic_nso(text_code))
    env = dict(os.environ, DM_RULESET='native', DM_HOST_TP='tpidr_el0')
    proc = subprocess.run([sys.executable, str(PATCH_NSO), str(nso_path), str(out_dir), 'mini'],
                          capture_output=True, text=True, env=env)
    sites = (out_dir / 'mini.sites.tsv').read_text() if (out_dir / 'mini.sites.tsv').is_file() else ''
    return proc, sites


class PatchNsoJumpTableLrTests(unittest.TestCase):
    def setUp(self):
        try:
            import capstone  # noqa: F401
            import lz4.block  # noqa: F401
        except ImportError as exc:
            self.skipTest(f'd2r dependencies unavailable in this Python environment: {exc}')

    def test_resolves_lr_fp_and_general_index_registers(self):
        for reg, wide in ((30, False), (30, True), (29, False), (5, False)):
            with self.subTest(reg=reg, wide=wide), tempfile.TemporaryDirectory() as tmp:
                proc, sites = _run_patch_nso(tmp, _synth_dispatch(reg, wide=wide))
                self.assertEqual(proc.returncode, 0, proc.stderr)
                self.assertIn('\tJ\t', sites)

    def test_negative_control_rejects_xzr_jump_table(self):
        with tempfile.TemporaryDirectory() as tmp:
            proc, _ = _run_patch_nso(tmp, _synth_dispatch(31, wide=False))
            self.assertNotEqual(proc.returncode, 0)
            self.assertIn('jump table', proc.stderr)
