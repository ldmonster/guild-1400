# Mesh LOD filename strategy + stock-object load-or-find + LOD attach

Reconstruction of the stock-object LOD pipeline that sits on top of the mesh asset
cache (`src/render/mesh_asset.{h,cpp}`). New file: `src/render/mesh_lod_name.{h,cpp}`.

## Reconstructed (1:1)

| Addr | Symbol | Where | Notes |
|---|---|---|---|
| 0x5d1034 | `VIBE_Mesh_BuildTexturePath` | `mesh_lod_name.cpp` `BuildTexturePath` | Composes `"*"+name+suffix` (the `unk_628F14`=`"*"` prefix); existence via the original's `VIBE_Vfs_ResolveAndBuildPath` routed through `MeshLodHooks::textureExists` (inert default false). Path FORMATTING is pure + tested. |
| 0x5d15fc | `VIBE_Mesh_BuildLodFileName` | `mesh_lod_name.cpp` `BuildLodFileName` | PURE string strategy, all branches verbatim. |
| 0x5d1824 | `VIBE_Mesh_AttachStockObjectLods` | `mesh_lod_name.cpp` `AttachStockObjectLods` | Faithful attach structure; draw-block writes via `MeshLodHooks`. |
| 0x5d1114 | `VIBE_Mesh_AttachStockTextures` | `mesh_attach_textures.cpp` `AttachStockTextures` | The LAST hop (was a gap, now reconstructed 1:1): vertex copy + per-poly vertex/material resolution + texture-set build + LOD-frame select. Wired as the real `attachStockTextures` hook via `render::InstallStockTextureAttach()` (called from `sim::InstallRealObjectAttachWiring`). Stock-cache / texture-record / refcount / SelectLodFrame leaves routed through `MeshAttachHooks` (inert defaults). |
| 0x5b0c10 | `VIBE_Object_AllocPolysAndPoints` | hook `MeshAttachHooks::allocPolysAndPoints` | Decompiled (40\*polyCount poly alloc, 80\*(vertCount+8) point alloc); engine `VIBE_Memory_AllocDebug/FreeDebug` -> hook. |
| 0x5da2e4 | `VIBE_Texture_IncrementRefCount` | hook `MeshAttachHooks::textureIncrementRefCount` | Decompiled; the `dword_1406A84` 128-byte texture-record table walk is not ours -> hook. |
| 0x5adb6c | `VIBE_Mesh_SelectLodFrame` | `node_lod.cpp` `SelectLodFrame` (math) + `mesh_attach_textures.cpp` `SelectLodFrameForNode` (raw-block adapter) | RECONSTRUCTED 1:1 + WIRED. The forced-LOD branch (`((4*node+531)>>6)-1` clamp), the distance branch (`clamp(trunc(\|obj-cam\|·count·scale), 0, count-1)` via `util::ConvertX` x87 truncate), the `result+8 && result+12` geometry gate, and the `+528 \|= 0x40` dirty bit are all verbatim. `SelectLodFrameForNode` bridges the clean math to the raw object node and is installed as `MeshAttachHooks::selectLodFrame` by `InstallStockTextureAttach` (rule 13 — previously dangling). The distance globals (`dword_13FCD1C`/`flt_13FC774`/`byte_64A068`) are threaded through `SetLodSelectView`; default = "no active camera" → forced branch (rule 8). |
| 0x5d345c | `VIBE_Mesh_LoadOrFindByName` | `mesh_asset.cpp` `Mesh_LoadOrFindByName(cache,name,dir)` | Orchestrates BuildLodFileName + the EXISTING `MeshAssetCache::LoadOrFind` (no duplicate base loader). |

### BuildLodFileName name rule (byte_64A098 = LOD-mode byte; modelled as `LodModeByte()`)
- low 7 bits = mode: `0`/`1` normal/multi-LOD, `2` switch-LOD; high bit (sign) = LOD enabled.
- `lodIndex < 0` ("_s" variant): if `(i8)mode >= 0` (LOD disabled) → return 0; else
  `out = name + "_s"`, `outSecond = secondName + "_s"`. Returns 1.
- `lodIndex == 0`:
  - mode 2: probe `"%s_%i"` downward — index `1`, then `0` — via `BuildTexturePath(out,".bgf")`;
    first existing wins. If neither exists, write the plain `name` and check THAT (return 1 if
    it exists, else 0 once the counter underflows). (Goto-LABEL_5 shape preserved: sprintf only
    at the label; the while-head re-checks the plain name without a re-sprintf.)
  - mode 0/1: plain base `name` (and `secondName`). Returns 1.
- `lodIndex > 0`: mode 2 flips the index (`a4 = 2 - a4`); `v42 = a4 - 1`;
  `out = "<name>_<v42>"`, `outSecond = "<secondName>_<v42>"`. Returns 1.

