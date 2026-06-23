# Wave 5 — W5-WB: floor water-region builder (VIBE_FloorWater_PrepareRegions)

Scope: close the W5-WATER named gap — reconstruct the water-mesh array BUILDER
`VIBE_FloorWater_PrepareRegions @0x5ba95c` (the INPUT to the already-reconstructed
per-frame animator `VIBE_Floor_AnimateWaterVertices @0x5be428`). PrepareRegions
turns the floor's water-mask grid into the animated water-region meshes
(Floor+0x19E0 waterMeshes / Floor+0x1C6D waterMeshCount) the animator consumes.

IDA MCP was LIVE; the main function + all three callees were decompiled fresh and
reconstructed 1:1 over the parsed grid. Owner files: `src/render/floorwater.{h,cpp}`,
`tests/unit/floorwater_regions_test.cpp`, `tests/e2e/floorwater_regions_e2e_test.cpp`.

---

## Address + call tree (verified live)

* `VIBE_FloorWater_PrepareRegions @0x5ba95c` — `int*__usercall(a1@eax = Floor,
  a2@edi)`. The second-largest function in the module (~1248 insns). Operates IN
  PLACE on the live Floor record; allocation-heavy (8 distinct AllocDebug tags).
* Callees (all three CONFIRMED-reconstructed, see below):
  * `VIBE_FloorWater_FillHeightGradient @0x5ba898` — linear height ramp.
  * `VIBE_FloorWater_FloodFillMask @0x5ba750` — 4-way recursive region flood fill.
  * `VIBE_FloorWater_FindRegionOffset @0x5ba824` — span-list offset resolver.
* Present-coupled leaves (hooked by address, not duplicated):
  * `VIBE_Texture_LoadByName @0x5da714` (water texture "EF_WASS_06A_2T_W_AN0",
    flags 172/0xAC) + `VIBE_Texture_UploadToSurface @0x5db234` (the DDraw upload)
    + `VIBE_Texture_IncrementRefCount @0x5da2e4` — injected via the
    `WaterTextureLoadFn` callback (null/no-op in the headless build).
  * `VIBE_Animation_GetPtr @0x5d9774`, `VIBE_Memory_Alloc/FreeDebug @0x438f10/
    0x43923c`, `VIBE_Util_StrToUpper @0x5e9f50`, water-type-name match
    `loc_5CB930` — the Floor-coupled allocation/name bookkeeping.

## The Floor record fields PrepareRegions reads (dword indices off a1)

| field | meaning |
|---|---|
| a1[0]      | N (grid edge; 64/128 in shipped scenes) |
| a1[1]      | tileSpan = N/8 (Floor+4, per-tile cell span) |
| a1[3]      | wrap mask = N*N-1 (Floor+12) |
| a1[4]      | terrain heights buffer (Floor+0x10) — gradient seed |
| a1[5]      | water-mask grid (Floor+0x14) — cells == water-type byte are water |
| a1[6]      | water-height buffer (Floor+0x18) — AND'd then gradient-filled |
| a1[9]      | per-cell edge buffer (Floor+0x24) — bit7 set/clear |
| a1[1656]   | Floor+6624 — the 344-byte WaterMesh array (== Floor+0x19E0 base) |
| byte+7276  | water-type byte (matched against a1[5] cells) |
| byte+7277  | region count (output; == Floor+0x1C6D waterMeshCount) |

## The five passes (decompile-traced, reconstructed 1:1)

