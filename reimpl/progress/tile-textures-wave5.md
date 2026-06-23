# Wave 5 — W5-TILE: terrain tile textures + flood-fill

Scope: close the wave-4 deferral "tile textures: VIBE_Floor_LoadTexture @0x5bd010
and VIBE_TextureCache_GetOrBuildTile @0x5ba1e8 bodies not in evidence (lost dump) —
ground renders level-shaded white-default". The IDA MCP is LIVE this wave; all three
named targets were decompiled and reconstructed/verified against the decompile.

---

## Targets — reconstruction status

### 0x5bd010 — VIBE_Floor_LoadTexture  — PURE CORES RECONSTRUCTED + wired
`int __usercall (Floor@eax, name@edx, requested@ebx)`. Full decompile captured.
Structure: resolve a slot index, copy the default (empty) slot name, clear the 3
per-layer texture pointers, then for each of 3 mip layers build `name + suffix`,
resolve `"*"+...+".BMP"` (VIBE_Texture_BuildBmpPath @0x5d97e8 → VIBE_Vfs_Resolve
AndBuildPath @0x4500a0), AllocDebug(0x4000) and VIBE_Bmp_LoadBuffer @0x5f0ce4.

This is an I/O + memory-manager boundary (BMP decode, alloc) — per rule 8 the
device body is NOT reconstructed; what IS reconstructed 1:1 (pure, golden-tested):
* **`FloorTextureResolveSlot`** — the slot resolution (`@0x5bd025..0x5bd03c`):
  `requested<0` → scan the 8 64-byte name slots at Floor+6756 for the first EMPTY
  slot (count consecutive non-empty, stop at name[0]==0); else `requested`;
  `>= 8` → -1.
* **`FloorTextureMipName` / `kFloorMipSuffix[3]`** — the per-mip name build
  (`@0x5bd127..0x5bd1d0`, suffix table **dword_5B8C90**, 11-byte stride; dumped
  bytes: layer 0 = "" (11 zero bytes @0x5B8C90), 1 = "_high_1" @0x5B8C9B,
  2 = "_high_2" @0x5B8CA6). The default slot name copied from **unk_628954** is the
  empty string (4 zero bytes; `\x00\x00\x00\x00` then "d3_fl:fltx_SourceData").

The remaining BMP-decode/alloc inside @0x5bd010 IS the by-name loader
**VIBE_Texture_LoadByName @0x5da714**, which is already reconstructed in
`render/texture_loader.cpp` + `render/texture_asset.cpp` (READ, not edited). The
floor's slot-name → loaded-texture path is wired end-to-end through it by the
**`FloorTextureResolver`** (below) — the deferral is CLOSED.

### 0x5ba1e8 — VIBE_TextureCache_GetOrBuildTile  — PURE CORES RECONSTRUCTED
`int* __userpurge (src@eax, N@edx, x0@ecx, y0@ebx, span, userPtr)`. Full decompile
captured; callees decompiled (`VIBE_TextureCache_LookupTile @0x5ba0b0`,
`VIBE_TextureCache_FindLruSlot @0x5ba03c`, `VIBE_Texture_CreateTileRecord @0x5db928`).
It is an LRU TEXTURE-TILE CACHE: CRC32-key lookup (`LookupTile` over the sampled
sub-block) → hit returns the cached record; miss → `FindLruSlot` (17-int slot stride
in **dword_64A040**, count **dword_64A034**) → copy the mask-wrapped source
sub-block into the slot scratch → either copy the format-string tile name
(**dword_5B8CC0** = "*", "*fltx1", "*fltx2", "*", "*fltx4"; 7-byte stride) or build a
new record via `CreateTileRecord`.

The LRU array + CreateTileRecord's record alloc are the texture-cache/alloc boundary
(rule 8). The GPU/IO-INDEPENDENT cores ARE reconstructed 1:1 (golden-tested):
* **`TileCacheSampleSubBlock`** — the mask-wrapped (toroidal) `(span+1)²` sub-block
  sample (`@0x5ba248..0x5ba2a9`): `dst[r*stride+c] = src[N*(mask&(y0+r)) +
  (mask&(x0+c))]`, mask = N-1. This is the exact block the CRC32 LRU key is taken
  over (`LookupTile @0x5ba0b0` samples it identically before `VIBE_Util_Crc32`).
* **`TileRecordTexelMask`** — the `+88` mask `(w-1)|(w*w-1)` (`@0x5db9d7`; identical
  to `texture.h TexelMask`, cross-checked in a golden).
* **`TileCacheWidthClamp`** — the width pick `min(span*(64>>byte_64A044),
  dword_64A038<<byte_64A045)` (`@0x5ba2f2`; all three globals = 0 in the shipped
  binary → degenerate, exposed for completeness/tests).

