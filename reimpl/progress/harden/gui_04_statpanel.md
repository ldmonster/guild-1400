# Hardening — gui/statpanel.cpp + gui/statchart.cpp (MCP-verified)

Chunk: `src/gui/statchart.cpp`, `src/gui/statpanel.cpp` (+ headers; dedicated tests
`tests/unit/gui_panels_test.cpp`, `tests/e2e/gui_panels_e2e_test.cpp`).

Reference functions (gilde.exe, imagebase 0x400000):
- VIBE_Coord_ConvertX                @0x5c6b08
- VIBE_StatPanel_ShowCityStatistics  @0x55f4ac
- VIBE_StatPanel_RenderChart         @0x55ee70
- VIBE_Stats_DrawBarChart            @0x55e408
- VIBE_StatPanel_DrawGraphLine       @0x55e600
- VIBE_StatPanel_DrawGraphSeriesA/B/C @0x55e778 / 0x55e9b4 / 0x55ebf0

Method: DECOMPILE + DISASM each function; diff every float->int site, constant,
struct stride and branch against the binary. DISASM is the reference of record.

## Float->int model (the central question)
ConvertX @0x5c6b08 (disasm 0x5c6b09..0x5c6b24): `fstcw` save CW; force CW high byte
to 0x1F => RC field = 0b11 (round TOWARD ZERO); `frndint` rounds st(0) to integer in
that mode (= truncation); `fldcw` restore. It rounds st(0) IN PLACE on the FPU stack
(it is a leaf that takes/returns st(0), no args). EVERY float->int site in this window
is `... ; call ConvertX ; fistp`. Because frndint already made st(0) integral, the
following `fistp` is exact; the composite is `(int)trunc(x)`. There is NO bare
round-to-nearest fistp anywhere in these functions. The reimpl `ConvertX == trunc`
plus `static_cast<int>` reproduces this exactly. **VERIFIED-1:1.**

x87 80-bit accumulations: products `series[k]*height` and `sample*scale` keep 80-bit
until the fistp. In every case both factors are float32 (or int->float), whose exact
product needs <=53 mantissa bits and is therefore EXACT as a C `double`; the
subtraction from a small integer baseline is likewise exact. So widening both factors
to `double` (as the source does) reproduces the 80-bit result bit-for-bit. The source
must NOT compute a float32 product first (that rounds to 24 bits and diverges, e.g.
320*0.1f). Confirmed correct in all plotters.

## Constants verified with get_bytes (all match the reimpl)
- flt_641DA8 = 0x00000000 = 0.0f                  (kChartFloor)
- dbl_624AD4 = 0x4004000000000000 = 2.5           (kGrowthScale)
- flt_624ADC = 0x40A00000 = 5.0f                  (kSupplyScale)
- flt_624AE0 = 0x42C80000 = 100.0f                (kDemandScale)
- dbl_624AE4 = 0x4059000000000000 = 100.0         (kPriceScale)
- flt_624AA8 = 0x3E800000 = 0.25f                 (kGridStep)
- flt_624AAC = 0xC1F00000 = -30.0f                (kGridWidthBias)
- flt_624AB0 = 0x41000000 = 8.0f                  (kGridLineBias)
- flt_624AB4 = 0x40A00000 = 5.0f                  (kGridLabelBias)
- flt_624A9C = 0x41200000 = 10.0f                 (kGraphLineBaselineBias)
- dword_5526D0 tick ladder = {500,1000,2000,4000,8000,10000,15000,20000,50000}
- dword_5526F4 legend RGB = 33 33 33 / 10 EE 10 / EE 10 10 / EE EE 00 / EE 10 EE
- growth phrase base 0x1AF1=6897, supply phrase base 0x1AF7=6903 (from `add eax,1AF1h`/`1AF7h`)

## Per-function verdicts

### statpanel.cpp

**ComputeCityStatReadouts @0x55f4ac (sites 0x55f584..0x55f6e4) — VERIFIED-1:1**
- floorMark: `fld flt_641DA8; ConvertX; fistp` => trunc(0.0)=0.  Source matches.
- growth (0x55f5ab): `fld v33[9]; fld1; faddp; fmul dbl_624AD4(2.5); ConvertX; fistp`
  then `cmp ecx,4; jle keep else 4; +6897`.  Source: `(int)ConvertX(((double)v[9]+1.0)*2.5)`,
  `min(.,4)+6897`.  Match (incl. <=4 clamp via jle).
