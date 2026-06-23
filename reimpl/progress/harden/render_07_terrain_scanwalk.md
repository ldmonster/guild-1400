# Hardening — render terrain scan/walk cluster (wave-07)

MCP-verified line-for-line diff of every provenance-tagged function in:
- `src/render/terrain_scan.cpp`
- `src/render/terrain_tile_query.cpp`
- `src/render/terrain_uvtable.cpp`
- `src/render/terrain_walk.cpp`

Reference of record: gilde.exe (imagebase 0x400000), Hex-Rays decompile + disasm.

## Per-function verdicts

### terrain_scan.cpp
- **0x426da0 ScanRowHeightRange — VERIFIED-1:1.**
  Decompile + disasm diffed. row in-range gate, lo/hi swap, span clip
  (`lo>=width || hi<0`), `lo<0->0`, `hi>=width-1->width-1`, byte cursor
  `heights + lo + width*row` (`[eax+10h]` = heights), per-cell
  min(`*lowOut`)/max(`*highOut`) fold, `cmp ecx,edi; jle` loop bound — all match.

### terrain_tile_query.cpp
- **0x5c3418 LookupTileAttribute — VERIFIED-1:1.**
  Bucket head int-index `200*(y/bs)+69+25*(x/bs)` confirmed against disasm
  (`lea edx,[esi+0E0h]` =224 + `[eax+34h]` =52 -> int 56+13=69). x@edx, y@ebx
  arg mapping; row(+4)<0 terminates; match `y==row && xMin<=x<=xMax` returns
  attr(+16) & 0xFF. `buckets[slot-2]` drops the two leading ints. Match.
- **0x5c6478 FindNearestWalkableTile — FIXED.**
  Diamond-ring spiral; control flow, abs(diag), span/clamp, row/col bounds,
  ring advance all matched EXCEPT the cell index.
  - Evidence @0x5c6550: `imul edx,ecx`(ecx=v10=**row**); `add edx,eax`(eax=v14=**col**);
    `imul edx,18h` -> linear index = **col + row*size** (stride 24).
  - Before: `grid->cells[24 * (row + col * s)]`  (row/col transposed — wrong layout).
  - After:  `grid->cells[24 * (col + row * s)]`.
  The bounds-check grouping (`v10>=0` outer = row, then row<size, col>=0, col<size)
  is a regrouping of the same four predicates the recon already had — equivalent,
  left as-is. On success `*foundX=col(v14)`, `*foundY=row(v10)` — already correct.

### terrain_uvtable.cpp
- **0x5b94cc BuildTerrainUvTable — VERIFIED-1:1.**
  All 24 a1[0..23] stores re-derived from the decompile with e=1/size, f=1-2e,
  v47=v51=0, v48=v50=v56=v57=f. Every slot's E/F pattern matches the recon's
  `out[]` table exactly. `e=(float)(1/(double)size)`, `f=(float)(1-e-e)` float
  rounding matches the x87 store-single flow.
- **0x5c1f88 TerrainSubTexId — VERIFIED-1:1.**
  `mov eax,edi; and eax,0FFh; add eax,base; mov al,byte_13DCE58[eax]; and al,3Fh`
  = `(quadIdx&0xFF)+base` indexing `&0x3F`, gated by cellFlag&0x40. Match.
- **0x5c1ff5 TerrainQuadUvT0 / 0x5c2015 TerrainQuadUvT1 — VERIFIED-1:1.**
  `imul edx,subTexId,60h` (=24 floats) -> tri0 = base[0..5] (poly+10),
  `add eax,18h` -> tri1 = base+6 floats (poly+38). TerrainUvBaseIndex=subTexId*24.
- **0x5bfd27 / 0x5c2348 TerrainSeamBlendUv — VERIFIED-1:1.**
  TL-BR arm @0x5bfd27 disasm: `fadd [edx+10h]`/`fadd [edx+14h]` with `fmul 0.5`
  stored to `[edx+10h]/[edx+14h]` -> rec[4]=(rec[2]+rec[4])*0.5,
  rec[5]=(rec[3]+rec[5])*0.5. BL-TR @0x5c2348 (bit clear) -> rec[0]/rec[1].
  Diagonal selector = `*(poly+26h)&1` (flags38 bit0). Match.
  Constant flt_628B48 = 0x3f000000 = 0.5 (get_bytes verified).

