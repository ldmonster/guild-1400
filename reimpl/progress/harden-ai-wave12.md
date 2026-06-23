# Wave-12 hardening — sim AI / NPC-action cluster

Scope (owned): `src/sim/` AI + NPC-action cluster — `aiaction*`, `aimethod*`,
`ai_recon*`, `npcaction*`, `npcevent*`, `npctarget`, `actionqueue`, `contextaction2`
and their tests. MCP was DOWN: **no new 1:1 reconstruction**, hardening only.

Method: ASAN+UBSAN build in a private dir (`build-asan-ai`, plus `build-asan-ai2`
with `-fsanitize-recover=alignment` to see past the first alignment trip), drive
the real entry points with malformed / boundary inputs, fix every OOB/UB, pin each
fix with a test, keep goldens + valid-input output byte-identical.

## Build commands used
```
cmake -S . -B build-asan-ai -DCMAKE_BUILD_TYPE=Debug -DGUILD_BACKEND=OFF \
  -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-sanitize-recover=all -g"
# triage variant (alignment recoverable, to expose anything hiding behind it):
#   ... -fsanitize-recover=alignment ...
```
Both build dirs cleaned up at the end. The normal `build/` is green for all owned
targets.

## Bugs found & fixed

### 1. OOB read in `NpcAction12_FormAllianceGroup` test harness (ASAN global-buffer-overflow)
- `tests/unit/npcaction12_test.cpp` — the synthetic `self` was a 12-byte `GRec`,
  but the source faithfully reads the alliance-group word at byte **+12**
  (`*(int*)(self+3-dword)`), which is in-bounds in the real ~536-byte Person
  record. The test's `field(self,12)` hook read one dword past the object.
- FIX (test only): backed `self` with a `{ GRec rec; i32 groupWord; }` so byte +12
  is inside the object. Source is correct and unchanged. Pinned by the existing
  `FormAllianceGroupPicksAndBroadcasts` test now running clean under ASAN.

### 2. Candidate-buffer over-read guards in `npctarget.cpp` (reconstruction hardening)
The combat/move-direction queries hand a fixed-size buffer to a collector hook and
then loop to the **returned count** without re-checking it against the buffer:
- `NpcTarget_FindNearestEnemy`: `collectSuccessors(catC, 5, cand)` → `cand[5]`,
  loop `for i in 0..n`.
- `NpcTarget_PickDirectionSeqA` / `_PickDirectionSeqB`:
  `collectByCategory(dir[k], 6, ids)` → `ids[6]`, loop `for i in 0..n`.

The original collectors always respect the cap, so on valid input the count never
exceeds the buffer — but the loop trusted `n` blindly (OOB if a hook over-reports).
- FIX: clamp `n` to `[0, buffer-len]` immediately after each collector call
  (`if (n<0) n=0; if (n>5/6) n=N;`). No observable change on valid input (the
  collector never exceeds the cap), so goldens are byte-identical.
- Pinned by `npctarget_boundary_test.cpp` (lying collector returns 99 / -3).

### 3. List-index guards in `RouteSecondarySearch` (`aiaction_dispatch_ai_recon2.cpp`)
- `pickFromList1` / `pickFromList2` did `list[rollIndex(list.size())]`. `rollIndex`
  models `RandomModulo(count)` (always `[0,count)`), but a degenerate hook could
  index out of the collected `std::vector`.
- FIX: bound the index against the list size before subscripting (early return on
  out-of-range). No-op on valid input; goldens unchanged. (`aiaction_dispatch`
  golden test still 57/57.)

## Boundary / malformed tests added (all pass, ASAN+UBSAN clean)
- `tests/unit/actionqueue_boundary_test.cpp` (28 checks): null/not-ready/null-step
  `DispatchCurrent`; step-replaced-head safe stop; `CheckDurationExpiry` tick
  boundary + abort-flag free; `FinishSetVisible`; `RunActionOrFree` idle/done;
  **pool capacity exhaustion** (exactly `kActionNodeCapacity` allocs, then null);
  dequeue-empty (`UnlinkEntry(null)`, `ClearAll` on empty); oversized `argc`
  clamped (no `args[64]` overflow).