- supply (0x55f5f0): `fld v33[2]; fmul flt_624ADC(5.0f); ConvertX; fistp`; clamp<=4;
  +6903.  Source matches.
- demand (0x55f631): `fld v33[1]; fmul flt_624AE0(100.0f); fstp var_4C(DOUBLE)`; then
  `fldz; fcomp; jnb` clamp (if 0.0 >= prod => 0); `ConvertX; fistp`.  The product is
  stored/clamped as a DOUBLE (x87-extended product of two floats).  Source models it
  as `double demand = (double)v[1]*(double)100.0f; if(demand<=0)demand=0; trunc`.  Match.
- price (0x55f69b): `fld v33[3]; fstp var_44(DOUBLE)`; `fldz; fcomp; jnb` clamp;
  `fld var_54(double); fmul dbl_624AE4(100.0); ConvertX; fistp`.  Source clamps the
  float then `trunc((double)price*100.0)`; (double)float compare-to-0 has the same sign
  as float compare-to-0f, product identical.  Match.

**ComputeLegendLayout @0x55f4ac (do..while v44<5) — VERIFIED-1:1 (window-base 0)**
- checkbox AddToWindow(win, y=30*i+5, x=5, gfx=1210); label x=25, y=30*i+4; message id
  6912+i (`v49=6912; ++v49`); color = bytes at dword_5526F4 + 3*i; swatch span inner
  `do{}while(v22 != 30*i+19)` with v22 from 30*i+9 => [30*i+9, 30*i+19); seriesBit
  toggled = 1<<i (`1<<v44`).  All match.
- NOTE: the binary's row-Y base is `30*SelectWindow_result + {4,5}` (disasm 0x55f75e
  `imul eax,ecx,1Eh`); the isolated layout helper assumes that window index == 0
  (decoupled from the live window system).  Documented; golden asserts base 0.

**LegendStateToMask / MaskToLegendState @0x55f4ac — VERIFIED-1:1**
  Rebuild loop `word_1233500 |= (GetDataPtr(row) ? 1<<i : 0)`; row i <-> bit i.  Match.

**ComputeGraphSeriesPoints @0x55e778 (A/B/C share body; disasm 0x55e7e1..) — VERIFIED-1:1**
  Per sample: `fldz;fcomp;jb` clamp `s = (sample>0.0)?sample:0` stored DOUBLE; then
  `fild(a12-22); fld height(float a12-30); fld s; fmul; fsubr; ConvertX; fistp` =>
  trunc((double)(a12-22) - height*s).  step=(a11-105)/16, x=45+k*step.  Source widens
  height,s to double (exact).  Match.  Closing element[15] uses baseline a12-32+10 ==
  a12-22 (same).

**ComputeGraphLinePoints @0x55e600 (disasm 0x55e69b..) — VERIFIED-1:1**
  NO per-sample clamp (no fldz in body).  baseline = `fild(a12-32); fadd flt_624A9C(10);
  fstp(FLOAT)` => (float)((double)(a12-32)+10.0).  height=(float)(a12-30).  Per column:
  yFAC = baseline - FAC[k]*height; yF98 = baseline - F98[k]*height; both ConvertX-trunc.
  The dead running-max loop over flt_1234FAC (v27 seeded -1.0) is never used.  Source
  matches (negative FAC samples dip below baseline, no clamp — golden k=10 => y=360).

**StatPanel_ShowCityStatistics @0x55f4ac orchestration — VERIFIED-1:1 (sink-hooked)**
  word_1233500 default-1, snapshot read, readouts+legend+RenderChart.  Sink-decoupled.

### statchart.cpp

**ConvertX @0x5c6b08 — VERIFIED-1:1** (see Float->int model above).

**ChartColumnStep @0x55ee70 idiom (disasm 0x55eece) — VERIFIED-1:1**
  `sub edx,69h; mov eax,edx; sar edx,1Fh; shl edx,4; sbb eax,edx; sar eax,4` = signed
  divide-by-16 truncating toward zero == C `(W-105)/16`.  Match.

