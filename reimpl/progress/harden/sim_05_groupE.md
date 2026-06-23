# Harden sweep — sim_05 group E (cutscene_misc3 / misc4 / misc5)

Full-tree 1:1 verification of every `gilde.exe 0xADDR`-tagged function in:
- `src/sim/cutscene_misc3.cpp` (+ `.h`)
- `src/sim/cutscene_misc4.cpp` (+ `.h`)
- `src/sim/cutscene_misc5.cpp` (+ `.h`)

MCP live (module gilde.exe). Every constant confirmed via `get_bytes`/`get_string`;
every float→int site checked against disasm (ConvertX @0x5c6b08 truncate-toward-zero
vs fistp vs (int) cast). ConvertX confirmed: `frndint` under a control-word set to
chop (HIBYTE=31 -> RC=truncate), so ConvertX == C `(int)` cast for the sign it sees.

Tests: rebuilt + ran the owned unit/integration/e2e suites in isolation
(test_main + the 3 .cpp + cutscene.cpp + util math/coord/trig).
**251 checks, 0 failures.**

---

## FIXED (divergences corrected to the binary)

### 1. CutsceneLeaseAutoResolve — 0x4a9a68 — MAJOR rewrite (misc3)
Register-tracing the __usercall (RandFloat@0x4aca48, SumCurrencyHeld@0x59152c,
FindRecordById@0x58bc6c all PRESERVE ecx) proved every Hex-Rays "uninitialized"
v10/v13/v14/v15 == `ecx` == **askRent** (a2[8]=v3[8], loaded @0x4a9aca, == a1[37]).
The old reconstruction was wrong on ~6 counts:
- threshold = `(float)(askRent + 32000*years)` — the `+ ecx`/`+ askRent` was dropped.
- **No early "accept-at-ask" return.** On `funds*will1 >= threshold` the binary sets
  counterBudget = `(int)trunc(threshold)` and draws NO will2; the else branch draws
  will2 and counterBudget = `(int)(funds*(will2/6+0.3))`. BOTH paths CONVERGE.
- Gate was inverted: binary requires `askRent <= counterBudget` (old: counterBudget<=ask).
- ratio = `askRent/counterBudget` (old: counterBudget/ask).
- cap = `min(counterBudget, 2*askRent)` (old: min(ask, 2*counterBudget)).
- span = `counterBudget - askRent`; counter = `roll*step + askRent` (old: +counterBudget).
- `d.kind` redefined to mirror a1[38] (0/1); there is no tri-state accept-vs-counter.
Golden vectors (misc3_test) recomputed from the binary and verified in Python:
(1e6,5000,1,.5,.5,.5,0)->(1,5008); (20000,10000,1,0,.5,.1,7)->(0,10000);
(40000,10000,1,0,.99,.99,7)->(1,10016). Tests updated.

### 2. SceneComputeProductionTickRate / kProductionBase — 0x502198 (misc5)
`flt_620D3C @0x620d3c` bytes `0x46BB8000` == **24000.0f**, NOT 16000.0f.
Only the salon path (typeByte==21) uses it: `(int)trunc(24000.0/sum)`; the generic
path uses the integer literal `16000/sum` (correct). Fixed `kProductionBase` 16000->24000,
header/.cpp comments, and the golden: salon case (sum=350) `24000/350=68` (was 45).
Float→int: binary does ConvertX then `(int)`; sum>0 so trunc==cast — OK.

### 3. CutsceneBirthVoiceBase — 0x4a7b5c (misc3)
father-ill-only and mother-ill-only bases were SWAPPED. Disasm:
0x4a7f0f father-ill-only -> **4**; 0x4a7ed3 mother-ill-only -> **2**
(both-ill 6, neither 0). Fixed function, header comments, and golden test.

### 4. CutsceneDeath voice sample index — 0x4a7fdc (misc3)
Staged voice indices are **{0,2,3,4,5,6}** (index 1 SKIPPED;
0x4a8392..0x4a8432), not the old `i+1` => {1,2,3,4,5,6}. Delays {0,4000,3000,2000,
4000,500} confirmed. Tag "_TOD_HS" ✓.

### 5. CutsceneDeath / CutsceneBankruptcy inheritance branch (misc3)
Both gated the reload-session vs distribute branch on the WRONG global.
Binary @0x4a8453 / @0x4a8757: `word_63C740 & 8`. Added `worldFlags` (word_63C740)
to `Cutscene3State`; Death now branches `(worldFlags & 8)` (was `replayGate`
placeholder); Bankruptcy now `if ((worldFlags&8)==0) distribute(person,0) else
reloadSession` (was UNCONDITIONAL distribute). Bankruptcy msg id now from
`worldFlags & 4` (was `duelMode`).

### 6. String literals corrected to .rdata (misc3)
- Birth music `cd2\Geburt.mp3` (was lowercase g); script `enter_Geburt.esc`
  (was `enter.esc`); voice tags `_GEBURT` / `_GEBURT_GESCHREI` (were `geburt`/...).
- Bankruptcy sky `sky_dunkel_01` lowercase (was `Sky_Dunkel_01`); script
  `enter_Pleite.esc` (was `enter.esc`).
- Salon music track is **NULL** (`Music_PlayCutsceneTrack(0)` @0x4a9c46), not "salon".
All confirmed via get_string. e2e/unit mocks updated (null-guard + lowercase key).

### 7. CutsceneLeaseCanAfford — 0x4a98e2..0x4a9916 (misc4)
Binary calls ClampValueRange ONLY for the display coord; its return is IGNORED.
The window-show gate is purely `funds >= baseRent` (0x4a9916, signed). Dropped the
spurious `baseRent < 1` guard. Updated unit golden (ask=0/-5 now affordable) and the
integration cross-check `ClampSaysAffordable` + the LeaseWindow-full-flow not-shown case.

