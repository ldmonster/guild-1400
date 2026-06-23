# Hardening sweep — sim chunk 06: debugcmd / dragslot / duel

MCP-live 1:1 verification of every provenanced function in:
- `src/sim/debugcmd.cpp`
- `src/sim/dragslot.cpp`
- `src/sim/duel.cpp`

DISASM is the reference of record where Hex-Rays collapsed `__usercall` args /
register reuse. All float/double constants confirmed via `get_bytes`; all
dispatch-table entries confirmed via `get_bytes`.

---

## duel.cpp

### DuelRng::Mod — 0x4a4b30  — VERIFIED-1:1
`v = dword_11AA540[dword_6315E8 % dword_6315E4] % a1; ++dword_6315E8;`. The
binary uses signed idiv for both moduli (positive cursor/len in practice). The
reimpl's `table.empty()/n==0 -> 0` is a defensive guard over the same math. OK.

### Duel_CheckFatalHit — 0x4a4a60  — VERIFIED-1:1 (with float-precision note)
`if (ComputeOutputRatio(unit) >= dbl_61CD94) return 0; dword_6315C4=1; return 1;`
- dbl_61CD94 bytes `9a 99 99 99 99 99 c9 3f` = **0.2** (confirmed).
- ComputeOutputRatio (0x57d384) = `(float)(currentOutput / maxOutput)` — the
  ratio is computed in SINGLE precision in the binary; the reimpl models it as a
  generic `currentHp/maxHp` double. The actual float rounding lives in the
  building module (out of chunk); the gate decision is reproduced. The boundary
  case 0.2 survives in both. Documented; no change.

### Duel_ResolveShot — 0x4a4b68  — FIXED (3 cross-wiring divergences)
Constants confirmed: dbl_61CE24=**0.8** (`9a..e9 3f`), dbl_61CE2C=**1.5**
(`00..f8 3f`), dbl_61CE34=**0.66** (`1f 85 eb 51 b8 1e e5 3f`), dbl_61CE3C=**0.01**
(`7b 14 ae 47 e1 7a 84 3f`). HP/damage float→int sites use `ConvertX`
(truncate) — matched by plain `(int)`.

Register map (disasm): `ecx = shooter`, `esi = target` (action args +0x180 /
+0x17C). Combatant A == dword_11B4E30, B == dword_11B4E34.

The reimpl's flag/score selection was wired to the SHOOTER's own A/B flags. The
binary CROSS-WIRES every gate. Header byte map: rattledA=byte_6315DE,
rattledB=byte_6315DF, aimA=byte_6315E0, aimB=byte_6315E1, scoreA=byte_6315DC,
scoreB=byte_6315DD.

1. **Rattled (disasm 0x4a4bcd..0x4a4be6).** shooter A reads byte_6315DF
   (rattledB); shooter B reads byte_6315DE (rattledA).
   - before: `rattled = shooterIsA ? state.rattledA : state.rattledB`
   - after:  `rattled = shooterIsA ? state.rattledB : state.rattledA`
2. **Aim band (disasm 0x4a4c10..0x4a4c25).** shooter A reads byte_6315E1
   (aimB); shooter B reads byte_6315E0 (aimA).
   - before: `aim = shooterIsA ? state.aimA : state.aimB`
   - after:  `aim = shooterIsA ? state.aimB : state.aimA`
3. **Score keying (disasm 0x4a4c36 `cmp esi,dword_11B4E30`).** Damage credits
   the byte keyed on the TARGET (esi), not the shooter. target == !shooter here.
   - before: `if (shooterIsA) scoreA += dmg else scoreB += dmg`
   - after:  `if (!shooterIsA) scoreA += dmg else scoreB += dmg`