### terrain_walk.cpp
- **0x5bf22c RenderTerrain — VERIFIED-1:1 (arithmetic-critical sites) / BOUNDARY.**
  3288-instruction driver; reconstruction is a documented hook/DrawList restructure.
  MCP-verified the divergence-prone arithmetic against disasm:
  - Constants get_bytes: flt_628B4C=0x40000000=2.0, flt_628B48=0x3f000000=0.5,
    dbl_628B54=0x406FE00000000000=255.0. All match kTypeLightMul/kSeamBlend/kFogShadeCap.
  - Backface cull @0x5c2a1a: `area=(a.sx-c.sx)*(a.sy-b.sy)-(a.sx-b.sx)*(a.sy-c.sy)`;
    `fcompp; jbe` (term1<=term2) -> `&0xBF` (clear 0x40); else `&0x7F` (clear 0x80).
    Screen coords at vertex+0x10/+0x14. Recon `if(area<=0) &=~0x40 else &=~0x80` — match.
  - Projection @0x5c29b9: `1/z` (vertex+8), `projXScale*x*invZ+projXOff`,
    `projYScale*y*invZ+projYOff` stored to +0x10/+0x14. Match (recon guards z==0,
    a safety no-op for valid input; engine never feeds z==0 to a visible vert).
  - sortKey @line 1912: engine `((tex - dword_1406A84) >> 7) + 1`; >>7 = /128 unsigned.
    Recon `texId / texBaseStride(0x80) + 1` — identical arithmetic; the absolute
    texture-record pointer is folded into a relative id (BOUNDARY, engine-internal).
  - BOUNDARY (documented, no third-party/portable substitution issue): texture
    cache GetOrBuildTile, per-tile light-table pointer (`&lightTable[26*(flag&0x3F)]`),
    DDraw/D3D blit and mipmap build, TransformTileGeometry inlined math, water-vertex
    animation — all routed through hooks; the GPU/DDraw boundary keeps only the device
    call as a hook (rule 3), the raster/projection MATH is reconstructed 1:1 above.

## Counts
- VERIFIED-1:1: 8 functions (ScanRowHeightRange, LookupTileAttribute,
  BuildTerrainUvTable, TerrainSubTexId, TerrainQuadUvT0, TerrainQuadUvT1,
  TerrainSeamBlendUv, RenderTerrain arithmetic sites).
- FIXED: 1 function (FindNearestWalkableTile cell index col+row*size).
- BOUNDARY: RenderTerrain texture-cache / light-table / GPU-blit / transform hooks.

## Golden-test corrections (binary is the reference)
The walk-grid tests encoded the transposed cell index that matched the OLD bug.
Fixed BOTH source and goldens to the binary layout `col + row*size` (@0x5c6550):
- `tests/unit/render_terrain_scan_test.cpp`:
  - `MakeWalk` index `24*(row+col*size)` -> `24*(col+row*size)` ({col,row} pairs).
  - `WalkableSingleFar`: planted {col5,row6}; expectations fx 6->5, fy 5->6.
  - `WalkableType13IsBlocked`: traced ring search -> finds planted {col2,row3};
    expectations fx 3->2, fy 2->3.
  (CenterImmediate/None/NullGuards + all hardening tests are symmetric/range-only.)
- `tests/e2e/render_terrain_scan_e2e_test.cpp`:
  - `put` lambda `24*(row+col*S)` -> `24*(col+row*S)`. The (4,4) walkable + (3,3)
    center are diagonal-symmetric so the golden fx/fy=4 is unchanged.

## Test status (all green)
Built specific targets only (no build/ wipe, no git). Suite run:
`render_terrain_scan_test`, `render_terrain_uvtable_test`, `render_terrain_walk_test`,
`terrain_collision_test`, `terrain_ground_test`, `render_terrain_scan_e2e_test`
-> 6/6 passed.

Note: a pre-existing truncated object (`npc_clip_select.cpp.o`, unrelated to this
cluster) broke `libguild.a` archiving; recompiled via `--target guild` (no source
edit) to unblock the test link.
