# Harden pass — ai/meister_economy, meister_events, meister_buildtasks

1:1 line-for-line diff (decompile + disasm) of every provenance-tagged function in
the three target modules against `gilde.exe` (imagebase 0x400000). DISASM wins over
Hex-Rays. Constants confirmed byte-for-byte via `get_bytes` / `get_global_value`.

Scope (edit-only): `src/ai/meister_buildtasks.cpp`, `src/ai/meister_economy.cpp`,
`src/ai/meister_events.cpp` (+ owned headers + their unit tests). The shared
decision cores (SecurityHeatDecrement, TaxPayout, MoodDecayRoll, MoodRelationDelta,
BuildProbability, BuildCandidateMask, BuildPickCategory) live in other modules
(rule_eval / building_needs / meister_trade2) and are OUT OF SCOPE here — their
call sites are verified but their bodies were not re-diffed in this pass.

## Result counts
- VERIFIED-1:1: 11 functions
- FIXED: 1 function (PlanProductionRatio constant swap) + 1 golden + 2 doc-comment blocks
- BOUNDARY (documented, unchanged): 0 new (existing synthetic/hook boundaries re-confirmed)

---

## meister_events.cpp — VERIFIED-1:1 (all 8)

Pure container ops over the two slot pools; no entity/render coupling. Each
diffed line-for-line; struct offsets confirmed against the dword/byte strides.

| fn | addr | verdict | notes |
|----|------|---------|-------|
| StrNCopyPad        | 0x5d9360 | VERIFIED-1:1 | copy-until-NUL then zero-fill; identical |
| RegisterApEvent    | 0x4c703c | VERIFIED-1:1 | scan tests primary(+0x08)/secondary(+0x0C)/label[0](+0x10); ttl=2,owner=a1,primary=a3,secondary=a2; StrNCopyPad 63. Overflow returns -1 vs original returns 0 (existing documented boundary — the original `return v11` is an uninit register). |
| ExpireApEventSlots | 0x4c70c0 | VERIFIED-1:1 | interleaved age/free walk; break on `v1 <= 0` (signed); cursor carried |
| SumApEventsByOwner | 0x4c7104 | VERIFIED-1:1 | sums primary where ownerKey(+0x04)==a1 |
| SumApEventsBySecondary | 0x4c7134 | VERIFIED-1:1 | sums secondary where ownerKey(+0x04)==a1 |
| RegisterEventSlot  | 0x4c6f68 | VERIFIED-1:1 | `if(slot0)` scan; kind(+0x08)=1, ttl(+0x04)=2, id, payloadA(+0x0C), payloadB(+0x10), payload(+0x14), label(+0x22) 127. payloadSize is the unrecovered `ecx` byte-count (existing documented param surfacing). |
| ExpireEventSlots   | 0x4c7004 | VERIFIED-1:1 | age/free walk; break on `!v1` (==0, NOT `<=0`) — distinct from the ApEvent variant, correctly modeled |
| NullTick           | 0x4c932c | VERIFIED-1:1 | single retn |

---

## meister_economy.cpp

### HireStaffDecision — 0x45c670 (core) — VERIFIED-1:1
Decision gate at 0x45c8d7: `(!v6 || (busy&8)==0 && wage<=budget && (u16)RandomModulo(0x48) >= 36 - 2*v6) && !v26`, with outer cap `v6 < staffCap` (0x45c80f). Source matches exactly. RNG: `!staffCount` short-circuits → no draw when staffCount==0; one RandomModulo(0x48) draw otherwise. Tests (HireStaffFirstWorker / HireStaffRollGate, `>= 34` at staffCount=1 = `36-2*1`) confirmed correct against disasm — no churn.

### TrainStaffDecision — 0x45d2ac (core) — VERIFIED-1:1
`if(!busy){ roll=(u16)RandomModulo(0x64); if(roll>=threshold){ result=FindFirstHandler(...); if(!result && trainerCount<3 && budget>=38400) ... } }`. Source matches; RandomModulo(0x64) drawn only when !busy. Budget gate 38400, cost 12800 confirmed (0x45d3c0). Tests confirmed correct — no churn.

### PlanProductionRatio — 0x4596e4 (core) — **FIXED**
**Divergence found (constant swap).** Disasm switch (0x459897):
- case 1 → `flt_6198F0` (0x4598a0)
- case 2 → `flt_6198EC` (0x459996)
- case 3 → `flt_6198E8` (0x4599ad)

Byte-confirmed values (`get_bytes`/`get_global_value`):
- `flt_6198E8` = 0x3683126f = **3.906250185536919e-06**
- `flt_6198EC` = 0x3703126f = 7.812500371073838e-06
- `flt_6198F0` = 0x3783126f = **1.5625000742147677e-05**
- `flt_6198F4` = 0x3f000000 = 0.5  (ratio scale)
- `flt_6198F8` = 0x3e800000 = 0.25 (ratio bias)

The prior reconstruction had case 1 ↔ case 3 constants **swapped**: it used 3.906e-06
for case 1 (binary uses 1.5625e-05) and 1.5625e-05 for case 3 (binary uses 3.906e-06).
The file banner and header comment also mislabeled flt_6198E8/flt_6198F0.

**Fix (source + golden + docs):**
- `src/ai/meister_economy.cpp`: renamed constants to `kProdCase1/2/3` carrying the
  correct byte-recovered values, case body now multiplies by the binary's
  per-case address (case1→F0=1.5625e-05, case3→E8=3.906e-06). Banner corrected
  with confirmed hex+value and the case→address mapping.