### BuildTexturePath format
`"*" + name + suffix` (e.g. `"*HOUSE.bgf"`), then `VIBE_Vfs_ResolveAndBuildPath`.

### AttachStockObjectLods object byte offsets (cited from 0x5d1824 / 0x5d1114)
- `object+492` : draw-data block pointer (alloc via `VIBE_Object_AllocDrawData` if null).
- `drawData+244` : base-LOD draw block.
- `drawData+1396` : the "_s" variant draw block.
- `drawData + 244 + 384*lod` : LOD frame draw blocks (384-byte LOD-frame stride).
- loop cap: running `384*lod` offset `< 1152` and `lod < 3` (3 frames).
- Attach via `VIBE_Mesh_AttachStockTextures(object, drawData+off, lodArg, stockName)`;
  the running offset advances by 384 per stock object found.

## Reused vs hooked
- REUSED: `MeshAssetCache::LoadOrFind` / `Find` (the 0x5d32d4 load+register + 0x5d10d0
  FindStockObject equivalents) — the orchestrator only adds the LOD name sequence.
- HOOKED (inert defaults, `MeshLodHooks`): the VFS file-existence probe
  (`textureExists`), the scene-graph draw-block writes (`attachStockTextures`,
  `allocDrawData`). `LodModeByte()` models `byte_64A098`.

## VIBE_Mesh_AttachStockTextures (0x5d1114) — offsets recovered
Draw block (a2): vertArr ptr a2[0], polyArr ptr a2[1], vertCount a2[2]=stock+68,
polyCount a2[3]=stock+76, stock ptr a2[4], texSet ptr a2[5]=alloc(4\*matCount),
flag bytes a2+380 / a2+381. Draw vertex (80B stride): src ptr +72 = stockVertArr+i\*24,
colour/blend +64/+68 (init -1), flag +76 (|=0x80 when referenced), 2-sided byte +77.
Draw poly (40B/10-dword): v0/v1/v2 = drawVertBase+80\*idx, stock poly ptr +16,
resolved tex handle +20, UV +24/+28/+32, flags +36/+38 (bit3 = 2-sided). Stock object:
vertArr +64, vertCount +68, polyArr +72, polyCount +76, refcount +476, matCount +480,
texname arr +516. Stock poly (56B): vtxIdx[3] +24/+28/+32, RESOLVED texRec idx +36
(< 0 = none), MATERIAL idx +40 (texture-set grouping key). Texture record
(`dword_1406A84`, 128B stride): flags +104 (bit0 = 2-sided), transparency +108
(!= 0xFF => alpha), mode bits +110 (bit0 = alpha mode 1, bit1 = additive mode 2).

### Material transparency -> per-vertex blend (matches scene_view.cpp kBlend* decode)
Enter the colour-write block iff `texRec+108 != 0xFF || (texRec+110 & 1) || (texRec+110 & 2)`.
- `texRec+110 & 1` (ALPHA / mode 1): per-vertex +64/+68 = the grey-alpha dword
  `0xAAAAAAAA` where AA = `texRec+108` (all four bytes = the transparency level).
- else (ADDITIVE / mode 2 path): per-vertex +64/+68 = `(texRec108<<24)|0x00FFFFFF`
  (LOWORD 0xFFFF, BYTE2 0xFF, HIBYTE = transparency).
- OPAQUE (`+108==0xFF && +110==0`): block skipped, +64/+68 keep their -1 init.
Always (matIndex >= 0): poly +38 bit3 = `(texRec+104 & 1) << 3`; each vertex +77 =
`texRec+104 & 1`. Same `+108`/`+110` semantics as the light-shaft work
(`render/scene_drawlist.h kBlendAdditive`, `play/scene_view.cpp`), here read from the
128-byte texture record rather than the parsed BgfMaterial.

### 32->64 pointer-width note
The original packs 4-byte ptr + adjacent 4-byte count dwords (e.g. stock +64 ptr / +68
count). At native 8-byte ptr width those collide, so pointer-bearing fields are stored
at dedicated native-width slots (documented vs their original offset); non-pointer
count/flag fields keep their EXACT engine byte offsets. The stock/draw blocks are opaque
(hook/loader-constructed), so this is a private layout contract, not a serialised format.

## Named gaps (rule 8)
- `MeshAttachHooks` leaves are decompiled but routed through the hook because they own
  process globals / tables this slice does not: `allocPolysAndPoints` (0x5b0c10,
  `VIBE_Memory_AllocDebug`), `textureRecord` + `textureIncrementRefCount` (0x5da2e4,
  the `dword_1406A84` 128-byte texture-record table + `dword_1406A80`), `selectLodFrame`
  (0x5adb6c, scene globals `dword_13FCD1C` / `flt_13FC774` / `VIBE_Coord_ConvertX`).
  Inert defaults keep the body headless-testable; the in-body LOD-record scan fallback
  (the `selectLodFrame == 0` path) IS reconstructed and tested.
