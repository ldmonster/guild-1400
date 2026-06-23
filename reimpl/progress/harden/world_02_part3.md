# Hardening sweep — world_02_part3 (law / law_apply / law_apply2 / law_text)

MCP-driven 1:1 verification of every provenance-tagged function in:
- `src/world/law.cpp` / `.h`
- `src/world/law_apply.cpp` / `.h`
- `src/world/law_apply2.cpp` / `.h`
- `src/world/law_text.cpp` / `.h`

Every function below was decompiled at its `gilde.exe 0xADDR` and diffed line-for-line
against the reconstruction (disasm consulted for the switch jcc directions and the
x87 float compare in EvaluateViolation). Constants/tables re-confirmed via
get_bytes / py_eval.

## law.cpp

### GesetzGetRecord — 0x4c244c — VERIFIED-1:1
`if (id >= 26) return 0; qmemcpy(out, base+36*id, 0x24); return 1`. Exact match.

### GesetzComputeMaxWantedLevelFromSum — 0x4c2ba0 — FIXED (float round-trip)
The scalar formula (`sum==75 ? 0.75 : (double)sum*0.01`) was correct, BUT the
original returns `(float)((double)maxSum * flt_61E588)` and the caller stores it via
`fstp [esp+var_1C]` where `var_1C` is a 32-bit `float`, then reloads it for the
`fcomp`. The reconstruction returned a bare `double` without the float round-trip.
- BEFORE: `return (double)guardStationSum * kWantedScalar;`
- AFTER: `float f = (float)((double)guardStationSum * kWantedScalar); return (double)f;`
- Evidence: disasm 0x4c2be6 `fstp [esp+74h+var_1C]`, stack_frame `var_1C ... type:float`.
- flt_61E588 confirmed `0a d7 23 3c` = 0x3c23d70a = 0.01f (get_bytes 0x61E588).

### GesetzEvaluateViolation — 0x4c2c5c — FIXED (4 inverted operator branches)
The op 3/4/5/6 violation tests were INVERTED. The disasm switch (0x4c2cd8, `dec al`)
routes to the roll path `loc_4C2CE9` (= "violation") on these conditions:
- op1 `jnz def`  -> violation when value == threshold  (was correct)
- op2 `jz def`   -> violation when value != threshold  (was correct)
- op3 `jge def`  -> violation when value <  threshold  **(was `!(value<thr)`)**
- op4 `jg  def`  -> violation when value <= threshold  **(was `!(value<=thr)`)**
- op5 `jle def`  -> violation when value >  threshold  **(was `!(value>thr)`)**
- op6 `jl  def`  -> violation when value >= threshold  **(was `!(value>=thr)`)**
- op7            -> violation when value != threshold && threshold != 2 (was correct)
- BEFORE: cases 3–6 used `violation = !(value OP threshold)`.
- AFTER:  cases 3–6 use  `violation =  (value OP threshold)` per the jcc above.
- Evidence: disasm loc_4C2D34 `jge`, loc_4C2D2C `jg`, loc_4C2D24 `jle`, loc_4C2D1C `jl`.
- The roll/caught test (`fcomp; jnb loc_4C2D5E`) = `if (RandomFloat() >= maxWanted)
  -> caught/queue` was already correct.
- Misleading `enum LawOp` comments in law_types.h corrected to the binary's sense.
- Golden fix: `tests/unit/world_law_test.cpp` `WorldLaw.ViolationOperatorMatch`
  law-13 (op5, threshold 16) expectations were backwards; value 20 (>16) is now
  `kViolationQueued`, value 10 is `kViolationNoMatch` (cited addr 0x4c2d24).

Constants confirmed: flt_62675C `00 01 00 38` = 0x38000100 (kRandFloatScale).
Law table first rows confirmed against get_bytes 0x631E98. Default RNG hook matches
VIBE_Math_RandomFloatScaled @0x58b910 = `(double)(int)RandNext() * flt_62675C`.

## law_text.cpp — ALL VERIFIED-1:1

