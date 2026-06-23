# Wave-20 — statistics-panel graph-series plotters (W20-STATPANEL)

Owner: W20-STATPANEL. Files (owned): `src/gui/statpanel.{h,cpp}`,
`tests/unit/gui_panels_test.cpp`, this doc. MCP-verified against `gilde.exe`.

## Scope
Reconstructed the four city-statistics line/graph series renderers and their callees
to genuine leaves, applying the wave-17 ConvertX-truncate + x87-extended-product
verdict to every float->int site.

| Function | Addr | Size | Reconstructed as |
|----------|------|------|------------------|
| VIBE_StatPanel_DrawGraphSeriesA | 0x55e778 | 0x23a | `ComputeGraphSeriesPoints` |
| VIBE_StatPanel_DrawGraphSeriesB | 0x55e9b4 | 0x23a | `ComputeGraphSeriesPoints` |
| VIBE_StatPanel_DrawGraphSeriesC | 0x55ebf0 | 0x27e | `ComputeGraphSeriesPoints` |
| VIBE_StatPanel_DrawGraphLine    | 0x55e600 | 0x175 | `ComputeGraphLinePoints` |

Reused (no ODR redefine): `ConvertX` (statchart.cpp -> `guild::util::ConvertX`,
0x5c6b08), `ChartColumnStep`, `kFirstColumnX`, `kChartHeightOffset`, `kLineTopOffset`,
`kChartSamples`, `kChartFloor`, `kGraphLineBaselineBias`, `ChartPoint` (all statchart.h).

## Callees (down to leaves)
- `VIBE_Coord_ConvertX` @0x5c6b08 — already reconstructed (`util::ConvertX` = std::trunc).
- `VIBE_Paintbox_DrawLine` @0x41ec40 — the pixel blit (rendering boundary); the plotters
  only contribute the integer endpoint (x,y) which is the load-bearing observable we pin.
  The blit is out of scope for these data-point functions.
- Series data globals (interleaved 5-float rows, stride 20 bytes, 16 rows) are modelled
  as input parameters; they are populated at runtime by `VIBE_City_SnapshotStats`
  @0x5783e4 (memmove of byte_1234FB4 into flt_1234FA0; closing scalars := dword_1234914..).
  Not in scope (separate function); the renderers read it, they do not produce it.

## Decompile findings (MCP-verified, disasm-checked per float->int site)

### Series A/B/C (0x55e778 / 0x55e9b4 / 0x55ebf0) — STRUCTURALLY IDENTICAL
Disasm 0x55e7e1.. / 0x55ea1d.. / 0x55ecc4.. confirms one shared body. They differ ONLY
in: series array (FA0 / FA8 / FA4 col), DrawLine colour args, and the closing scalar
(12350CC / 12350D4 / 12350D0) — which is just element[15] of the same series
(flt_1234FA0 + 15*20 = 0x12350CC, etc.). One point loop reproduces every observable.
- `height = (float)(a12-30)`  [var_24/var_29/var_27, fild+fstp -> float]
- per sample: `s = max(series[k], 0.0)` (fldz/fcomp clamp; flt_641DA8 == 0.0)
- `y_k = (int) trunc( (double)(a12-22) - (double)height * (double)s )`
  - `(a12-22)`: loop via `fild(ebx-16h)`; closing via `fild(var_40+10) == (a12-32)+10`
    — same integer, so modelled as `windowH-22`.
  - product `height*s` evaluated on x87 stack (`fmul`/`fsubr`) => EXTENDED precision;
    both factors widened to double to match (verified divergence below).
- C additionally runs the DrawGraphLine-style dead running-max loop over flt_1234FAC
  (v28, seed -1.0); its result is never used (no consumer in the disasm).

### DrawGraphLine (0x55e600) — overlays TWO series, NO clamp
Disasm 0x55e69b loop body loads BOTH flt_1234FAC[esi] and flt_1234F98[esi] (Hex-Rays
collapsed the FAC read). Each segment joins the FAC point at the prev column to the F98
point at the current column.
- `height = (float)(a12-30)`  [var_2C]
- `baseline = (float)( (double)(a12-32) + flt_624A9C )` where flt_624A9C = 10.0
  (get_bytes 0x624a9c = 00 00 20 41 = 10.0f) -> stored as a FLOAT [var_30], == a12-22.
