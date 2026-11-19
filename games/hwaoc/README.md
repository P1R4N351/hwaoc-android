# Hyrule Warriors: Age of Calamity — M0–M6 Android Bring-Up & Verification

[![Support with Bitcoin](https://img.shields.io/badge/Support_with-Bitcoin-F7931A?style=for-the-badge&logo=bitcoin&logoColor=white)](#support)

Status: **M0–M6 verified** on `1.3.0` (`v327680`): all three modules (`subsdk0`, `sdk`, `main`) pass `patch_nso.py`, `structck.py`, and `equiv.py`; Vulkan graphics verified through VP9 cutscenes and 3D battle rendering (`Pass 3b` to frame `1702`, `0` unimplemented NVN symbols) with separable `nvnCommandBufferBindProgram`, `nvnCommandBufferSetCopyImageStride`, `nvnTexturePoolRegisterImage`, and `nvnCommandBufferDrawArraysIndirect` (`QUADS` compute rewrite); `53` canonical `V` rows harvested into [`assets/shader_list.tsv`](assets/shader_list.tsv) (`0` warm misses); flat `1 MiB` `svdt` save management wired (`SaveManager.FLAT_SAVE`); and `AoC Native` (`0.1.0-beta.1` / `0.1.0-dev`) APK packaging configured via [`tools/build_app.sh`](../../tools/build_app.sh).

---

## 1. Title & Dump Identity

| Field | Value |
|---|---|
| Title | Hyrule Warriors: Age of Calamity |
| Developer / Publisher | Koei Tecmo (Omega Force) / Nintendo |
| Title ID | `01002B00111A2000` |
| Inspected base dump | `v0` (`1.0.0`), Program NCA `7956cd1ae9fd7200fa78637a1f9aa64f.nca`, Control `a3ca6e24ead9ee9b0686b8d60f9b85ba.nca` |
| Inspected update dump | `v327680` (`1.3.0`), Program NCA `7c0af4d41ade4b493c23321ec3131b1f.nca`, Control `cfd4988b48eb95916245fb2d7066026b.nca` |
| Add-on content base ID (NACP) | `01002B00111A3000` |
| Master key revision | `0xA (9.1.0-)` |
| NintendoSDK version | `9.6.0` (update Data/Meta NCAs packaged with SDK `13.2.0.0`) |
| Later upstream version (not on device) | `v458752` (`1.4.1`) + Expansion Pass DLC `01002B00111A3001`–`01002B00111A3004` (per `ly05010419/MacNativeGames` `games/aoc`, MIT; DLC there targets `1.4.1` only) |

### NACP & NPDM Parameters (`v0` / `v327680`)

- `user_account_save_data_size`: `2,097,152` bytes (`2 MiB`), `user_account_save_data_journal_size`: `2,097,152` bytes (`2 MiB`).
- `device_save_data_size`: `0`, `bcat_delivery_cache_storage_size`: `0`, `cache_storage_size`: `0`.
- `save_data_owner_id`: `01002B00111A2000`, `presence_group_id`: `01002B00111A2000`.
- `main.npdm`: `META`, `program_id = 01002B00111A2000`, `mmu_flags = 7`, `main_thread_priority = 44`, `main_thread_core = 0`, `sys_resource = 0` (uses `svcSetHeapSize`), `main_thread_stack = 0x100000` (`1 MiB`).

---

## 2. ExeFS Module Inventory

All `v327680` (`1.3.0`) file sizes, SHA-256 digests, build IDs, and uncompressed segment digests match `MacNativeGames/games/aoc/assets/expected-1.3.0.json` byte-for-byte. `rtld`, `subsdk0`, and `sdk` are identical across `v0` and `v327680`.

### `v327680` (`1.3.0`, merged base + update)

| File | NSO Size | NSO SHA-256 | Build ID | `text` / `rodata` / `data` / `bss` Sizes (B) | Imports / Exports / SVCs |
|---|---:|---|---|---|---:|
| `rtld` | 6,790 | `b09545d23efe76925191e9ded2c5ca81179f07fc823162c2b367a63b82d8d9fc` | `51b657503637c8a8d6729ce7d8ddaa6a163c356a000000000000000000000000` | `5,728` / `3,208` / `640` / `3,456` | 19 / 0 / 4 (`0x6, 0x26, 0x27, 0x28`) |
| `main` | 17,901,336 | `84fbac8bb4734cdbeb3362a87479e484915682791688a2e8b2258b8229541022` | `c3cf52bf2b05d7310884971b50bcccb8974577bc000000000000000000000000` | `28,651,856` / `11,427,504` / `1,298,896` / `4,423,216` | 387 / 19 / 0 |
| `subsdk0` | 3,522,072 | `b515e170ed8799af63e0f2e1a58b344ec8c78605976a2148374d13e6ced93d30` | `3ab92c3444d4ece2f52417b5d8ee86eb115212a3000000000000000000000000` | `3,582,656` / `2,447,056` / `198,344` / `1,161,528` | 650 / 10,863 / 0 |
| `sdk` | 5,368,143 | `71a49827250b6bc2883ac44e5d42f4d20110a95d930c2387311c7ff415eb4534` | `95832210b36121e88b4f9b7b66239dade9c82a67000000000000000000000000` | `5,519,040` / `6,642,000` / `397,408` / `692,128` | 13 / 25,206 / 61 |
| `main.npdm` | 1,572 | `6ffc47950fde1f67e4024e621a01f8f7e062f233ccd2b892844424f95e7a7063` | — | — | — |

`v327680` `main` segment SHA-256s: `text` `9edb6d68df02cd74a6012bf1d42a961dc57f4fde7a092f9743311aa3673ed8ee`, `rodata` `29708a5d7854540011a4295ac0d2d831fc1dc2a90ea69857418859c3cddab21f`, `data` `69c66a51af0e672e886e35ae9feada3d120653d3f331fa72cf176d34d122a396`. `main` `DT_NEEDED`: `multimedia.nss` (build-machine path) and `nnSdk.nss`.

### `v0` (`1.0.0`, base differences only)

- `main`: `16,814,561` B, SHA-256 `b79bed9eb80ced087dd6be22970e9f64e8645daab29b586a5ee82a9b4703caed`, Build ID `935ebe83c4a0658348a8a1c05cf8dac3f43ceb84000000000000000000000000` (`text` `26,608,592` B `c09cd65f...`, `rodata` `11,041,440` B `1df965e9...`, `data` `1,210,552` B `354ef7a7...`, `bss` `4,405,064` B).
- `main.npdm`: `1,572` B, SHA-256 `50ea1d0998e5cd4152720e4c17204367019821526b014072e2cc211eab9fb70e`.

---

## 3. Engine, Middleware & RomFS Layout

- **Engine**: Koei Tecmo **KTGL** (`KTGL For NX(a64)`) + **KIDS** (`KIDSTaskScheduler`, `KIDSResourceScheduler`), NintendoSDK `9.6.0` (`NintendoSDK_gfx-9_6_0-Release`, `NintendoSDK_NVN-9_6_0-Release`, `NintendoSdk_nnSdk-9_6_0-Release`, `NintendoSDK_libz-9_6_0-Release`).
- **Audio**: **KTSL2** (`KTSL2HLUpdate`, `KTSL2_Render`, `ASRS`) backed by `nn::audio::AudioRenderer` / `AudioRenderSystem` and `OpusDecoder`. **No CRIWARE ADX/Sofdec** middleware is present in ExeFS.
- **Movies**: `subsdk0` is `NintendoSDK_movie-9_6_0-Release` (software VP9 + Opus). RomFS holds `97` `.webm` files (`93` in `movie/`, `4` in `movie_logo/`).
- **Exclusive-load atomics**: Capstone scan of `v327680` `main` `.text` found `25,976` `ldaxr` and `24,855` `stlxr` instructions and **zero** plain `ldxr` (unlike BotW's `sead` spin locks).
- **Shaders**: `v327680` `main` holds `230` `0x00220015` GLSLC magic hits (`MacNativeGames` reports `118` shaders in `main`), with the remaining shaders stored in KTGL `G2S` containers inside the `.rdb` asset archives.
- **RomFS layout**:
  - `v0`: `1,086` files (`asset/`: `8` `.rdb`, `8` `.rdb.bin`, `970` `asset/data/*.file`, `1` `.txt`; `data/`: `LinkData2.bin`, `LinkInfo2.bin`; `movie/` + `movie_logo/`: `97` `.webm`).
  - `v327680`: `1,101` files (`asset/`: `8` `.rdb` with `_DRK0000` magic, `24` `.rdb.bin`/`.bin1`/`.bin2`/`.bin3` with `IDRK0000` magic, `970` `asset/data/*.file` with `IDRK0000` magic; `data/`: `2` files; `movie/` + `movie_logo/`: `97` `.webm`).
- **Second screen**: `igawa6/eden-duo-companions` currently has no Age of Calamity companion module; M7 will require authoring a new module and declarative page.

---

## 4. M1 Rewriter & Merged RomFS Results (`DM_HOST_TP=tpidr_el0`)

With the `lr`/`fp` jump-table register index fix in `games/totk/third_party/d2r/patch_nso.py`, all three `v327680` modules pass `patch_nso.py`, `structck.py`, and `equiv.py`:

| Module | `patch_nso.py` | `structck.py` | `equiv.py` (`--min-eff 2 --max-extra 16`) | Patched SHA-256 |
|---|---|---|---|---|
| `subsdk0` | **PASS** — `10,201` sites (`plain=10025`, `cond=147`, `pcrel=25`, `br=4`), `0` gates, patch `1,245,204` B (`PATCH_OFF=0x720000`) | **PASS** (`STRUCT subsdk0 OK`, `violations=0`, `失败项=0`) | **PASS** (`10,201/10,201` sites, `31,197` trials, `27,464` effective, `0` divergent, `150/150` caught) | `146e4de3ef491bde117d1160df3aa10723b79b2a0fbaf0904239f77c10913961` |
| `sdk` | **PASS** — `11,455` sites (`window=3`, `plain=11130`, `cond=114`, `tpidrro_r=60`, `pcrel=45`, `br=5`, `cntpct=32`, `tpidr_w=3`, `svc=61`, `tpidr_r=2`), `93` gates, patch `519,580` B (`PATCH_OFF=0xcb0000`) | **PASS** (`STRUCT sdk OK`, `violations=0`, `失败项=0`) | **PASS** (`11,362/11,362` sites + `93` gates, `34,777` trials, `30,392` effective, `0` divergent, `150/150` caught) | `b6cc413ad5854ab562a601dfe751c3ac588d6e3173893abd7b2bb4f8d828b573` |
| `main` | **PASS** — `37,228` sites (`window=250`, `plain=35413`, `cond=1043`, `pcrel=519`, `br=3`), `0` gates, patch `1,658,032` B (`PATCH_OFF=0x2bc0000`) | **PASS** (`STRUCT main OK`, `violations=0`, `失败项=0`) | **PASS** (`37,228/37,228` sites, `113,367` trials, `103,731` effective, `0` divergent, `300/300` caught) | `9adf35712d6c2d58dd52c4fe6eba0970e04cf5efec8a6dcc7acc36977185d84b` |

Merged `v327680` RomFS (`romfs.bin`, IVFC levels 0–5 `(GOOD)`): `11,719,230,892` B (`1,101` files), `ROMFS_IMAGE_SHA256 = 9006c2b8f752c27e2ca628507e45f871ea54d464945aa8d562266bc399f3f0e2`, `runtime-index.bin` (`55,652` B) `ROMFS_INDEX_SHA256 = 76628b10dabab48e1ce2dc37606c90ffcb2cb05f1edd641653fc278a8143c376`.

### Root cause resolved in `patch_nso.py` for `0x138216c`

Disassembly of `v327680` `main` `.text` at `0x138214c`–`0x138216c`:

```text
138214c: cmp  w12, #0xa
1382150: b.hi #0x1382204
1382154: and  x12, x12, #0xffffffff
1382158: adr  x18, #0x1382170
138215c: ldrb w30, [x14, x12]
1382160: add  x18, x18, x30, lsl #2
1382164: mov  w30, w13
1382168: mov  x12, x17
138216c: br   x18
```

In `patch_nso.py`, `p.reg_name(p.operands[2].reg)` returns Capstone's alias `'lr'` for `x30` in `add x18, x18, x30, lsl #2`, whereas `num()` (`r'[xw](\d+)'`) returned `None` for `'lr'` (and `'fp'`), leaving `idx` as `None`. Mapping `{'lr': 30, 'fp': 29}` and guarding `if idx is not None:` resolved `0x138216c`.

---

## 5. Known M2–M7 Bring-Up Hazards (from `MacNativeGames/games/aoc`, MIT)

1. **`SetHeapSize` memory accounting**: `rtld` + `main` + `subsdk0` + `sdk` occupy `~66.6 MiB` mapped (`> 64 MiB` default `mem_used`); report `>= 72 MiB` used so `nninitStartup`'s `SetHeapSize` fits all `15` game subheaps (`3,098 MiB` total).
2. **KTSL sound heap cut (`1.3.0`)**: `main+0x8d9610`/`0x8d9618` splits a single sound block into `15 MiB` engine + `2 MiB` + `142 MiB` data; on host `AudioRenderSystem::GetWorkBufferSize` (`0x760000`), engine setup requests `~16.9 MiB` and stalls `nnMain+0x8d9bc8` unless `4 MiB` is shifted from the data heap to the engine heap.
3. **Three-clock simulation pacing**: KIDS task (`0x166dab4`/`0x166dc60`), KTGL device (`0x8d94b0`), and scene/battle float-fps (`0x8c3610`) are three independent clocks; default simulation is `30 fps` (`1.3.0` does not carry `1.4.1`'s cutscene 30-fps lock check).
4. **Graphics hazards**: VP9 movie playback writes NV12 into `1280x720 R8` + `640x360 RG8` CPU-mapped textures flushed via `nvnMemoryPoolFlushMappedRange`; KTGL full-screen stencil effects self-read the `1440x810 RG11B10Float` scene target; soldier draws use `Half2`/`Short2` attributes feeding integer vertex shader inputs across large mesh pools.

---

## 6. Sibling Target: Hyrule Warriors: Age of Imprisonment (`04003F40232DE000`)

Inspected directly from `/sdcard/Games/Switch 2/Hyrule_Warriors_Age_of_Imprisonment.nup` (`47,526,817,060` B, unencrypted ZSTD NUP v1, TOC SHA-256 verified):

| Field | `PKG[0]` (`kind=0`, `ver=0`) | `PKG[1]` (`kind=1`, `ver=196608`) |
|---|---|---|
| `main` size / SHA-256 | `20,877,964` B / `564effff...aa9f1c921` | `21,107,182` B / `4c33cfd4...de604c3` |
| `main` Build ID | `a49a9ab58153cf5a8e6f6ec3c3b11d38a5f592e8...` | `659cb288b5c5b2b49853add616c0d5bf4ea4e1ba...` |
| `sdk` / `subsdk0` / `rtld` Build IDs | `820cd7d0...` / `2f1ad415...` (`NintendoSDK_movie`) / `2672a2eb...` | Identical to `PKG[0]` |
| SDK / Engine | `NintendoSDK 20.5.15`, `KTGL For Ounce(a64)` + `KIDS` | `NintendoSDK 20.5.15`, `KTGL For Ounce(a64)` + `KIDS` |
| Graphics / Shaders | **NVN2** (`729` `nvn*` strings, `nvnNativeImage*`, `CopyEngineNVX`), **161** Ampere (`0x98761234`) + **322** GLSLC (`0x00220015`) hits | Same NVN2 + Ampere counts |
| RomFS | `b6e28c40/`: `1,842` `.fdata` (`PDRK0000`/`IDRK0000`, `10.39 GB`); `ebc8c781/`: `106` `.bin` (EBML `.webm`, `23.85 GB`) | `b6e28c40/`: `1,930` `.fdata` (`10.59 GB`); `ebc8c781/`: `106` `.bin` (`23.85 GB`) |

**Feasibility note**: `botw-android` / `totk-android` currently targets Switch 1 NVN 1, Maxwell shaders (`libtotk_xlate.so`), and Switch 1 Horizon services. Age of Imprisonment uses **NVN2**, **Ampere `sm86`** shaders, and **SDK 20.5.15** (`multimedia_compat_nx.nss`), so it remains a separate Switch 2 research track after Age of Calamity.


## Support

If this work is useful to you, donations are gratefully accepted:

- Bitcoin: `bc1q4a3lqcg87rvw7vxulausdqhhuwkwwa6w6v4mxx`
- VitaDB profile: [PiSCES](https://www.rinnegatamante.eu/vitadb/#/user/PiSCES)
