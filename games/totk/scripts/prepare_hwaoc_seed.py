#!/usr/bin/env python3
"""Hyrule Warriors: Age of Calamity 1.3.0 (VERSION=hwaoc130): Vulkan shader seed builder.

Unpacks KTGL _S2G3000 / _DOK0000 containers from asset/system.rdb.bin* in RomFS and
embedded GLSLC shaders from rewritten/hwaoc130/*.patched.bin, translates them via
totk_xlate --service --vulkan, and packs seed.vkpack + seed-id.txt for Android.
"""
import argparse
import concurrent.futures
import hashlib
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import threading
import time
import zlib

sys.path.insert(0, str(Path(__file__).resolve().parent))
from pack_shader_seed import pack_seed   # noqa: E402
import prepare_shader_seed as pss   # noqa: E402
from shader_list import ShaderList   # noqa: E402

VERSION = 'hwaoc130'
GAME = 'hwaoc'
REPO_ROOT = Path(__file__).resolve().parents[3]
DEFAULT_LIST = REPO_ROOT / 'games/hwaoc/assets/shader_list.tsv'
CTL_MAGIC = b'\x34\x12\x76\x98'
GFX_MAGIC = b'\x78\x56\x34\x12'
IDRK_MAGIC = b'IDRK0000'
RFS_MAGIC = b'TOTKRFS1'
SHADER_TIDS = frozenset({0x7bcd279f, 0xb1630f51, 0x20a6a0bb, 0x1fdcaa40})
SHADER_PREFIXES = (b'_S2G', b'_DOK')
STAGES = {0: 'vs', 1: 'ps', 5: 'cs'}
STAGE_NUMS = {'vs': 0, 'ps': 1, 'cs': 5}
MAX_IDRK_ENTRIES = 1_000_000
MAX_ZLIB_CHUNKS = 65_536
MAX_ENTRY_BYTES = 64 << 20
MAX_SHADERS_PER_BLOB = 200_000
MAX_RUNTIME_INDEX_FILES = 1_000_000


def read_runtime_index(index_path):
    """Parse TOTKRFS1 runtime-index.bin into {romfs_path: {path, offset, size}}."""
    data = Path(index_path).read_bytes()
    if len(data) < 24:
        raise ValueError(f'truncated runtime-index.bin: {index_path}')
    magic, count, str_base = struct.unpack_from('<8sQQ', data, 0)
    if magic != RFS_MAGIC or count > MAX_RUNTIME_INDEX_FILES or str_base < 24 + count * 24 or str_base > len(data):
        raise ValueError(f'invalid runtime-index.bin header: {index_path}')
    entries = {}
    for i in range(count):
        off, sz, soff, slen = struct.unpack_from('<QQII', data, 24 + i * 24)
        start, end = str_base + soff, str_base + soff + slen
        if slen == 0 or end > len(data):
            raise ValueError(f'corrupt runtime-index.bin string at row {i}')
        rel = data[start:end - 1].decode('utf-8')
        entries[rel] = dict(path=rel, offset=off, size=sz)
    return entries


def _parse_romfs_config(config_path):
    for line in Path(config_path).read_text().splitlines():
        if line.startswith('ROMFS_IMAGE_SHA256 '):
            val = line.split(None, 1)[1].strip().lower()
            if len(val) == 64:
                return val
    raise ValueError(f'missing ROMFS_IMAGE_SHA256 in {config_path}')


def _hash_files_bounded(paths):
    h = hashlib.sha256()
    for p in paths:
        sz = p.stat().st_size
        chunks = (sz >> 20) + 2
        with p.open('rb') as f:
            for _ in range(chunks):
                block = f.read(1 << 20)
                if not block:
                    break
                h.update(block)
    return h.hexdigest()


def _resolve_romfs_sha(romfs, romfs_config, index_meta, rdb_paths):
    if index_meta and 'romfs_sha256' in index_meta:
        return index_meta['romfs_sha256']
    candidates = [romfs_config] if romfs_config else [
        romfs / 'romfs-config.txt',
        romfs.parent / 'game-data' / VERSION / 'romfs-config.txt',
        romfs.parent.parent / 'game-data' / VERSION / 'romfs-config.txt',
    ]
    for c in candidates:
        if c and c.is_file():
            return _parse_romfs_config(c)
    if (romfs / 'romfs.bin').is_file():
        return _hash_files_bounded([romfs / 'romfs.bin'])
    return _hash_files_bounded(rdb_paths)


