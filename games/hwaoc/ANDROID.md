# Hyrule Warriors: Age of Calamity 1.3.0 on this port (M2 bring-up)

[![Support with Bitcoin](https://img.shields.io/badge/Support_with-Bitcoin-F7931A?style=for-the-badge&logo=bitcoin&logoColor=white)](#support)

This branch adds a third Switch 1 game to the Android port: **Hyrule Warriors: Age of Calamity**, version `1.3.0` (`v327680`, title `01002B00111A2000`, `main` build ID `c3cf52bf2b05d7310884971b50bcccb8...`). It reuses the shared Horizon runtime, NVN 53 HLE, and Vulkan graphics layer. No copyrighted game code or assets live in this repository; provide your own dump.

## What differs from Tears of the Kingdom (`v143`)

| Area | Age of Calamity (`hwaoc130`) |
|---|---|
| Build | `make -C games/totk/android VERSION=hwaoc130 TARGET=android APP=0 GPU={null,vk} ...` |
| Defines | `-DGAME_HWAOC -DGAME_HWAOC130` (`games/games.toml`) |
| Game patches | `runtime/game_patches_hwaoc.c`: shifts `4 MiB` from the KTSL2 data heap to the KTSL2 engine heap (`main+0x8d9610`/`0x8d9618`, `15+142 -> 19+138 MiB`, disabled with `TOTK_KTSL_HEAP=0`) and emits 30s frame telemetry |
| Memory pool reserve | `services.c`: `GetInfo(7/22)` includes a `16 MiB` default reserve (`TOTK_MEM_RESERVE`) before `SetHeapSize` runs so `nninitStartup` (`main+0x8ad2f0`) leaves room inside `APP_POOL` for `MapPhysicalMemory` worker thread stacks |
| NVN level | `hle_nvn.c`: reports NVN `53.312` (`main+0x9f94..0x9f9c`), `NEAR_IS_MINUS_W` default depth convention, and `nvnQueueAcquireTexture` |
| Companion | `gpl_companion = false`, `companion = "none"` until M7 authors an Age of Calamity Duo module |

## Staging layout on Thor (developer probe route)

Executables and rewritten code live in `/data/local/tmp/hwaoc-probe`, while the merged `11,719,230,892`-byte `romfs.bin` + `runtime-index.bin` (`1,101` files) live on microSD (`/storage/699D-14CC/hwaoc-probe/romfs`):

```text
/data/local/tmp/hwaoc-probe/
  totk-native, totk-native-vk, libtotk_xlate.so, libtotk_turnip.so
  rewritten/hwaoc130/{main,subsdk0,sdk}.patched.bin
  game-data/hwaoc130/{runtime-tables.bin,romfs-config.txt,shared-fonts.txt}
  user-data/, state/, shots/, shader-requests/
/storage/699D-14CC/hwaoc-probe/romfs/
  romfs.bin, runtime-index.bin
```


## Support

If this work is useful to you, donations are gratefully accepted:

- Bitcoin: `bc1q4a3lqcg87rvw7vxulausdqhhuwkwwa6w6v4mxx`
- VitaDB profile: [PiSCES](https://www.rinnegatamante.eu/vitadb/#/user/PiSCES)