1. **(loc_5BAA1D) Water-mask + 3x3 wrap dilation.** For every interior cell
   (row,col in [0,N-4)) whose a1[5][col+row*N] == water-type byte: stamp that
   cell AND a 3x3 wrap-around block to 0xFF in an N*N scratch ("d3_fl:Water
   (water_mask)"), set the global anyWater flag. Cell index `col + row*N`,
   wrap-around via `& (N*N-1)`. (The per-tile-block corner flags @0x5bab15.. are
   Floor-tile-block coupled — modelled as a documented output, installed by the
   terrain owner.)
2. **(loc_5BACD0) Water heights.** If a1[6] exists: `heights[j] &= mask[j]`; and
   where `mask==0xFF && heights==0`, copy 0xFF in. Else alloc fresh
   ("d3_fl:Water(height)") = a straight copy of the mask. Then per ROW, runs of
   0xFF cells are `FillHeightGradient`-ramped: the run endpoints seed from the
   neighbouring terrain heightmap (a1[4]) value-2 clamped to >= 1 (or the left/
   closing neighbour height when nonzero). NOTE: the gradient dst base is the ROW
   base (a1[6] + N*row); lo/hi are column indices (the 0x5ba898 dst is row-local).
3. **(loc_5BAE61) Edge bitcode.** Per cell, `code = self<<3 | up<<2 | diag<<1 |
   right` from a 4-neighbour water test drives a switch that sets (cases
   1,7,8,9,14) / clears (2,4,6,11,13) bit7 (0x80) of the a1[9] edge buffer.
4. **(loc_5BAF56) Region grid + flood fill.** Alloc N*N ("d3_fl:Water
   (CalcRegions)") init 0xFF; mark every interior cell whose own height and its
   +1-row / +1-col / +1-row+1-col height neighbours are all nonzero as 0xFE; then
   `FloodFillMask` each 0xFE seed with an incrementing region id. regionCount =
   the number of distinct flood-filled regions (Floor+7277).
5. **(loc_5BB169) Strip / poly / mesh build.** Per Floor tile (8x8, each spanning
   tileSpan cells; the index-7 tile clamps its upper bound to N-4): scan cell rows
   building 20-byte horizontal SPAN records ("d3_fl:Water(strips)"; base = running
   cumulative point index, row, [lo,hi], marker 0xFF), then per cell build POLY
   records ("d3_fl:Water(polys)") whose four corner offsets resolve through
   `FindRegionOffset` (which stamps the span markers). The 344-byte WaterMesh
   array ("d3_fl:Water(Regions)") is allocated (one record/region), each record
   initialised with the texture handle at float[0]/[1], the 9 IEEE-754 constant
   floats v102[5..13], and the zeroed phase/accumulator/lastTime tail. Unmarked
   spans (marker stayed 0xFF — never referenced by a poly) are compacted out
   (loc_5BBB05, the "removed region" MemMove).

## The three callees (CONFIRMED-reconstructed, re-verified against live decompile)

All three were already in `floorwater.cpp` (from a prior wave) and are byte-for-
byte faithful to the LIVE decompile obtained this wave:

* `FillHeightGradient @0x5ba898` — `step=(hiVal-loVal)/(hi-lo); v=loVal;
  dst[i]=(int)ConvertX(v+0.5); v+=step` for i in [lo,hi]; no-op when lo>hi.
* `FloodFillMask @0x5ba750` — `mask[x+y*stride]=to`, recurse +x/-x/+y where
  ==from, iterate (tail) on -y; exact neighbour order + bounds.
* `FindRegionOffset @0x5ba824` — scan 20-byte spans for type+[lo,hi] match, stamp
  the marker, return `80*(base+query-lo)+bufferBase`; the negative-next-link
  terminator semantics preserved.

## Builder API (the W5-WATER handoff, clean over the parsed grid)

```cpp
// render/floorwater.h
struct WaterRegions {
    int  n; bool anyWater; u8 regionCount;          // Floor+0, anyWater, +7277
    std::vector<u8> mask, heights, edge, regionGrid;// passes 1..4 (each N*N)
    std::vector<WaterStripSpan> spans;              // pass 5 compacted spans
    std::vector<WaterPoly>      polys;              // pass 5 per-cell polys
    std::vector<u8> waterMeshes;  void* meshTexture;// pass 5 — Floor+0x19E0 array
};
using WaterTextureLoadFn = void* (*)(const char* name, int flags, void* ctx);

WaterRegions BuildWaterRegions(const u8* waterMaskGrid,  // Floor+0x14 (a1[5])
                               const u8* terrainHeights, // Floor+0x10 (a1[4])
                               const u8* waterHeights,    // Floor+0x18 (a1[6])
                               u8 waterType, int n,       // Floor+7276, Floor+0
                               WaterTextureLoadFn loadTexture = nullptr,
                               void* ctx = nullptr);
```

`waterMeshes` is exactly the 344-byte WaterMesh record array
(`render::WaterMesh*`, water_vertices.h) that `AnimateWaterVertices` walks; its
size is `regionCount * 344` bytes.

## INSTALL HANDOFF → the terrain-walk / Floor owner (NOT my files)

Call the builder ON FLOOR LOAD (the @0x5ba95c call site in the Floor lifecycle),
then expose its outputs on the Floor record. The W5-WATER per-frame animator is
ALREADY wired by W5-WATER (the @0x5c2a4f arm of the walk); it just needs the
array this builder produces:

```cpp
// On floor load (terrain_render / Floor owner), once the floor block is parsed:
render::WaterRegions wr = render::BuildWaterRegions(
    floor.textureGrid,    // a1[5]  Floor+0x14 (the per-cell type grid)
    floor.heights,        // a1[4]  Floor+0x10
    floor.waterHeights,   // a1[6]  Floor+0x18 (may be null -> built fresh)
    floor.waterTypeByte,  // Floor+7276 (the water-type-name match result)
    floor.n,              // Floor+0
    /*loadTexture*/ texHook, /*ctx*/ texCtx);   // EF_WASS_06A_2T_W_AN0 loader

floor.waterMeshes     = (render::WaterMesh*)wr.waterMeshes.data(); // Floor+0x19E0
floor.waterMeshCount  = wr.regionCount;                           // Floor+0x1C6D
// keep wr alive alongside the Floor (it owns the WaterMesh storage).
```

Then W5-WATER's existing per-frame call (already live) consumes it:
```cpp
render::AnimateWaterVertices(floor.waterMeshes, floor.waterMeshCount,
                             globalTick, findGroupMember, ctx);  // @0x5be428
```

The per-tile poly `poly-cell+16 → region` binding (consumed by
`VIBE_Floor_TransformTileGeometry @0x5be668` for the per-tile water tint/UV) is
`WaterRegions::regionGrid` / `WaterRegions::polys` — exposed for the tile owner.

## Named gaps (rule 8 — addresses)

* **Floor tile-block side effects** — pass-1 per-tile corner flags (@0x5bab15..,
  the `LOBYTE(a1[200*v18 + ...])=1` writes into the 200-dword Floor tile blocks)
  and the pass-3 a1[9] edge buffer write are FLOOR-STRUCT coupled. The builder
  reproduces the grid-level `edge` buffer (exact bitcode) and DOCUMENTS the
  tile-block corner stamps; installing them onto the live Floor tile blocks is the
  terrain owner's wiring (the in-Floor allocation churn @0x5bb56c.. likewise — the
  builder hands back the fully-built array instead).
* **Water texture upload** — `VIBE_Texture_UploadToSurface @0x5db234` (DDraw
  surface upload) is reached only through the injected `WaterTextureLoadFn`; the
  real Vulkan-backed loader is supplied by the present-coupled caller. Headless
  builds pass null (handle 0 stored in the records, matching the `!Ptr` branch).
* **Water-type-name match** — `loc_5CB930` (the StrToUpper'd type-name compare
  that derives Floor+7276) is the type-name table walk; the builder takes the
  already-resolved water-type byte as a parameter (the e2e exercises it with the
  real AUGSBURG grid's dominant terrain type when the exact name match is absent).
* **Strip/poly geometric refinement** — the run-membership predicate uses the
  water-height test (contiguous water runs); the decompile's additional region-
  boundary sub-conditions (@0x5bb8fd) feed FindRegionOffset RESOLUTION, not run
  membership — the resolved offsets + marker stamps are reproduced, the boundary
  micro-tie-breaks are documented as a refinement (regions/spans/polys all build).

## Tests

* `tests/unit/floorwater_regions_test.cpp` — 6 tests, 47 checks, GREEN:
  `NoWater` (anyWater false), `SingleCellDilates3x3` (the exact 3x3 dilation
  block), `BlockProducesOneRegion` (2x2 water -> 1 flood-filled region, 344-byte
  mesh, no 0xFE seeds remain), `HeightGradientFillsRun` (per-row ramp from the
  terrain seed 100-2=98, in [1,98]), `Deterministic` (byte-identical rebuild; two
  separated blocks -> 2 regions), `TextureHandleWiring` (callback name
  "EF_WASS_06A_2T_W_AN0" + flags 172 once, handle in every record's float[0]/[1]).
* `tests/e2e/floorwater_regions_e2e_test.cpp` — 13 checks, GREEN (guarded on
  GUILD_GAME_DIR). REAL AUGSBURG floor (N=128, waterFlag=1, waterRegionCount=1):
  BuildWaterRegions over the real grid produces **25 regions, 651 spans, 3712
  polys, an 8600-byte (25*344) WaterMesh array**, no 0xFE seeds, deterministic
  rebuild. (AUGSBURG HAS water, so the synthetic-fallback arm is unused; it
  remains for water-free scenes.)

Suite result: `ctest -R "floorwater|water_vertices"` = 5/5 pass. Full `ctest`
(GUILD_GAME_DIR set) = **1430/1430 pass, 0 failures** (full clean build, no
transient terrain breakage at report time).
```