def decompress_idrk(pay, flags):
    """Inflate an IDRK0000 payload (chunked zlib when flags & 0x100000)."""
    if not (flags & 0x100000):
        return pay
    out, pos = bytearray(), 0
    for _ in range(MAX_ZLIB_CHUNKS):
        if pos == len(pay):
            break
        if pos + 4 > len(pay):
            raise ValueError('truncated IDRK zlib chunk header')
        zsz = struct.unpack_from('<I', pay, pos)[0]
        pos += 4
        if zsz == 0:
            break
        if pos + zsz > len(pay):
            raise ValueError('IDRK zlib chunk extends past payload')
        chunk = zlib.decompress(pay[pos:pos + zsz])
        if len(out) + len(chunk) > MAX_ENTRY_BYTES:
            raise ValueError('decompressed IDRK entry exceeds limit')
        out.extend(chunk)
        pos += zsz
    return bytes(out)


def _peek_idrk_prefix(pay, flags):
    if not (flags & 0x100000):
        return pay[:4]
    if len(pay) < 4:
        return b''
    zsz = struct.unpack_from('<I', pay, 0)[0]
    if zsz == 0 or 4 + zsz > len(pay):
        raise ValueError('invalid first IDRK zlib chunk')
    return zlib.decompressobj().decompress(pay[4:4 + zsz], 4)[:4]


def ktgl_shaders(blob):
    """Yield (key, stage, control, code) for KTGL NVN shaders inside blob."""
    pos, n = 0, len(blob)
    for _ in range(MAX_SHADERS_PER_BLOB):
        hit = blob.find(CTL_MAGIC, pos)
        if hit < 0 or hit + 0x880 > n:
            break
        if hit % 4 != 0:
            pos = hit + 1
            continue
        ctl_len = struct.unpack_from('<I', blob, hit + 0x20)[0]
        key_len = pss.control_key_len(blob[hit:hit + 0x880])
        control = blob[hit:hit + key_len]
        c1, clen = struct.unpack_from('<II', control, 0x700)
        stg_num = struct.unpack_from('<I', control, 0x714)[0]
        stage = STAGES.get(stg_num)
        code_end = hit + ctl_len + clen
        if (not 0x7cc <= ctl_len <= 0x900 or stage is None or not 0 < clen <= (16 << 20)
                or c1 > clen or code_end > n):
            pos = hit + 4
            continue
        code = blob[hit + ctl_len:code_end]
        if stg_num in (0, 1) and (clen < 0x30 or code[:4] != GFX_MAGIC):
            pos = hit + 4
            continue
        yield pss.digest(control + code), stage, control, code
        pos = code_end


def iter_rdb_shaders(data):
    """Walk IDRK0000 entries in a KTGL .rdb.bin* container and yield shaders."""
    pos, n = 0, len(data)
    for _ in range(MAX_IDRK_ENTRIES):
        o = data.find(IDRK_MAGIC, pos)
        if o < 0 or o + 0x38 > n:
            break
        esz, csz = struct.unpack_from('<QQ', data, o + 8)
        tid, flags = struct.unpack_from('<II', data, o + 0x28)
        if esz < 0x38 or csz > esz or o + esz > n:
            pos = o + 8
            continue
        pay = data[o + esz - csz:o + esz]
        if tid in SHADER_TIDS or (tid == 0 and _peek_idrk_prefix(pay, flags) in SHADER_PREFIXES):
            dec = decompress_idrk(pay, flags)
            yield from ktgl_shaders(dec)
        pos = o + ((esz + 3) & ~3)


def _is_shader_rdb_name(rel_path):
    name = Path(rel_path).name
    return rel_path.startswith('asset/') and '.rdb.bin' in name and (
        name.startswith('system.rdb.bin') or not (Path(rel_path).parent / 'system.rdb.bin').name)


