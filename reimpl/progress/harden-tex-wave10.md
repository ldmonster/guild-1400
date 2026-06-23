# Wave-10 hardening — W10-TEX (texture / palettize / shape-convert / map-view)

MCP DOWN — hardening only (no new 1:1 reconstruction). ASAN+UBSAN memory-safety
pass + degenerate/edge-case test coverage over the wave-6..9 texture cluster.

## Cluster (owned)
- `src/render/texture.{h,cpp}`
- `src/render/texture_palettize.{h,cpp}`
- `src/render/texture_set_table.{h,cpp}`
- `src/render/texture_asset.{h,cpp}`
- `src/render/texture_bin.{h,cpp}`
- `src/render/shape_convert16.{h,cpp}`
- `src/play/map_view.{h,cpp}`
- tests: `render_texture_test`, `render_texture_palettize_test`,
  `render_texture_set_table_test`, `material_rgb24_standin_test`,
  `material_seasonal_resolve_test`, `shape_convert16_test`, `mapview_test`,
  `texture_bin_test`
- NOT edited: `play/real_texture_source.*` (bind-site-adjacent, owned elsewhere).

## Build / run
```
cmake -S . -B build-asan -DCMAKE_BUILD_TYPE=Debug -DGUILD_BACKEND=OFF \
  -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-sanitize-recover=all -g"
cmake --build build-asan --target <the 8 cluster tests> -j$(nproc)
```
All 8 cluster tests pass under ASAN+UBSAN (and in the normal `build/`):
render_texture 113 / palettize 230 / set_table 77 / rgb24_standin 25 /
seasonal_resolve 58 / shape_convert16 81 / mapview 58 / texture_bin 52 — 0 failures.
Cluster e2e `shape_convert16_e2e_test` also ASAN-clean (43 checks, exercises the
new converter free paths — no leak).

## Bugs FIXED (faithful — in-contract path byte-identical; goldens unchanged)

### 1. `texture_palettize.cpp` — null-pointer offset UB in the dither arm (UBSAN)
`VIBE_Quant_MapImageToPalette` (0x6033b4): on a degenerate (0-area) image the
serpentine row choreography (`a1 += 3*width-3`, `a2 += width-1`,
`a1 += 3*width+3`, …) forms out-of-bounds / null-offset pointers that are never
dereferenced (inner loop runs 0×) — UB in C++ (`applying non-zero offset
18446744073709551613 to null pointer`, caught by UBSAN on a 0-width / null-src
input). FIX: early-return `true` when `rows <= 0 || width <= 0` (no texels to
map; both arms write nothing). The engine never maps a 0-area texture
(width==height>0 gated upstream at `VIBE_Texture_LoadByName` 0x5da714), so the
in-contract width>0/rows>0 path is unchanged. Pinned by
`RenderTexturePalettizeEdge.ZeroSize`.

