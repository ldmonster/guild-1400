# Hardening sweep — world_00_city chunk

Files: src/world/city.cpp, city_load.cpp, city_population_census.cpp,
city_satisfaction_grid.cpp (+ tests/unit/city_satisfaction_grid_test.cpp,
world_city_census_harden_test.cpp, world_city_load_harden_test.cpp).

MCP IDA Pro live (gilde.exe, imagebase 0x400000). Every provenance-carrying
function decompiled + disasm-diffed; all FP/int constants and tables read with
get_bytes and confirmed byte-for-byte.

## city.cpp
- **UtilParseInt 0x5dc070** — VERIFIED-1:1. Skip-blank / sign / base-10 digit loop
  with wrapping accumulate (`v3=(u8)c+10*v; v=v3-48`) and `-v2` negate reproduced
  via wrapping u32. Char-class table byte_64A208 (bit2 blank / bit0x20 digit)
  modeled with ASCII checks.
- **CityParseCsvFieldList 0x50704c** — VERIFIED-1:1. Pre-scan two-step token scanner
  (comma/NUL sentinel), 256-byte scratch + StrNCopyPad + ParseInt, `if(v16)break`
  last-field flag, `v6>=4*maxFields` cap — all match. (Wave-16 fix holds.)
- **CityInitParameterTable 0x577a9c** — VERIFIED-1:1 (good table) + scope note.
  All THREE 28-entry tables confirmed against the per-word stores in the disasm
  AND get_bytes:
    - driftWeight (word_1234750[8*g]): {0,0,0,300,100,350,800,600,600,800,65535,
      1000,1000,1000,500,1500,200,800,250,1500,250,250,250,1200,1200,1200,1200,0}
    - contribWeight (+2): {0,100,10,5,25,10,10,20,20,20,30,30,30,30,30,15,30,5,30,
      20,30,30,20,10,10,10,10,0}
    - cap (+4): {0,0,0,150,500,500,500,250,400,600,0,2000,2000,2000,1000,0,500,
      1800,250,300,250,250,400,1000,1000,1000,1000,0}
  GoodSlot stride 16, accum@+8/priceDelta@+12 zero-loop matches. capDivisor =
  flt_641DA8. SCOPE: the original also memsets several adjacent BSS blocks
  (dword_1235238=−1, snapshot flt_1234FA0, grid word_12349A0, dword_1234910/938/
  12350E0) and returns 1800 — those blocks are BSS (already zero) and owned by the
  snapshot/grid modules; left out of scope (no observable difference at init).
- INI mapper / CityLoadFromIni — non-original convenience (Win32 GetPrivateProfile
  boundary); field placement mirrors the loader. Unchanged.

## city_load.cpp
- **CityGetDistrictCoord 0x5783c4** — FIXED. Disasm: `cmp al,48h; jl` = SIGNED char
  compare `(i8)id >= 72`, `movsx esi,al; word_641DB0[esi*8]` = 8-byte stride.
  Before: `(unsigned)district >= 72u`. After: signed-char `id >= 72` upper bound +
  an explicit `id < 0` low guard (the original has NO low bound and would index
  word_641DB0 OOB for negative ids — unreachable in practice; documented). Stride/
  field reads (out[0]=dword0, out[1]=dword1) already correct. Golden unchanged
  (DistrictCoordBounds still passes: 72/100000/-1 all → 0).
- CityLoadDefinitionIni 0x507144 — VFS/INI boundary (Win32 INI API behind shim);
  slot bound + NachbarStadt recursion preserved. Unchanged.

## city_population_census.cpp
- **CityAggregateDistrictStats 0x578abc** — VERIFIED-1:1. Two scan loops, pop floor
  clamp `v3=max(v3,2*word_641DB8)`, popDensity5 product, residents4/popClamped8/
  v47sum7, density0=InterpolatedLawScore, ratioB1/D3 = 1−v46/v44 & 1−v49/v44, the
  step-5 grid reduction (max SatWeight, SatDenom int-bits clamp ≥0x3F800000 [proven
  equivalent to float ≥1.0 across all signs], SatCount/(v1*2), product chain into
  v55), and the law fold out[2]/out[9] with v37=Gesetz(0).threshold /
  v35=Gesetz(1).threshold — all match disasm. All 8 FP consts confirmed:
  dbl_625694=0.5, flt_62569C=2.0, flt_6256A0=0.25, dbl_6256A4=0.75, dbl_6256AC=0.25,
  flt_6256B4=0.7, dbl_6256BC=0.3, flt_6256C4=1/3. (Live Person/Object arrays are an
  injected-input boundary, documented in the header.)