def _load_romfs_sources(romfs, is_tree, romfs_config):
    index_meta = None
    blobs = []
    paths_for_hash = []
    if is_tree:
        sys_rdbs = sorted((romfs / 'asset').glob('system.rdb.bin*'))
        rdbs = sys_rdbs or sorted((romfs / 'asset').glob('*.rdb.bin*'))
        for p in rdbs:
            raw = p.read_bytes()
            blobs.append((p.relative_to(romfs).as_posix(), raw))
            paths_for_hash.append(p)
        sha = _resolve_romfs_sha(romfs, romfs_config, None, paths_for_hash)
        return blobs, sha
    if (romfs / 'runtime-index.bin').is_file():
        entries = read_runtime_index(romfs / 'runtime-index.bin')
    elif (romfs / 'index.json').is_file():
        index_meta = json.loads((romfs / 'index.json').read_text())
        if index_meta.get('status') != 'pass':
            raise ValueError('RomFS has not passed integrity verification')
        entries = {r['path']: r for r in index_meta['files']}
    else:
        raise ValueError(f'missing runtime-index.bin or index.json in {romfs}')
    sys_names = sorted(k for k in entries if k.startswith('asset/system.rdb.bin'))
    names = sys_names or sorted(k for k in entries if k.startswith('asset/') and '.rdb.bin' in k)
    with (romfs / 'romfs.bin').open('rb') as stream:
        for rel in names:
            row = entries[rel]
            stream.seek(row['offset'])
            raw = stream.read(row['size'])
            if len(raw) != row['size']:
                raise ValueError(f'short RomFS read for {rel}')
            blobs.append((rel, raw))
    sha = _resolve_romfs_sha(romfs, romfs_config, index_meta, [])
    return blobs, sha


def inventory(romfs, is_tree=False, modules=None, romfs_config=None):
    """Collect unique shaders from RomFS KTGL containers and patched modules."""
    blobs, romfs_sha = _load_romfs_sources(romfs, is_tree, romfs_config)
    shaders, archives = {}, []
    for rel, raw in blobs:
        count = 0
        for key, stage, control, code in iter_rdb_shaders(raw):
            shaders.setdefault(key, (stage, control, code, rel))
            count += 1
        if count:
            archives.append(dict(path=rel, sha256=pss.digest(raw)))
    if modules:
        for mod in sorted(Path(modules).glob('*.patched.bin')):
            mod_data = mod.read_bytes()
            src = f'module:{mod.name[:-len(".patched.bin")]}'
            for key, stage, control, code in ktgl_shaders(mod_data):
                shaders.setdefault(key, (stage, control, code, src))
    if not shaders:
        raise ValueError('no AoC KTGL/GLSLC shaders discovered')
    return shaders, archives, romfs_sha


def _translate_one(process, staging, key, stage, control, code, answers, variant, retained_meta, mine, variants):
    c1, length = struct.unpack_from('<II', control, 0x700)
    code_path = staging / f'{variant}.code'
    code_path.write_bytes(code[0 if stage == 'cs' else 0x30:length])
    request = dict(
        variant=variant, stage=stage, code=str(code_path.resolve()),
        c1=code[c1:length].hex(), depth_minus_w=True,
        early_fragment_tests=(retained_meta['early_fragment_tests'] if retained_meta
                              else stage == 'ps' and len(control) == 0x880 and bool(control[0x718])),
        **answers,
    )
    if stage == 'cs':
        request['local'] = list(struct.unpack_from('<III', control, 0x748))
    process.stdin.write(json.dumps(request) + '\n')
    process.stdin.flush()
    response = process.stdout.readline()
    code_path.unlink(missing_ok=True)
    if not response:
        raise ValueError('shader preparation service exited unexpectedly')
    reply = json.loads(response)
    if reply.get('stage_mismatch'):
        raise ValueError(f'prepared shader stage mismatch for {variant}: {reply.get("error")}')
    if not reply.get('ok'):
        return 'failed', reply.get('error')
    deps = pss.vulkan_row(staging, key, variant)
    lines = (staging / f'{variant}.vk').read_text().splitlines()
    if deps is None or lines[:2] != ['vk 1', f'stage {STAGE_NUMS[stage]}']:
        raise ValueError(f'prepared shader identity mismatch: {variant}')
    pss.validate_spirv_stage((staging / f'{variant}.spv').read_bytes(), stage)
    if deps in mine and retained_meta is None:
        for suf in ('.spv', '.vk'):
            (staging / f'{variant}{suf}').unlink(missing_ok=True)
        (staging / f'{key}.vv').write_text(''.join(f'{v}\t{d}\n' for v, d in variants.items()))
        return 'dup', None
    mine.setdefault(deps, variant)
    variants[variant] = deps
    (staging / f'{key}.vv').write_text(''.join(f'{v}\t{d}\n' for v, d in variants.items()))
    return 'ok', variant