### 0x5c5530 — VIBE_Heightmap_FloodFillTileType  — ALREADY RECONSTRUCTED (verified)
`int __usercall (heightmap@eax, fromType@dl, toType@bl)`. Full decompile captured.
A SINGLE-PASS 4-neighbour morphological terrain-type fill over the heightmap's
24-byte cell grid (`size = *(hm+32)`, `cells = *(hm+36)`, type byte at +0): for each
interior cell == `from`, retype to `to` IFF up/down/left/right are all `from`|`to`.
The `*(int*)(v5+21) >> 24` right-neighbour read is the sign-extended byte at v5+24
(the right cell's +0). It mutates the buffer LIVE (a flipped cell is a `to`-neighbour
for later cells in the same sweep). Only caller: VIBE_Heightmap_BuildTerrainMesh
@0x5c5610 (xref 0x5c6020).

**This is ALREADY reconstructed 1:1 in `render/heightmap.cpp`** (`FloodFillTileType(
Heightmap*, u8, u8)`). Per the grep-before-defining / reuse rule it was NOT
re-implemented; this wave RE-VERIFIED the in-tree body against the fresh decompile
(byte-exact match incl. the live single-buffer behaviour and the size-1 return) and
added golden coverage. A reuse note replaced a transient duplicate in
`render/tile_geometry.h`.

---

## The tile-texture resolver (the wave-4 deferral, CLOSED)

`src/render/floorgfx_recon.{h,cpp}` — **`FloorTextureResolver`**: the clean,
present-coupled path that turns the floor's per-cell texture id into the loaded
texture, end-to-end through the REAL by-name load path.

API:
```cpp
class FloorTextureResolver {
  void Bind(const char slotNames[8][64], TextureAssetCache* cache);
  bool bound() const;
  const render::Texture* LoadSlot(int slot);   // 0x5bd010 via the real loader
  const render::Texture* Resolve(u8 typeByte); // typeByte -> slot -> texture
  static const void* GetTileTextureHook(u8 typeByte);  // hook trampoline
};
void SetActiveFloorTextureResolver(FloorTextureResolver* r);
FloorTextureResolver* ActiveFloorTextureResolver();
```
* `Bind` takes the 8 floor slot names (Floor+0x1A64, parsed live per wave-4 into
  `FloorGround::typeNames` / `SceneFloorBlock::typeNames`) + the VFS-backed
  `TextureAssetCache` (the @0x5da714 slot manager).
* `LoadSlot(slot)` builds `"*" + name + ".BMP"` (the @0x5bd010/@0x5d97e8 convention)
  and calls `TextureAssetCache::LoadByName` (the real VFS slurp + BMP decode in
  texture_asset.cpp), caching the resolved slot index so per-tile calls don't
  re-scan. Empty slot / absent BMP / no cache → nullptr (the engine's untextured
  fallback).
* `Resolve(typeByte)` applies ComputeTileIllumination's hole gate (high bit 0x80 →
  nullptr) and indexes the 8 slots by the low 3 bits — the body the terrain walk's
  per-tile fetch (`VIBE_TextureCache_GetOrBuildTile`) routes through.
* `GetTileTextureHook` is a free function with the EXACT
  `TerrainRenderHooks::getTileTexture` signature `((u8)->const void*)`, bound to the
  process-active resolver — the install handoff (below).

---

## Wiring (rule 13) + HANDOFF → terrain-render owner (play/terrain_render.* not mine)

`play/terrain_render.h` already declares the hook field
`const void* (*getTileTexture)(u8 typeByte)` (inert default nullptr). The resolver's
`GetTileTextureHook` matches it exactly. The terrain-render owner installs it with:

```cpp
// In the terrain/ground bind path (e.g. GroundFrame::Bind or the CityView3D
// terrain setup), after the FloorGround + the texture cache are available:
static render::FloorTextureResolver s_floorTexResolver;   // outlives the frame
s_floorTexResolver.Bind(g.typeNames /* Floor+0x1A64, char[8][64] via .name */,
                        view.textureCache() /* the mounted TextureAssetCache */);
render::SetActiveFloorTextureResolver(&s_floorTexResolver);

play::TerrainRenderHooks hooks{};
hooks.getTileTexture = &render::FloorTextureResolver::GetTileTextureHook;
play::SetTerrainRenderHooks(&hooks);
```
Notes for the owner:
* `g.typeNames` is `render::TileLightSource[8]` (each `.name` is a 64-byte slot); the
  resolver wants `const char slotNames[8][64]` — pass `g.typeNames[0].name` reinterpreted,
  or add a tiny `char names[8][64]` copy (the e2e does the copy explicitly).
