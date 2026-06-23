# Harden: AI MeisterAi cores (meisterai.cpp / meisterai2.cpp / meisterai3.cpp)

Scope: every function carrying `gilde.exe 0xADDR` provenance in
`src/ai/meisterai.cpp`, `src/ai/meisterai2.cpp`, `src/ai/meisterai3.cpp`.
Method: decompile + disasm each original, diff line-for-line (control flow,
switch arms, constants via `get_bytes`, float→int trunc sites, RNG draw order,
struct offsets). DISASM wins over Hex-Rays.

Tests (all green):
```
cmake --build build --target ai_meister_test meisterai2_test meisterai3_test \
  meisterai_ruleeval_ai_recon2_test ai_meister_subplanners_test \
  meisterai3_e2e_test meisterai3_itest meisterai2_e2e_test -j
cd build && GUILD_GAME_DIR=$PWD/../europe_guild_1400_original \
  ctest -R "meisterai|ai_meister_test|ruleeval|subplanners" --output-on-failure
# 8/8 passed (ai_meister_subplanners, ai_meister_test, meisterai2, meisterai3,
#   meisterai_ruleeval_ai_recon2, meisterai3_itest, meisterai2_e2e, meisterai3_e2e)
```

---

## meisterai.cpp — extracted cores of VIBE_MeisterAi_ProcessPlayerTurn (0x5321ec)

All four cores are slices of the one giant `__usercall` turn function; verified
against its decompile + the surrounding disasm context.

| Function | Addr | Status | Notes |
|---|---|---|---|
| `SecurityHeatDecrement` | 0x532e89/0x532e95 | VERIFIED-1:1 | `(i8)(heat - 2*sec)`; if `<0` → 0. Matches `HIBYTE(v123)=byte-2*sec; if(v123<0)0`. |
| `MoodRelationDelta` | 0x53265f..0x5327f7 | VERIFIED-1:1 | Part1 (`/3`) and Part2 (`/5`) bands, `>=100 || relation<=-120` gates, min-1 clamps, signed-char accumulation in HIBYTE(v124). |
| `TaxPayout` | 0x532e73 | VERIFIED-1:1 | `16000 * (u8)multiplier`. |
| `MoodDecayRoll` | 0x532485 | VERIFIED-1:1 | roll `> 0x1E` gate, kind∈{13,12,11,16}/flag&1 skip, `-(RandomModulo(3)+2)`. RNG draw order preserved. |
| `ConfrontationDecision` | 0x5328a3 | VERIFIED-1:1 | `<-26` gate, `-(u16)RandomModulo(0x4A) < newRel` gate, `RandomModulo(0x64)<=0x32 ? 52 : 51`. |

No source or golden changes needed.

---

## meisterai2.cpp — RuleEval decision/scoring family (0x464d84..0x4657f5)

Constants re-confirmed byte-for-byte via `get_bytes` (all match):
-2.0/5.0 (619FFC/61A000, 61A004/61A008), 3.0/-2.0 (61A044/61A048),
0.66/0.33 (61A050/61A054), 3.0/(1/700) (61A058/61A05C), 0.45/0.55
(61A060/61A064), -2.0,1.1,0.8,1.2,0.5,0.5 (61A068/70/74/78/7C/80).

| Function | Addr | Status | Notes |
|---|---|---|---|
| `RuleEvalIncreaseSetting` | 0x464d84 | VERIFIED-1:1 | `trunc((f43-2)*5)` clamp `>=4→4`; cmp law(6).current. (disasm confirms the clamp the Hex-Rays elided). |
| `RuleEvalAdjustSetting` | 0x464ddc | VERIFIED-1:1 | clamp `>=4→4 else <1→1`; group looked up & discarded; cmp law(7). |
| `RuleEvalRangeLow` | 0x4653c0 | VERIFIED-1:1 | city_count<4 gate; group8: `cur>trunc(range*0.33+low)`→`low+rng(3)`; else `cur>=trunc(range*0.66+low)`→0, else `high-rng(3)`. `(u16)` mask preserved. |
| `RuleEvalRangeHigh` | 0x465588 | VERIFIED-1:1 | mirror with 0.55/0.45, rng(6); group9 `cur<target`→`high-rng(6)`; else `cur<=target`→0 else `low+rng(6)`. |
| `RuleEvalInterpolatedTarget` | 0x4654a4 | VERIFIED-1:1 | t=clamp((3-f43)*(f7/700)) via float-bit compares; `(high-low)*t+low`; `abs(target-cur)<=3`→0. |
| `RuleEvalSeasonalStock` | 0x4652a8 | VERIFIED-1:1 | Verified against full disasm. The original's `max(cur,target)`→clamp[low,high] is behavior-equivalent to source's `clamp(target,low,high)`+compare for all L<=H (proved by case analysis). group7 `target>cur`, else `target<cur`. |
| `RuleEvalCostBenefit` | 0x4657b0 | VERIFIED-1:1 | score=max(f52-2,f55-2)*groupMul(11/12→1.2, 7→0.8, else 1.1); base=(f61-2)*f63; high=(f67-2)*f69; two-arm toggle vs law(15).enable; returns enable(=1) on second arm. |