### 2. `shape_convert16.cpp` — fixed stack-array overrun on a malformed bank/shape
The two converters (`VIBE_Shape_ConvertRgbTo16` 0x5d7c0c,
`VIBE_Shape_Convert8To16` 0x5d7924) stage one row in `WORD rowPx[1152]` and the
row-offset table in `DWORD rowOff[864]` — the original's fixed stack arrays. A
malformed shape with `width > 1152` (raw branch), a run `n > 1152` (RLE), or
`height > 864` overran the stack in gilde.exe and equally here. FIX: faithful
bound checks that REJECT the out-of-contract shape (return `nullptr`, freeing the
already-malloc'd `dst` on the per-run path — no leak) instead of writing past the
arrays. The shipped assets are ≤ 800×600 (well inside the bounds), so every
in-contract conversion is byte-identical (verified by the unchanged golden-blob
tests). Pinned by `ShapeConvert16.MalformedOversizedShapesRejected`.

## Reviewed and found SAFE (no change needed)
- **Dither error buffers** (`bufA`/`bufB`, `3*(width+2)` words): traced every
  `cur[-3..+3]` / `nxt[-3..+8]` access across forward and reverse rows and the
  last iteration — all land inside `[0, 3*width+6)`. In bounds for width ≥ 0.
- **Palette / index buffers** (`palR/palG/palB[256]`, `outIndices[w*h]`): with the
  texture path's `maxColors == 256`, `HeapReduceColors` reduces `leafCount ≤ 256`
  before `TreeCollectPalette` writes `palCount ≤ leafCount ≤ 256`. No overrun.
  `FindClosestColor` reads `palR[i]` for `i < leafCount ≤ 256`. Safe.
- **`.TXS` parser** (`ParseTextureSetTable`): `(u64)sets*per > size` plausibility
  gate, per-name `i >= size` truncation guard, and `NameFor` range gate all hold.
  Header-only / truncated / 0-set / budget-edge buffers reject without OOB.
  Pinned by `TextureSetTable.ParseBoundsHardening`.
- **Colour-key index-0 reservation** (`PalettizeDecodedBmp`): the black-pixel
  search + label swap + index remap are all bounded by `n` / `palette.size()`;
  the no-black, all-black, and non-zero-blackIdx cases are exercised
  (`RenderTexturePalettizeEdge.ColourKeyIndexZeroReservation` / `DecodedBmpDegenerate`).
- **Map-view marker draw** (`MapView_RenderOverview`): null-pixel surface and
  zero-size viewport are guarded up front; markers off-screen are centre-clipped
  AND the draw leaves (`MenuFillRect`/`SurfaceDrawRectOutline` →
  `SurfaceSetPixelRgb`) clip per-pixel against the surface clip rect, so a
  1×1 surface with off-screen / far-negative markers writes nothing OOB. The
  sorted-record `obj` index stays in `[0,n)`. Pinned by the new `MapViewUnitEdge.*`.
- **`texture_bin` codec front-end** (`DecodeBmpBuffer`): empty / short / truncated
  / non-"BM" buffers decode to `ok=false`; 8-bit palette expansion indexes
  `pal[ci*3+2] ≤ 767`. Pinned by `TextureBinEdge.MalformedBuffersRejected`.

## Tests ADDED (all owned files)
- `render_texture_palettize_test.cpp`: `OnePixel`, `AllBlack`,
  `ColorCountBoundaryNoPaletteOverrun` (512 distinct buckets force the merge),
  `ZeroSize` (0×0 and 0×4), `DecodedBmpDegenerate`,
  `ColourKeyIndexZeroReservation`.
- `render_texture_set_table_test.cpp`: `ParseBoundsHardening` (header-only,
  size-11, single-set-to-last-byte, over-budget header, all-empty rows with a
  full NameFor sweep, unterminated last name).
- `material_seasonal_resolve_test.cpp`: `CountMismatchAndOutOfRangeSeasonNoOOB`
  (namesPerSet > materialCount; absurd / negative active season).
- `shape_convert16_test.cpp`: `MalformedOversizedShapesRejected` (width>1152 raw,
  height>864, run n>1152 RLE, plus an in-contract control that still converts).
- `mapview_test.cpp`: `RenderZeroMarkers`, `RenderMarkersOffScreen`,
  `RenderNullSurfaceNoOp`, `RenderOnePixelSurface`, `ClickToWorldViewportEdges`.
- `texture_bin_test.cpp`: `MalformedBuffersRejected`, `DecodeThenPalettizeWiring`.

## BEHAVIORAL — needs MCP to confirm against the decompile
1. **`HeapReduceColors` with `maxColors == 0`** (`texture_palettize.cpp`
   0x603068): if the heap ever reduced a level-0 (root) node, `pl = lvl-1`
   underflows to `0xFF` and `s.levels[0xFF]` is OOB. This is unreachable on the
   live path (`maxColors == 256` ⇒ reduction stops at `leafCount == 256 ≫ 1`,
   never collapsing to the root). NOT changed — adding a `lvl == 0` guard would
   alter control flow on a domain the engine never enters; confirm against the
   0x603068 decompile whether the original has any such guard.
2. **`TexelMask` / `WidthShift` integer overflow on a giant width**
   (`texture.h` 0x5db724): `(width-1) | (width*width-1)` and `1 << (s+1)` overflow
   `int` for a width ≳ 46341 / ≳ 2^30. Only reachable via a malformed BMP whose
   header claims such a width AND that `BmpReadHeaderInfo` (shared codec, not this
   cluster) accepts; the shipped textures are ≤ 512. NOT changed — an upper-bound
   clamp would be an invented constant; confirm the original's width validation
   (the square-check at `VIBE_Bmp_ReadHeaderInfo` 0x5F0C10) before adding a bound.

## Out-of-cluster note (NOT fixed — ownership)
`materials_w4c_e2e_test` reports a large leak under ASAN, all stacks rooted in
`compress::Inflater::NewBlocks()` (`src/compress/inflate.cpp:1094`) — the archive
decompressor, a different cluster. Pre-existing, unrelated to these changes;
flagging for the compress/IO owner.
