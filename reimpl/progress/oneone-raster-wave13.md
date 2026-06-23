# 1:1 audit — the raster leaves (wave-13, agent W13-RASTER)

**Scope:** the pixel-level 1:1 math of the software rasterizer — the leaf
functions and their recovered constants/tables. Modules:
`src/render/raster.{h,cpp}`, `raster_textured.{h,cpp}`, `raster_blend.{h,cpp}`,
`hicoltab.{h,cpp}`, `texture_palettize.{h,cpp}`.
**MCP:** DOWN — this is the MCP-free 1:1 pass: cross-check the reconstruction
against the IN-TREE evidence (provenance headers + the wave-4/5/7 progress docs)
and PIN every recovered value with a golden test. No fresh decompile.
**Prior verification:** these leaves were verified vs the binary in waves 4
(captured evidence), 5 (live MCP), and 7 (live MCP, fog). This wave re-confirms
the GOLDEN coverage is complete and closes the remaining un-pinned constants.

---

## 1. Inventory (every reconstructed function + provenance address)

All functions carry a `// gilde.exe 0x... ` provenance comment — **no RED FLAGs**
(no provenance-less function in the segment).

### raster.{h,cpp}
| addr | function | role |
|---|---|---|
| 0x5F7840 | InterpolateEdgeZTex | long-edge X + shade (13FC578) interpolator |
| 0x5F6A8C | InterpolateEdgeZ | short right-edge X interpolator |
| 0x5F71AD | FillSpanTextured | 16bpp textured inner span (`pal[lightRow8\|idx]`) |
| 0x5F721A | FillSpanTexturedMasked | as above + index-0 skip (`test dl,dl/jz`) |
| 0x5F7960 | FillTexturedSpansShaded | 8bpp shaded span loop + tile-stamp + ROR'd adc |
| 0x5F7D58 | RasterizeTexturedTriangle | shaded-affine pipeline (signed-area winding) |
| 0x603ED4 / 0x603DA8 / 0x603D00 | RasterizeFlatTriangle | shadow-stencil flat fill + winding/cull |
| 0x5F76E2 | BilinearBlendBlock | MMX texture-block magnifier (scalar recon; off-path) |

### raster_textured.{h,cpp}
| addr | function | role |
|---|---|---|
| 0x5F6930 | InterpolateEdgeRgbz | edge X+U+V (+fog) two-path slope |
| 0x5F6A8C | InterpolateEdgeZ (RgbzRasterState overload) | short right-edge X |
| 0x5F6B34 | FillSpanLoop / FillSpanLoopMasked | textured scanline driver |
| 0x5F6C30 | RasterizeTexturedTriangleRgbz[Masked] | full affine textured pipeline |
| 0x5F7500 / 0x5f753f / 0x5F76CD | BuildSpanTexParams | self-mod patcher reconstruction |

### raster_blend.{h,cpp}
| addr | function | role |
|---|---|---|
| 0x5F728A | FillSpanTexturedBlend | 50/50 blend (`(src>>1)&m + (dst>>1)&m`) |
| 0x5F7310 | FillSpanTexturedBlendMasked | blend + index-0 skip |
| 0x5F739C | FillSpanTexturedOr | `dst \|= src` |
| 0x5F740A | FillSpanTexturedOrMasked | OR + index-0 skip |

### hicoltab.{h,cpp}
| addr | function | role |
|---|---|---|
| 0x5d9db8 | HiColTabAddEntry | direct 565 + 63-row ramp (`ch/62*L + 0.5`) |
| (readers) | HiColTabDirect / HiColTabRamp | block accessors (byte 0x7E00 / 512*L) |

### texture_palettize.{h,cpp} (VIBE_Quant_* family)
| addr | function | role |
|---|---|---|
| 0x6029f0 | QuantBuildPalette | orchestrator |
| 0x602aa4 | QuantInitLookupTables | bit-interleave + squared-diff tables |
| 0x602be0 / 0x602c8c | Alloc/FreeColorNodes | 6-level octree |
| 0x602d2c | QuantBuildHistogram | 15-bit histogram + leaf heap + masks |
| 0x602f3c | QuantHeapSiftDown | min-heap by subtree count |
| 0x603068 | QuantHeapReduceColors | merge least-populated leaves |
| 0x603180 | QuantTreeCollectPalette | DFS bit 7..0 first; rounded average |
| 0x603a30 | QuantFindClosestColor | sq-distance; query bucketed `(c&0xF8)+4` |
| 0x6033b4 | QuantMapImageToPalette | serpentine FS dither / undithered arm |
| 0x602a70 | QuantCopyPaletteEntries | planar 256-R/G/B copy-out |