No source or golden changes needed.

---

## meisterai3.cpp — AiMethod scorers + session orchestrator + event aggregators

Constants re-confirmed via `get_bytes` (all match): 0.25/2.0/6.0, -2.0/0.66/0.33,
0.01/1.1/0.9, 5.0/0.1/0.7, 66.666/33.333, 0.025; early-out bit patterns
1077936128=3.0f, 1099956224=18.0f.

| Function | Addr | Status | Notes |
|---|---|---|---|
| `EvalSocialInteraction` | 0x467768 | VERIFIED-1:1 | gauge early-outs, mood trunc, trait bands (90/70/190), budget cmp, fav tie-break, `RandomModulo(2)`. |
| `EvalPurchaseDesire` | 0x467df0 | VERIFIED-1:1 | tier + law-gate bumps (66.666/33.333), 5→4 / 7→6 collapses, clamp [0,9]. |
| `ComputeChoiceWeights` | 0x4684ec | **FIXED** | see below. |
| `RollWeatherActivity` | 0x468964 | VERIFIED-1:1 | base+rng, 47/13/10 weather byte, bonus 0/-0.25/0.1, `>1.0`. (DispatchByType / rating curve = documented hooks.) |
| `EvaluateSessionDecision` | 0x4a481c | **FIXED (partial) + BOUNDARY** | see below. |
| `ApplyDrinkAction` | 0x46a488 | VERIFIED-1:1 | build-op90 debit, slot-reset, return 4; drink field8=-1. (payload collapse = hook.) |
| `ApplyEatAction` | 0x46abd8 | VERIFIED-1:1 | as above, return 5; eat field8=field7. |
| `RollGroupStateMask` | (in 0x469c5c) | VERIFIED-1:1 | bit0=1, rng2→2, rng3→4, rng3→8, rng2→16 (exact draw order). |
| `BroadcastGroupState` | 0x469c5c | VERIFIED-1:1 | walks slot table, `byte+2==3` gate, emit mask (emission = hook). |
| `RequestCmd107` | 0x4c71f0 | VERIFIED-1:1 | handler-exists gate → no emit; else slot-reset type 107. (payload/return collapse = hook.) |
| `RequestCmd122` | 0x4c727c | VERIFIED-1:1 | identical, type 122. |
| `RequestPersonCmd34` | 0x4c7430 | VERIFIED-1:1 | personValid && !handlerMatch → emit type 34/kind2/flag1; gating faithful. |
| `TickRegisteredEvents` | 0x4c6f0c | VERIFIED-1:1 | `dword_632248>=0` guard, 1024 cap, `active && (flags&0x10) && type<0x88` → worldpos+dispatch. count maps to dword_632248+1. |

### FIXED — ComputeChoiceWeights (0x4684ec)

Evidence: disasm at 0x468576/0x468597 plus table `dword_4664B8`
(`get_bytes` = `04 00 03 01 | 03 01 02 00 | 02 00 04 01`). Per slot the original:
- writes a **fixed base byte** to `out+4` = `row[k][0]` (4, 3, 2);
- writes a **table-resolved pick** to `out+5` = `row[k][1 + RandomModulo(3)]`.

The prior source modeled `pick[k] = RandomModulo(3)` (raw) and omitted the base
byte entirely — a real 1:1 divergence.

Fix:
- `meisterai3.cpp`: added `kChoiceTable[3][4]` (the three `dword_4664B8` rows),
  set `out.base[k]=row[k][0]` and `out.pick[k]=row[k][1+rng_mod(3)]`.
