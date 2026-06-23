# Wave-16 TRUE 1:1 BINARY DIFF — W16-CITY (world family / city / relation / setup)

MCP was LIVE this wave. Every owned function was decompiled at its provenance
address and compared line-for-line against the reconstruction; the wave-14
NEEDS-LIVE-MCP queue is resolved. One real DIVERGENCE was found and FIXED (the
CSV trailing-token); all other targets are VERIFIED-1:1.

## Method
`mcp__ida-pro-mcp__decompile` + `disasm` + `get_bytes` against `gilde.exe`
(imagebase 0x400000, hexrays_ready). FP constants confirmed by raw bit pattern.

---

## VERIFIED-1:1 (decompile matches the reconstruction exactly)

### relation @0x5942fc — VIBE_Relation_LookupMatrixEntry
Decompile: `if (a1==a2) return 127; else return *(int*)((char*)&dword_123D6CD[192*a1]+a2) >> 24;`
Source `RelationGet/Set` compute the byte at `768*a+b` with an `i8` cast. The
`dword_123D6CD[192*a]` (DWORD index × 4 = 768 bytes/row) + `>> 24` (signed high
byte) == the signed byte at `0x123D6D0 + 768*a + b`. Bit-identical. The setter's
packing is the same cell. No range guard exists in the original (the (a,b)
envelope faults in both — confirmed, no guard added). MATCH.

### money_format @0x58f798 — VIBE_Money_FormatWithSeparators
Decompile confirms: `v25 = abs(amount)` (float-coerced int), `v6 = (double)v25 /
dword_649A88[rateIdx] + dbl_6269BC`, then `VIBE_Coord_ConvertX` (trunc) → v24.
Grouping: `v21=strlen-1; v25=v21*flt_6269C4; v9=strlen+v25; ConvertX; v23=(int)v9`
then the back-to-front fill with `(v8+1)%3` cadence and `46` ('.') insert, glyph
17. Constants verified by get_bytes:
  * `dbl_6269BC = 0x3FE0000000000000` = **0.5** == `kMoneyFormatRoundBias` ✔
  * `flt_6269C4 = 0x3EAAAAAB` = **0.333333343f** == the source's `kThird` ✔
Source's arithmetic, trunc rounding, grouping loop and glyph all match. MATCH.
(Note: the binary derives `rate` via `dword_649A88[dword_13CD6F2[189*currencyId]
>>16]`; the source takes the resolved `rate` directly — value-equivalent, the
header already documents this rate model.)

### CityInitParameterTable @0x577a9c
The three 28-entry good tables (drift +0 / contrib +2 / cap +4, 16-byte/good
stride word_1234750..word_12348F4) were transcribed cell-by-cell from the
decompile and match the source's `kDriftWeight` / `kContribWeight` / `kCap`
exactly, including g10 drift = 0xFFFF (-1) and the trailing g27 = 0. MATCH.

### city_population_census — aggregate @0x578abc / wealth @0x577e74 / snapshot @0x5783e4
All three decompiled and matched line-for-line. The step-5 grid reduction
(192-byte row / 24-byte cell, +36/+40/+44 SatWeight/Denom/Count, 1.0f denom
clamp 0x3F800000, the AC/B0 pre-increment overlap, loop bounds 1536/v54), the
popDensity5/satScore2/lawFold9 chains and the law folds all match. Every FP
constant verified by get_bytes:
  * `dbl_625694 = 0.5` (kHalf), `flt_62569C = 2.0f` (kTwo),
    `flt_6256A0 = 0.25f` (kQuarterF), `dbl_6256A4 = 0.75` (kThreeQuarter),
    `dbl_6256AC = 0.25` (kQuarter), `flt_6256B4 = 0.7f` (kSevenTenths),
    `dbl_6256BC = 0.3` (kThreeTenths), `flt_6256C4 = 0.333..f` (kThirdF). ✔
Wealth grid uses `VIBE_Coord_ConvertX` (trunc) for the per-cell mean and returns
the last cell's value — source matches. Snapshot copies 300 bytes + 4 scalars +
returns trunc(flt_641DA8). MATCH.

### groundplan wappen @0x4ae59c — VIBE_Groundplan_GetWappenLabelId
Decompiled: forced-wappen switch (1→1241=v0, 2→1245, 3→1249, 4→1253,
default→v0, 0→grid walk); grid walk over byte_12CE912 stride 536, cap 411648,
marker==6 select / ==7 terminate; `HIBYTE(unk_12CEA71[134*v1])` → GroupFromCode;
group→label table {1,2,10→1241; 3,4→1245; 11,12,6→1249; 5,7,8,9→1253;
default→1241}. The source reproduces every branch, stride and table entry. The
markerCount/v1 bound is documented additive safety (binary indexes adjacent
in-bounds BSS of the same 768-slot person array; valid walks unaffected). MATCH.