- `tests/unit/npctarget_boundary_test.cpp` (11 checks): oversized/negative collector
  counts (pins fix #2); empty/no-office candidate set; combat-gate rejects; unknown
  sub-method.
- `tests/unit/contextaction2_boundary_test.cpp` (27 checks): **bad object**
  (null `dragSource` in drag mode); out-of-range/unknown `mode` byte;
  non-matching profession gate; address-keyed lookup miss vs hit.
- `tests/unit/npcaction_dispatch_boundary_test.cpp` (10 checks): the **dispatch
  jump-table bounds** — day<8 gate → 0, null record → -1, type ≥ 0x45 (== table
  size) and 0xFFFF → -2 without indexing the 69-entry table, last in-range type
  dispatches, `NpcAction_TableEntry` clamps negative / over-range indices.
- `tests/unit/aimethod_index_boundary_test.cpp` (21 checks): attribute-name lookup
  unknown/null → -1 (the "no attribute" slot gate); method-id range gate that
  bounds the `148*id` catalog index (id≤0, ≥61, signed-negative bytes → reject);
  unknown desire attr stored as -1; `StreamRecord` null guards; degenerate
  `PickMostDislikedRelation` (all-at-cap → -1) and empty/single `SelectBestAiMethod`.

## Verified-safe (already guarded; tests pin behaviour)
- `npcaction.cpp` dispatch: `type >= 0x45` and `NpcAction_TableEntry` bound-check
  both present — double-guarded.
- `ai_recon5_decisions.cpp` `AiMethod_SelectConversationTarget`: `pool[8]` writes
  guarded (`eligible < 8`) and the random pick re-checked (`idx>=0 && idx<8`).
- `aiaction_recon.cpp` `SelectActionSplit`/`ActionSplitSurvivors`: `kSplitDivisors`
  index clamped to `[0,survivors-1]`, `kSplitThresholds[v45]` v45∈[0,3].
- `npcaction2.cpp` `kPlagueScanSteps[RandomModulo(0x10)]` (0..15 in a [16]).
- `npcaction7.cpp` `CollectBuildings`/`people[16]`/`rooms[12]`: writes happen before
  the lock-step bound check; max index never exceeds the array (traced by hand).
- `npcaction10.cpp` `hist[b]`/`hist[(b+14)%13]`: b∈[0,12] over a [13].
- `npcaction12.cpp` `slots[pick]` (`pick=randomModulo(n)`, n≤32) / `picks[3]`.
- `aimethod_score_ai_recon2.cpp` `PickMostDislikedRelation` over `std::array<3>`.
- `npcevent_recon4_eval_target.cpp` `slotIds[5]` (fixed 0..4 loop); null records
  handled.

## UBSAN alignment notes (engine envelope — NOT fixed)
Most owned tests trip UBSAN `reference binding to misaligned address` / `store to
misaligned address` on the **packed `HeRecord` / `GameTime`** raw-offset accessors
(e.g. dword reads at byte +169/+173/+358..+361, the +180 wait-counter word). These
are the engine's intentional byte-faithful layout: the original reads/writes the
record by explicit offset off a register base, and unaligned access is legal on the
x86 target. With alignment made recoverable (`build-asan-ai2`) **every owned test
passes 0 failures with NO ASAN buffer-overflow and NO other UB** — the only trips
are alignment-class. Reworking the packed layout to satisfy strict UBSAN would
change the byte-faithful struct map and is out of scope; documented as the engine's
envelope per the wave brief.

## Flagged for other owners (root cause outside this cluster)
- **`src/sim/charaction.cpp` (NOT in this cluster — charaction/wave-10):**
  `VIBE_Character_DeclareAction` (0x405558) gates only `if (type > 63) return 0`,
  then indexes `g_actionTypes[type]` (a `[64]` array). A **negative** `type` passes
  the signed gate and indexes the array negatively (OOB). The live call tree only
  ever passes literal 0..63 ids (RegisterHandlers), so it never fires in practice;
  the binary's own `type>63` signed gate has the same latent property. Faithful fix
  if desired: also reject `type < 0` before the array access. Left for the
  charaction owner. (The action-queue *driver* in `actionqueue.cpp`, which IS owned
  here, is clean; the inserters all mask `type & 63`.)
- A transient compile error in `src/sim/charaction_misc.cpp`
  (`Gi_DwellCounter` undeclared) was observed mid-run from a concurrent sibling
  edit; it had self-resolved by end of wave. Not owned here, no action taken.

## Status
- Source edits: `npctarget.cpp` (3 clamps), `aiaction_dispatch_ai_recon2.cpp`
  (2 index guards). All no-ops on valid input; goldens byte-identical.
- Test files: 1 fix (`npcaction12_test.cpp`), 5 new boundary suites
  (97 new checks, all passing under ASAN+UBSAN and in the normal build).
- Normal `build/` green for all owned targets; ASAN build dirs removed.
