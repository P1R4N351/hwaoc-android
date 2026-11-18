#!/usr/bin/env python3
"""Tests for games/totk/scripts/prepare_hwaoc_seed.py (positive and negative controls)."""
import hashlib
import json
from pathlib import Path
import struct
import sys
import tempfile
import unittest
import zlib

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'games/totk/scripts'))
import prepare_hwaoc_seed as hwaoc_seed   # noqa: E402
import prepare_shader_seed as pss   # noqa: E402


def _make_nvn_shader(stage_num, salt, with_gfx_magic=True):
    ctl = bytearray(0x880)
    ctl[0:4] = hwaoc_seed.CTL_MAGIC
    struct.pack_into('<I', ctl, 0x20, 0x880)
    code_len = 0x100
    struct.pack_into('<II', ctl, 0x700, 0x80, code_len)
    struct.pack_into('<I', ctl, 0x714, stage_num)
    if stage_num == 5:
        struct.pack_into('<III', ctl, 0x748, 8, 1, 1)
    code = bytearray(code_len)
    if stage_num in (0, 1) and with_gfx_magic:
        code[0:4] = hwaoc_seed.GFX_MAGIC
    elif stage_num == 5:
        code[0:4] = b'\xf0\x07\x40\xe2'
    code[0x30:0x34] = struct.pack('<I', salt)
    return bytes(ctl), bytes(code)


def _wrap_idrk_chunked(payload, tid=0x7bcd279f):
    c = zlib.compress(payload)
    chunked = struct.pack('<I', len(c)) + c + struct.pack('<I', 0)
    hdr_sz = 0x38
    esz = hdr_sz + len(chunked)
    hdr = bytearray(hdr_sz)
    hdr[0:8] = b'IDRK0000'
    struct.pack_into('<QQQ', hdr, 8, esz, len(chunked), len(payload))
    struct.pack_into('<II', hdr, 0x28, tid, 0x100000)
    return bytes(hdr) + chunked


def _write_runtime_index(folder, files):
    strings, rows = bytearray(), bytearray()
    for rel, off, sz in files:
        enc = rel.encode('utf-8') + b'\0'
        rows.extend(struct.pack('<QQII', off, sz, len(strings), len(enc)))
        strings.extend(enc)
    hdr = struct.pack('<8sQQ', b'TOTKRFS1', len(files), 24 + len(rows))
    (folder / 'runtime-index.bin').write_bytes(hdr + rows + strings)


def _write_mock_translator(path, mismatch_stage=False):
    script = f"""#!{sys.executable}
import json, pathlib, struct, sys

argv = sys.argv[1:]
out = pathlib.Path(argv[argv.index('--out') + 1])
mismatch = {mismatch_stage!r}
models = {{'vs': 0, 'ps': 4, 'cs': 5}}
nums = {{'vs': 0, 'ps': 1, 'cs': 5}}

for line in sys.stdin:
    if not line.strip():
        continue
    req = json.loads(line)
    var = req['variant']
    stg = req['stage']
    assert pathlib.Path(req['code']).is_file()
    em = 4 if (mismatch and stg == 'vs') else models[stg]
    # Valid SPIR-V header (5 words) + OpEntryPoint(15, model, 1, "main\\0\\0\\0\\0", ...) = 6 words
    words = [0x07230203, 0x00010000, 0, 2, 0, (5 << 16) | 15, em, 1, 0x6e69616d, 0]
    (out / f'{{var}}.spv').write_bytes(struct.pack(f'<{{len(words)}}I', *words))
    (out / f'{{var}}.vk').write_text(f'vk 1\\nstage {{nums[stg]}}\\n')
    flat = ';'.join(f'{{k}}:{{s}}={{v if isinstance(v, str) else v["type"]}}'
                    for k in ('attr', 'fout', 'img', 'tex')
                    for s, v in sorted(req.get(k, {{}}).items()))
    key = var.split('-')[0]
    vv = out / f'{{key}}.vv'
    prev = vv.read_text() if vv.is_file() else ''
    vv.write_text(prev + f'{{var}}\\t{{flat}}\\n')
    sys.stdout.write(json.dumps({{'ok': True}}) + '\\n')
    sys.stdout.flush()
"""
    path.write_text(script)
    path.chmod(0o755)