**BarAxisMaximum @0x55e408 — FIXED**
  - Disasm 0x55e4a2 (`cmp eax,ecx`/`jl`, ecx=v35[0]=500), 0x55e4ae (`add esi,4`/
    `cmp esi,20h`/`jl`), 0x55e5e9 (`cmp eax,v35[esi]`/`jge` advance else
    0x55e5f2 pick).  The selection loop scans ONLY the first EIGHT ladder entries
    (idx 0..7 = 500..20000).  It returns the first entry strictly greater than the
    (clamped, truncated) peak.  If the peak is >= every checked entry (>= 20000) the
    loop falls through with var_2C unchanged => the axis max is the PEAK ITSELF.  The
    9th ladder entry (50000) is dead code, never selected.
  - BEFORE: iterated all 9 entries and returned `kBarTickLadder.back()` (50000) for
    peaks above the ladder.  WRONG for peak >= 20000.
  - AFTER: scan `kBarTickLadderChecked == 8` entries; `return peak` if none exceed.
  - Golden fixed: `BarAxisMaximum(60000)` 50000 -> 60000; added 20000->20000 and
    15000->20000 boundary cases.

**ComputeBarSeriesPoints @0x55e408 — VERIFIED-1:1 (after BarAxisMaximum fix)**
  peak loop: int samples dword_1234FB0[i] stride 5(=20B), seeded -1, keep-on-tie max.
  `v36=(float)peak; clamp to flt_641DA8(0); ConvertX-trunc => P(int)`; ladder => top.
  `scale = (float)((double)(a12-30)/(double)top)` (fild/fild/fdivp/fstp FLOAT).
  Per column k: y = trunc((double)(float)(a12-22) - (double)sample[k]*(double)scale).
  Source widens to double (exact).  16 columns at x=45+k*step are the observable
  plotted points (the binary draws segments F9C[k]==FB0[k-1] -> FB0[k]; both endpoints
  cover series[0..15]; the flt_641DA8/dword_12350DC right-edge closing segment is a
  draw artifact beyond the 16 columns).  Match.

**ComputeLineSeriesPoints @0x55e408-family — VERIFIED-1:1**
  Same arithmetic as ComputeGraphSeriesPoints: clamp s>0, height=(float)(a12-30),
  baseline=a12-22, y=trunc(baseline - (double)s*(double)height).  Match.

**ComputeGridlineX / ComputeGridLabelX @0x55ee70 (sites 0x55efec/0x55eff7,
0x55f04e/0x55f054) — VERIFIED-1:1**
  Disasm 0x55efd5..: `fild k; fmul flt_624AA8(0.25); fild D; fadd flt_624AAC(-30);
  fmulp; fadd flt_624AB0(8)/flt_624AB4(5); ConvertX; fistp` =>
  `trunc(k*0.25*(D-30) + bias)`, bias 8.0 (lines) / 5.0 (labels).  FPU order
  (k*0.25)*(D-30) matches the source's left-assoc grouping; float consts promote to
  double exactly.  Match.
  - NOTE: the binary's `D` is v52 = (`*((_DWORD*)&unk_67EB88 + 238*idx`) >> 16), the
    window HEIGHT-field (same struct as the width-field v56 used for the column step),
    not the chart width.  The reconstructed helper is parametric in `windowW`; the
    golden passes 425 for both, so the formula/value match.  Naming-only imprecision
    documented; no behavioral change.

## Counts
- VERIFIED-1:1 : 13 functions
  (ConvertX, ChartColumnStep, ComputeBarSeriesPoints, ComputeLineSeriesPoints,
   ComputeGridlineX, ComputeGridLabelX, ComputeCityStatReadouts, ComputeLegendLayout,
   LegendStateToMask, MaskToLegendState, ComputeGraphSeriesPoints,
   ComputeGraphLinePoints, StatPanel_ShowCityStatistics)
- FIXED        : 1 function  (BarAxisMaximum @0x55e408 — 8-entry scan, peak passthrough;
                 source + golden corrected with disasm evidence)
- BOUNDARY     : 0
- Constants byte-verified : 12 scalars + 2 tables (ladder, legend RGB) — all match.

## Tests
- gui_panels_test         : PASS
- gui_panels_e2e_test     : PASS
- statistic_recon_test    : PASS (unaffected; sanity)
