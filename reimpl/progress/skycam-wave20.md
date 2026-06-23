# Wave-20 — W20-SKYCAM (sky dome + heightmap create + floor reload + camera target)

**Agent:** W20-SKYCAM · **Date:** 2026-06-16 · **MCP:** live (`gilde.exe`, imagebase 0x400000)

Reconstructs five entry-reachable engine-math/scene leaves flagged by the wave-18
coverage audit as "pure game logic" (render-adjacent, NOT GPU-API boundaries). All
five were confirmed un-reconstructed before this wave (only hook-pointer stubs and
virtual `note()` stubs existed in `render_recon2.cpp` / `gamelogic_recon.h` /
`gui_dialogs*.h`).

## Reconstructed (1:1)

| addr | name | new module | notes |
|------|------|-----------|-------|
| 0x5ef980 | VIBE_Sky_BuildDomeMesh | `src/render/sky_dome.{h,cpp}` | 6x8 lat/long dome vertex grid; double VectorLerp + Normalize + atan2 |
| 0x5c63a0 | VIBE_Heightmap_Create | `src/render/heightmap_create.{h,cpp}` | 0x30 record + heights/entries buffers + BuildTerrainMesh dispatch |
| 0x5bd2d8 | VIBE_Floor_ReloadTextures | `src/render/floor_reload.{h,cpp}` | 8 slots x 3 mip layers name-build + path resolve + decode |
| 0x4c0864 | VIBE_Camera_ComputeWorldTarget | `src/play/camera_target.{h,cpp}` | flag-branched (0x40 / 0x100 / ortho) target projection |
| 0x5d8ae8 | VIBE_Coord_Push | `src/play/camera_target.{h,cpp}` | 4-int coord-state store, returns first arg |

## Key reverse findings

### Sky dome (0x5ef980)
- Builds the geometry for the `VIBE_Sky_Create @0x5efd28` object (0xACC bytes).
  Distinct from the time-of-day **colour** path in `render/sky.{h,cpp}` (the clear
  colour) — this builds actual vertices.
- Grid is **6 rows x 8 cols** (outer `v2<6`, inner `v3<8`).
- Constants (verified `get_global_value`, bit-exact):
  - `flt_62C148 = 0x3E124925 = 1/7` — column phi/U step (8 cols span 0..1).
  - `flt_62C14C = 0x3E4CCCCD = 0.2` — row V step (6 rows: 0,.2,.4,.6,.8,1.0).
- Per cell: `dir = VectorLerp(VectorLerp(c30,c10,tRow), VectorLerp(c20,c00,tRow), tCol)`,
  `VectorNormalize(dir)`, vertex longitude = `atan2(dir.x, dir.z)` (stored at +0x5FC).
  U accumulator resets to `(float)rectXmin` per row, `+= uStep` per col; V =
  `rectYmin + r*vStep`. Two homogeneous `1.0f` (3F800000) per texcoord entry.
- Corner vectors `flt_13DCE00/10/20/30` and rect `dword_13ECE58/5C/60/64` are
  **runtime BSS** (zero in the static IDB; populated at scene load) — exposed as
  `SkyDomeInputs` so the math is testable and the live caller passes the real data.
- The `rebuild==false` path is the early-out (only resets +0xAA8, no geometry).

### Heightmap create (0x5c63a0)
- Allocates the 0x30 record (`render/heightmap.h` `Heightmap`, reused — no ODR),
  the `size*size` height bytes (+0x28) and, for `size>0`, the `24*size*size`
  tile-record array (+0x24), then dispatches to `BuildTerrainMesh @0x5c5610`.
- **1:1 sign quirk:** buffer sizes use `sz = abs(size)`, but the entries-vs-null
  branch and OOM guard test the **signed** `size` — so a negative size still allocs
  `heights` but sets `entries = null`. Reproduced + golden-pinned.
- **Host-ABI note:** the binary's record is exactly 0x30 (48 B, 4-byte pointers);
  on a 64-bit host `entries`/`heights` are native 8-byte pointers so the struct is
  64 B. `Create` allocs `sizeof(Heightmap)` (so the field writes are in-bounds) —
  the buffer byte-counts (`size*size`, `24*size*size`) are host-independent and
  exact. (Caught by my own ASan test before it could ship.)