- **CityComputeWealthGrid 0x577e74** — VERIFIED-1:1. worth/count 8x8 accumulate;
  mean = worth/(i16)count; ConvertX (truncate-toward-zero, 0x5c6b08 sets FPU RC=11
  + frndint) then fistp == `(int)trunc` — matches util::ConvertX. Cell index
  8*x+1+y / byte 192*x+24*y stride consistent.
- **CitySnapshotStats 0x5783e4** — VERIFIED-1:1. 300-byte block copy, 4 scalar
  dwords, return `(int)trunc(flt_641DA8)` via ConvertX. (src/dst block + scalars
  are the injected IO of the BSS snapshot region.)

## city_satisfaction_grid.cpp
- **CoordWorldToCityTile 0x577e04** — VERIFIED-1:1 (divisor = mapTileSpan>>3 with
  sign-bias is the injected HeightmapDivisor leaf; `tx/div, tz/div` matches).
- **CityAddCrimeToGrid 0x577fc4** — FIXED (two bugs):
    1. CONTROL FLOW: original IGNORES WorldToCityTile's return and writes the
       centre+ring UNCONDITIONALLY (x/y keep the RNG fallback when off-map). Was a
       wrong `if(!WorldToCityTile)return y;` early-exit. Now calls it, ignores
       result, always writes.
    2. RING OFFSET (0x5780d2): the inner loop PRE-increments the cell pointer by 24
       before the store, so the crime-AREA write lands at byte 192*cx+24*(cy+1) ==
       GridCrimeArea(cx, cy+1), not (cx,cy). Centre word_12349A2 (+26) is NOT
       shifted. Fixed.
- **CityRemoveCrimeFromGrid 0x578110** — FIXED (same two bugs as Add; exact inverse).
- **CityBuildSatisfactionGrid 0x5788d4** — FIXED. Ring (0x578a7f) reads +40 (SatDenom)
  of cell (cx,cy) and writes +16/+12 of the NEXT cell; the +24 pre-increment
  collapses those to the SAME bytes → effectively `SatDenom(cx,cy)+=contrib` and
  `SatWeight(cx,cy)+=contrib*0.5`. The previous code wrote the un-shifted +16/+12
  bytes (SatB/SatA of (cx,cy)), corrupting every cell except where the alias
  coincided. Now writes GridSatDenom(cx,cy)+=ringB, GridSatWeight(cx,cy)+=ringA.
  Centre flt_12349AC (+36) write, eligibility gate (¬0xB..0xD ∧ ≠0x10), live/id/
  scene gates, v6=scaled13/scale19*weight18 all already correct. flt_62568C=0.5
  confirmed.
- **CityPlaceRandomCrime 0x578240** — VERIFIED-1:1 for RNG/band (RandomModulo(span)
  twice, tx=rnd+span*districtX, tz=rnd; span=mapTileSpan>>3). Heightmap TileToWorld
  is a render BOUNDARY (rule 3) surfaced as the (tx,tz) tile.
- **CitySendSyncCommand 0x579460** — VERIFIED-1:1. Body v[0]=a1, v[2]=clock
  (qword_13CE852), v[3]=a3, v[4]=a4, v[6]=a5, v[1/5/7/8]=0, v[9]=0x69707974
  (1768843636). Template dword_1235238 confirmed all-zero (40 bytes). v[13] (ecx
  arg) is outside the 40-byte body → non-observable.
- **CitySendResetCommand 0x5794e0** — VERIFIED-1:1. v[0]=1, v[9]=1701732972; same
  zero-template + out-of-body v[13]=seq.

## Golden test fixes (binary is truth)
- city_satisfaction_grid_test.cpp: added AreaRing() = source ring shifted +1 in y
  (0x5780d2). AddCrimeCentreAndRing + AddCrimeEdgeRingClamps now assert the shifted
  area cells. BuildSatisfactionWeightGolden value (20) unchanged but its rationale
  comment corrected to the +24-alias byte math. Round-trip / no-player / skip tests
  unaffected. 203 checks pass.

## Counts
- VERIFIED-1:1: 9  (UtilParseInt, CityParseCsvFieldList, CityInitParameterTable,
  CityAggregateDistrictStats, CityComputeWealthGrid, CitySnapshotStats,
  CoordWorldToCityTile, CitySendSyncCommand, CitySendResetCommand)
- FIXED: 4  (CityGetDistrictCoord, CityAddCrimeToGrid, CityRemoveCrimeFromGrid,
  CityBuildSatisfactionGrid)
- BOUNDARY (rules 3-5 / injected live state): heightmap divisor+TileToWorld,
  Win32 INI loader, live Person/Object arrays, BSS snapshot/grid IO, op86 template.

Tests: 203 + 88 + 40 = 331 checks, 0 failures. All four sources compile clean
(-std=c++17 -fsyntax-only). NOTE: an unrelated build error in src/sim/
charaction_steps6.cpp (outside this chunk) blocks the full ALL target; my targets
build and pass.