### 8. SceneClassifyMeisterRecords — 0x504ce0 (misc5)
First-anchor scan has **no "already found" guard** (0x504d2e..0x504df5): every
class-12 record overwrites its anchor; loop stops only at found==3. So anchorA ==
the LAST sub-0 before termination, not the first. Removed the `(found&bit)==0`
guards; added a discriminating golden (two sub-0 before sub-1 -> anchorA = last).

---

## VERIFIED-1:1 (no change needed)
- CutsceneSeasonWindowIndex (0x4a7fdc `<` / 0x4a851c `>=`, table {8,10,12,15,18,20}
  @0x49d8b8/0x49d8d0 — bytes confirmed).
- CutsceneBankruptcyMessageId (word_63C740&4 -> 7343/5822) — kernel correct.
- CutsceneDuelChoiceFromButton (0x4a65e4: 1210->1, 1155->0, both latch quit).
- CutsceneDuelOutcomeFromButton (0x4a673c: init 4; idx 0/1/2 -> 2/3/4).
- CutsceneFormatLodDebug (0x4aa1f4) — exact format string, fields.
- CutscenePlayTobyScene (0x4a697c) — call sequence (RunParticipants modeled).
- kExecutionDurations table bytes (0x49d8a4 = {14000,12000,12000,15000,10000}).
- CutsceneFindParticipantById / FindPriorValidParticipant / ClassifyRunMode /
  ParticipantCallbackTable / SalonClassifyTransition / SalonSceneKind /
  LeaseFindOffer (misc4 scanners — integer logic).
- SceneRetZero (0x5e9020), SceneCollectMatchingObject (0x503678),
  SceneCollectTorchObject (0x504774: count@[32], id@[count]),
  SceneFlagBuildingGate (0x5048c4), SceneFlagGateObject (0x505b3c),
  SceneLoadStadtScene (0x500218, fmt + mask 6), SceneSyncBuildingEntrance
  (0x5023b8, 9->310+cmd15, {4,16,19} exempt, else 308),
  SceneRefreshBuildingEffects (0x504910, masks 64/192) — all offset/byte ops
  confirmed; host-only leaves (visual-byte +530 mutation, octree rebuild,
  guard-target/packet-wait, ChrMove) documented as hook abstractions.

Lease coefficients confirmed: flt_61D718=0.16666667, flt_61D71C=0.30000001,
flt_61D720=0.10000000 (bytes @0x61d718/71c/720).

---

## BOUNDARY / HANDOFF

### CutsceneExecution — 0x4a6b90 — MAJOR pre-existing misreconstruction (NOT fixed)
The reconstructed body bears NO resemblance to the real 0x4a6b90:
- Real: scene **Richtplatz.ed3**, music **cd2\ArmerDelinquent.mp3**, sky band from
  `RandInt(3)` (sky_schwer_01/Sky_Mittel_01/Sky_Schoen_01 + inner RandInt(3|4)),
  `Rain_Create(500)` when band==1, role-template name strings for 4 persons
  (Office_ResolveStaffModel / FindRoleTemplate + RandomModulo draws on missing
  records), scripts hinrichtung-create/-intro/-{kopf,galgen,shaufen,kanone}.esc
  (switch on a1+124), Building_AdjustStockAndNotify with a ConvertX-truncated
  negated float `-(person[+28] + flt_61D22C)`, hinrichtung-exit.esc, two frame pumps.
- Reconstruction: "Hinrichtung.ed3", music "execution", bank "hinrichtung.sbf",
  script "enter.esc", a 2-beat RunTimedScript chain. **All wrong.**
- The `dword_49D8A4` table (kExecutionDurations) is the function's ONLY xref but is
  copied to stack v41..v45 and **never consumed** (dead data). `RandInt(3)` is the
  SKY/RAIN band selector, NOT a duration-table index. `CutsceneExecutionIntroDuration`
  is therefore a fabricated semantic (kept only as a faithful table-reader; doc fixed).

Reason left as handoff: a faithful 0x4a6b90 needs ~8 new hooks (role-template
resolve, the 5-arm hinrichtung script switch, AdjustStockAndNotify, sky-band string
pick) + a signature/struct expansion + RNG-draw-order reproduction across the 4
person records — a re-reconstruction beyond this float/int hardening pass, and a
partial patch on the wrong skeleton would mislead. Existing Execution tests are
loose (no music gated / has teardown / has timed) and still pass; the table-reader
golden is unaffected. **Recommend a dedicated wave to re-port 0x4a6b90.**

### Documented hook abstractions (faithful, not divergences)
- RunParticipants / SalonFadeTransition / Salon / SyncBuildingEntrance /
  RefreshBuildingEffects route host I/O (net wait, command staging, scene-graph,
  surface fades, building visual-byte mutation) through inert hooks; the
  deterministic decision kernels are verified 1:1. EnqueueCmd15 target uses objId
  vs the binary's linked-object (`dword_12CE914[...]`) — documented in-source.

## Counts
- Functions with provenance verified: 28 (misc3: 14, misc4: 9, misc5: 11 incl. kernels).
- VERIFIED-1:1: 19.  FIXED: 8 distinct divergences across 8 functions/constants.
  BOUNDARY/handoff: 1 (CutsceneExecution, pre-existing major misreconstruction).
- Tests: 251 checks pass (unit+integration+e2e), goldens updated for fixes 1–8.