- `BuildTerrainMesh @0x5c5610` itself is the deep render-coupled draw-list walk +
  raster submission (decompiled, 470+ lines); its **scale math** is already
  reconstructed in `render/heightmap.cpp::DeriveGridScaleXZ`. The full walk is
  **DEFERRED** (render boundary) and routed through `HeightmapBuildFn` — see Handoff.

### Floor reload (0x5bd2d8)
- `Floor_InvalidateTiles @0x5ba704` (== `render/terrain_mesh.cpp::InvalidateTiles`)
  then **8 texture slots x 3 mip layers** (outer 0..7, inner 3 = `96/32`).
- Per (slot, mip): name = slot template + mip suffix; the suffix table is
  `dword_5B8C90` (11-byte stride: `""`, `"_high_1"`, `"_high_2"`) — already
  modeled as `floorgfx_recon.cpp::kFloorMipSuffix` / `FloorTextureMipName`
  (REUSED, no redefinition).
- **Boundary (rule 3):** `Bmp_LoadBuffer @0x5f0ce4` (flags `17` = `0x10|0x01`) is the
  BMP decode + pixel upload into the tile surface buffer (`tile[48]`) — routed
  through `BmpLoadFn`; the path resolve `Texture_BuildBmpPath @0x5d97e8` through
  `BuildBmpPathFn`. The **selection / name-build / iteration logic is reconstructed
  1:1**; only the decode/upload crosses the hook.

### Camera world target (0x4c0864) + Coord_Push (0x5d8ae8)
- Constants (bit-exact): `flt_61E510=0.5`, `flt_61E514=6.0`, `flt_61E518=10.0`,
  `flt_62D224=1.0`.
- Three branches on the 16-bit camera flag word:
  - `mask & 0x40` set, `mask & 0x100` set: `A4 = trunc(py - 6.0)`, `A0 = trunc(px + 10.0)`.
  - `mask & 0x40` set, `0x100` clear: `A4 = trunc(py)`, `A0 = trunc(px)`,
    where `px = viewHalfX*0.5 + centerX`, `py = viewHalfY*0.5 + centerY`.
  - `mask & 0x40` clear (ortho): `A4 = packedX >> 17`, `A0 = packedY >> 17`
    (signed arithmetic shift of a 16.16 fixed-point coord; `>>16` then `>>1`).
- **ConvertX truncation verified against disasm:** every float→int site is
  `frndint` with RC=truncate-toward-zero (`VIBE_Coord_ConvertX @0x5c6b08`, reused
  as `guild::util::ConvertX == std::trunc`) followed by `fistp` — NOT round-to-even.
  Golden-pinned with negative inputs (toward-zero) and the offset sub-mode.
- `Coord_Push` stores `dword_64A1B8/BC/C0/B4` (edx/ebx/ecx/eax order) and returns
  the first arg; modeled as `CoordState` with the binary's exact store order.

## Reuse (extern, no ODR)
- `guild::util::ConvertX` (`util/coord.h`) — truncate toward zero.
- `guild::util::VectorLerp` / `VectorNormalize` / `Atan2` (`util/math.h`) — the
  exact engine math primitives the dome uses.
- `guild::render::Heightmap` (`render/heightmap.h`), `DeriveGridScaleXZ`.
- `guild::render::FloorTextureMipName` / `kFloorMipSuffix` (`render/floorgfx_recon.h`).

## Wiring / handoffs (rule 13)
All five reaching root is `0x527fa4` (display init) and/or `0x4c09a0` (frame loop).
The bind-site files that hold the call hooks are **not in my ownership**, so I expose
the functions + document the one-line handoff (per brief):

- **Sky dome** — caller `VIBE_Sky_Create @0x5efd28` (and rebuild sites in
  `Combat_LoadScenarioAssets`, `Cutscene_ExecMainFunc`, `Render_SetEngineEnabled`).
  Hook field: `render_recon2.h` `RenderHooks::skyBuildDomeMesh(int sky, int rebuild)`
  (currently `d_skyBuildDomeMesh` no-op). **Handoff:** bind `skyBuildDomeMesh` to a
  thunk that fills `SkyDomeInputs` from the live sky rect/corners and calls
  `guild::render::BuildDome`, writing the 48 vertices into the sky object at +0x5FC.