- `src/ai/meister_economy.h`: corrected the tier→value annotations.
- `tests/unit/ai_meister_economy_test.cpp` (PlanProductionRatio golden): case1 now
  asserts 1.5625e-05, case3 asserts 3.906e-06 (cited disasm addrs 0x4598a0/0x4599ad).
Result computation `(double)stock * const`, then `* 0.5 + 0.25` — unchanged, matches.
Default case leaves v22 uninitialized in the binary (multiplier always 1/2/3 for
production buildings); source uses 0.0 → bias-only (existing documented boundary).

### ProcessPlayerTurn — 0x5321ec — VERIFIED-1:1 (synthetic model; RNG order confirmed)
Large entity/query/render-coupled turn; reconstructed as a synthetic-faction
control-flow + command-emission-order + RNG-draw-order model (documented in the
file banner / header). Diffed line-for-line for the load-bearing 1:1 elements:
- Phase-1 cmd25 gate `(+91 flag & 0x800)` (0x5322a1); kinds 4/16/19 security sweep
  (heat `>5u` at +55, FindById guarding building), kind 5 banker (gate
  byte_12CE912!=6/7 && SumCurrencyHeld<160000, tax `16000*(u8)+583`), kinds 13/12/11
  guard target (HIBYTE +544). Order and `++v3` increments match.
- Market-supervision block: Quad56 + SlotReset28 op27, op49 only for the
  market-supervised building (`Begin == dword_12CEA7C[...]`).
- Mood-decay roll: `RandomModulo(0x64) > 0x1E` then kind∉{13,12,11,16} && (Begin[45]&1)==0,
  decay `-(RandomModulo(3)+2)` (0x532485). Routed through MoodDecayRoll core.
- Phase-2 per-slot: QueueRequest16, mood/relation delta → Coord27 (delta!=0),
  State22 (unconditional), confrontation `newRel < -26` →
  `-(u16)RandomModulo(0x4A) < newRel` → RandomModulo(0x64)<=0x32 ? variant52 : variant51,
  each consuming RandomModulo(6) (0x5328a3..0x53294b). **RNG draw count + order match
  the binary exactly** (0x4A, then 0x64, then 6). Craft-profession set
  {2,1,19,20,16,15} (0x53317c).
- Phase-3 production-worth: two EnqueueCmd15 payouts gated on v102[12]/v102[15] and
  SumValuesAtLocation>0 (nodes 232/240).
Synthetic boundaries (banker always-taken gate, security has-building modeled as
securityLevel>=0, entry-slot→worker collapse, 32-flush total preserved) are the
existing documented model — re-confirmed, unchanged.

---

## meister_buildtasks.cpp — 0x4c7774 — VERIFIED-1:1 (in-scope orchestration)

### BuildTallySweep — VERIFIED-1:1
256-slot sweep (stride 169) at 0x4c7810: alive byte, `*aiType < 0x17u` (<23),
`++totals[type]`, owned/free `owner==0xFFFF || owner==current_player` → `++owned[type]`,
else type 7 → `HIBYTE(dword_12CE919[134*owner])` ? leaders : members. Source matches
line-for-line (leaderCount = HIBYTE set; memberCount otherwise).

### SelectBuildToConstruct (phases 2 & 3 orchestration) — VERIFIED-1:1
- Phase 2 (`%4==2`): bit0=type18, bit1=type21, bit2=type20, bit3=type14
  (anchor LABEL_111 `*a2==18`; case2→21, case4→20, case8→14). k=4, RandomModulo(4),
  `%4` wrap. Matches `kPhase2Types = {18,21,20,14}`.
- Phase 3 (`%4==3`): bit0=type8, bit1=type22 (`*a2==8` / `*a2==22`). k=2,
  RandomModulo(2), `%2`. Matches `kPhase3Types = {8,22}`.
- Pick path: force-mask (total==0) preempts the probabilistic pick; else
  `RandomFloatScaled() > p → return`, then RandomModulo(k) rotating-bit scan.
  Externalized to the cores (BuildCandidateMask / BuildPickCategory) + the
  `roll_float`/`roll_modk` inputs — existing documented hook.
- group_of constants (variant): p2 {18→1,21→3,20→2,14→5}; p3 {8→6,22→4} — routed
  through `env.variant_of` (BuildEnv hook). Construct emission (RandomModulo(2),
  RandomModulo(4), EnqueueObjectInteraction + GetPacketStatusById spin + EnqueueCmd15)
  is the documented command hook.
Phases 0 (`%4==0`, owned<2 threshold + type-7 leader/member split) and 1 remain
DEFERRED with phase-specific entanglement (existing, unchanged).
NOTE for downstream: the binary's candidate `< 3` threshold (phase 2/3) is on the
**city-wide total** (e.g. `v101 < 3 || owned/total >= flt_61E85C`), not on `owned` —
the in-scope wrapper forwards both `owned` and `total` to BuildCandidateMask, so the
exact threshold semantics live in that (out-of-scope) core.

---

## Build + tests
```
cmake --build build --target ai_meister_economy_test ai_meister_events_test \
      ai_meister_farming_test meister_economy_recon_planners_test -j   # clean
GUILD_GAME_DIR=.../europe_guild_1400_original ctest -R \
  "meister_economy|meister_events|farming|buildtask|economy_recon_planners"
```
6/6 passed (incl. ai_meister_economy_e2e_test, ai_meister_events_e2e_test).