def _process_key(proc, key, indices, jobs, shaders, staging, retained, shared):
    mine, variants = {}, {}
    stage, control, code, source = shaders[key]
    lock, completed, failed, produced, done, started = shared
    for idx in indices:
        _, answers, variant, why = jobs[idx]
        if variant in variants:
            continue
        ret = retained.get(variant)
        if why == 'listed' and ret is None and json.dumps(answers, sort_keys=True, separators=(',', ':')) in mine:
            continue
        status, info = _translate_one(proc, staging, key, stage, control, code, answers, variant, ret, mine, variants)
        with lock:
            if status == 'ok':
                completed.append(info)
                done[0] += 1
            elif status == 'failed':
                failed.append(dict(key=key, variant=variant, why=why, stage=stage, source=source, error=info))
                done[1] += 1
            if (done[0] + done[1]) % 250 == 0 and (done[0] + done[1]) > 0:
                print(f'prepared {done[0]}/{len(jobs)}, failed {done[1]}; {time.monotonic() - started:.1f}s', flush=True)
    if mine:
        with lock:
            produced[key] = mine


def _run_workers(jobs, shaders, staging, translator, retained):
    by_key = {}
    for idx, job in enumerate(jobs):
        by_key.setdefault(job[0], []).append(idx)
    completed, failed, produced = [], [], {}
    lock, stop, started = threading.Lock(), threading.Event(), time.monotonic()
    pending = iter(by_key.items())
    shared = (lock, completed, failed, produced, [0, 0], started)

    def worker():
        with subprocess.Popen([str(translator), '--service', '--vulkan', '--out', str(staging), '--chain', pss.CHAIN],
                              stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True) as proc:
            try:
                for _ in range(len(by_key) + 1):
                    if stop.is_set():
                        return
                    with lock:
                        item = next(pending, None)
                    if item is None:
                        return
                    _process_key(proc, item[0], item[1], jobs, shaders, staging, retained, shared)
            except BaseException:
                stop.set()
                raise
            finally:
                if proc.stdin:
                    proc.stdin.close()

    workers = max(1, min(8, os.cpu_count() or 4, len(by_key)))
    with concurrent.futures.ThreadPoolExecutor(workers) as pool:
        for f in [pool.submit(worker) for _ in range(workers)]:
            f.result()
    return completed, failed, produced, time.monotonic() - started


def _finalize_seed(staging, output, shaders, archives, identity, completed, failed, retained_names, elapsed):
    wanted = set()
    for variant in completed:
        wanted.update([variant + '.spv', variant + '.vk', variant.split('-')[0] + '.vv'])
    for p in staging.iterdir():
        if p.name not in wanted:
            p.unlink(missing_ok=True)
    scope = pss.shader_key_scope(shaders, VERSION, identity['source_romfs_sha256'])
    (staging / 'shader-keys.json').write_text(json.dumps(scope, separators=(',', ':')) + '\n')
    wanted_final = wanted | {'shader-keys.json'}
    report = dict(
        **identity, shaders=len(completed), discovered=len(shaders), startup_shaders=len(shaders),
        skipped=failed, archives=archives, seconds=elapsed,
        files={p.name: pss.digest(p.read_bytes()) for p in sorted(staging.iterdir()) if p.name in wanted_final},
    )
    if retained_names:
        report['retained_variants'] = len(retained_names)
    (staging / 'seed-manifest.json').write_text(json.dumps(report, indent=2) + '\n')
    if output.exists():
        shutil.rmtree(output)
    staging.rename(output)
    return report


def _build_identity(translator, romfs_sha, shader_list, retained, retain_vulkan_seed):
    identity = dict(
        schema=1, chain=pss.CHAIN, version=VERSION, game=GAME, vulkan=True,
        source_romfs_sha256=romfs_sha, translator_sha256=pss.digest(Path(translator).read_bytes()),
        gather_bias=os.environ.get('TOTK_GATHER_BIAS') != '0',
        early_fragment_tests_policy=pss.EARLY_FRAGMENT_TESTS_POLICY,
        preparation_sha256=pss.digest(Path(__file__).read_bytes()),
        shader_list_sha256=pss.digest(shader_list.read_bytes()) if shader_list.is_file() else None,
    )
    if retain_vulkan_seed is not None:
        identity['retained_vulkan_sha256'] = pss.digest(json.dumps(retained, sort_keys=True, separators=(',', ':')).encode())
    return identity