---

## 2. The brief's enumerated 1:1 values — pin status

| recovered value | source addr / doc | PINNED BY |
|---|---|---|
| edge two-path slope `>=0x10000` direct divide | 0x5F6930 (rtx-w5) | `render_raster_textured_test` EdgeInterpFixedPoint / InterpolateEdgeRgbzExact |
| edge two-path slope `<0x10000` reciprocal `(recip*num)>>14` | 0x5F6930 (rtx-w5) | `render_raster_textured_test` InterpolateEdgeRgbzShortPath |
| FillSpan ceil edges `(x+0xFFFF)>>16` | 0x5F6B34 (rtx-w5) | render_raster_textured_test FillSpanLoopSingleRow / TexturedTriangleBitExact |
| masked index-0 skip (`test dl,dl/jz`) | 0x5F721A (ras-w5) | **W13** FillSpanLightRowOrIndexAndMaskZeroSkip + existing FillSpanLoopMaskedSkipsIndexZero |
| `pal[lightRow8\|idx]` palette lookup | 0x5F71AD/0x5F70BD (ras-w5) | **W13** FillSpanLightRowOrIndexAndMaskZeroSkip + render_raster_test LightRow8SelectsHiColTabRow |
| RGBZ UV scale ×65536 (flt_62C3D4) | 0x5F6C30 (rtx-w5) | **W13** RgbzUvScaleIs65536 + render_raster_textured_test TexelUnitUvShiftMovesOneColumn |
| flags38 winding gate (bit2 + cross test) | 0x5f6f16 (rtx-w5) | **W13** Flags38WindingGateTruthTable + Flags38Bit2ReversesBackWoundTriangle |
| 8bpp ROR'd adc shade chain (carry 1px late) | 0x5f7a53 (raster-w4) | **W13** ShadedAdcCarryLandsOnePixelLate + render_raster_test ShadedSpanAdcCarryLandsOnePixelLate |
| per-pixel fog blend BlendFog565 | fog-perpixel-w7 | **W13** BlendFog565EndpointsAndFastReturn + render_fog_test SpanGradientGolden |
| HiColTab 63-row ramp + dbl_6295E8 == 0.5 | 0x5d9db8 (ras-w5) | **W13** HiColTabRampExactHalfRounding / ...AllThreeChannelsHalfBoundary / Has63RampRows + render_surface_test RampEndpoints |
| flt_62C3D4 == 65536.0 constant | get_bytes 0x62C3D4 (rtx-w5) | **W13** RgbzUvScaleIs65536 (the ×65536 step) |
| octree quantizer bucket `(c&0xF8)+4` | 0x603a30/0x602d2c (ras-w5) | **W13** OctreeBucketCentreFormula + render_texture_palettize_test SolidColorYieldsBucketCenter |
| blend per-field LSB-clear mask (no carry) | 0x5F728A (blend) | **W13** BlendAndOrSpanCombineOps + render_raster_test BlendSpanOnePixelMaskEdge |

### Gap closed this wave
The **only** under-pinned value found was the HiColTab mid-ramp `+0.5` rounding:
`render_surface_test.RampEndpoints` range-checked the mid step (`r in [120,136]`)
instead of pinning the exact `dbl_6295E8 == 0.5` result. W13 adds
`HiColTabRampExactHalfRounding` (L=31 -> r8 == 128 -> 565 0x8000, NOT 0x7800 —
the +0.5 is the deciding bit) and `...AllThreeChannelsHalfBoundary` (a colour
whose R channel rounds UP at L=40 only because of the +0.5: `100/62*40 == 64.516`
-> 65). Everything else already had an exact golden; W13 also adds a
self-contained pin per enumerated value so the segment's coverage is legible in
one file (no value relies on a single cross-suite assertion).

---

## 3. Internal consistency check (drift)

Re-read every leaf body against its provenance comments + the wave-4/5/7 docs.
**No drift found.** Spot checks confirmed:
- raster.cpp `FillSpanTextured`: colour fetch is `palBase[lightRow8 | idx]`,
  masked variant gates on `idx != 0` — matches 0x5F71AD/0x5F721A (ras-w5 item 3).
- raster_textured.cpp two-path slope boundary is `dy >= 0x10000` direct /
  `else` reciprocal `(0x40000000/dy)`, `>>14` — matches 0x5F6930 (rtx-w5).
- raster_textured.cpp flags38 gate is `(polyFlags38 & 4) && (x0-x2)*(y0-y1) >
  (x0-x1)*(y0-y2)` — matches 0x5f6f16 (rtx-w5).
