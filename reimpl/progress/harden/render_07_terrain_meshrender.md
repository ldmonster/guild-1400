# Harden Wave — render_07: terrain mesh + render leaves

Scope: every provenance-tagged function in
`src/render/terrain_mesh.cpp`, `terrain_render.cpp`, `terrain_render2.cpp`,
`terrain_render3.cpp`. Each was decompiled (mcp decompile) and, where precision
mattered, disassembled (mcp disasm) and diffed line-for-line against the
reconstruction. Constants/tables verified with get_bytes / get_global_value.

MCP module gilde.exe, imagebase 0x400000. Tests: all 5 suites GREEN.

---

## terrain_mesh.cpp

| addr | function | status |
|------|----------|--------|
| 0x5bdec4 | ComputeTileVertices | VERIFIED-1:1 |
| 0x5ba704 | InvalidateTiles | VERIFIED-1:1 (buffer abstraction) |
| 0x5bbc74 | MarkUniformTiles | VERIFIED-1:1 |
| 0x5bbdb0 | SummarizeTileElevations | VERIFIED-1:1 (3rd-pass extract) |

- **ComputeTileVertices @0x5bdec4** — axis globals (flt_13FFD40/44/48 colU,
  flt_13FD500/04/08 rowV, dword_13FD520/24/28 height, flt_1404250/54/58 origin)
  are gathered into TileBuildParams per the header; the per-corner add order and
  the two height-sample lifts match the decomp exactly. colA used for colBaseA on
  all 3 axes, colB for colBaseB. h1 = tileRec+92, h2 = floor[100*col+800*row+317].
- **MarkUniformTiles @0x5bbc74** — `byte_5B8CE3 = {1,2,4}` confirmed via get_bytes
  (`01 02 04 ...`). blocks = size/span; flag plane = *(a1+36+4*k); |=0x40 / &=~0x40.
- **SummarizeTileElevations @0x5bbdb0** — only the self-contained per-tile min/max
  3rd pass is modelled (the slope-flag passes + VIBE_FloorWater_PrepareRegions are
  deferred per header). Inclusive `c<=tileSpan`/`r<=tileSpan`, overlay skipped when
  byte==0, min/max identical. The binary also writes tile+319=-1; that byte is
  outside the modelled TileElevationSummary (documented abstraction).

---

## terrain_render.cpp

| addr | function | status |
|------|----------|--------|
| 0x5c5610 | SelectTileMeshLod | FIXED (x87 wide divide) |
| 0x5c5610 | SelectTileMeshLodFromBounds | FIXED (wide denom/divide) |
| 0x5bf22c | TileSubdivCount | VERIFIED-1:1 |

Constants get_bytes-confirmed: flt_628BAC=50.0 (`00 00 48 42`), dbl_628BB4=0.5
(`00..00 e0 3f`, **double**), flt_628BA4=-1.75 (`00 00 e0 bf`).

- **FIXED SelectTileMeshLod @0x5c5610** — disasm `fdiv dword[esi+10h]` then
  `fadd ds:dbl_628BB4` shows 50.0/scaleX is computed on the x87 stack (80-bit) and
  the +0.5 bias is a *double*. Reconstruction rounded the quotient to float32:
  `(double)(kLodNumer/scaleX)+kLodBias`. Changed to `(double)kLodNumer/(double)scaleX
  + kLodBias` so the divide is done wide before ConvertX-truncate + the `sar 2`
  signed /4. (Edge-case-affecting only.)
- **FIXED SelectTileMeshLodFromBounds** — binary builds the denominator with
  `fild size -> double` then `+ flt_628BA4(-1.75)`; quotient stored to [esi+16] as
  float32. Reconstruction did a float add `(float)size + kGridDenomBias`. Changed
  to `(float)(((double)maxX-(double)minX)/((double)size+(double)kGridDenomBias))`.
- **TileSubdivCount @0x5bf22c** — verified against decomp lines 609-616 of the
  3565-line VIBE_Floor_RenderTerrain: `v179=v376/v372+1`; col-index==7 → `-=4/v372`,
  row-index==7 → other axis `base-4/v372`. Per-axis extract is faithful.

---

## terrain_render2.cpp

| addr | function | status |
|------|----------|--------|
| 0x5c31f0 | FindNearestEntryToPoint | FIXED (unconditional divide) |
| 0x5c34c4 | BlendSubdivideTerrain | FIXED (clampU8 negative case) |
| 0x5c4034 | ProjectPointToView | VERIFIED-1:1 core / BOUNDARY (plane loop) |
| 0x5c67b8 | RaycastFromCursor | VERIFIED-1:1 (ray-source abstraction) |
| 0x5c2ddc | PickTileAtPoint | VERIFIED-1:1 |
| 0x5bcb38 | AllocTileBuffers | VERIFIED-1:1 |
| 0x5bce10 | AllocInflateBuffers | VERIFIED-1:1 |
| 0x5bced8 | AllocLightBuffers | VERIFIED-1:1 |

