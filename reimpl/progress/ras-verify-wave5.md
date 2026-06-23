# Raster fidelity verification — wave 5 (W5-RAS)

**Scope:** clear the wave-4 UNVERIFIED queue items the MCP could now decompile —
the dispatch-table installers, the flat-fill family winding, the `*(tex+72)`
per-texture light table / lightRow8 range, and re-confirm claim 4b.
**Owned files:** `src/render/raster.{h,cpp}`, `src/render/meshlist.{h,cpp}` and
their unit tests. **IDA MCP: LIVE** — every verdict below cites a fresh
decompile (gilde.exe, imagebase 0x400000).

---

## Verdict table

| # | Item (wave-4 queue) | Decisive decompile evidence | Verdict | Action |
|---|---|---|---|---|
| 1 | Dispatch-table installers (13D8780..) — which span/triangle leaf each slot points to | `xrefs_to 13D8780..13D8794`: the **only** writer of any of the six slots is `InitEngineDevice` @0x5AF984 (0x5AFB87..0x5AFBA5), all set to `NullStub13` (0x5F6EE8 `retn`). **No second installer exists.** Flush 0x5AEC88 indexes `dword_13D8780[(*(int*)&v45[1])>>24]`, v45[4]=4 (opaque) / 3 (translucent: `*(tex+108)-(*(tex+110)&1)<0xFF`) → slot 3 or 4 → **NullStub13**. | **DIVERGES from wave-4 claim** ("texture-bind patches live slots" — FALSE) | meshlist.{h,cpp} doc rewritten: 6 slots, all NullStub in the binary; real textured rendering is the D3D path `DrawTexturedTriangles` @0x5AE434 (vertex buffers → `DrawTriangleList`, off-table); 0x5AEC88 is the software/DDraw-Lock flush. slot[] sized 6 (was 7). slot[3/4]→textured leaf kept as documented HOST choice so the headless flush draws. |
| 1b | Is 0x13D8798 a 7th dispatch slot? | xrefs: `0x13D8798[0]/879C/87A0` is the 3-entry **clip-input vertex-pointer array** seeded before `ClipPolygonToPlane` (0x5AD813/0x5AD87A in the clipper; 0x5AE989 in DrawTexturedTriangles; 0x5AED8F in the flush). Adjacent in memory, not a function pointer. | **DIVERGES** | "7-entry / 7th slot used by the flush" comment removed. |
| 2 | Flat-fill family 0x603DA8 / 0x603D00 winding ("forward-only assumed") | `0x603DA8 FillSpans` = pure per-row span loop (8bpp `memset(ceil(xL)+base, dword_13FC5E0, len)`; 16bpp word-store) — **NO vertex/winding logic**. `0x603D00 ComputeEdgeSlope` = LEFT-edge slope+start (two-path divide, same as InterpolateEdgeZ but writes xLeft/xLeftStep). The winding lives in the ONLY caller, `VIBE_Shadow_RasterizeTriangle` @0x603ED4 @0x603F0F: `if ((x0-x2)*(y0-y1) > (x0-x1)*(y0-y2))` → back-wound: `if (poly+38 & 4)==0 return;` (CULL, 0x603FB0) else reverse-load (`v18=v+2; --v18`); front-wound → forward load. | **DIVERGES** (it was forward-only) | `RasterizeFlatTriangle` now does the literal cross-product winding test, the +38 bit2 gate, reverse-load, and the back-wound cull. New `polyFlags38` param (default 0). Goldens. |
| 3 | `*(tex+72)` per-texture light table — row count / lightRow8 range | `BindActive` @0x5DB5C1: `dword_1406A78 = *(tex+72)` — **+72 is a POINTER** to the shared HiColTab block, not a private array. Block built by `FindOrBuild` @0x5DA04C (`AllocDebug(0x8200)`) / `AddEntry` @0x5D9DB8: ramp **rows L=0..62** (`v7+=512`, `v9<0x3F`) at byte `512*L` = u16 `256*L`; 256 direct-565 entries at byte 32256. Span fetch (raster.cpp) `palBase[lightRow8\|texel]`, `lightRow8 = dword_13FC5E0 = avg(+66)<<8` (0x5F70BD). So `palBase[(avg<<8)\|texel]` = element `256*avg+texel` = **ramp row `avg`, entry `texel`**. | **RESOLVED** | +72 = HiColTab block ptr; **valid lightRow (avg) range = 0..62** (63 rows). raster.h SpanTexParams.lightRow8 + meshlist.cpp docs updated with the layout. avg-shade indexing was already in-range for 0..62; +66 bytes evidenced to 254 are scaled into 0..62 by the lighting stage (table has no rows >62). Golden. |
| 4b | 0x5C5120 `768*max(+66)` is the SORT key; meshlist white-default shades by AVG (0x5F70BD) | 0x5F70BD: `dword_13FC5E0 = ((+66[v0]+ +66[v1]+ +66[v2])/3) << 8` (AVG, shift 8 → row index). Confirms the avg selector. The 768*max is the draw-list key (per wave-4 0x5C545F). | **CONFIRMED** | meshlist.cpp comment upgraded from "wave-4 evidence" to "VERIFIED". |
| -- | `flt_62C3D4` / `dbl_6295E8` (UV scale 65536, HiColTab 0.5) | `get_bytes 0x62C3D4 = 00 00 80 47` = **65536.0f**; `0x6295E8 = ..E0 3F` = **0.5**. Also `flt_62C6C0 = 00 00 80 47` = 65536.0f (flat path projection scale). | **CONFIRMED** | resolves wave-4 "get_bytes 0x62c3d4" queue item. |