- GetBaseTextId — 0x4c2008 — `return 51`. VERIFIED-1:1
- WaitForChange — 0x4c2010 — `do r=RandomModulo(0x33); while((u16)r==cur); return r`. VERIFIED-1:1
- GetTextIdForType — 0x4c2034 — `if(t<0x33) return RandomModulo(0xA); return 6662`. VERIFIED-1:1
- GetVariantTextId — 0x4c2070 — clamp [0,9]; oob->fallback; `+6662`/`+6672` on +4 flag. VERIFIED-1:1
- GetRecordIndex/Ptr — 0x4c20c8 — `t<0x33 ? index : -1`. VERIFIED-1:1
- CheckSeverityAllowed — 0x4c3978 — `sev=packed>>4; op1->sev<3; op2..7->false; def->false`. VERIFIED-1:1
- BuildPenaltyText — 0x4c2dc4 — subcat switch (0/1/2/3 -> +0/+4113/+4122/+4128 over
  record +24); category = buffer `*(DWORD*)(v5+1)>>24` (record +0); format=5*cat+4146,
  prompt=5*cat+4145. Byte-for-byte buffer aliasing matches the original stack frame. VERIFIED-1:1
- `util::RandomModulo` confirmed = VIBE_Math_RandomModulo @0x58b89c (`(int)RandNext()%n`,
  0 when n==0).
- `kLawTypeVariantFlag[51]` re-extracted via py_eval (byte 40*k+4 of dword_4C1810);
  matches the C++ array byte-for-byte.

## law_apply.cpp — ALL VERIFIED-1:1 (1 documented orphan note)

Field-global mapping confirmed: byte_B59850 = base+8 (type), dword_B5984C = base+4
(city/owner), and OfficeDefReqCode(t) == `dword_62EC8E[3*t] >> 24` (HIBYTE, table
offset 12*t+3) == the disasm's `>>24` test. OfficeDefBookCat(t) == byte_62EC92[12*t]
== table offset 12*t+4.

- CollectByCategory — 0x47f03c — top-down 696..0 step 24; reqCode HIBYTE test; copy 24B. VERIFIED-1:1
- CollectByCategoryResolved — 0x47f0b4 — 174..0 step 6; reqCode + FindRecordById gate. VERIFIED-1:1
- CollectElectiveOffices — 0x47f150 — 180..222 step +6; HIBYTE==7 && (allowVacant||id!=-1). VERIFIED-1:1
- CollectHoldersByCategory — 0x47fe8c — 0..672 step 24; BYTE2 bookCat (>=0x25 -> rec0); emit type. VERIFIED-1:1
- LookupHolderCharacter — 0x47f66c — scan type @byte_B59850[v*4]; resolve +4 id. VERIFIED-1:1
- FindHighestVacantRank — 0x47fac0 — clamp>=0x1B->27; down to first state(+16)==3 && rank(+12)<=1. VERIFIED-1:1
- HasAvailableSuccessor — 0x47f79c — reqCode pool of 6; IsNextRankInCategory && !busy(+433). VERIFIED-1:1
- CollectCategoryRankList — 0x47f928 — bookCat%3 progression, v21 target, +flag terminal return. VERIFIED-1:1
- GetSecondaryHolderEntry — 0x47e070 — gate marker/+361; slot30 fast path (dword_B59B1C==
  dword_B5984C[180]) then 6-step scan; GetDefinition(type) == `&def[3*type]+2` 3 dwords. VERIFIED-1:1
- CopyEntriesByIndex — 0x47fa8c — NOTE: in-place 24B copy per record is byte-exact; the
  binary returns the final one-past-end POINTER (eax = buf+24*count), the port returns
  `count`. The function has **no xrefs** in gilde.exe (orphan leaf), so the return value
  is unobservable; the load-bearing side effects match. Left as count (documented).

## law_apply2.cpp