- **FIXED FindNearestEntryToPoint @0x5c31f0** — binary at 0x5c331e:
  `v15 = sqrt(v19)/a4 * (*(v11+8) / flt_13FCF3C)` is an **unconditional** divide by
  the runtime scale global flt_13FCF3C (the reconstruction's `kEntryScale`).
  Reconstruction had an invented guard `scale = (kEntryScale!=0)?kEntryScale:1.0`.
  Removed it; now divides by kEntryScale directly. (`sqrt((float)v14)` cast already
  faithful to `v19 = v14` float store.)
- **FIXED BlendSubdivideTerrain @0x5c34c4** — clamp at 0x5c39ab/0x5c3a13:
  `if (cr<0.0 || 255.0>=cr) take max(cr,0); else 255`. The 255 branch fires only for
  `cr>255`; **negative cr must clamp to 0**. Reconstruction's clampU8 used
  `if (!(v<0.0) && 255>=v)` which routed negatives to 255. Corrected to
  `if (v<0.0 || 255>=v) { r=(v>=0)?v:0; return (u8)(int)r; } else return 255`.
  Pass-1/pass-2 index math, extent clamps, and the `a6+v23<=0` pre-roll all match.
- **ProjectPointToView @0x5c4034** — VERIFIED-1:1 for the box-face projection math:
  rotX/rotY/rotZ rows (a3[0,4,8] / a3[1,5,9] / a3[2,6,10]), near/far corners, the
  six face dots (-Z,+X,-X,+Z), and the v17/v18/v19 min-nesting all match the decomp.
  **BOUNDARY**: the trailing frustum-plane scan (flt_13DCE00, 0x5c448f..0x5c4539) is
  driven by a table that VIBE_Render_BuildViewMatrix (0x5accd0) repopulates every
  frame (get_global_value=0 only in the static image; xrefs confirm BuildViewMatrix
  writes it). That table's owner is outside this module's scope, so the loop is not
  faithfully sourceable here; the reconstruction reproduces the table-zero path
  (return fabs(v19)) and the comment now cites the owner + predicate constants
  (dbl_628B8C, dbl_628B84=1e10, both byte-verified).
- **RaycastFromCursor @0x5c67b8** — VERIFIED-1:1. dbl_628C04=1e-4 epsilon, DDA
  normalize by max(|dx|,|dz|), ConvertX-trunc row/col, `(short)height >= v37`,
  increment order v37/v36/v35. Ray source is the documented explicit-`CursorRay`
  abstraction of the dword_13FCD1C view block.
- **PickTileAtPoint @0x5c2ddc** — VERIFIED-1:1. The full barycentric pick incl. the
  LOD type-byte select (v13==2/4 → v9, v10Off=4*(v13>>1)+36), the hole test
  `*(signed char)(v10 + size*(v21/v9)/v9 + v20/v9) < 0`, and BOTH triangle branches
  of BOTH the hole and non-hole paths verified corner-by-corner against the decomp.
  Final `(u*v51+v*v52+v45)*F(+196)+F(+148)`.
- **Alloc{Tile,Inflate,Light}Buffers** — size formulas + scalar field side-effects
  verified: points 80*(v6+2)^2, polys 40*(v6+1)*(2v6+2), split 48*(v6+2),
  bp 24*((2v6+2)*(v6+1)+(8v6+16)); poly init 16.0f at +24, 0xFFFFFF marker at point
  +64; +94=-1, +7276=-1 (light), divide buffers size*size>>{0,2,4}, light-offset
  4*size*size, +7280|=1, ComputeSlopeFlags/BuildTilePolys tails. The overlapping
  pointer-slot Free* siblings remain deferred (header DEFERRED note, LP64 layout).

---

## terrain_render3.cpp

| addr | function | status |
|------|----------|--------|
| 0x5efdc8 | InitDefaultColors | VERIFIED-1:1 |
| 0x5efc78 | SetLayerScrollSpeed | VERIFIED-1:1 |
| 0x5efca8 | SetLayerFade | VERIFIED-1:1 |
| 0x5efb78 | CreateLayer | VERIFIED-1:1 |
| 0x5efb14 | RemoveLayer | VERIFIED-1:1 |
| 0x5efcf0 | LayerLoadTexture | VERIFIED-1:1 |
| 0x5efd28 | SkyCreate | VERIFIED-1:1 |
| 0x5efda0 | SkyDestroy | FIXED (extra global-clear removed) |
| 0x5efe0c | SkyDestroyGlobal | VERIFIED-1:1 |
| 0x4b1e10 | ApplyVertexColors | VERIFIED-1:1 |
| 0x4b236c | RefreshDomeColors | BOUNDARY (dest-layout abstraction) |
| 0x5c3a48 | GridDimWeight | VERIFIED-1:1 |
| 0x5c3a48 | EmitGridQuad | VERIFIED-1:1 |
| 0x4c05ac | ThunderShouldTrigger | VERIFIED-1:1 |

All six colour tables byte-verified (get_bytes + IEEE754 decode):
kAvcDayExt/DayInt/NightExt/NightInt (unk_631AF8/B68/C48/CB8) and
kDomeDay/kDomeNight (unk_631BD8/D28) — exact match. flt_628B80=0.7
(`33 33 33 3f`) confirmed (kGridDim). flt_62C164≈1e-6 (`bd 37 86 35`) confirmed.

- **FIXED SkyDestroy @0x5efda0** — the binary only drains layers + FreeDebug; it
  does NOT touch the global-dome slot dword_64A7C8 (no xref in the fn). Reconstruction
  had an extra `if (g_globalDome==saved) g_globalDome=nullptr;`. Removed (only
  SkyDestroyGlobal clears the global).
- **SetLayerFade @0x5efca8** — both branches (|dur|!=0 vs ==0) verified incl. the
  `LOBYTE(a1)=*(a2+38)` old-flag read and the +36/+37 writes; return = old flag.
- **CreateLayer @0x5efb78** — stride 0x2C, all field writes verified (scroll=0,
  fadeCur=1.0f, posX/Y, sizeX(+24)/sizeY(+20), fadeState(+36)/fadeTgt(+37)=a3,
  fadeRate=0, texture(+28), prepend/tail list insert at head=+2752). NOTE: the
  binary leaves +38 (fadeFlag) unwritten here; the reconstruction mirrors a3 into it
  (documented; alloc is zeroed in this module so harmless).
- **ApplyVertexColors @0x4b1e10** — table select {night,interior} matches; copy loop
  writes 6 vertices, stride 56, colour quad at +24, reading 4 floats/vertex.
- **ThunderShouldTrigger @0x4c05ac** — short-circuit `!RandomModulo(300) ||
  (type==3 && !RandomModulo(100))` with exact draw order/count (0x12C, 0x64).
- **GridDimWeight/EmitGridQuad @0x5c3a48** — dim ladder `1.0; *0.7 if(mask1&i||
  mask1&j); *0.7 if(mask2&...)`; quad P-A/P+A/P-B/P+B with dim in slots [3]/[11]
  (slots [7]/[15] left unwritten in binary, modelled as 0).
- **BOUNDARY RefreshDomeColors @0x4b236c** — value extraction is 1:1 (table select
  day/night, 7 rows of RGB skipping the alpha by advancing 4, lastRGB=final row →
  flt_64A074/78/7C). BUT the binary writes FOUR distinct destinations with specific
  strides: a packed RGB array on the scene node (off_649D64+200, stride 96 bytes)
  and three SINGLE-CHANNEL global arrays flt_13FD158/15C/160 each at float-index
  24*row (R, G, B separately). The reconstruction's dstA/dstB/dstC =
  three identical packed-RGB copies is a test-only abstraction that does NOT bit-match
  the scattered engine-global layout (those globals + the node aren't modelled in
  this isolated module). Documented as boundary; the colour VALUES are faithful.

---

## Counts

- Functions reviewed: 24 (4 + 3 + 8 + 14 incl. the two grid sub-fns @0x5c3a48 and
  the two LOD helpers sharing 0x5c5610/0x5bf22c).
- VERIFIED-1:1: 18
- FIXED: 5 — SelectTileMeshLod (+FromBounds wide divide) @0x5c5610,
  FindNearestEntryToPoint unconditional divide @0x5c31f0,
  BlendSubdivideTerrain clampU8 negative→0 @0x5c34c4,
  SkyDestroy extra global-clear removed @0x5efda0.
- BOUNDARY: 2 — ProjectPointToView frustum-plane loop @0x5c4034 (flt_13DCE00 owned
  by VIBE_Render_BuildViewMatrix), RefreshDomeColors dest layout @0x4b236c.

## Tests

cmake targets built; ctest:
- render_terrain_render_test ... Passed
- render_terrain_test .......... Passed
- terrain_mesh_test ............ Passed
- terrain_render2_test ......... Passed
- terrain_render3_test ......... Passed

5/5 suites GREEN (0 failed). No golden vector encoded a now-corrected behavior
(the fixes affect ranges/edge-cases the existing goldens did not exercise), so no
test edits were required.