class TestPrepareHwaocSeed(unittest.TestCase):
    def test_romfs_runtime_index_and_modules_pack(self):
        vs_ctl, vs_code = _make_nvn_shader(0, 1)
        ps_ctl, ps_code = _make_nvn_shader(1, 2)
        cs_ctl, cs_code = _make_nvn_shader(5, 3)
        mod_ctl, mod_code = _make_nvn_shader(0, 4)

        s2g = b'_S2G3000' + b'\0' * 0x20 + vs_ctl + vs_code + ps_ctl + ps_code + cs_ctl + cs_code
        idrk = _wrap_idrk_chunked(s2g)

        with tempfile.TemporaryDirectory() as tmp:
            t = Path(tmp)
            romfs = t / 'romfs'
            romfs.mkdir()
            romfs_bin = b'PAD!' + idrk
            (romfs / 'romfs.bin').write_bytes(romfs_bin)
            _write_runtime_index(romfs, [('asset/system.rdb.bin', 4, len(idrk))])
            cfg = t / 'romfs-config.txt'
            rsha = hashlib.sha256(romfs_bin).hexdigest()
            cfg.write_text(f'ROMFS_IMAGE_SIZE {len(romfs_bin)}\nROMFS_IMAGE_SHA256 {rsha}\n')

            mods = t / 'modules'
            mods.mkdir()
            (mods / 'main.patched.bin').write_bytes(b'\0' * 0x100 + mod_ctl + mod_code)

            xlate = t / 'mock_xlate'
            _write_mock_translator(xlate)
            slist = t / 'shader_list.tsv'
            slist.write_text('# empty\n')
            out = t / 'seed'

            report = hwaoc_seed.prepare(
                romfs, out, xlate, shader_list=slist, modules=mods, romfs_config=cfg
            )
            self.assertEqual(report['discovered'], 4)
            self.assertEqual(report['shaders'], 4)
            self.assertEqual(report['skipped'], [])
            pack = out / 'seed.vkpack'
            self.assertTrue(pack.is_file())
            self.assertEqual(pack.read_bytes()[:8], b'TOTKVKP1')
            sid = (out / 'seed-id.txt').read_text()
            self.assertEqual(sid, hashlib.sha256(pack.read_bytes()).hexdigest())
            self.assertEqual(len(sid), 64)
            scope = json.loads((out / 'shader-keys.json').read_text())
            self.assertEqual(scope['source_romfs_sha256'], rsha)
            self.assertEqual(len(scope['keys']), 4)

    def test_romfs_tree_and_shader_list_variants(self):
        ps_ctl, ps_code = _make_nvn_shader(1, 99)
        ps_key = pss.digest(ps_ctl + ps_code)
        dok = b'_DOK0000' + b'\0' * 0x20 + ps_ctl + ps_code

        with tempfile.TemporaryDirectory() as tmp:
            t = Path(tmp)
            tree = t / 'tree'
            (tree / 'asset').mkdir(parents=True)
            # Raw uncompressed IDRK with tid=0x20a6a0bb
            hdr = bytearray(0x38)
            hdr[0:8] = b'IDRK0000'
            struct.pack_into('<QQQ', hdr, 8, 0x38 + len(dok), len(dok), len(dok))
            struct.pack_into('<II', hdr, 0x28, 0x20a6a0bb, 0)
            (tree / 'asset/system.rdb.bin').write_bytes(bytes(hdr) + dok)

            slist = t / 'shader_list.tsv'
            deps = '{"attr":{},"fout":{},"img":{},"tex":{"0":"2d_depth"}}'
            slist.write_text(f'V\t{ps_key}\t{deps}\n')
            xlate = t / 'mock_xlate'
            _write_mock_translator(xlate)
            out = t / 'seed'

            report = hwaoc_seed.prepare(tree, out, xlate, is_tree=True, shader_list=slist, pack=False)
            self.assertEqual(report['discovered'], 1)
            self.assertEqual(report['shaders'], 2)
            self.assertFalse((out / 'seed.vkpack').exists())
            self.assertEqual(len((out / f'{ps_key}.vv').read_text().splitlines()), 2)

    def test_negative_controls(self):
        # 1. VS without GFX_MAGIC must be rejected
        bad_vs_ctl, bad_vs_code = _make_nvn_shader(0, 7, with_gfx_magic=False)
        self.assertEqual(list(hwaoc_seed.ktgl_shaders(bad_vs_ctl + bad_vs_code)), [])

        # 2. Corrupt zlib chunk in IDRK raises
        hdr = bytearray(0x38)
        hdr[0:8] = b'IDRK0000'
        bad_chunk = struct.pack('<I', 8) + b'not-zlib' + struct.pack('<I', 0)
        struct.pack_into('<QQQ', hdr, 8, 0x38 + len(bad_chunk), len(bad_chunk), 100)
        struct.pack_into('<II', hdr, 0x28, 0x7bcd279f, 0x100000)
        with self.assertRaises((zlib.error, ValueError)):
            list(hwaoc_seed.iter_rdb_shaders(bytes(hdr) + bad_chunk))

        # 3. Corrupt runtime-index header raises ValueError
        with tempfile.TemporaryDirectory() as tmp:
            bad_idx = Path(tmp) / 'runtime-index.bin'
            bad_idx.write_bytes(b'BADMAGIC' + b'\0' * 32)
            with self.assertRaises(ValueError):
                hwaoc_seed.read_runtime_index(bad_idx)

        # 4. Stage-mismatched SPIR-V raises ValueError
        vs_ctl, vs_code = _make_nvn_shader(0, 55)
        idrk = _wrap_idrk_chunked(b'_S2G3000' + b'\0' * 0x20 + vs_ctl + vs_code)
        with tempfile.TemporaryDirectory() as tmp:
            t = Path(tmp)
            romfs = t / 'romfs'
            romfs.mkdir()
            (romfs / 'romfs.bin').write_bytes(idrk)
            _write_runtime_index(romfs, [('asset/system.rdb.bin', 0, len(idrk))])
            xlate = t / 'mock_xlate'
            _write_mock_translator(xlate, mismatch_stage=True)
            with self.assertRaises(ValueError):
                hwaoc_seed.prepare(romfs, t / 'seed', xlate, shader_list=t / 'missing.tsv')


if __name__ == '__main__':
    unittest.main()
