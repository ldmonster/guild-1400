# Wave-12 hardening — law / justice cluster (W12-LAW)

MCP DOWN → hardening only (no new 1:1 reconstruction). ASAN+UBSAN build, malformed/
boundary tests, OOB/UB fixes that preserve byte-identical behavior on valid input.

## Scope (owned)
`src/world/`: law, law_apply, law_apply2, law_text, law_types, gesetz_flow,
gesetztable_law_recon, straftat_resolve, straftat_sync, straftat_table, crime,
trial, trial_session, court_council2, privilege, privilege_cmd, privilege_law
(+ their tests). NOT touched: office.{h,cpp}, office_law3, office_recon_privilege,
sim/command_recon4 law table, any play/ bind sites.

## Build / method
ASAN+UBSAN build dir `build-asan-law` (cleaned up at end):
```
cmake -S . -B build-asan-law -DCMAKE_BUILD_TYPE=Debug -DGUILD_BACKEND=OFF \
  -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-sanitize-recover=all -g"
```
All 11 cluster unit targets + 9 e2e targets built and run clean (no ASAN/UBSAN
trips). Each new bug was first reproduced with a probe under ASAN, then fixed, then
pinned with a permanent test.

## Bugs found & fixed (genuine reconstruction OOB)

### 1. `GesetzLoadState` — global-buffer-overflow on malformed save (gesetz_flow.cpp)
`crimeCount` / `evidenceCount` are read straight from the (untrusted, possibly
truncated/forged) save blob and used as loop bounds writing into the fixed-size
`g_crimeTable[512]` and interleaved `g_evidenceOwner/CrimeId[4096]` tables, with no
bound on the declared count. A count past capacity (or negative) drives a
global-buffer-overflow.
- ASAN: `global-buffer-overflow ... in memcpy` (crimeCount=600 with payload).
- Fix: reject the load (`return 0`, the same value every stream-failure path uses)
  when `crimeCount < 0 || crimeCount > kCrimeCount` or
  `evidenceCount < 0 || evidenceCount > kEvidenceCapacity`, BEFORE the read loops.
- Faithfulness: a VALID save always has counts within table capacity, so the guard
  is inert on real input — it only turns memory corruption into a clean "load
  failed". The valid SaveLoad roundtrip golden (`world_office_flow_test`) and an
  exact-at-capacity load both still pass byte-identically.

### 2. `BeweisCollectByOwner` — heap/stack overflow on a degenerate buffer (crime.cpp)
The collect loop is a `do { out[count]=...; } while(...)`; the write happens before
the post-check. With a too-small / zero-capacity output buffer (`outCapacity <= 0`,
or `< 32`) the first matching record writes `out[0]` out of bounds.
- ASAN: `heap-buffer-overflow ... in BeweisCollectByOwner` (outCapacity 0, 0-byte buf).
- Fix: gate the write on `count < outCapacity`. For every valid call (the engine
  always passes its 32-entry / 128-byte buffer) the existing `outBytes < maxBytes`
  termination — `maxBytes == min(128, outCapacity*4)` — already stops the loop at or
  before the buffer end, so the guard never blocks a real write and cannot cause a
  spin/infinite loop; it only stops the overrun on a degenerate caller buffer.

## Areas audited and found already-safe (boundary tests added)
- **Law table (26-entry)**: `GesetzGetRecord`, `GesetzEvaluateViolation`,
  `GesetzBuildPenaltyText`, `GesetzRequestApply`, `GesetzApplyAndNotify` all guard
  `id >= 26`. `GesetzFindRecordByPair`'s `936/stride` walk visits ids 0..25 only.
- **51-entry law-TYPE table (dword_4C1810)**: `LawGetTextIdForType`,
  `LawGetVariantTextId`, `LawGetRecordIndex` all gate on `< 0x33u`; the
  `kLawTypeVariantFlag[type]` read is behind that gate.
- **Crime/straftat**: `StraftatSetRecordState` guards `index >= 512`;
  `MissionTrackCrimeProgress` handles the full u8 type range (untracked -> 0) and a
  missing source; `StraftatTableFind*` scan the fixed 128 records.
- **Trial**: `TrialComputeEvidenceScore` clamps its dedup mirror to 512 and tolerates
  null/0/negative count; `TrialTallyVerdict` tolerates null votes / 0 jurors;
  `TrialSessionStep` clamps `tortureInstrument` into `[0,6]` before indexing the
  7-entry `.esc` table.
- **Privilege**: `PrivilegeSimpleCmdResult` / `PrivilegeBuildCmdResult` /
  `PrivilegeSendSimpleCmd` / `PrivilegeSendBuildCmd` are pure value rules, no arrays.
- **Office collect leaves (law_apply / law_apply2)**: all raw-offset scans over
  `g_officeHolders[37]` (888 B) stay within bounds (max byte offset 720; v5/v6/v8
  walks bounded by 222/180). `OfficeCollectActorsByOwner` guards `count < outCapacity`.

## Tests added (all under ASAN+UBSAN, all green)
- `tests/unit/world_law_test.cpp` (+ section `LawHarden`, 9 tests):
  GesetzGetRecord/EvaluateViolation out-of-range; 51-entry law-type table boundary;
  PenaltyText bad id; StraftatSetRecordState index bound; BeweisCollect zero/too-small
  buffer (pins fix #2); GesetzLoad rejects oversize crimeCount / oversize evidenceCount
  / negative counts / truncated+bad header+null stream (pins fix #1); GesetzLoad
  accepts exact-capacity counts (valid envelope preserved).
- `tests/unit/straftat_table_test.cpp` (+ section `StraftatTableHarden`, 4 tests):
  find/contains on empty table; mission-track untracked/missing/mismatch types;
  every tracked code accepted; find-and-init at the last slot.
- `tests/unit/world_trial_test.cpp` (+ section `TrialHarden`, 5 tests):
  null/empty/negative evidence; >512-crime dedup (no consumed[512] overrun); 0/null/
  many jurors; out-of-range & negative torture instrument clamp; empty-participant
  session runs to kDone.

## Items flagged (NOT changed — behavioral / non-owned)
- `OfficeTallyCategoryCounts(int* out)` (court_council2.cpp): `++out[bucket]` where
  `bucket = OfficeDefBookCat(office)` (u8). The in-tree public overload uses the
  256-int `g_categoryCounts` (safe). The `int* out` overload is documented to take a
  40-dword histogram; a `bucket >= 40` would overrun a caller's 40-int buffer. On
  valid office defs bookCat stays small, so this is a behavioral/contract nuance, not
  a live OOB — left as-is (clamping could change observable output on edge data;
  needs MCP to confirm the original's histogram bound).

## Test counts (ASAN+UBSAN run)
unit: world_law 156, world_law_text 47, world_trial 135, world_court_session 86,
court_council2 121, straftat_table 123, gesetztable_law_recon 29, privilege_law 63,
office_recon_privilege 79, law_apply 39, law_apply2 59.
e2e: law_apply 19, law_apply2 27, court_council2 12, privilege_law 24,
straftat_table 55, world_court_session 44, world_law 22, world_law_text 14,
world_trial 28. All 0 failures. Normal `build/` for the changed targets: green
(world_office_flow_test 126/0 — the valid SaveLoad golden unchanged).