* The CityView3D path currently mounts `RealTextureSource` (Textures.BIN), NOT a
  `TextureAssetCache`. To make the floor textures resolve through the resolver, the
  owner needs a `TextureAssetCache` over the same texture VFS (or a thin adapter
  from `RealTextureSource` to a `const render::Texture*` per slot name). Until that
  cache is bound, `getTileTexture` stays inert (nullptr → the established untextured
  16bpp level-shaded fallback) — i.e. flipping the install is SAFE and additive: an
  unbound resolver returns nullptr exactly like the current inert default, so pinned
  frames stay byte-identical until a real texture cache is supplied.
* The water-region per-texture flag derivation (`(texByteC&1)|(4*(texByteB&0xF))`,
  scene_floor.h) is a SEPARATE deferred path; the floor terrain slots load with the
  default 0 flags here (matches @0x5bd010's call shape into the loader).

---

## Files

* `src/render/floorgfx_recon.h` / `.cpp` — `FloorTextureResolveSlot`,
  `FloorTextureMipName` + `kFloorMipSuffix`, `TileCacheSampleSubBlock`,
  `TileRecordTexelMask`, `TileCacheWidthClamp`, `FloorTextureResolver` +
  `SetActiveFloorTextureResolver` / `ActiveFloorTextureResolver`. The @0x5bd010
  deferral note updated to reflect the wave-5 cores + the wired resolver.
* `src/render/tile_geometry.h` — reuse note for FloodFillTileType (no code added;
  the canonical body lives in render/heightmap.cpp).
* `tests/unit/render_tile_textures_test.cpp` (NEW).
* `tests/e2e/tile_textures_e2e_test.cpp` (NEW, guarded on GUILD_GAME_DIR).

Not edited (other agents / read-only): play/terrain_render.*, scene_floor.*,
render/texture*.{h,cpp}, render/heightmap.*, any raster file.

---

## Tests

* **`render_tile_textures_test`** (NEW) — **79 checks, 0 failures**. Golden vectors:
  slot resolution (explicit + first-empty scan + the >=8 / walk-off-end -1),
  mip-suffix name build (the dword_5B8C90 table), mask-wrapped sub-block sample
  (no-wrap + toroidal wrap), texelMask + width-clamp (incl. cross-check vs
  texture.h TexelMask), FloodFillTileType (enclosed fill / no-eligible / live
  single-pass propagation over a Heightmap), and the FloorTextureResolver mapping
  (typeByte→slot→record, hole/empty/no-cache → nullptr, slot-cache identity, the
  hook trampoline).
* **`tile_textures_e2e_test`** (NEW, guarded) — **1130 checks, 0 failures** over the
  REAL AUGSBURG floor: the floor block parses (slot[0] = "WIESE", 8 named slots), a
  real 8x8 indexed BMP is decoded through the REAL VFS slurp + BMP-decode path
  (`TextureAssetCache::LoadByName` over a temp `DiskFileSystem`), the resolver maps
  the real per-cell texture grid bytes to the decoded record (1110 slot-0 cells
  resolved), the hole bit / empty slots fall back to null, and the hook trampoline
  returns the same record.
* Neighbour suites re-run green: `floorgfx_recon_test` 54, `render_tile_lighting_test`
  97, `render_terrain_walk_test` (built+green). `guild` core library builds clean.

## Named gaps (rule 8)

* **@0x5bd010 BMP decode / 0x4000 alloc / the 3-mip per-layer load** — that body IS
  the by-name loader VIBE_Texture_LoadByName @0x5da714 (reconstructed in
  texture_asset/texture_loader); the resolver loads the BASE mip through it. The
  `_high_1`/`_high_2` mip records are an LOD detail the single bound record does not
  need (the mip-name builder is exposed + golden-tested).
* **@0x5ba1e8 LRU array (dword_64A040, 17-int stride) + CreateTileRecord alloc** —
  the texture-cache/memory boundary; the resolver supersedes the per-tile cache for
  the software path (one record per slot, reused by name via FindActive). The CRC32
  key body (LookupTile) is not needed once the slot record is bound.
* **Texture-cache adapter for CityView3D** — the install needs a `TextureAssetCache`
  bound to the texture VFS (handoff above); until then the hook is inert (safe).

## Unrelated note (NOT my change)

`terrain_ground_test.GroundFrameRenderHeadless` (play/terrain_render.cpp) reports 1
failure in the working tree — `play/terrain_render.cpp` carries +305 lines of
in-progress edits from the terrain-render owner (W5-TERR). It does not reference any
W5-TILE symbol; it is the owner's WIP, flagged here, not introduced by this wave.
