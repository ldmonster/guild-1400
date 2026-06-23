# Hardening — src/sim/npcaction3.cpp

1:1 hardening pass against gilde.exe (imagebase 0x400000) via IDA MCP. Each
provenance'd function was decompiled + disassembled and diffed line-for-line:
switch arms, state transitions, time deltas, member-array strides, RNG draws,
constants (get_global_value on every cited address). DISASM > Hex-Rays where they
disagreed.

## Constants (get_global_value, byte-exact)

| addr | bytes | value | symbol | status |
|------|-------|-------|--------|--------|
| 0x61FB80 | 3f847ae147ae147b | 0.01 (dbl) | kBurglaryStockMul | OK |
| 0x61FB88 | 3a83126f | 0.001f | kBurglaryStockFloor | OK |
| 0x61FB8C | 3c23d70a | 0.01f | kBurglaryGuardMul | OK |
| 0x61FB90 | 3fd6666666666666 | 0.35 | kBurglaryGuardMul2 | OK |
| 0x61FB98 | 3f50624dd2f1a9fc | 0.001 | kBurglaryLootRand | OK |
| 0x61FBA0 | 3b23d70a | 0.0025f | kBurglaryLootMul | OK |
| 0x61FBA8 | 3fb999999999999a | 0.1 | kBurglaryItemMul | OK |
| 0x61FA8C | 3dcccccd | 0.1f | kJailCrowdMul | OK |
| 0x61FA90 | 3c23d70a | 0.01f | kJailStationMul | OK |
| 0x61FA98 | 3fd3333333333333 | 0.3 | kJailEscapeBias | OK |
| 0x61FAA0 | 3fe0000000000000 | 0.5 | kJailFineMul | OK |
| 0x61FAA8 | 3fe6666666666666 | **0.7** | kJailFineRandMul | **FIXED** (was 0.5) |
| 0x61E9A8 | 3f8ccccd | 1.1f | kRecruitMoodCeil | OK |

## GameTime_Advance arg mapping (0x583150)

Verified: `VIBE_GameTime_Advance(rec, edx, ecx, ebx)` where ecx→seconds field,
ebx→minutes field, edx→hours-carrying-to-days. The reimpl `GameTimeAdvance(rec,
addDays, addSeconds, addMinutes)` maps positional arg1→edx (added to hour,
carries to day on 24-wrap). All call sites consistent: `+N min`, `+1 sec`,
`+24h` (state-0 day advance), `+2h` (state-5 contract) all map correctly.

## BurglaryStep @0x4eb518 — VERIFIED-1:1 (with 1 fix)

Switch on (state+2), 8 arms. All transitions/time-deltas verified:
- case 0/1 teardown: single49 + namedObject53 + ChangePlayerAction(0,0,...) + free. **FIXED**: the namedObject53 is emitted unconditionally — when no bldg/storable, the binary still emits the (-1,-1) "Einbruch" fallback (0x4ec6ab). C++ previously skipped it.
- case 2 (state 0): QueryBegin(FilterA+172); +4 min; state 1. Call order ChangePlayerAction→single49→namedObject53 matches.
- case 3 (state 1): wait-at-door; notAllAtDoor → +4 min stay; else +1 sec → state 2.
- case 4 (state 2): violation register → state 3 / no-target → state 5 +15 min.
- case 5 (state 3): packet gate, pair36, state 4 +10 min / else state -1.
- case 6 (state 4): handler still busy → +6 min / else resolve heist (loot, detection), state -1.
- case 7 (state 5): loot path + quad43 + free.
Float-physics leaves (loot valuation, detection roll, gesture target) are the
documented hooks; control flow around them is exact.

## JailCellStep @0x4ea1e8 — VERIFIED-1:1 (with 3 fixes)

Switch on (state+2), 5 arms.
- **FIXED** case 4 (state 2): the cell query keys off **+180** (`QueryBegin(*(a1+180))`, 0x4ea45d), not FilterB(+176). Was `He_FilterB`; now `He_ViolationPk` (+180). Goldens updated to set +180.
- **FIXED** case 0/1 teardown: now emits `single49(member)` + `namedObject53(...,"Entf")` (gated on bldg && storable) + `ChangePlayerAction(bldg,0,0,...)` per member (0x4eaab3..0x4eaaff). Was missing the single49/namedObject53.
- **FIXED** case 2 (state 0): ChangePlayerAction building arg is **null** (0x4ea2f4 `ChangePlayerAction(eax=0, ecx=record)`); namedObject53 name is **"Entf"** (was ""). flag=1.
- case 2: +30 min, state 1. case 3 (state 1): allAtDoor → +1 sec state 2 / else +10 min stay. All verified.
- case 4 escape/fine outcome + final ChangePlayerAction(cell,...) recall + free verified. Detailed cell-occupancy gates (IsObjectSlotActive 380, +101!=-1, +433 flag) folded into the jailEscapeRoll hook — documented boundary.

## RecruitmentState @0x4cce04 — VERIFIED-1:1 (1 boundary noted)

Teardown (state>=0xFFFFFFFE) + active gate + switch over 0/1/4/5.
- State 0 proximity arms (-1024/-1025/-1026, -1027, in-range progress, out-of-range) — all transitions + time deltas (+2 min, +24h hour=8 moved=1 wander) verified.
- State 1 advertising deltas → +1 min entity29(5). Verified.
- State 4 → +2 min entity29(-1), **no re-stamp** (matches 0x4cdc9c). Verified.
- State 5 contract seal: +2h then minute=0, hour clamp [7,22]→11 (>22 also ++day), paired=1, entity29(4). Clamp logic byte-exact (0x4cdc0f).
- BOUNDARY: the active gate's person-class check `*(recA+6)>>24 != *(recB+3)>>24`
  (0x4ccf20) and state-5 reciprocation `*(recA+92)==*(recB+4) && *(recB+92)==*(recA+4)`
  read person-record class/partner high bytes not exposed by any hook. Modeled via
  recruitProximity()>0 / hasCharacter. Genuine hook boundary (no fabricated hook).

## Tests
sim_npcaction3_test + sim_npcaction3_e2e_test: PASS. Goldens updated for the
+180 cell query (2 tests) and kJailFineRandMul.
