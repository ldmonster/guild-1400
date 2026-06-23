# Wave-11 hardening — APP spine + portable shim backends (W11-APP)

MCP DOWN: no new 1:1 reconstruction. This pass adds ASAN+UBSAN malformed/edge
input tests and fixes one UB in the owned code. Goldens unchanged; the in-bounds
valid-asset path is byte-identical.

## Cluster owned
- `src/app/config_write.cpp` / `.h` — settings serializer (`VIBE_Config_WriteGfxSettings`
  @0x56af54) + itoa helper `VIBE_AnimationState_Update` @0x5d92ec.
- `src/app/real_boot.cpp` / `.h` — `MountRealGameAssets`, `RealCityPath`,
  `DefaultResourceArchives`, `RealGameAssets::archiveForMember`.
- `src/shim_impl/` portable backends: `mem_filesystem`, `disk_filesystem`,
  `memory_graphics`, `null_platform`, `scripted_platform`, `loopback_socket`.
- Tests: `tests/unit/shim_backends_test.cpp`, `tests/unit/app_config_write_test.cpp`,
  `tests/unit/app_real_boot_edge_test.cpp` (new), the guarded e2e
  `app_real_boot_e2e_test`, `app_config_write_e2e_test`, `shim_backends_e2e_test`.

## Build
ASAN+UBSAN build in a DEDICATED dir to avoid racing sibling wave agents on the
shared `build-asan`/`build`:
```
cmake -S . -B build-asan-w11 -DCMAKE_BUILD_TYPE=Debug -DGUILD_BACKEND=OFF \
  -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-sanitize-recover=all -g"
cmake --build build-asan-w11 --target <owned test targets> -j$(nproc)
GUILD_GAME_DIR=$PWD/europe_guild_1400_original ./build-asan-w11/<test>
```
Normal `build/` also rebuilt + green for the owned targets.

## FIX (UB, owned) — FAITHFUL, observably identical
- **`src/app/config_write.cpp:21` `AnimationState_Update` — signed-overflow UB on
  INT_MIN.** Original (`VIBE_AnimationState_Update` @0x5d92ec) does
  `value = -value` (x86 `neg`) for radix-10 negatives, then uses it as unsigned.
  In C++ `-(INT_MIN)` is signed-overflow UB; UBSAN trips:
  `negation of -2147483648 cannot be represented in type 'int'`.
  Fix: negate in the UNSIGNED domain (`mag = 0u - (u32)value`), which reproduces
  the exact bit pattern x86 `neg` yields (0x80000000) with no UB. Output for every
  input is byte-identical; pinned by `AppConfigWrite.AnimationStateUpdateExtremeRadix10`
  ("-2147483648") and the existing radix goldens.

## Edge tests ADDED (drive the real parse/load/apply entries)
### shim_backends_test.cpp (+199 checks; 307 total, 0 fail)
- `MemFileReadPastEofAndEmpty` — empty file read, 0-byte request, seek-to-EOF read,
  seek-past-EOF read, last-byte partial read (early `pos_>=sz` guard exercised).
- `MemFileNegativeSeekAndRawBytes` — negative SEEK_SET / SEEK_CUR underflow /
  SEEK_END underflow all return -1 and leave the cursor put; bad whence -> -1;
  embedded-NUL / NUL-less raw bytes preserved.
- `MemFileSparseWriteZeroFillsGap` — seek past EOF then write zero-fills the gap;
  backing vector grows to the right size (no OOB memcpy at the write offset).
- `MemFileBadPathsModesAndPermissions` — null/empty/unknown mode -> nullptr;
  write to read-only and read from write-only return 0; `close(nullptr)` safe.
- `LoopbackEmptyAndZeroLength` — recv on empty/drained buffer + 0-length recv/send
  + null-data 0-length send all in-bounds, report 0.
- `LoopbackLargeFillDrainBoundary` — 4096-byte send drained in 7-byte recvs, every
  byte in order (ASAN on the per-byte deque pop at the boundary).
- `LoopbackRecvSendAfterClose` — recv after own close -> -1 (no UAF on the reset
  inbox); send with no peer -> -1; double close safe; buffered bytes still drain.
