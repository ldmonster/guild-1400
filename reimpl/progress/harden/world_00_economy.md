# Hardening sweep — world economy chunk

Files: `src/world/economy.cpp`, `src/world/economy_quality.cpp`, `src/world/economy_tick.cpp`
(+ headers economy.h / economy_quality.h / economy_tick.h).
MCP live (gilde.exe, imagebase 0x400000). Every function decompiled + diffed against
the binary; every FP constant/table re-read with `get_bytes`.

## economy.cpp

### EconomyComputeGoodsDemand — 0x578438 — VERIFIED-1:1
- Zero loop: disasm `eax=16,32,..,448 step 16` stores `dword_1234748[eax]` = g_goods[0..27].accum
  (28 entries). The reimpl `for g 0..27: accum=0` is exact (the decompiler's `i!=112` is the
  /4-scaled view; the real stride is 16 bytes). Confirmed via `disasm 0x578438`.
- accum index: `flt_1234758[v13]`, v13=4..108 step 4 ⇒ g_goods[1..27].accum. ✓
- Money total `flt_641FD4 = w[1].contrib*acc[1] + w[2].contrib*acc[2]`; goods total v7=48..432
  step 16 ⇒ g=3..27. ✓ Term order (Flat `need*scalar + accum`; Service/Default `accum + term`)
  preserved. Class predicate (==3 / 7,19,15,>22) exact.
- Constants get_bytes-verified: dbl_62563C=0.5, dbl_625634=2.0, dbl_625644=3.0, dbl_625654=-1.0,
  dbl_62564C=1.5. All match eco:: literals.
- Accumulator note: binary uses x87 80-bit for the goods-total sum; reimpl uses `double`
  (project convention). Not bit-exact to 80-bit but accepted; no change.

### EconomyComputeGoodsSupply — 0x578634 — VERIFIED-1:1 (index/return = documented boundary)
- `sar ecx,10h` (signed >>16) gives g; reimpl takes g as a param (surfaced state, per header).
  accum reset + class accumulation match with supply constants get_bytes-verified:
  dbl_62565C=2.0, dbl_625664=0.5, dbl_62566C=3.0, dbl_62567C=-1.0, dbl_625674=1.5. ✓
- The dword_641FCE index advance / `HIWORD==28 → 1` return is the documented external-index
  boundary (caller drives the index); accumulation core is 1:1.

### EconomyComputePriceDeltas — 0x5787d4 — VERIFIED-1:1
- cap-skip `(double)cap >= flt_641DA8 → delta 0`; raw `(double)drift*accum/divisor` narrowed to
  float v6; compares use float v6 >= 1.0; v4/v5 = (double)v6; normal vs inverted (g==4||g==16)
  branches exact. dbl_625684=-1.0 (kDeltaTail) verified.
- v0==28 over-read of the 28-entry table is the documented memory-safety boundary (reimpl
  bounds g to 3..27; identical for every in-bounds slot).

### EconomyLookupRateScalar — 0x579a24 — VERIFIED-1:1
- `if (a1<5) return byte_123525C[a1]; else 126`. Low-end guard is the documented safety fix
  (engine only passes 0..4); >=5 path returns 126 exactly.

## economy_quality.cpp

### EconomyComputeAverageQuality — 0x579a38 — VERIFIED-1:1
- numerator/denominator over typeDef+583 / +584; `if(!denom) 0.0`; `(float)((double)num/(double)den)`
  promoted to st0. Reimpl matches.

### ClampRatio / EconomyComputeIndustryRatio (0x57a3c8) / EconomyComputeResidentialRatio (0x57a474) — FIXED
- **Bug (float-narrow on upper clamp).** Binary @0x57a3ec/0x57a404: `v7 = (float)ratio; if (v7 > 1.0)
  return 1.0` — the UPPER clamp compares the FLOAT-narrowed ratio, while the `>= floor` gate
  (0x57a3f9) and lower clamp (0x57a435) compare the full DOUBLE. Reimpl previously compared the
  double `ratio > 1.0`, diverging for values that round across 1.0 on float narrowing.