- **Heightmap create** — callers incl. `Scene_EnterCity @0x503a9c`,
  `Save_PostLoadInitScene`, `Universe_SwitchActiveSlot`, `Scene_LoadFromStream`,
  `Character_ResolveMesh`. Hook field: `gametick_recon4_orchestration.h`
  `heightmapBuildTerrainMesh` (0x5c5610). **Handoff:** call
  `guild::render::Create(srcAsset, size, flag)`; install `SetHeightmapBuilder` with
  the (deferred) BuildTerrainMesh thunk and `SetHeightmapAllocator` with
  `VIBE_Memory_AllocDebug @0x438f10`.
- **Floor reload** — caller `VIBE_Render_SetGammaTable @0x5b9ef4` and
  `Snow_UpdateScene @0x42a2cc`. Hook field: `render_recon_objlist.h`
  `floorReloadTextures(u32)` (0x5bd2d8). **Handoff:** bind to a thunk that fills a
  `FloorTileAccess` over the real floor record (slot names at floor+6756 stride 64,
  surfaces at tile+48 stride 32) and calls `guild::render::ReloadTextures`; install
  `SetFloorBmpLoader` → `Bmp_LoadBuffer @0x5f0ce4` and `SetFloorBmpPathBuilder` →
  `Texture_BuildBmpPath @0x5d97e8`.
- **Camera target** — caller `VIBE_GameLogic_RunFrameLoop @0x4c0f4e` (and turn
  transition, options screens). Hook: `gamelogic_recon.h`
  `cameraComputeWorldTarget(u32)` / options_run.h `CameraComputeWorldTarget()`.
  **Handoff:** bind to a thunk that loads `CameraTargetGlobals` from
  `flt_13FC778/77C/13FCF48/13FCD14` + `dword_69FFBC/B8` + the camera flag word and
  calls `guild::play::ComputeWorldTarget`, writing back `dword_69FFA4/A0`.
- **Coord_Push** — many callers (Object_Update, surface-blit clip install, groundplan).
  Hook fields: `gui_dialogs4.h`/`gui_dialogs7.h` `coordPush`. **Handoff:** bind to
  `guild::play::CoordPush` over a `CoordState` aliasing `dword_64A1B4/B8/BC/C0`.

## Tests
`tests/unit/skycam_wave20_test.cpp` — 16 cases, **277 checks, 0 failures**, clean
under AddressSanitizer (one self-found heap-overflow bug in the record alloc was
fixed before shipping). Golden vectors:
- dome: bit-exact constants; rebuild=false no-op; 48-vertex grid; U/V accumulators;
  longitude == `atan2(normalize(lerp))` pinned per column.
- heightmap: zero→null; positive→3 allocs (record/heights/entries) + build; negative
  sign quirk (heights allocated, entries null, 2 allocs).
- camera: bit-exact constants; ortho `>>17`; perspective plain/negative truncation;
  offset sub-mode; Coord_Push store order + return.
- floor: null early-out; 8x3=24 attempts all-loaded; missing-surface skip; mip suffix.

## Build status
The five modules + the test compile and link **clean** (`-Wall`, `-fsyntax-only`
green for all four `.cpp`; standalone test binary built and run). The full-tree
`make` is currently blocked by a **pre-existing, unrelated** break in
`src/sim/command_leaves.cpp` (`RunActionOrFree` undeclared — another in-flight
wave's file, not touched by this agent). CMake GLOB picked up all new files on
reconfigure. No `progress/INDEX.md` edits; no commits.

## Verdict: reconstructed vs leaves
- **Reconstructed 1:1 (5):** Sky_BuildDomeMesh, Heightmap_Create, Floor_ReloadTextures
  (selection/iteration), Camera_ComputeWorldTarget, Coord_Push.
- **Genuine leaves reached (reused, already present):** ConvertX (0x5c6b08),
  VectorLerp (0x5ca2fc), VectorNormalize (0x5cb148), Atan2 (0x5f5701),
  FloorTextureMipName/kFloorMipSuffix (0x5bd010/0x5B8C90), Heightmap struct +
  DeriveGridScaleXZ (0x5c5610 scale core), InvalidateTiles (0x5ba704).
- **Deferred boundaries (rule 3, routed through hooks):** BuildTerrainMesh draw-list
  walk + raster (0x5c5610), Bmp_LoadBuffer decode/upload (0x5f0ce4),
  Texture_BuildBmpPath VFS resolve (0x5d97e8), Memory_AllocDebug host heap (0x438f10).
