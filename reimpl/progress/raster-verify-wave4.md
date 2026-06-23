# Raster fidelity verification — wave 4 (T-2)

**Scope:** verify the wave-3 raster-layer claims against the CAPTURED binary
evidence (`/tmp/guild_va_evidence/va1_raster.md` — 40 IDA tool results pulled
before the MCP went offline) and fix real divergences 1:1.
**Owned files:** `src/render/raster.{h,cpp}`, `raster_textured.{h,cpp}`,
`meshlist.{h,cpp}`, `texture.{h,cpp}`, `hicoltab.{h,cpp}`,
`texture_palettize.*` (verify only), their tests.
**IDA MCP:** OFFLINE for this pass — every verdict below cites only the
captured evidence; nothing was decompiled fresh.

---

## Verdict table

| # | Claim (wave 3) | Decisive captured evidence | Verdict | Action |
|---|---|---|---|---|
| 1a | 0x5F7960 span bytes == plain 16.16 accumulation ("bit-identical output bytes") | decompile 0x5F7960, 0x5f7a53..0x5f7a60: `v8 = __ROR4__(acc,16); v9 = __ROR4__(grad,16); v10=0; do { *v6 = v8; v11 = CF+v8; CF = ...; v8 = v9+v11; }` — the fraction carry exits at bit 31 and re-enters the integer half on the **next** `adc` | **DIVERGES** | `FillTexturedSpansShaded` now runs the rotated adc chain verbatim (carry lands one pixel late; int-half overflow spills into the fraction — all reproduced). Goldens regenerated; micro-test `ShadedSpanAdcCarryLandsOnePixelLate` pins the 0,0,1,1 trace |
| 1b | 0x5F7D58 "both vertex-scan loops produce the same arrays" (shared forward load) | decompile 0x5F7D58: `if (area_expr <= 0.0)` → forward loop (0x5f7ef2, `v22 += 2; ++v23`); else **reverse** loop (0x5f7db3, `v10 = a1+4; v8 = a2+2;` walking **down**) | **DIVERGES** | back-wound (area > 0) triangles now load slot i ← vertex 2-i (winding normalisation); the literal area expression `x2*y1 - y2*x1 + x1*y0 - y1*x0 + x0*y2 - y0*x2` is used. Test `BackWoundTriangleEqualsForward` |
| 1c | 0x5F7960 24-byte-stride buffer "DEFERRED (shade markers only)" | decompile 0x5F7960 (complete): a4&5 stamps `11` (or `0` when a4&1) into the 24-byte-stride array (BuildTerrainMesh's `[esi+24h]` tile cells), with blur-radius halos (per-row widened, a3-gated top pyramid, trailing bottom pyramid); disasm 0x5c5fa6..0x5c5fda shows the a5/a6/a7 call args | **DIVERGES (gap now closed)** | full tile-stamp path implemented 1:1 (`modeMask`/`tileBase`/`blurRadius`/`gridPitch` params, defaults preserve old callers); the a5 masking `if(!a6) a5&=2; if(!a4) a5&=5` is now literal. Tests `TileStampCoreNoBlur` / `TileStampClearValueWithBit0` / `TileStampBlurHaloPyramid` / `ModeMaskGatesEverything` (hand-derived from the decompile) |
| 1d | "the 8bpp guard: the SMC dispatch never aims 0x5F7960 at 16bpp" | xrefs_to 0x5f7d58 → **single** caller 0x5c5fda in `VIBE_Heightmap_BuildTerrainMesh` (targets the terrain byte map `[esi+28h]` and tile array `[esi+24h]`, never a framebuffer); InitEngineDevice fills the 13D8780 dispatch table with NullStub13 only | **CONFIRMED (as recon posture)** | the guard is now expressed as the original's own `if (!a4) a5 &= 5` masking (non-8bpp fb == no byte map). Observable behaviour of the old test preserved. Note: which routine installs the real 13D8780 entries is **unverified** (not captured) |
| 2a | Masked span 0x5F721A structure: index-0 skip, same patched constants | disasm 0x5F721A: identical body to 0x5F71AD + `test dl,dl / jz loc_5F7280`; disasm 0x5f753f patches the same six sources (1406A8C, 1406A78, 13FC594, 13FC5A8, 1406A88, 1407A91) into the 5F72xx immediates | **CONFIRMED** | none (already 1:1) |
| 2b | Span loop / constant-patch semantics (`SpanTexParams` field map) | disasm 0x5F71AD/0x5F721A: `edx` upper bytes = dword_13FC5E0 (light row), per-pixel `mov dl, texel` → colour = `pal[(light<<8)\|texel]`; 13FC594/13FC5A8 are the U/V **fractional** step immediates; integer steps travel in dword_13DCE54[0]/13DCE50 via `adc`/`sbb` (V-frac add pre-run in the prologue → carries exactly timed) | **DIVERGES** (light row missing; two fields mis-documented) | `SpanTexParams.lightRow8` added (colour fetch is now `palBase[lightRow8 \| idx]`); `RgbzVertex.light` added; 0x5f6c30's `dword_13FC5E0 = ((l0+l1+l2)/3) << 8` computed in the triangle setup. Separate-accumulator equivalence **proven** and documented (raster.cpp note). Tests `LightRowSelectsPaletteRow`. Defaults (light=0) keep 256-entry-palette callers exact |
| 3 | Keyed routing trigger "texture flags & 8 == the 0x5db234 v9 arm" selects the masked span | decompile 0x5db234: `flags&8` only chooses `v9 = 0` vs `dword_64A1FC` as **arg 5 of VIBE_Render_LoadAndStretchTexture @0x5dea50** (not captured). xrefs_to 0x5f721a and 0x5f753f → **empty**; find_bytes of their addresses → **no matches** (the masked span has no recovered runtime selector at all) | **UNVERIFIED-BY-CAPTURED-EVIDENCE** | flag-semantics docs corrected in `texture.h` (+104 bits per the 0x5db234 evidence, v9 meaning marked unverified); `raster_textured.h` masked-variant banner rewritten with the evidence status; **handoff snippet** below for the bind sites (default to the PLAIN span) |
| 4a | BindActive @0x5db564 slot==0 binds a "1x1 white" | decompile 0x5db564: the `+76 == 0` arm stores 1406A88=0, **1406A8C=0 (texel base!)**, 1406A78=0, 1406A7C=1, **1407A91=1 (shift=1, not 0)** — a NULL binding; ResetBinding @0x5db5f0 stores the same | **DIVERGES — host stand-in** | no white texture exists in the binary; the faithful state (reads of address 0) is unimplementable. White stand-in **kept** but re-documented as host-defined (texture.{h,cpp}); **rule-8 sign-off item** (below) |
| 4b | "level = max vertex +66; 768*level draw keys" | decompile 0x5c5120, 0x5c545f..0x5c547a: `v22 = max(+66 bytes); *dword_13FC570 = 768*v22` — the **draw-list sort key**. The **span shade** selector is 0x5f6c30 @0x5f70bd: `dword_13FC5E0 = ((l0+l1+l2)/3) << 8` — the **avg** | **DIVERGES** (max used as shade selector in meshlist RasterTri) | meshlist white-default shade now keyed by the AVG; the 768*max key documented as sort-only. Row *contents* of the stand-in table stay the linear gray (the +72 light-table layout is unverified — the 0x5d9db8 63-row ramp is the sprite-bank HiColTab, and +66 bytes are evidenced up to 254, which contradicts a 64-row table). Test `WhiteDefaultShadeUsesAvgLightNotMax` |
| 4c | HiColTabAddEntry @0x5d9db8 linear ramp | decompile 0x5d9db8: search over `256 - a3[193]` entries (rgb at +4, stride 3); direct 565 at `*a3 + 2i + 32256`; ramp rows `512*L + 2i`, L = 0..62, channel = `ch/62.0*L + 0.5` chopped (dbl_6295E8 == 0.5 confirmed by get_bytes); `--a3[193]` | **CONFIRMED** (hicoltab.cpp matches line-for-line) | none |
| 4d | InitTables/64A1FC | decompile 0x5d988c + get_int: static dword_64A1FC == 8, runtime = InitEngineDevice a3 = config v26[9] == 4 (0x527fa4) | (context for #3) | recorded |
| 5a | RGBZ leaf 0x5F6C30 pipeline vs raster_textured.cpp | decompile 0x5f6c30 + disasm 0x5f6b34/0x5f7500: edge selection, two-path slopes, FillSpanLoop row walk, gradient cross products, `ResetBinding` after — match | **CONFIRMED** (structure) — but see 5b/5c/5d | — |
| 5b | UV scale: `vu = u * texScale(mipWidth)` | 0x5f6c30 @0x5f6c64: `v53 = (double)(u32)mipWidth * flt_62C3D4(=65536.0)`; per-vertex `vu = (u + off) * v53` (16.16 TEXELS); gradient `v18 = mipWidth * (65536/area)` | **DIVERGES** (the 65536 factor was dropped from the vertices and doubled into the gradient) | fixed: `vu = u_texels * 65536.0`, `v18 = 65536/area` (callers pre-multiply by mipWidth — one float-multiply association differs from the original's `(u+off)*(w*65536)`; recorded residual). Goldens regenerated; test `TexelUnitUvShiftMovesOneColumn` |
| 5c | Winding: shared forward load | 0x5f6c30 @0x5f6f16: reverse loop gated on `(*(a1+38) & 4)` AND `(x0-x2)*(y0-y1) > (x0-x1)*(y0-y2)` | **DIVERGES** | `polyFlags38` param added (default 0 = old behaviour); literal cross test + reversed load. Test `Flags38Bit2ReversesBackWoundTriangle`. meshlist RasterTri forwards `tri.flags38` |
| 5d | Surface-clip clamp "zero-init disables it; pixel-neutral in-bounds" | original 0x5F71AD writes unclipped; the clamp re-derives U/V from the clamped xL (same accumulator value at that pixel) | **CONFIRMED** (reconstruction-only, pixel-neutral for in-bounds spans; zero-init disables) | kept, documented |
| 6 | Extra items the interrupted agent examined | RasterizeMeshList @0x5aec88 (dispatch via 13D8780[key>>24]); InitEngineDevice @0x5af984; InitDisplayAndPaths @0x527fa4; NullStub13 @0x5f6ee8 (`retn`); xrefs: 0x5f6c30 has **no xrefs** (reached only via the un-captured 13D8780 installs) | recorded | meshlist.cpp dispatch comments already consistent; 13D8780 install site listed as an IDA target |

## Code changes (all with addresses)

* `src/render/raster.cpp` / `raster.h`
  - `FillTexturedSpansShaded` (0x5F7960): exact ROR'd-adc byte accumulator;
    full a4-mode semantics (bit1 byte map, bits 0/2 tile-type stamp value 11/0);
    blur halos (per-row, firstBatch top pyramid with the `v41 > 0` row gate,
    trailing bottom pyramid off the saved v34/v31/v33 state, the `v35 <= a2`
    square-grid row gate); signature `(rs, fb, rowCount, y0, firstBatch=1,
    modeMask=2)`; `rs.shadeBase` = the 1408AA4 tile cursor (persists across the
    two sub-triangle fills), `rs.blurRadius` = 1408AA0.
  - `RasterizeTexturedTriangle` (0x5F7D58): literal signed-area winding select +
    reversed load; literal a5 masking; `modeMask/tileBase/blurRadius/gridPitch`
    params (defaults = previous behaviour); firstBatch 1/0 on the two fills.
  - `FillSpanTextured`/`FillSpanTexturedMasked` (0x5F71AD/0x5F721A): colour =
    `palBase[lightRow8 | idx]`; combined-index equivalence note added.
  - `SpanTexParams`: `lightRow8` added (dword_13FC5E0); `uStepFrac`/`vStep`/
    `lightStart` re-documented per the captured patcher disasm (13FC594/13FC5A8
    are the FRACTIONAL step immediates; `lightStart` kept as the patcher-state
    mirror for texraster_recon2_spanpatch).
* `src/render/raster_textured.cpp` / `.h` (0x5F6C30/0x5F6B34/0x5F6930):
  UV scale fix; `RgbzVertex.light`; lightRow8 = avg(+66)<<8; `polyFlags38`
  winding gate; banner/doc corrections; masked-variant evidence status.
* `src/render/texture.h` / `.cpp`: +104 flag bits re-documented from 0x5db234;
  white default re-documented as a host stand-in for the 0x5db564 null binding.
* `src/render/meshlist.cpp`: RasterTri 16bpp white default shades by the AVG
  light selector (0x5f70bd) and forwards `flags38`; 768*max documented as the
  sort key only (0x5c5120).
* `src/render/hicoltab.{h,cpp}`: verified against 0x5d9db8 — no change.
* `src/render/texture_palettize.*`: verification-only scope; no captured
  evidence for 0x5da34c/0x6029f0 in va1_raster.md → untouched, not re-verified
  this wave.
* Goldens: `tests/unit/render_raster_vectors.inc`,
  `render_raster_textured_vectors.inc` regenerated by `/tmp/raster_ref_w4.py`
  (pure-Python transcription of the captured decompiles — adc chain, winding
  loops, UV scale). `tests/e2e/materials_w4c_e2e_test.cpp` frame pins re-pinned
  (10814 / 8392) for the corrected leaf.

## Rule-8 / sign-off items (host stand-ins kept, flagged)

1. **White default binding** (texture.{h,cpp}): the binary's slot-0/reset
   binding is texBase=0, pal=0, mask=0, width=1, shift=1 — a span through it
   reads address 0. The white texel + flat palette stand-in is host-defined.
2. **White stand-in row contents** (meshlist.cpp): linear gray
   `PackColor(L,L,L)` at the AVG level — the original `*(tex+72)` light-table
   layout is unrecovered.
3. **Byte-map clipping** in `FillTexturedSpansShaded`: the original writes the
   bit-1 byte span unclipped; the reconstruction clips to the surface rect
   (documented; the tile stamps use only the original's own width clamps).

## UNVERIFIED — exact IDA targets for when the MCP returns

| Target | Why |
|---|---|
| `decompile 0x5dea50` (VIBE_Render_LoadAndStretchTexture) | meaning of arg 5 (`v9` — the flags&8 / dword_64A1FC value); resolves claim 3 |
| `xrefs_to 0x64a350`, writers of texture flag +104 bit 3 | the flags&8 producer; which records carry it |
| installs of the 13D8780..13D8798 dispatch table (search `mov dword_13D8780, imm` writers beyond 0x5af984) | which span/triangle leaves are live (0x5f6c30 and 0x5f721a have no static xrefs) |
| `decompile 0x5f7840` (InterpolateEdgeZTex), `0x5f6a8c` (InterpolateEdgeZ), `0x5f6930` (InterpolateEdgeRgbz) | not in the capture — current bodies are wave-2 work, re-verify |
| `get_bytes 0x62c3d4` (4 bytes) | confirm 65536.0f (assumed from identical usage; 0x62C3D8 confirmed) |
| `decompile 0x603da8 / 0x603d00` (flat fill family) | winding handling of the flat path (forward-only assumed) |
| `*(tex+72)` light-table allocator / layout (writers of record +72) | row count of the per-texture light table (the lightRow8 range question; +66 light bytes are evidenced up to 254) |
| `decompile 0x5c5530` (FloodFillTileType) + the BuildTerrainMesh tile-stamp wiring (`[esi+24h]/[esi+28h]/var_18` producers) | terrain owner needs them to wire the new tile-stamp params (handoff below) |
| `decompile 0x5da34c / 0x6029f0` | texture_palettize re-verification (out of this wave's capture) |

## Handoff snippets (bind sites owned by the concurrent agent)

**(a) universe_render.cpp / city_view3d.cpp — keyed routing (claim 3).**
The captured evidence does NOT support `flags & 8` as the masked-span selector
(0x5f721a/0x5f753f have no xrefs at all; flags&8 only zeroes the
LoadAndStretchTexture arg-5). Until 0x5dea50 is decompiled:
```cpp
// raster-verify-wave4: the masked-span runtime selector is UNRECOVERED
// (0x5f721a/0x5f753f have no static refs; flags&8 only feeds 0x5dea50 arg 5).
// Route ALL records through the plain span until evidence lands:
const bool keyed = false;   // was: (bt->tex->flags & 8) != 0
```
(Behaviour-neutral today: decoded records carry flags == 0.)

**(b) bind sites — per-vertex light + winding for the textured leaf.**
`RgbzVertex` now carries the +66 light byte and the leaf takes the poly +38
flag byte; to model the engine's lit span exactly:
```cpp
rv[i].light = vp[i]->lightIdx;            // vertex +66 -> avg row (13FC5E0)
... RasterizeTexturedTriangleRgbz(fb, rv, *bt->tex, bt->palette,
                                  tri.flags38);   // +38 bit2 winding gate
```
NOTE: with nonzero lights the palette must cover `(maxAvg<<8)+255` entries
(the original's `*(tex+72)` table); with the current 256-entry palettes keep
`light = 0` (the default) — lookups are then bit-identical to before.

**(c) terrain owner — BuildTerrainMesh tile stamping (0x5c5fda call).**
The 0x5F7D58 leaf now implements the full a5/a6/a7 contract:
```cpp
// a1/a2 = verts + level bytes, a3 = grid width, a4 = byte map (8bpp Surface),
// a5 = mode byte ([eax+41h] at 0x5c5faa), a6 = tile array [esi+24h] (24-byte
// stride), a7 = blur radius (var_18):
render::RasterizeTexturedTriangle(byteMapSurface, tri,
                                  /*modeMask=*/tileRec.modeByte41,
                                  /*tileBase=*/floor.tileCells,
                                  /*blurRadius=*/blur,
                                  /*gridPitch=*/floor.size);
```

## Test counts / results

* New unit tests: 9 (`ShadedSpanAdcCarryLandsOnePixelLate`,
  `BackWoundTriangleEqualsForward`, `TileStampCoreNoBlur`,
  `TileStampClearValueWithBit0`, `TileStampBlurHaloPyramid`,
  `ModeMaskGatesEverything`, `LightRowSelectsPaletteRow`,
  `TexelUnitUvShiftMovesOneColumn`, `Flags38Bit2ReversesBackWoundTriangle`)
  + `WhiteDefaultShadeUsesAvgLightNotMax` (material_rgb24_standin suite).
* Suites: render_raster_test 1004 checks / 0 fail; render_raster_textured_test
  1074 / 0; render_raster_e2e_test 402 / 0; raster_clip_test 9 / 0;
  texraster_recon2_test 889 / 0; render_fidelity_w3a_e2e_test 3976 / 0;
  materials_w4c_e2e_test re-pinned and green; play_terrain_render_itest,
  render_raster_textured_itest, object_mesh_render_itest, world_render_itest
  green.
* Full ctest (final run of this session, after the concurrent terrain agent's
  in-flight edits settled and a clean rebuild): **1424/1424 passed**.
  (Mid-session, `terrain_ground_e2e_test` transiently failed while that agent
  was editing city_view3d/universe_render and the test itself — counters
  produced upstream of this layer; resolved by its own subsequent edits.)
