# Wave-16 TRUE 1:1 binary diff — law / justice cluster (W16-LAW)

MCP LIVE. Decompiled every reconstructed function in the law/justice cluster and
compared LINE-FOR-LINE against the binary; fixed every divergence; golden-pinned
the corrections; resolved the wave-12 NEEDS-MCP queue. Method per the wave-16
brief: `decompile` + `get_bytes` each target, classify VERIFIED-1:1 / DIVERGES,
fix source+golden to the binary (rule 2: the golden was wrong, not the binary).

## Headline fix — the 26×36 Gesetz law table (unk_631E98 @0x631E98)

`get_bytes(0x631E98, 936)` vs `kLawTableDefault` in `src/world/law.cpp`:
**record 1 was a 32-byte initializer (missing 4 zero bytes at +24..27)**, so the
C aggregate zero-fill drifted its op/threshold/string-pointer fields. Wave-14 had
KNOWINGLY pinned this as `Record1IsShortAndZeroPaddedDrift` ("CORRECT 36 bytes …
queued for the next live-MCP pass"). Now resolved:
- `law.cpp` record 1 → the binary's exact 36 bytes
  `01 02 00 00 00 00 00 00 01 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 18 02 00 00 f8 47 46 00`.
- Verified the WHOLE table is now BYTE-IDENTICAL to 0x631E98 (936/936, script diff).
- Goldens corrected to the binary (rule 2):
  - `world_law_test.cpp` `kLawTableGolden[1]` → full 36 bytes.
  - `AllRecordFieldDecode` exp[1] `{0,0,536}` → `{0,0,0}` (drift value 0x218=536 was the corruption).
  - drift-pin test `Record1IsShortAndZeroPaddedDrift` → `Record1Is36ByteCorrect`
    (pins the 0x004647f8 string ptr now at +32..35, op/threshold = 0).
  - `straftat_table_test.cpp` penalty-text golden vec[1] `arg 4658` → `4122`
    (v7 = record1 +24 dword was 0x218 corrupt → now 0; subcat 2 ⇒ 0+4122).

## VERIFIED-1:1 (matches the binary exactly)

### law_text.cpp / law_text.h — dword_4C1810 law-TYPE accessors
- `LawGetBaseTextId` 0x4c2008, `LawWaitForChange` 0x4c2010,
  `LawGetTextIdForType` 0x4c2034, `LawGetVariantTextId` 0x4c2070,
  `LawGetRecordIndex` 0x4c20c8, `LawCheckSeverityAllowed` 0x4c3978 — all exact
  (10*a1 indexing = 40-byte stride, field +4, `<0x33u` bound, the 6662/6672 ids,
  the severity `>>4 < 3` for op==1, op 2..7 fall-through to false).
- `kLawTypeVariantFlag[51]` — parsed field +4 of all 51 records from
  `get_bytes(0x4C1810, 2040)`: **BYTE-IDENTICAL** to the source array.
- `GesetzBuildPenaltyText` 0x4c2dc4 — exact: subcat = buffer byte 5 (record +1),
  v7 = buffer +28 (record +24), category = top byte of buffer[1..4] (record +0),
  fmt=5*cat+4146, prompt=5*cat+4145, arg per-subcat {v7, +4113, +4122, +4128}.

### law.cpp / law.h — Gesetz data/rules core
- `GesetzGetRecord` 0x4c244c — exact (`>=26 → 0`, qmemcpy 0x24, stride 36).
- `GesetzEvaluateViolation` 0x4c2c5c — operator table (cases 1..7) verified incl.
  case 7 `!(value==thr || thr==2)` and default → no-match (the binary's `v24`
  is 0-always → LABEL_19 → -1). The distinct enum return codes (-3/-2/-1/0) are a
  documented test affordance; the binary collapses badId/noMatch/escaped to -1 and
  returns the command result on the caught path — see "Documented modeling" below.

### gesetz_flow.cpp — flow + save/load
- `GesetzRequestApply` 0x4c247c — clamp [v5[1],v5[2]], initiator id resolution,
  op-70 command {initiator, lawId, value}. Exact.
- `GesetzApplyAndNotify` 0x4c24f8 — `dword_631EB0[9*id] = newThreshold`, notify
  gate `initiatorId==master || !FindRecordById → return 1`. Exact.
- `GesetzFindRecordByPair` 0x4c258c — keys on record +28 / +29 (byte_631EB4/B5),
  stride 36, 936 bound. Exact.
- `GesetzSaveState` 0x4c25e0 — per-field write order verified against the reader.
- `GesetzLoadState` 0x4c28d8 — threshold load (stride 36), format-version gate
  `< 0x10041` default {128,512}, per-field crime read order, evidence stride-2.
  **FIXED clear loop** (see below).

### trial.cpp / trial.h — verdict / evidence-scoring rules core of 0x4a0eb8
The 12 KB cutscene FSM is a documented deferred shell; the deterministic decision
substrate is reconstructed and its constants are VERIFIED via get_bytes:
- `dword_49D644[5] = {1.4,1.2,1.0,0.8,0.6}` (get_bytes 0x49D644) — exact.
- `flt_61CCF4..CD00 = 0.2, 0.01, 1.1, 0.8` (get_bytes 0x61CCF4) — exact.
- Verdict threshold (`voteTotal >= 2 → acquit`), wanted-weight clamp [0,4],
  guilty-fine `score - favor*(score*0.2)*0.01`, torture confess/deny 1.1/0.8.
- trial_session.cpp torture-instrument clamp [0,6] over the 7-entry .esc table —
  verified bound; the 7 .esc names are the recovered scene table.

### privilege cluster — VERIFIED-1:1 (sub-agent decompile, spot-confirmed)
All of `privilege.cpp` (0x561700 simple-cmd, 0x561a74 build-cmd), `privilege_cmd`
(Send* builders, RandomModulo(9)+6518 / +6509 roll order), and `privilege_law`
(0x47fcf0, 0x47fc24, 0x555eb4, 0x556108/23c/370, 0x49db90 collect — stride 268,
owner dword +97, 768/31 bounds, pre-increment 1-based write) match exactly.
Return codes -127/2/1/0/16/96/-1 and the `+1-min` range arithmetic verified.

### straftat / crime — VERIFIED-1:1 (sub-agent decompile, two FIXED, below)
`StraftatFindFreeSlot/FindIndexById/SetRecordState/CountActiveByTarget` (0x4c3390
/3bc/3874/3a48), `BeweisFindOrAllocSlot/Add/ExistsForPair/CollectByOwner`
(0x4c347c/3338/3518/32ec), `StraftatSyncAllToNetwork` (0x4c33f4),
`StraftatUpdateMatchingRecords` (0x4c39a4), `StraftatTable*` (0x53846c/a0/538524),
`MissionTrackCrimeProgress` (0x539054, branch chain {0x0B,0x13,0x17,0x1C,0x28}) —
all exact on offsets (perp +22, wanted +26 u16, location +28, target +33,
provenState +37), strides (45 / stride-2 / 36), and `>=2`/`<2`/`==1` checks.

## FIXED divergences (source + golden corrected to the binary)

1. **Gesetz law table record 1** — headline fix above.

2. **`StraftatBroadcastAccusation` 0x4c3728 — missing flagField mask-out.**
   The binary, after delivering to a recipient, runs
   `dword_12CEAF4[134 * v8] &= v12`, where `v8` is the matching evidence-PAIR
   index (the count of stride-2 slots scanned), so it masks `recipients[v8]`
   (NOT the delivering recipient `j`) — a faithful bug-for-bug detail. The source
   omitted it. FIXED: `recipients` made non-const, write
   `recipients[v8].flagField &= mask` (guarded to `v8 < recipientCount` on the
   abstracted recipient view). Pinned by `BroadcastAccusationMasksRecipientByPairIndex`.

3. **`GesetzLoadState` clear loop 0x4c2af4 — wrong field cleared.** The binary
   clears, per uncleared record: +0 id=-1, **+18 dword=-1**, +22 perp=-1,
   +37 provenState=0, +26 wanted=0. The source cleared **+33 (target)** instead
   of +18. FIXED to clear +18 (and NOT +33). Pinned by
   `GesetzLoadClearLoopMatchesBinaryFields` (+33 left as scrub residue).

4. **`StraftatResolveAndClear` 0x4c354c — missing City-grid call.** The cleared
   path calls `VIBE_City_RemoveCrimeFromGrid(target@+33, location@+28)` once,
   unconditionally, before the evidence loop. The source dropped it. FIXED: added
   `StraftatSetResolveRemoveGridFn` hook (cross-cluster City leaf, default no-op)
   and fire it on the cleared path. Pinned by `ResolveAndClearFiresRemoveGridOnce`.

## RESOLVED wave-12 NEEDS-MCP item

**`OfficeTallyCategoryCounts(int* out)` (court_council2.cpp, 0x47fdfc).** Wave-12
flagged "`++out[bucket]` (bucket = u8) could overrun a 40-int caller buffer".
Decompile resolves it definitively:
- The tally ALWAYS increments the fixed module global `dword_B59820`
  (`g_categoryCounts`), NOT the caller pointer. The loop body is
  `++dword_B59820[(u8)BYTE2(*v5)]` — bucket is a full u8 (0..255), so the global
  must hold 256 entries (it does: `g_categoryCounts[256]`). **No 40-int overrun.**
- The `this`/out pointer is used ONLY by `VIBE_Light_SetGrayColorThunk(0,40,this)`
  which zeroes 40 dwords AT THE CALLER BUFFER (separate scratch from the tally).
- The function RETURNS `dword_B59820` (the global), not the caller buffer.
- The function has NO xrefs (dead/indirect; not on the live call tree).

FIXED source: the `int* out` overload now zeroes 40 dwords of `out`, tallies into
the GLOBAL, and returns the global — exactly the binary. The wave-12 goldens (which
checked `hist[]` and `r == hist`) encoded the wrong model; corrected to the binary:
`court_council2_test.cpp` Tally tests now check the returned global and add
`OutBufferIsScratchTallyIsGlobal`; the e2e test reads the returned global.

## Documented modeling (not changed — faithful-on-valid-input or non-1:1 return)

- **`GesetzEvaluateViolation` return codes.** The binary returns -1 for
  badId/noMatch/escaped and the command-queue result for the caught path. The
  reconstruction uses distinct enum values (-3/-2/-1/0) so the rules core is
  testable in isolation; the control flow / branch outcomes are 1:1. Left as the
  documented affordance (changing it would only collapse test observability and
  the binary's caught-path return is a command result not reproducible headless).
- **`GesetzLoadState` count guards (wave-12).** The binary has no bound check on
  the declared crimeCount/evidenceCount (it loops straight into the fixed tables).
  The wave-12 `>kCrimeCount / >kEvidenceCapacity → return 0` guard is inert on any
  VALID save (counts are always ≤ capacity) and only turns a malformed-input OOB
  into a clean load-fail. Kept (safety; byte-identical on valid input).
- **`BeweisCollectByOwner` bound (wave-12).** Binary uses a fixed `outBytes < 128`
  bound; the source `min(128, outCapacity*4)` + `count<outCapacity` guard is
  identical for the real 32-entry caller, differing only for a degenerate buffer.
- **`StraftatClearWantedFlagOnNpcs` return value** (touched-count vs the binary's
  byte-counter 411648) — documented in the header; the load-bearing `&= ~mask`
  effect is exact.

## Build / tests

All cluster source + test files compile cleanly (g++ -fsyntax-only). The affected
test binaries were rebuilt and run GREEN against the full library:
- `world_law_test` 1324 checks / 0 fail
- `straftat_table_test` 123 / 0
- `court_council2_test` 163 / 0
- `world_office_flow_test` 129 / 0
- `world_court_session_test` 86 / 0, `world_trial_test` 163 / 0,
  `privilege_law_test` 63 / 0, `gesetztable_law_recon_test` 29 / 0

NOTE (not my cluster): a concurrent wave-16 agent left
`src/world/townhall_location_recon.cpp:175` referencing a not-yet-added
`CitizenshipDeps::FamilyGrantCap`, which currently breaks the shared `guild`
library link for fresh builds. This is a city/townhall file outside the law
cluster (ownership boundary) — left untouched; my targets built green before that
regression was introduced.

## Files touched
- src/world/law.cpp (record 1 → 36 bytes)
- src/world/gesetz_flow.cpp (clear loop +18 not +33)
- src/world/straftat_sync.{h,cpp} (flagField mask-out side effect)
- src/world/crime.{h,cpp} (RemoveCrimeFromGrid hook on cleared path)
- src/world/court_council2.cpp (tally → global; resolve the wave-12 item)
- tests/unit/world_law_test.cpp (record-1 goldens; clear-loop pin; grid-hook pin)
- tests/unit/straftat_table_test.cpp (penalty-text golden vec[1])
- tests/unit/court_council2_test.cpp (tally goldens → global)
- tests/unit/world_office_flow_test.cpp (BroadcastAccusation mask pin)
- tests/e2e/court_council2_e2e_test.cpp (tally → global)