- `meisterai3.h`: `ChoiceWeights` gains `int base[3]`; `pick` doc corrected.
- Golden (`tests/unit/meisterai3_test.cpp`) for rng seq {0,1,2}:
  base = {4,3,2}; pick = row0[1]=0, row1[2]=2, row2[3]=1 → {0,2,1}
  (was the incorrect {0,1,2}). Weights unchanged (62/125/250).

### FIXED (partial) — EvaluateSessionDecision (0x4a481c) random moduli

Evidence: sibling scorers disassembled:
- `VIBE_AiMethod_RandomBoolCheck` @0x46797c = `mov eax,2; call RandomModulo`
  → type-1 modulus is **hardcoded 2** (not a session field).
- `VIBE_AiMethod_RandomValue` @0x467994 = `mov eax,7; call RandomModulo`
  → type-2 modulus is **hardcoded 7** (not a session field).
- case 6 @0x4a48e2 = `RandomModulo(2u)`.

Source previously used `in.randModulus` for types 1, 2, 6.

Fixes applied (binary-faithful, no test breakage):
- type 1 → `rng_mod(2)` (no test exercises type 1).
- type 6 → `rng_mod(2)` (integration test passes randModulus=2; stays green).

### FIXED (orchestrator follow-up) — type-2 modulus now hardcoded 7

Binary truth is `RandomModulo(7)` (VIBE_AiMethod_RandomValue @0x467994:
`mov eax,7; call RandomModulo; and eax,0FFFFh`). The case-2 call site at 0x4a481c
calls `RandomValue` unconditionally — there is NO session-supplied modulus in the
binary. The reconstruction had fabricated a `SessionInputs::randModulus` field and
two goldens baked modulus 5. All fixed to the binary (these test files ARE within
the chunk's scope — "their tests"):
- `src/ai/meisterai3.cpp` case 2 → `e.rng_mod(7)` (was `e.rng_mod(in.randModulus)`).
- `src/ai/meisterai3.h` → removed the fabricated `randModulus` field (binary has none).
- `tests/e2e/meisterai3_e2e_test.cpp` → `13 % 7 = 6` (was `13 % 5 = 3`).
- `tests/integration/meisterai3_itest.cpp` → `RandomModulo(7)` (was `RandomModulo(5)`);
  also dropped the now-removed `randModulus` set in the type-6 test.
Verified: all 87 meisterai3 unit/itest/e2e checks pass (built standalone — the in-tree
`guild` lib was blocked by unrelated `src/play/slice_council.h` breakage from another wave).

### BOUNDARY / KNOWN DIVERGENCE — guard-fail return

1. **guard-fail return value**: when a case's id-match guard fails, the original
   returns `*(actor+16)` = the actor's self entity id (e.g. case 0 sets
   `result=*(v2+16)` before the `if`). `SessionInputs` deliberately does not carry
   the actor pointer / self id, so the existing model returns the session-type
   byte instead. This is an entity-coupled value absent from the modeled inputs;
   the non-editable e2e golden (line 50) enforces the type-byte abstraction.
   Treated as a documented boundary. The matched-case path (which stores the
   scorer result — the meaningful effect) is faithful.

Other documented hooks (entity/command-coupled leaves, unchanged): person-record
lookup, favorability, total-wealth, distance2d, the command emitters
(BuildOp90 / SlotReset28 / Coord27 / delta-packet), per-event tick dispatch,
He-handler filter scans. These remain injected leaves with inert defaults.

---

## Counts

- meisterai.cpp:  5 functions — 5 VERIFIED-1:1, 0 FIXED.
- meisterai2.cpp: 7 functions — 7 VERIFIED-1:1, 0 FIXED.
- meisterai3.cpp: 13 functions — 11 VERIFIED-1:1, 2 FIXED
  (ComputeChoiceWeights; EvaluateSessionDecision moduli — types 1,2,6 all now
  hardcoded to the binary's 2/7/2). 1 documented BOUNDARY (guard-fail return is
  entity-coupled). The earlier type-2 boundary was RESOLVED to RandomModulo(7) in
  source + both goldens.
- Total: 25 functions audited; 23 VERIFIED-1:1, 2 FIXED, 1 boundary note.