- **Fix:** `if (ratio >= floor) { float v7=(float)ratio; if (v7>1.0f) return 1.0; } if (ratio<floor)
  return -1.0; return (float)ratio;`. Reciprocal stays `(float)(1.0/divisor)` and the subtraction
  stays float (both match the binary's v9/v3 float temporaries).
- Constants: dbl_62577C / dbl_625784 = -1.0 verified.

### EconomyLoadDemandSnapshot — 0x57a5dc — VERIFIED-1:1
- null→0.0; copy 40 bytes; out[6]=flt_641DA8; return out[9]. Exact.

### EconomyComputeLawSatisfaction — 0x57a520 — VERIFIED-1:1
- records 0,1 (+24 threshold); `(4-t0)*0.25f*0.75 + (1-t1)*0.25`. flt_62578C=0.25,
  dbl_625794=0.75, dbl_62579C=0.25 verified.

### EconomyComputeWeightedLawScore — 0x57a580 — VERIFIED-1:1
- laws 16..25 (v2<26), 10 weights; float accumulator v10 narrowed each step; return promoted.
  dword_577A24[10] bytes = {0.06,0.05,0.08,0.04,0.13,0.10,0.13,0.12,0.14,0.15} verified.

### EconomyComputeInterpolatedLawScore — 0x57a990 — FIXED
- **Bug (int-subtract before cast).** Binary @0x57a9ec: `((double)v9 - (double)v7) / ((double)v8 -
  (double)v7)` — each i32 field is fild'd to double FIRST, then subtracted. Reimpl computed
  `(double)(value - lo)` (i32 subtraction, wraps on overflow) before casting.
- **Fix:** `(double)value - (double)lo` / `(double)hi - (double)lo`. Float t, float term, float sum
  narrowing all already matched; tail double-count `sum + term` preserved.
- Fields lo=+4, hi=+8, value=+24 confirmed (matches header). Weights dword_577A4C[7] =
  {0.30,0.25,0.12,0.10,0.05,0.08,0.10} verified.

## economy_tick.cpp

### EconomyTickPriceLevel — 0x579098 — FIXED (EMA/spread precision)
- target `(float)((1.0 + (double)snap9) * (double)flt_641FD4)`, first-tick test
  `LODWORD&0x7FFFFFFF`, ConvertX = std::trunc then (int) — all VERIFIED-1:1.
- **Fix (x87-80-bit MAC modeled in float).** EMA @0x579191 `(target-dac)*0.5+dac` and broadcast
  spread @0x579153 `(dac-div)*1.03*0.000712` run entirely on the x87 stack (80-bit) and narrow
  once on the fstp. Reimpl computed in float (rounding each subexpression) / narrowed the
  difference to float before the double mults. Rewrote with double intermediates, single float
  narrow — closer to the 80-bit semantics and avoids the premature float difference.
- snap[6] persisted as the (old) divisor via LoadDemandSnapshot's out[6] patch (matches v4[6]).
- Constants: flt_6256C8=0.5, dbl_6256CC=0.75, dbl_6256D4=1.03, dbl_6256DC=0.000712507… verified.

### CityTickStatsAndBroadcast — 0x57919c — FIXED (snap[6] persist + EMA/spread precision)
- **Bug (missing snap[6]=divisor persist).** Binary @0x5791f4 writes `flt_1234928 = flt_641DA8`
  i.e. the persisted snapshot's slot[6] = cap divisor. Reimpl only commented it; the persisted
  g_demandSnapshot[6] kept stats[6]. Added `snap[6] = g_capDivisor;`.
- **Fix (EMA/spread precision).** Same x87-80-bit modeling as the price tick (EMA @0x5792cc,
  spread @0x579279) — double intermediates, single float narrow; spread subtraction in double.
- Body assembly v1[0..10] (snap[0..4], dac, div, snap[7..9], spread), snap[5]=dac, clock
  snapshot order, null-out→0 all VERIFIED-1:1. flt_6256E4=0.5, dbl_6256EC=0.95 verified.

### EconomyComputePopulationTrend — 0x57a008 — FIXED (float-narrow chain)
- **Bug (float temporaries modeled as double).** Binary holds v21 (t1), v19 (births), v20 (deaths)
  as FLOATs; `v22=(float)(v19+v20)`; `v2=(double)v21+(double)v22`; the LOWER clamp tests the
  DOUBLE v2 (`v2<1.0 && v18<=floor`) while the UPPER clamp tests the FLOAT v22b=(float)v2
  (`v22>=1.0`) and stores it. Reimpl kept t1/births/deaths as doubles, summed once
  `(float)(t1+b+d)`, and ran both clamps off the single float — diverging on narrowing.
- **Fix:** narrow t1→v21 (clamp `<0` on the float), births→v19, deaths→v20; `v22=v19+v20`;
  `v2=(double)v21+(double)v22`; `v22b=(float)v2`; lower clamp on v2 (double), upper clamp on
  v22b (float). growth stays all-double (verified exact).
- Tail inline law-sat (records 0,1; flt_62575C=0.25, dbl_625764=0.75, dbl_62576C=0.25 — identical
  to the standalone) reused via EconomyComputeLawSatisfaction (same 2 GetRecord side-effects).
  Constants dbl_62574C=-1.0, flt_625754=0.30, flt_625758=0.65, dbl_625774=0.05 verified.

### EconomyFillLawRangeRatios / FromTable — 0x57aa30 — FIXED (int-subtract before cast)
- Fields lo=+0, hi=+4, value=+20 CONFIRMED via disasm/stack-frame (var_38=0, var_34=4, var_24=20;
  the fild/fsub/fsubrp/fdivrp chain computes (value-lo)/(hi-lo)). Header/triple layout correct.
- **Bug + Fix:** same int-vs-double subtraction as InterpolatedLawScore — binary fild's each int
  before subtracting (@0x57aa6c/0x57aa70). Changed to `(double)value-(double)lo` /
  `(double)hi-(double)lo`. out[8..14] base (eax+32) preserved.

### CityCopyStateStruct — 0x579448 — VERIFIED-1:1
- `qmemcpy(result, &dword_1235238, 0x24)` (36 bytes); return result. Exact.

## Counts
- Functions reviewed: 17.
- VERIFIED-1:1: 8 (GoodsDemand, AverageQuality, LoadDemandSnapshot, LawSatisfaction,
  WeightedLawScore, PriceDeltas core, LookupRateScalar, CopyStateStruct).
- VERIFIED-1:1 with documented boundary: 2 (GoodsSupply index/return; PriceDeltas v0==28 over-read).
- FIXED: 7 (Industry/ResidentialRatio via ClampRatio, InterpolatedLawScore, TickPriceLevel,
  CityTickStatsAndBroadcast, ComputePopulationTrend, FillLawRangeRatios).
- Constants re-verified via get_bytes: all economy/quality/tick FP scalars + the two weight tables
  (dword_577A24[10], dword_577A4C[7]) — every byte matches the existing literals.

## Tests
- world_economy_test: 145 checks, 0 failures.
- world_economy_quality_test + economy_tick_test: 64 checks, 0 failures.
- turn_economy_test (e2e via play/turn_economy.cpp): 24 checks, 0 failures.
- Affected goldens use small in-range integer inputs, so the int-vs-double and float-narrow fixes
  are behavior-identical on them (no overflow / no value at the float 1.0 boundary) — no golden
  needed editing.

## Handoffs / notes
- Full library build currently fails in `src/gui/widget_layout.cpp` (`w.ld<i32>()` — a member that
  does not exist on `Widget`; another wave's WIP). Unrelated to this chunk. My three files compile
  `-fsyntax-only` clean and link standalone against city/law/coord/sim/crt + their tests.
- No shared symbols changed. No files outside the chunk edited. No commits.