RNG draw COUNT/ORDER (unchanged, re-verified): RandFloat (hit) → on hit RandInt
(0xF or 0x1E) → the dword_11AA540 replay draw happens in the
fatal/hit/miss tail (in the caller's presentation, not this factored core). The
HP-on-fatal ordering (binary subtracts only on a NON-fatal hit, after the
pre-subtraction CheckFatalHit) lives in cutscene_duel.cpp — see HANDOFF.

### Duel_ResolveTaunt — 0x4a4eb4 choice-2  — FIXED (branch + flag-branch)
Disasm trace: `v33=EvalProductionRating(attacker,4)`, `v35=EvalProductionRating
(defender,4)`, `v36=RandFloat()+v33` (1st draw), `cmp (RandFloat()+v35) >= v36`
(2nd draw, `jnb 0x4a5002`). RNG draw order matches the reimpl.
- DEFENDER prevails on `def >= v36` (jnb) → **no** rattle byte.
- ELSE (def < v36, attacker LANDS) → set the rattle byte under the ATTACKER's
  identity: attacker==A(esi==E30) → byte_6315DE (rattledA) @0x4a503f;
  attacker==B → byte_6315DF (rattledB) @0x4a5166.
- before: flag set inside `if (def >= v41)`, returned kTauntFailed there.
- after:  flag set in the `else` (attacker-win) branch; `def>=v36`→kTauntFailed
  (no flag), `def<v36`→kTauntLanded (rattle attacker's own byte). The cross-read
  in ResolveShot then penalizes the DEFENDER's next shot (net: loser rattled).

### Duel_ResolveAim — 0x4a4eb4 choice-3  — VERIFIED-1:1
`v34=EvalProductionRating(self,2); if (RandFloat() >= v34) miss; else set aim`.
Setter is NOT cross-wired: self==A → byte_6315E0 (aimA) @0x4a5208. Reimpl
`if (roll>=ownRating2) miss; else playerIsA?aimA:aimB` matches. (The cross-wire
is on the READ side, fixed in ResolveShot.)

---

## dragslot.cpp

### DragSlotResetTable — 0x41f9dc  — VERIFIED-1:1
Writes E4[3..18]/E8[3..18] (E4=F0-12, E8=F0-8); slot=result/3-1. All 6 slots
key=-1/accum=0. Returns 18*4=72.

### DragSlotCountUsed — 0x41fa00  — VERIFIED-1:1
`for(i=0;i!=18;i+=3) if(F0[i]!=-1) ++v0`. slot=i/3.

### DragSlotAddItem — 0x41f880 (eax=key,edx=qty)  — VERIFIED-1:1
qty==0 → returns the untouched key. Locate-by-key then first-free; ACCUMULATE
`accum += qty`. Returns the DWORD index 3*slot, or 6 when full.

### DragSlotRemoveItem — 0x41f900 (eax=key,edx=qty)  — VERIFIED-1:1
Subtract qty; free (key=-1) when total hits 0. Returns BYTE offset result*4
(72 when absent, 0 when slot0 holds the key).

### DragSlotStoreItem — 0x41f95c (eax=key,edx=qty)  — FIXED (return value)
Disasm 0x41f9ae-0x41f9b8: `mov edx,eax; shl eax,2; sub eax,edx; shl eax,2`
(eax = 12*slot) is computed BEFORE the writes and that SAME eax is the return.
The Hex-Rays `return v4` collapsed the register reuse.
- before: `return v4;` (slot index 0..6)
- after:  `return v4*12;` on a successful store (BYTE offset 12*slot); `return
  v4;` (==6) only on the full-table path.
Header + golden updated. OVERWRITE semantics and qty==0-free unchanged.

---

## debugcmd.cpp

### DebugCmdRandomModulo — 0x58b89c  — VERIFIED-1:1
`(n==0)?0 : (int)RandNext() % n`. `int % u16` promotes u16→int (signed %),
matches `(int)RandNext() % a1`.

### DebugCmdScaledGold helper  — VERIFIED-1:1
`(int)((double)wealth * ((double)roll+base) * scale)`; ConvertX truncates →
plain `(int)` truncates. Match.

### DebugCmdSpawnEntityScaledA — 0x5712a0 (index 0)  — FIXED (emit order)
dbl_62541C bytes `7b 14 ae 47 e1 7a 84 3f` = **0.01** (confirmed). Roll%3,
gold=wealth*(roll+1)*0.01, gate `!RecordById → 1`.
- Binary emit order (0x571323→0x57133a→0x57138d): RenderFormattedMessage →
  **QueueRequest16 → He_SendEntityMessage**.
- before: `sendEntityMessage(...)` then `queueRequest16(...)` (REVERSED).
- after:  `queueRequest16(...)` then `sendEntityMessage(...)`.

### DebugCmdSpawnEntityIfNotType7 — 0x571794 (index 4)  — VERIFIED-1:1
dbl_62544C bytes `7b 14 ae 47 e1 7a 84 3f` = **0.01** (confirmed). Gate
`GroupFromCode(HIBYTE(rec+353))==7 → 1024` (host pre-maps buildType to the group
code; GroupFromCode is a building-type leaf, out of chunk). Roll%4, gold=
wealth*(roll+1)*0.01, emit order queue16→send already matched the binary.

### DebugCmdSendEntityWithFlagA — 0x5737b0 (index 32)  — VERIFIED-1:1
`amount = RandomModulo(3)+2` rolled BEFORE the find; `!RecordById → 1`;
`amount > rec[101] → 1024`; send then RequestBuildOp90(**-amount**, id). Order
matches (send 0x57384f → op90 0x57385e).

### DebugCmdSendEntityWithFlagB — 0x573870 (index 33)  — VERIFIED-1:1
Find first (`!RecordById → 1`, no roll consumed); `v3 = RandomModulo(3)+3`;
`v4 = MultiplyByRate(1, market)`; send then RequestBuildOp90(**+v3**, id).

### DebugCmdDispatchByType — 0x5711ec  — FIXED (signed gate / negative type)
Disasm 0x5711f3 `cmp cl,0x2E; jge` is SIGNED. 0x57120a `movsx ecx,cl; call
funcs_57120D[ecx*4]` uses a SIGN-EXTENDED index.
- before: `if (type >= 46 || type < 0) return 64;` — negatives wrongly → 64.
- after:  `if (type >= 46) return 64;` (signed). A NEGATIVE type is NOT rejected;
  it falls through to the funcs_57120D call. With a real person (kind<10) the
  reimpl routes to the deferred-handler stub (0). Gates re-ordered to match
  (type → person-null → kind).
- BOUNDARY: for `type < 0` the binary performs an OOB `funcs_57120D[neg]` call
  into data preceding the table — out-of-tree, not faked (Rule 8). The gate
  decision (negatives are NOT 64) is reproduced; the OOB target is not.

### DebugCmdNpcTableEntry mapping  — VERIFIED-1:1 (table bytes)
funcs_5766CB @0x63d964: [0]=0x5712a0 ScaledA, [4]=0x571794 IfNotType7,
[32]=0x5737b0 FlagA, [33]=0x573870 FlagB — all confirmed via get_bytes.

---

## Tests updated (to the binary; goldens recomputed via the real CutsceneRng LCG)

- `tests/unit/sim_combat_test.cpp` — DuelTauntAndAimFlags (flag branch flipped),
  DuelAimNarrowsDamageBand (aimB cross-wire), DuelShotMissAndHitScoring (scoreB
  cross-wire).
- `tests/e2e/sim_combat_e2e_test.cpp` — PistolDuelToDeath (cross-wired model:
  A's taunt/aim no longer affect A's own shots; recomputed shots
  29/37, scoreB=66, 2 rounds), RattledPenaltyAltersShot (set rattledB to rattle
  A's shot).
- `tests/unit/sim_dragslot_test.cpp` — StoreItem returns 12*slot byte offset.
- `tests/unit/sim_cutscene_process_test.cpp` — DispatchByType(-1) → 0 (signed
  gate, not 64).

## HANDOFF (files outside this chunk)

- `tests/unit/sim_cutscene_types_test.cpp` (cutscene_duel owner):
  DuelProcessIntroChoiceShootHit golden changed from `scoreA==dmg` to
  `scoreB==dmg && scoreA==0` (target-keyed score). Edited + verified passing.
- `src/sim/cutscene_duel.cpp` (cutscene_duel owner): two integration nuances the
  binary has but the factored reimpl does not — (a) on a FATAL hit the binary
  does NOT subtract the target's HP (it branches to the death anim), whereas
  Duel_ResolveShot subtracts unconditionally on a hit and CheckFatalHit is then
  called on the post-subtraction HP; (b) the taunt call passes `actor.skill` /
  `target.skill` but the binary uses EvalProductionRating(_,4) for BOTH the
  attacker and defender terms. Both are caller-side; left for that owner.

## Counts
- VERIFIED-1:1: 11 (Mod, CheckFatalHit, ResolveAim, ResetTable, CountUsed,
  AddItem, RemoveItem, RandomModulo, ScaledGold, IfNotType7, FlagA, FlagB,
  NpcTableEntry mapping) — note FlagA/FlagB/IfNotType7 + table = several.
- FIXED: 6 (ResolveShot ×3 cross-wirings as one fn, ResolveTaunt, StoreItem,
  SpawnEntityScaledA order, DispatchByType signed gate).
- BOUNDARY: 1 (DispatchByType negative-type OOB call).

All affected unit/integration/e2e targets rebuilt and green:
sim_combat_test (293), sim_combat_e2e_test (54), sim_dragslot_test (63),
sim_dragslot_itest (19), sim_dragslot_e2e_test (11),
sim_cutscene_process_test (100), sim_cutscene_process_e2e_test (25),
sim_cutscene_types_test (268).
