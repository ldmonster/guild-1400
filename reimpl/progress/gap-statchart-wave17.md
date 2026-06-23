# Gap closed (wave-17): statistics chart/panel rounding — ConvertX vs fistp

Owner: W17-STAT. Files: `src/gui/statchart.{h,cpp}`, `src/gui/statpanel.cpp`,
`tests/unit/gui_panels_test.cpp`, `tests/e2e/gui_panels_e2e_test.cpp`.

## The gap (as flagged)
`statchart.cpp`'s `RoundToNearestEven()` carried a NEEDS-MCP-VERIFY note: wave-15 PROVED
`VIBE_Coord_ConvertX @0x5c6b08` TRUNCATES toward zero, but the chart/statpanel helper was
modelled as round-to-nearest-even (`std::nearbyint`), and the specific city-statistics
plotter had not been located by name. So whether each float->int site routed through
ConvertX (truncate) or a bare `fistp`/`+0.5` (round) was UNCONFIRMED.

## What the decompile showed (all addresses MCP-verified this wave)
Located the window: `VIBE_StatPanel_ShowCityStatistics @0x55f4ac`, dispatcher
`VIBE_StatPanel_RenderChart @0x55ee70`, plotters `VIBE_Stats_DrawBarChart @0x55e408`,
`VIBE_StatPanel_DrawGraphLine @0x55e600`, `…DrawGraphSeriesC @0x55ebf0`,
`…DrawGraphSeriesA @0x55e778`, `…DrawGraphSeriesB @0x55e9b4`.

`VIBE_Coord_ConvertX @0x5c6b08` disasm:
```
fstcw [esp+4]            ; save CW
mov   byte [esp+1], 1Fh  ; CW high byte = 0x1F -> RC bits(10..11) = 0b11 = toward zero
fldcw [esp]              ; install round-toward-zero
frndint                  ; round st(0) to integer  => TRUNCATE toward zero
fldcw [esp+4]            ; restore CW
```
Confirms wave-15: ConvertX == `std::trunc`. (A canonical reconstruction already exists at
`src/util/coord.cpp guild::util::ConvertX = std::trunc`; the gui helper now delegates to it.)

### Per-site verdict — EVERY float->int site truncates; NO bare fistp anywhere
| Site | Address | Idiom | Verdict |
|------|---------|-------|---------|
| statpanel floorMark `trunc(flt_641DA8)` | 0x55f584 / ConvertX 0x55f58a | ConvertX | TRUNCATE |
| statpanel growth `(v[9]+1)*2.5` | 0x55f5b6 / 0x55f5bc | ConvertX | TRUNCATE |
| statpanel supply `v[2]*5.0` | 0x55f5f7 / 0x55f5fd | ConvertX | TRUNCATE |
| statpanel demand `max(v[1]*100,0)` | 0x55f63e clamp / 0x55f67a | ConvertX | TRUNCATE |
| statpanel price `max(v[3],0)*100` | 0x55f6b5 clamp / 0x55f6e4 | ConvertX | TRUNCATE |
| grid X `k*.25*(W-30)+8` | RenderChart 0x55efec / 0x55eff7 | ConvertX | TRUNCATE |
| grid label X `k*.25*(W-30)+5` | RenderChart 0x55f04e / 0x55f054 | ConvertX | TRUNCATE |
| bar axis-max `max(peak,floor)` | DrawBarChart 0x55e48c / 0x55e493 | ConvertX | TRUNCATE |
| bar y per sample | 0x55e513 / 0x55e519, 0x55e534/0x55e53c, 0x55e577/0x55e579, 0x55e5b0/0x55e5b4 | ConvertX | TRUNCATE |
| line y per sample (Line/A/B/C) | e.g. 0x55e6ab/0x55e6d1/0x55e70c/0x55e749 | ConvertX | TRUNCATE |

There is no `fistp` and no `+0.5` rounding correction in any of these routines.

## What was reconstructed/fixed
1. `RoundToNearestEven` (== `std::nearbyint`, WRONG) replaced by `ConvertX` (== `std::trunc`,
   delegating to `guild::util::ConvertX`). Header declaration + all call sites updated
   (statchart grid X / grid label X; statpanel all five readouts).
2. **x87-80bit-vs-SSE precision, reconstructed (NOT left as a boundary).** The line-series
   y is `(double)baseline - series[k] * (float)height` formed entirely on the x87 stack, so
   the `series*height` product is evaluated in extended precision. Computing it as a float32
   product first diverges after truncation: `320.0f * 0.1f` rounds to exactly `32.0f`
   (→ y=296) whereas the x87/double product is `31.99999995…` (→ trunc y=295). Promoting both
   factors to `double` reproduces the x87 result exactly (double's 52-bit mantissa is wider
   than the float32 rounding that caused the divergence; for these magnitudes 80-bit and
   64-bit agree after truncation). Applied to `ComputeLineSeriesPoints`; bar y already used a
   double product. `statpanel` demand/supply products likewise widened to double to match the
   `double v40 = float*float` x87 store in the decompile.
3. Constants re-verified via `get_bytes`: dbl_624AD4=2.5, flt_624ADC=5.0, flt_624AE0=100.0,
   dbl_624AE4=100.0, flt_624AA8=0.25, flt_624AAC=-30.0, flt_624AB0=8.0, flt_624AB4=5.0,
   flt_624A9C=10.0 — all byte-exact, headers unchanged.

## Golden corrections (were computed with the wrong round-nearest model)
- Line-series y (W=425,H=350): `{…296…72…136…200…232…264…296…312}` ->
  `{328,295,248,168,88,8,40,71,104,135,168,199,231,263,295,311}` (truncation + x87 product).
- Grid X (W=425): `{8,107,206,304}` -> `{8,106,205,304}`.
- Grid label X (W=425): `{5,104,202,301}` -> `{5,103,202,301}`.
- City readouts growth (`(0.4+1)*2.5=3.5`): phrase `6901` (round->4) -> `6900` (trunc->3).
- Bar y, demand(250), price(120), supply(6906), and all e2e values were unchanged by the fix
  (their inputs landed on exact integers / power-of-two fractions).

## Tests
Added `GuiPanelChart.ConvertXTruncatesTowardZero` pinning the truncation idiom on values where
truncate and nearbyint differ (0.5,1.5,2.5,3.5,3.9,-1.5,-3.9). Re-pinned the chart/readout
goldens to the binary-confirmed truncated values.
- `gui_panels_test`: 426 checks, 0 failures (was 418).
- `gui_panels_e2e_test`: 56 checks, 0 failures.
- `guild` library builds clean.

## Remaining boundary
None for this gap. ConvertX is a fully reconstructable truncation; the only precision subtlety
(x87 extended product vs float32) is reproduced exactly by widening the product to double.