- NO per-sample clamp (no fldz/fcomp in the loop body).
- `y(s) = (int) trunc( (double)baseline - (double)s * (double)height )`
- closing segment uses flt_12350D8 (== flt_1234F98[15]).
- the top running-max loop over flt_1234FAC (v27, seed -1.0) is DEAD — preserved as a
  comment only.

### Per-site rounding verdict (matches gap-statchart-wave17.md)
Every float->int site is a `... ConvertX(); fistp ...` pair => TRUNCATE toward zero.
There is NO bare `fistp` (round-to-nearest) and NO `+0.5` anywhere in the four routines.
Sites: A 0x55e851/0x55e872 (+close 0x55e939/0x55e954); B 0x55ea90/0x55eaae (+close
0x55eb75/0x55eb90); C 0x55ed07/0x55ed25 (+close 0x55edee/0x55ee06); Line 0x55e6ab/
0x55e6d1 (+close 0x55e70c/0x55e749). All disasm-confirmed.

## x87-80bit vs SSE precision (reproduced, NOT papered over)
The `series*height` product is formed on the x87 stack in extended precision. A float32
product diverges: with height=320, sample=0.1f, `(float)(320*0.1f) == 32.0` -> y=296,
but the x87/double product is `320 * 0.10000000149.. == 32.00000047..` -> `328 - that ==
295.99999..` -> trunc 295. Both factors are promoted to `double` (52-bit mantissa wider
than the float32 rounding that caused the split; agrees with 80-bit after truncation).
Pinned by `GuiPanelStat.GraphSeriesX87Product` (expects 295).

## Constants (get_bytes, byte-exact)
- flt_624A9C = 0x41200000 = 10.0  (DrawGraphLine baseline bias)
- flt_641DA8 = 0x00000000 = 0.0   (sample floor / clamp)

## Wiring (rule 13)
The four plotters are the leaf renderers dispatched by `VIBE_StatPanel_RenderChart`
@0x55ee70 on the series bitmask `word_1233500` (call sites 0x55ef1b Line, 0x55ef6b A,
0x55ef43 C, 0x55f3b9 B). RenderChart itself is NOT yet reconstructed as a function (only
its grid-X / grid-label-X math lives in statchart.cpp); the dispatch is currently routed
through `StatPanelCommandSink::RenderChart(seriesMask, snap)` called from
`StatPanel_ShowCityStatistics` @0x55f4ac.

HANDOFF (one line): when RenderChart @0x55ee70 is reconstructed (owner of statchart/
RenderChart), its dispatcher should call — for the set series bits — `ComputeGraphLinePoints`
(bit 0x02), `ComputeGraphSeriesPoints` with flt_1234FA4 (bit 0x04 / C), flt_1234FA0
(0x08 / A), flt_1234FA8 (0x10 / B), then hand the points to the DrawLine blit. The
bar-series bit 0x01 already maps to `ComputeBarSeriesPoints` (statchart.cpp).

## Tests (tests/unit/gui_panels_test.cpp)
- `GuiPanelStat.GraphSeriesPoints` — 16 (x,y) golden points (W=425,H=350); matches the
  existing LineSeriesPoints golden (same math/data -> cross-check).
- `GuiPanelStat.GraphSeriesFloorClamp` — negative samples clamp to baseline (windowH-22).
- `GuiPanelStat.GraphSeriesX87Product` — pins the double-widened product (295, not 296).
- `GuiPanelStat.GraphLinePoints` — both FAC/F98 columns; verifies NO clamp (the -0.1 FAC
  sample plots below baseline at y=360).
- Full suite: `gui_panels_test` = 526 checks, 0 failures (was 426). `guild` lib + test
  build clean.

## Remaining boundary
None for the data-point math. The only non-reconstructed piece touching these is the
`VIBE_Paintbox_DrawLine` pixel blit (a rendering boundary, separate from the recovered
coordinate logic) and the RenderChart dispatcher (handoff documented above).