def prepare(romfs, output, translator, *, is_tree=False, shader_list=None, modules=None,
            romfs_config=None, retain_vulkan_seed=None, pack=True):
    """Discover, translate, verify, and optionally pack the hwaoc130 Vulkan shader seed."""
    shader_list = Path(shader_list) if shader_list else DEFAULT_LIST
    shared = ShaderList.load(shader_list)
    retained = pss.retained_vulkan_requests(retain_vulkan_seed)
    shaders, archives, romfs_sha = inventory(romfs, is_tree=is_tree, modules=modules, romfs_config=romfs_config)
    identity = _build_identity(translator, romfs_sha, shader_list, retained, retain_vulkan_seed)
    report = pss.reusable_seed(output, identity, min_shaders=1)
    if report is None:
        staging = output.with_name(output.name + '.partial')
        if staging.exists():
            shutil.rmtree(staging)
        staging.mkdir(parents=True)
        jobs = pss.plan(shaders, set(shaders), shared.variants, all_default=True, game=GAME)
        scheduled = {variant for _, _, variant, _ in jobs}
        retained_names = {v for v, req in retained.items() if req['key'] in shaders}
        for variant in sorted(retained_names):
            req = retained[variant]
            if req['stage'] != shaders[req['key']][0]:
                raise ValueError(f'retained shader stage disagrees with game inventory: {variant}')
            if variant not in scheduled:
                jobs.append((req['key'], req['answers'], variant, 'retained'))
        completed, failed, produced, elapsed = _run_workers(jobs, shaders, staging, translator, retained)
        missing_retained = retained_names - set(completed)
        if missing_retained:
            raise ValueError(f'retained Vulkan variants failed translation: {sorted(missing_retained)}')
        if not produced:
            raise ValueError('no AoC shaders translated successfully')
        report = _finalize_seed(staging, output, shaders, archives, identity, completed, failed, retained_names, elapsed)
    if pack:
        packed = pack_seed(output)
        sid = _hash_files_bounded([packed])
        (output / 'seed-id.txt').write_text(sid)
        report['seed_id'] = sid
        report['pack_bytes'] = packed.stat().st_size
    return report


def build_parser():
    """Construct the CLI argument parser for Age of Calamity seed preparation."""
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    source = parser.add_mutually_exclusive_group(required=True)
    source.add_argument('--romfs', type=Path, help='Folder containing merged romfs.bin and runtime-index.bin/index.json')
    source.add_argument('--romfs-tree', type=Path, help='Loose RomFS tree containing asset/system.rdb.bin*')
    parser.add_argument('--romfs-config', type=Path, help='Optional romfs-config.txt for ROMFS_IMAGE_SHA256')
    parser.add_argument('--modules', type=Path, help='rewritten/hwaoc130 (*.patched.bin) for embedded GLSLC shaders')
    parser.add_argument('--translator', type=Path, required=True, help='Path to totk_xlate / libtotk_xlate.so')
    parser.add_argument('--output', type=Path, required=True, help='Destination shader seed folder')
    parser.add_argument('--shader-list', type=Path, default=DEFAULT_LIST, help='Shared shader list TSV path')
    parser.add_argument('--retain-vulkan-seed', type=Path, help='Existing loose Vulkan seed whose variants stay named')
    parser.add_argument('--no-pack', action='store_true', help='Leave loose .spv/.vk/.vv files without seed.vkpack')
    return parser


def main(argv=None):
    """Validate CLI inputs and prepare the hwaoc130 Vulkan shader seed."""
    args = build_parser().parse_args(argv)
    src = args.romfs_tree or args.romfs
    if not src.is_dir():
        sys.exit(f'prepare_hwaoc_seed: missing RomFS directory: {src}')
    if not args.translator.is_file():
        sys.exit(f'prepare_hwaoc_seed: missing translator binary: {args.translator}')
    if args.shader_list and not args.shader_list.is_file():
        sys.exit(f'prepare_hwaoc_seed: missing shader list: {args.shader_list}')
    report = prepare(
        src.resolve(), args.output.resolve(), args.translator.resolve(),
        is_tree=bool(args.romfs_tree),
        shader_list=args.shader_list.resolve() if args.shader_list else None,
        modules=args.modules.resolve() if args.modules else None,
        romfs_config=args.romfs_config.resolve() if args.romfs_config else None,
        retain_vulkan_seed=args.retain_vulkan_seed.resolve() if args.retain_vulkan_seed else None,
        pack=not args.no_pack,
    )
    print(f'[{VERSION}] prepared {report["shaders"]} variants of {report["discovered"]} shaders '
          f'({len(report["skipped"])} failed)', flush=True)


if __name__ == '__main__':
    main()