### BuildPromotionList — 0x47f1cc — VERIFIED-1:1
### BuildPromotionListFiltered — 0x47f410 — FIXED (maxCount<=0 outRankBlocked write)
Both: zero+seed (cost=-100.0f=0xC2C80000, type=0, cat=0); slot scan 0..720 step 24;
gate `state(+16)==3` + rank(+12)<4 (unfiltered: before CanPromoteRank; filtered:
inside, rank>=4 -> *outRankBlocked) + CanPromoteRank; dedup by type+0; (category,cost)
insertion sort with `byte_62EC92[12*type]` (==OfficeDefBookCat) and float costs compared
as doubles; cap at maxCount. All byte offsets / float compares verified — no float->int
truncation in this routine (costs stay float, promoted to double for `>`/`<=`).

FIX (filtered only): the original's `if (a2 <= 0) return 0` (0x47f434) executes BEFORE
the `if (v29) *v29 = v34` store, so *outRankBlocked is left UNTOUCHED on the maxCount<=0
early return. The port wrote `*outRankBlocked = 0` there.
- BEFORE: `if (maxCount <= 0) { if (rankInside && outRankBlocked) *outRankBlocked = 0; return 0; }`
- AFTER:  `if (maxCount <= 0) { return 0; }`
- Evidence: decompile/disasm — early `return 0` precedes the `if(v29)*v29=v34` block.
- Golden fix: `tests/unit/law_apply2_test.cpp` `LawApply2.PromotionListMaxZero` now
  expects `rb` to stay -1 (untouched), not 0.

### GetHolderEntryByCity — 0x47dfec — VERIFIED-1:1
Gate marker/+358; slot0 fast path then 6-step scan v8<180 (30 slots); copy 24B +
GetDefinition(type). Matches.

### EvalApplyForCandidacy — 0x46b8a8 — VERIFIED-1:1
`opcode==10 && ApplyForCandidacy -> 10 else 0`. Matches.

### TryPromoteCharacter — 0x47ebd4 — VERIFIED-1:1
All-non-null gate; resolve from(a2)/to(a3) slots; bookCat (def word0>>16, byte+2) and
reqCode (def word0>>8, byte+1) must match; emit cmd {personId=p+4, fromType=v7[0],
toType=v6[0], tag=6}. The decompile's `v9` (def byte+2 = bookCat) and `v8>>24`
(def byte+1 = reqCode) map exactly to the port's `>>16` / `>>8`. Matches.

## Counts
- Functions verified: 25
  - VERIFIED-1:1: 22
  - FIXED: 3 (GesetzComputeMaxWantedLevelFromSum, GesetzEvaluateViolation,
    BuildPromotionListFiltered)
- Golden tests corrected to the binary: 2
  - world_law_test.cpp ViolationOperatorMatch (op5 sense)
  - law_apply2_test.cpp PromotionListMaxZero (outRankBlocked untouched)
- Documented note (no behavioral bug): CopyEntriesByIndex return-value (orphan leaf).

## Build / test status
- All four owned .cpp files compile clean (`g++ -std=c++17 -Wall -Wextra`).
- `world_law_test` builds + passes: **1324 checks, 0 failures** (covers law.cpp
  operator fix + maxwanted float round-trip + law_text + tables).
- law_apply2_test.cpp syntax-checks clean.
- NOTE: the full guild lib link for law_apply_test / *_e2e_test currently FAILS due to
  pre-existing breakage in OTHER agents' files outside this chunk:
  `src/sim/command_apply5.cpp` (`'crt' has not been declared`) and
  `src/sim/charaction_steps5.cpp` (function-pointer conversion errors). Not touched
  (out of chunk) — HANDOFF to whoever owns those sim files.

## Handoffs
- `src/sim/command_apply5.cpp:267` — `'crt' has not been declared` (missing include
  of crt/rand.h or namespace). Blocks linking law_apply*/e2e test targets.
- `src/sim/charaction_steps5.cpp:56-57` — function-pointer type mismatches on hook
  setters (likely against world/law_apply{,2} or util hooks signatures). Same blocker.