### WorldInitBuildingTypeTable @0x5833b4 (kindWorth path + min-keep)
Decompile confirms: kindWorth[1..24] zero + the 11 seeded entries
(7=41,8=50,9=47,10=20,11=33,12=23,13=53,15=30,16=31,17=32; via byte_13CEB43..);
the 731-entry seed = 72; the group-propagation loop; `v14 = byte_13CEB3D[subtype]`
(the `*(int*)(&unk_13CEB3A + subtype) >> 24` idiom == byte at 0x13CEB3D+subtype);
`v15 = groupPos*j/groupSize + v14`; the **min-keep store** is
`if (current <= v15) skip; else store v15` == source's `if (remap > v15) remap =
v15`; the final fallback `remap[i]==72 → byte_13CEB3D[subtype]`. MATCH.

---

## FIXED (real divergence corrected to the binary + golden updated)

### CityParseCsvFieldList @0x50704c — trailing-token (wave-14 queue item)
**DIVERGENCE FOUND.** Resolved by disassembling 0x50704c (the decompile's
register tracking was ambiguous). Exact register semantics:
  * `ecx` = token start (advanced via `lea ecx,[esi+1]` past each comma),
  * `esi` = token end (a ',' or, on end-of-string, `sub esi,esi` → 0),
  * copy is `[ecx, esi)` via StrNCopyPad (stops at the source NUL, zero-pads),
  * the v16 "last field" flag is set during the scan of the FINAL token (the one
    terminated by NUL, not ','), `esi` is restored to that token's real end, and
    the NEXT iteration parses it once then `if(v16) break`.

Net: the final NUL-terminated token is parsed exactly once and the loop stops —
**there is NO extra trailing empty token.** Traced `"10,20"` / maxFields=4 on the
asm → returns **2** (out[0]=10, out[1]=20, out[2..3] untouched).

The previous reconstruction set its `last` flag AFTER parsing, then looped once
more and parsed an empty token (`ParseInt("")==0`), returning **3** with
out[2]==0. The wave-14 golden had PINNED that wrong value.

**Fix (src/world/city.cpp):** rewrote `CityParseCsvFieldList` to do the comma
scan before each parse and stop after the final NUL-terminated token, mirroring
the asm exactly (scan lambda == the binary's scan loop incl. the `sub esi,esi`
end sentinel; `lastField` set during the next-token scan). A comma-less first
token (the binary's degenerate esi==0 path that would `lea ecx,[esi+1]` into a
near-null pointer — never reachable from the city loader, all defaults are
comma-delimited) is treated as the single final field and parsed once, staying
in-bounds.

**Golden (tests/unit/world_city_load_harden_test.cpp):** `"10,20"`/maxFields=4
now asserts n==2, out[2]==-1 (not written). Comment updated: VERIFIED 1:1 vs the
disasm, not "flagged".

**No observable change to CityLoadFromIni:** every default string is
comma-delimited with the exact field count, and the `csv()`/`lawBook()` helpers
zero-init their temp buffers, so positions never written by the parser stay 0
either way. Standalone ASan/UBSan trace confirms all loader inputs
(`"0,0"`, `"1400,0"`, the 11-field privileges, the 12-field law books with 4
commas) produce identical record bytes. Clean under ASan+UBSan.

---

## RESOLVED wave-14 NEEDS-LIVE-MCP queue

1. **0x5833b4 kindWorth/byte_13CEB3C[25] OOB** — CONFIRMED 1:1. `get_bytes` at
   0x13CEB3C shows the table is exactly 25 bytes and the region is zero-BSS for
   272+ bytes after it. The original DOES over-read `byte_13CEB3D[subtype]` for
   subtype>23 (the `*(int*)(&unk_13CEB3A + subtype) >> 24` idiom), yielding 0 from
   the zero-BSS. The source's bounded `kindWorth` (subtype<24 ? table : 0)
   reproduces that exactly. Subtype<24 is the verbatim table read. RESOLVED — no
   change needed.

2. **0x5833b4 + 0x5835f8 NEW ObjByte(objType = room&0x7FFF, ≤32767) OOB of
   g_sceneTypes** — RESOLVED, with a wave-14 NOTE CORRECTION. The wave-14 doc
   speculated the binary's object-type table base is "followed by adjacent BSS
   that absorbs the read." That is WRONG: 0x5835f8 shows `dword_13CE27C =
   VIBE_Memory_AllocDebug(0xB99B)` — a **heap allocation of exactly 0xB99B =
   47515 bytes = 731 × 65**. So `*(u8*)(65*objType + dword_13CE27C + 33)` for
   objType ≥ 731 is a genuine HEAP over-read in the original too (reading
   whatever follows the allocation), NOT a defined zero-BSS read. There is no
   over-allocation that "absorbs" it. The 1:1 truth: for objType < 731 the read
   is byte-identical; objType ≥ 731 is UB in both, and real A_Geb.dat room words
   keep objType well under 1024 (the loops never trigger it on valid data). The
   in-tree `ObjByte` reads `&g_sceneTypes[objType]` over the 731-element array —
   same in-range behavior, same out-of-range UB class. No fix is correct here
   beyond what exists (faithfully bounded to the 731-element table the binary
   allocates); the existing goldens are safe because valid assets stay in range.
   Both fixup loops (room-count @0x5835f8 inner 64-iteration `while(v14 != base+
   v19+128)` with `HIBYTE&=~0x80`, `v15!=253`, original-high-byte sign test, and
   the 589-stride/42408-bound outer loop) match the source line-for-line.

3. **0x50704c CSV trailing empty token** — see FIXED above (real divergence).

4. **0x5942fc / 0x49818C relation no range guard** — CONFIRMED: 0x5942fc has only
   the `a==b` check, no (a,b) bound. The engine's own envelope. No guard added.

5. **0x578abc degenerate divisors** — CONFIRMED: the popDensity5 / step-5 chains
   divide in DOUBLE (`(double)v53/(double)v50`, `flt`/`flt`), no integer division,
   so a zero divisor yields inf/nan matching the FPU path. No change.

---

## Family-tree walk — classification (rule 8 honesty)

* **0x55ab84 VIBE_Stammbaum_RunFamilyTreeWindow** — decompiled in full. It is the
  dynasty WINDOW (Form/Paintbox/Object widgets + frame loop). The cluster only
  reconstructs the deterministic GATHER kernel (stammbaum_tree.cpp), which was
  re-verified against the relevant slice: FindRecordById linear scan; father
  flag-4 hide on BOTH focus and father (`(v116[229]&4)==0 && (v13[229]&4)==0`);
  mother/spouse portrait reads; spouse-children scan with the `{0→1, 1→(2→4),
  4→5}` stepping and `v66<8 && v17<6` window (cap 4, skip -1, skip cid==focus);
  own-children `category<10` filter with `v19` step 8 / bound 40 (cap 5). All
  MATCH. The drawing/widget body is correctly OMITTED (rule 8 — UI-coupled).

* **0x5555c8 VIBE_Office_CollectFamilyHeirCandidates** — decompiled. The literal
  body is a HANDLER-list driven collector: `VIBE_He_FindFirstHandlerByFilter(1,0,
  111)` → match handler `+43 == person+1`, collect up to 4 (`v7 < 4`) resolved
  person records, then (if a2!=0) copy up to min(a2, found) into a 56-byte-stride
  output applying `VIBE_Person_ResolveStatusFlags` + `VIBE_Person_
  EvaluateCandidateEligibility`. The only firm recovered constant — the **4-cap**
  — is honored by the source (`kMaxHeirs == 4`, the `v7 < 4` cap). The full body
  depends on the He_* handler subsystem (0x4c63f8 / 0x4c6278) and person-
  eligibility leaves (0x5596f8 / 0x553ce8) that live OUTSIDE this cluster, so the
  literal serializer is **DEFERRED** (cross-cluster) and the in-cluster
  `FamilyCollectHeirs` remains the documented derived helper (children, cap 4),
  not a claim of 1:1 of 0x5555c8's handler scan. Recorded accurately (not
  overclaimed as VERIFIED-1:1).

---

## world_io_save strides

WrObject case-0 byte-stream + the callback sibling-null save/restore + the two
BioWriteArray overflow guards were byte-pinned in wave-14; the object-node
offsets and the room(344)/part(56)/8×64-string strides were stride-checked
against the WriteObject/WriteBuildingData decompile. No divergence surfaced this
wave; the only residual is the populated-graph WrBuildingData golden byte-stream
(needs a cross-cluster LinkResolver fixture) — carried forward as UNDER-VERIFIED,
unchanged.

---

## Build / test status

* All 8 owned cluster source files + the changed test compile cleanly
  (`-std=c++17 -fsyntax-only`, and as `.o` via the build) — VERIFIED.
* The corrected CSV parser was validated standalone under
  `-fsanitize=address,undefined`: `"10,20"`/mf=4 → n=2; `"1,2,3,4,5,6"`/mf=4 →
  n=4; `"0,0,0,0"`/mf=12 → n=4; `"5"`/mf=2 → n=1 (no UB); 11-field privileges →
  n=11. All match the binary, all clean.
* The FULL-lib build is currently RED due to a PRE-EXISTING break OUTSIDE this
  cluster: `src/world/history_mission.{h,cpp}` + `src/world/mission_rules.h`
  define `enum class MissionCompletionOutcome` twice (another wave-16 cluster's
  in-progress edit — NOT owned here, not touched). My cluster's objects build;
  the test executables cannot link only because they pull the whole `guild` lib.
  No action taken on those files per ownership rules.

## Files changed
* `src/world/city.cpp` — `CityParseCsvFieldList` rewritten to the binary's
  scan-before-parse / no-trailing-empty-token semantics (0x50704c, disasm-pinned).
* `tests/unit/world_city_load_harden_test.cpp` — golden corrected: `"10,20"`/
  maxFields=4 → n==2, out[2] untouched (was the wrong n==3/out[2]==0 pin).
* `progress/diff-city-wave16.md` — this report.