- `DiskFileNonexistentAndBadArgs` — nonexistent read, null path/mode, missing
  listDir, fresh empty-file size/tell, listDir frees cleanly (no leak).
- `GraphicsUninitAndNullPalette` — backbuffer/present/setPalette before init are
  safe no-ops; re-init resize touches the full realloc'd extent; null after shutdown.
- `ScriptedPlatformEmptyTimeline` — empty timeline never quits, default state,
  pollText "" with no queued text.
- `ScriptedPlatformQuitBoundaries` — quitAfterPumps(0) immediate; (3) clamps.
- `ScriptedPlatformOverLongTimeline` — 10001 steps over 5 pumps (duplicate +
  far-future indices, big held-key sets) apply in-bounds, no OOB.

### app_config_write_test.cpp (+ checks; 211 total, 0 fail)
- `AnimationStateUpdateExtremeRadix10` — INT_MAX / INT_MIN into the CHAR String[32]
  scratch (the UB fix's regression pin).
- `AnimationStateUpdateRadix2WidthBound` — -1 base-2 == 32 '1's + NUL stays in a
  40-byte buffer (longest width case).
- `SerializerEmitsNulTerminatedValuesToOddSink` — extreme field values driven into
  a sink that copies each value through a fixed C-buffer; all 45 keys produce
  well-formed NUL-terminated strings (ASAN over-read check).
- `StadtVerbatimEmptyAndLong` — empty + 50-char stadt pass through verbatim (no
  itoa, no truncation in the serializer).

### app_real_boot_edge_test.cpp (NEW, 52 checks, 0 fail) — no real assets needed
Drives `MountRealGameAssets` over an in-memory `MemFileSystem`:
- `MissingDirectoryAllDefaults` — empty fs (== missing dir): no INI -> defaults,
  every archive mount fails (mounted=false, 0 members), `archiveForMember`
  (incl. null) -> nullptr.
- `PartialInstallIniOnlyNoArchives` — Gilde.INI parsed (Stadt/show_intro/master_vol)
  but no .BIN files; archives all fail gracefully.
- `EmptyIniFile` — 0-byte INI: SlurpText resizes to 0, reads 0, all defaults.
- `TruncatedAndMalformedArchives` — 0-byte / 1-byte / header-only PKZIP sig /
  truncated EOCD / pure-garbage archive blobs: mount fails safe, no read off the
  truncated bytes; bogus OpenMember never crashes.
- `ArchiveForMemberOddQueries` — empty / null / non-existent member queries in-bounds.
- `RealCityPathEdge` — empty city -> "", normal city upper-cased + .cty path,
  non-ASCII city name doesn't crash `toupper`.

## BEHAVIORAL — needs MCP (NON-owned cluster: src/world/, do NOT edit here)
- **`src/world/data_load.cpp:134` (and `:114`) — OOB read in
  `WorldInitBuildingTypeTable` (`VIBE_World_InitBuildingTypeTable` @0x5833b4).**
  `ObjByte(i, 33)` = `g_sceneTypes[kSceneTypeStride*i + 33]` for `i` up to
  `kSceneTypeLoadCount-1` (730); the resulting byte indexes `kindWorthD`
  (`g_kindWorth + 1`). On a REAL install the `g_sceneTypes` backing buffer is
  shorter than `kSceneTypeStride * 731`, so the read runs past the buffer end.
  UBSAN (triggered via `RunHeadlessRealAssets` in `app_config_write_e2e_test`,
  real-asset path): `load of address ... with insufficient space for an object of
  type 'const u8'`. This is the WORLD cluster's to resolve (likely a buffer-size /
  loop-bound vs. the original's table extent — needs MCP to confirm the original's
  bound at 0x5833b4). Owned app + shim code is clean under ASAN+UBSAN.

## Status
- Owned ASAN+UBSAN test targets: GREEN (307 + 211 + 52 + e2e checks, 0 failures).
- Guarded real-asset e2e over `europe_guild_1400_original`: AppRealBoot.MountAndLoad
  GREEN (22 checks); shim_backends_e2e GREEN (25). app_config_write_e2e headless
  GREEN; its real-asset sub-case surfaces the world/data_load OOB above (not owned).
- Normal `build/` rebuilt + GREEN for owned targets.
- One owned UB fixed (faithful). One OOB documented for the world owner.
