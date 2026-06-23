# Hardening — src/sim/npcaction4.cpp

1:1 hardening pass against gilde.exe (imagebase 0x400000) via IDA MCP. Each
provenance'd function decompiled + disassembled and diffed line-for-line.

## Constants (get_global_value, byte-exact — all OK)

| addr | bytes | value | symbol |
|------|-------|-------|--------|
| 0x61EA24 | 3cc30c31 | 0.0238095f (1/42) | kPatrolWageCoordMul |
| 0x61EA34 | 3f847ae147ae147b | 0.01 | kRaidWorkstationMul |
| 0x61EA3C | 4059000000000000 | 100.0 | kRaidSecurityBias |
| 0x61EA44 | 42c80000 | 100.0f | kRaidDefenderWeight |
| 0x61EA48 | 42fa0000 | 125.0f | kRaidAttackerWeight |
| 0x61FC28 | 3f847ae147ae147b | 0.01 | kAttackWorkstationMul |
| 0x61FC30 | 4059000000000000 | 100.0 | kAttackSecurityBias |
| 0x61FC38 | 42c80000 | 100.0f | kAttackDefenderWeight |
| 0x61FC3C | 42fa0000 | 125.0f | kAttackAttackerWeight |

Raid (0x61EA34) and Attack (0x61FC28) blocks confirmed byte-identical.

## PatrolStep @0x4cdf74 — VERIFIED-1:1

Entry guard (state==-1 free; flag 0x04 → -2 free / else noop; 4-slot member scan
@+196 → none live → +1 min entity29(-1) free; -2 → state 1 + justStarted; +212!=-1
→ state 1024) all verified against disasm.
- state>=2 dispatch: 1024 lap-loop (anyBusy → +4 min NO re-stamp entity29(1024);
  else re-stamp, appt.day=-1 sentinel, +2 min entity29(3)); state 2 wait
  (active member → return; else +2 min entity29(3)); state 3 cursor dispatch
  (pointFree + diff<=480 → new lap cursor=0 ++lap entity29(0); >480 → entity29(1);
  else walk-to-point cursor++ entity29(2)). All time deltas + entity29 args match.
- state 1 (dispatch): home query (FilterB+176); per-member wage roll; justStarted →
  free; else processed → cmd15 + msg(5751); +2 min entity29(-1).
- state 0 (escort): target query (FilterA+172); +2 min entity29(2).
- RNG order verified: state-3 per-member `flag55 = RandomModulo(2)+1` then a
  DISCARDED `RandomModulo(3)` (0x4ce839/0x4ce83a) — count + order exact.
- Member-base stride +196 (`*(v+49)`) used throughout, matches +196 accessor.
Wage curve (flt_61EA24) + the discarded RandomModulo loop are inside the
patrolWageRoll hook — documented boundary; surrounding control flow exact.

## RunSabotage @0x4e2158 — VERIFIED-1:1

Outer +200 packet gate, 9-case switch (states -2..8).
- case -2/-1: pair33(+196,1) → free.
- case 0: seq from +200 → family ledger += +208, +196=seq+4, ChangePlayerAction,
  cmd16(-1, player, +208), +200=-1, ++state / miss → -1.
- case 1: namedObject53("Sabotage"), ++state.
- case 2: +200=-1; rec active → +4 min stay 2 / idle → ++state / no rec → -1.
- case 3: gesture target → violation(23), +204=seq, state 7 / no gesture →
  flag55(+196,1) + namedObject53(+16,"Sabotage") ++state.
- case 4: `GameTimeCompare(&appt, &clock) < 0` (0x4e25fb) → retry=0; roll;
  success → ++state +10 min; failure stays 4. Verified compare direction.
- case 5: retry set → state 6 / else fx hook + re-stamp + `RandomModulo(2)` minutes
  (ebx, 0x4e2d4e) ++retry. RNG draw count for the minute advance verified.
- case 6: rec active → +4 min stay 6 / else -1.
- case 7: violation packet wait; handler +204 maps to this record → pair36, state 8 +10 min / mismatch → -1.
- case 8: handler busy → +6 min / else namedObject53 +4 min state 6 / no handler → -1.
The discovery roll (RatingCurveA + RandomFloatScaled) and the particle/sound/bone
FX (with their internal RandomModulo draws) are the sabotageDamageRoll /
sabotagePlayFx hooks — documented boundaries; the +194 retry gating and all state
transitions around them are exact.

## RaidStep @0x4cea84 — VERIFIED-1:1 (with 1 fix)

state -2 entry (flag 0x02 → state 1 + firstPass, else free); +132 packet gate;
switch over (state+1) cases 0..6.
- case 0 (state -1) teardown (flag 0x02), case 1 (state 0) target escort
  (`personOwnerWord==0xFFFF` → LABEL_23 +1 min entity29(-1)), case 2 (state 1)
  staging (firstPass → free), case 3 (state 2) wait-all (+2 min entity29 3/2),
  case 5 (state 4) packet seq → +184 +1 sec state 5, case 6 (state 5) cutscene
  slot wait (+5 min / +1 sec → state 1). All verified.
- case 4 (state 3) combat resolve via shared CombatCaseResolve(isAttack=false):
  not-detected/escorts → cmd39 state 4; no escorts → strength + msg(6241) +1 sec
  state 1. **FIXED**: detected/"caught" branch sets state **-1** (0x4cf456/0x4cf46f),
  not 5. The shared helper previously hard-coded 5 (correct only for Attack).
  Now `He_State = isAttack ? 5 : -1`. Golden updated.
- BOUNDARY: the cmd39 escort branch in the binary gates on `v34 && v30` (live
  escorts AND townsfolk-witness count from global person columns
  byte_12CE918/dword_12CEA7C). The witness scan is host-side (gathered by the
  bridge); C++ gates on escort count only. Documented boundary.

## AttackTargetStep @0x4ed95c — VERIFIED-1:1

Switch on (state+2), cases 0/1/7 (states -2/-1/5) grouped teardown, 2..6.
- teardown: single49(personId), namedObject53("Angriff"), ChangePlayerAction(0,0,...), free.
- case 2 (state 0): both tgt && home required else free; +10 min state 1.
- case 3 (state 1): wait-all → +1 sec state 2 / else +10 min stay.
- case 4 (state 2): combat resolve (isAttack=true); detected → state 5 (correct
  for Attack, 0x4ee186/0x4ee19f). The get-away quad43 (gated on a matched scene
  node v69) is folded into the roll hook — documented boundary (noted in source).
- case 5 (state 3): packet seq → +184 +1 sec state 4.
- case 6 (state 4): cutscene slot wait (+5 min / +1 sec → state 5).
All verified.

## Tests
sim_npcaction4_test + sim_npcaction4_e2e_test: PASS. Golden
`State3_combatResolve_detected_to_state5` renamed/retargeted to expect state -1
(Raid caught branch).