- hicoltab.cpp ramp loop is `step < 0x3F` (63 rows L=0..62), `ch/62.0*step + 0.5`
  chopped, packed at `512*step + 2*i`, direct at `0x7E00 + 2*i`, `--freeCount` —
  matches 0x5d9db8 (ras-w5). The header comment "63 light-ramp tables (L=0..62)"
  and the code `step < 0x3F` agree.
- texture_palettize.cpp bucket centre is `(c & 0xF8) + 4` in both
  QuantFindClosestColor (0x603a30) and the histogram leaf sums (0x602d2c) — agree.
- raster_blend.cpp blend is `((src>>1)&mask)+((dst>>1)&mask)`, OR is `dst|=src`,
  masked variants gate on `idx != 0` — matches 0x5F728A/0x5F7310/0x5F739C/0x5F740A.

No source edit was required (the brief's drift-fix clause is for evidence-backed
typos only; none present).

---

## 4. Confidence map

All segment leaves are **GOLDEN-PINNED** (constants + control flow tested →
high 1:1 confidence). None are UNDER-VERIFIED. The carried residuals/host
stand-ins below are documented, not fidelity gaps:

| function cluster | confidence | note |
|---|---|---|
| InterpolateEdgeRgbz/Z, InterpolateEdgeZTex | GOLDEN-PINNED | both slope paths + sub-scanline advance pinned |
| FillSpanTextured/Masked + FillSpanLoop | GOLDEN-PINNED | ceil edges, light-row OR, index-0 skip pinned |
| FillTexturedSpansShaded | GOLDEN-PINNED | ROR'd adc carry, tile stamp, mode mask pinned |
| RasterizeTexturedTriangle[Rgbz] | GOLDEN-PINNED | winding select, UV ×65536, flags38 pinned |
| RasterizeFlatTriangle | GOLDEN-PINNED | cross-product winding + back-wound cull pinned |
| raster_blend (4 leaves) | GOLDEN-PINNED | blend/OR/masked combine ops pinned |
| HiColTabAddEntry/Ramp/Direct | GOLDEN-PINNED | 63 rows + exact 0.5 rounding pinned (W13) |
| VIBE_Quant_* (palettizer) | GOLDEN-PINNED | bucket centre, collect order, dither vector pinned |

### Carried (documented, NOT fidelity gaps)
- **UV-scale float-multiply association** (claim 5b): original fuses
  `(u+off)*(mipWidth*65536)`; recon folds mipWidth into the caller, ×65536 here.
  Same value, association moved — float-rounding-order residual (rtx-w5).
- **Host stand-ins** (raster-w4 rule-8 sign-offs): white default binding,
  white stand-in row contents, byte-map defensive clip. Not part of this
  segment's leaf math; unchanged.
- **Masked-span runtime SELECTOR unrecovered** (raster-w4 claim 3): the masked
  span BODY is 1:1, but which records route to it is not statically reachable
  (0x5f721a/0x5f753f have no xrefs). Bind sites default to the plain span. This
  is a routing question, not a leaf-math question — the leaf is golden.

### NEEDS-LIVE-MCP
None for the leaf math of this segment — every enumerated 1:1 value traces to a
live-MCP verdict (wave 5/7) or get_bytes-sourced constant already in the docs.
The only outstanding MCP targets are the off-leaf routing items above
(0x5dea50 arg-5, the 13D8780 installers) which belong to the bind-site segments,
not the raster leaves.

---

## 5. Tests / results

New file: `tests/unit/oneone_raster_wave13_test.cpp` — suite `OneOneRasterW13`,
**10 tests, 62 checks, 0 failures**:
HiColTabRampExactHalfRounding, HiColTabRampAllThreeChannelsHalfBoundary,
HiColTabHas63RampRowsAndDirectBlock, RgbzUvScaleIs65536,
Flags38WindingGateTruthTable, FillSpanLightRowOrIndexAndMaskZeroSkip,
ShadedAdcCarryLandsOnePixelLate, BlendFog565EndpointsAndFastReturn,
OctreeBucketCentreFormula, BlendAndOrSpanCombineOps.

No existing golden changed (all byte-identical). Segment suites rebuilt clean:
- render_raster_test 1822 / 0
- render_raster_textured_test 1350 / 0
- render_texture_palettize_test 230 / 0
- render_surface_test 212 / 0 (HiColTab + Quant suites)
- texraster_recon2_test 889 / 0
- oneone_raster_wave13_test 62 / 0

Source edits: NONE (audit + golden pins only). Build/ green.