## Code changes (with addresses)

* `src/render/raster.cpp` / `raster.h`
  - `RasterizeFlatTriangle` (0x603ED4 + leaves 0x603DA8/0x603D00): screen-space
    cross-product winding test (0x603F0F), `+38 & 4` gate, reverse-load, and the
    back-wound **cull** when the flag is clear. New `polyFlags38` param
    (default 0). The flat-fill leaves documented as winding-free (the winding is
    the caller's). `color` kept as the host fill index (the binary's fixed
    stencil is `dword_13FC5E0` = 1 / 0xFFFF; generalised for the solid-fill
    callers).
  - `SpanTexParams.lightRow8` doc: +72 = shared HiColTab block ptr
    (BindActive 0x5DB5C1 / FindOrBuild 0x5DA04C / AddEntry 0x5D9DB8); 63 ramp
    rows (0..62), row L at u16 offset 256*L; `lightRow8 = avg<<8` selects ramp
    row `avg`, valid avg range 0..62.
* `src/render/meshlist.cpp` / `meshlist.h`
  - Dispatch table: 6 slots (was "7"), all NullStub13 in the binary (no second
    installer — verified); 0x13D8798 reclassified as the clip-vertex array, not
    a slot. slot[3]/slot[4]→textured leaf documented as a host renderer choice.
    `SpanDispatch::slot[6]` (was `[7]`), loop bound 6.
  - RasterTri white-default light comment: HiColTab 63-row layout + avg selector
    upgraded to VERIFIED.

## Tests (tests/unit/render_raster_test.cpp)

New: `FlatBackWoundCulledWhenFlagClear`, `FlatBackWoundReversedEqualsForward`,
`FlatFrontWoundIgnoresFlag`, `LightRow8SelectsHiColTabRow` (4).
Results (rebuilt clean):
* render_raster_test **1790 / 0**
* render_raster_textured_test 1082 / 0; raster_clip_test 9 / 0;
  render_binder_test 24 / 0; texraster_recon2_test 889 / 0;
  scene_recon5_raster_test 40 / 0
* e2e: render_scene_e2e 15 / 0; render_mesh_load_e2e 45 / 0;
  render_raster_e2e 402 / 0 (no regression from the new back-wound cull —
  those flows use front-wound triangles)

## Dispatch-table slot map (final)

| slot | global | binary install | leaf addr |
|------|--------|----------------|-----------|
| 0 | dword_13D8780 | NullStub13 | 0x5F6EE8 |
| 1 | dword_13D8784 | NullStub13 | 0x5F6EE8 |
| 2 | dword_13D8788 | NullStub13 | 0x5F6EE8 |
| 3 | dword_13D878C | NullStub13 (translucent index) | 0x5F6EE8 |
| 4 | dword_13D8790 | NullStub13 (opaque index) | 0x5F6EE8 |
| 5 | dword_13D8794 | NullStub13 | 0x5F6EE8 |
| (n/a) | dword_13D8798/879C/87A0 | clip-input vertex ptr array | — |

Note: the high-byte index in the flush is `(*(int*)&v45[1])>>24` which reads
v45[4] (=4 or 3), so only slots 3/4 are ever indexed; both are NullStub13 in
the shipped binary. Real textured triangles render through 0x5AE434 (D3D),
which does not consult this table.