- The stock-object CACHE that `findStockObject` returns (the raw 0x5d10d0 registry block)
  is NOT yet populated by the live load path (the `.bgf` stock loader stores into the
  C++ `MeshAssetCache`, a different representation). Until a raw-block stock loader is
  wired, `findStockObject` defaults to null and `AttachStockTextures` faithfully returns
  null (the original's stock-not-found leg). Address: 0x5d10d0 / 0x5d32d4. Reason: the
  raw engine stock-object record layout (600+ bytes, +64/+72/+476/+480/+516...) is a
  separate reconstruction target from the parsed Mesh cache.
- `VIBE_Vfs_ResolveAndBuildPath @0x4500a0` IS reconstructed (io/vfs_tree.cpp) but the
  stock-object path uses globals `dword_1406110`/`unk_1406114`/`dword_62EB78` (the mesh
  search-list root) that this slice does not own; existence is therefore routed through
  the hook rather than calling ResolveAndBuildPath directly.

## Tests (rule 11) — all passing, headless, no assets
- `tests/unit/mesh_lod_name_test.cpp` (suite `MeshLodName`): 15 tests / 35 checks —
  BuildTexturePath compose + hook existence; every BuildLodFileName branch incl. the
  mode-2 downward probe (higher-index hit, lower-index hit, plain fallback, none-exist→0)
  and the mode-2 explicit-LOD index flip.
- `tests/unit/mesh_attach_lods_test.cpp` (suite `MeshAttachLods`): 4 tests / 23 checks —
  attach offset+name sequence per mode via recording hooks.
- `tests/unit/mesh_load_or_find_test.cpp` (suite `MeshLoadOrFind`): 4 tests / 20 checks —
  load-or-find name sequence per LOD mode against a mock VFS + synthetic .BGF; cache-hit
  returns the same handle with no re-open.
- `tests/unit/mesh_attach_textures_test.cpp` (suite `MeshAttachTextures`): 6 tests / 100
  checks — (a) stock-not-found -> null; (b) vertex copy fills src ptr/flags + header
  counts + refcount bump; (c) per-poly 3 vertex ptrs = base+80\*idx + +76|=0x80 + stock
  ptr; (d) OPAQUE leaves +64/+68 at init, MODE-1 sets 0xAAAAAAAA, MODE-2 sets
  0xAAFFFFFF + the 2-sided +38 bit3 / +77 byte; (e) texture-set copies each material's
  handle + bumps refcount, missing handle -> "texture not found" + "Not all Textures"
  errors; (f) LOD-frame select picks the FIRST valid LOD record; (g) `SelectLodFrameForNode`
  FORCED-LOD branch (index from node+531 bits 4-5, dirty bit on change/none, empty-frame→0);
  (h) `SelectLodFrameForNode` DISTANCE branch (clamp(trunc(dist·count·scale))). Mock hooks +
  synthetic stock object, no assets.
- `tests/unit/render_skeleton_pose_test.cpp` (suite `RenderPose`): the clean
  `render::SelectLodFrame` golden vectors (forced index extraction, distance→index, empty
  frame→-1) the adapter delegates to.

## 2026-06-10 — SelectLodFrame @0x5adb6c wired + scan-offset reconciliation
The per-object LOD-frame picker was already reconstructed faithfully in `node_lod.cpp`
(`render::SelectLodFrame`) but left DANGLING — both consumers' `selectLodFrame` hooks were
inert defaults, so it was only ever reached from unit tests. Fixed (rule 13):
- New `render::SelectLodFrameForNode(u8* node)` raw-block adapter (`mesh_attach_textures.cpp`)
  reads the live node fields (drawData@+492, lodCount@+2316, pos@+76.., render flags@+531,
  active frame@+460) into a `LodObject`, runs `SelectLodFrame`, applies `node+528 |= 0x40` on
  change, and returns the chosen LOD-frame block address (`drawData+244+384*idx`) or 0. The
  distance globals are threaded via `SetLodSelectView`; the no-camera default takes the forced
  branch exactly as the original does when `dword_13FCD1C == 0`.
- Installed as `MeshAttachHooks::selectLodFrame` in `InstallStockTextureAttach` so step 6 of
  `AttachStockTextures` picks the active frame instead of falling straight to the scan.
- RECONCILIATION BUG FIXED: the step-6 scan fallback read the frame validity dwords at the
  LITERAL engine offsets (frame +8 vertCount / +12 polyCount / +16 stock) but `AttachStockTextures`
  WRITES those at the 64-bit-relocated `hdr::kVertCount(16)/kPolyCount(20)/kStockPtr(24)`. The
  scan now reads the relocated offsets it actually wrote, so the fallback finds real frames.
  Both presets 1191/1191.
