# Wave-H1b FIX-CHARACTION — 5 baseline failures fixed 1:1

Owner files: `src/sim/charaction_steps4.{cpp,h}`, `charaction_steps6.{cpp,h}`,
`charaction_misc.{cpp,h}` + their unit/integration/e2e tests.

All 5 targets GREEN:
- `charaction_steps4_test`         PASS
- `charaction_steps4_e2e_test`     PASS (was SEGFAULT)
- `charaction_steps6_test`         PASS
- `charaction_steps6_itest`        PASS (was SEGFAULT)
- `sim_charaction_misc_e2e_test`   PASS

Every fix verified against gilde.exe via IDA MCP (disasm/get_bytes). In all five the
SOURCE was already binary-correct; the frozen GOLDEN/test wiring was the divergence
(plus two tests missing required hook installs, which produced null-call SEGFAULTs).

---

## 1. charaction_steps4_test — GossipSendsRumor (golden was wrong)

Test expected `req16Vals[0] == 20`; source produced 18.

gilde.exe 0x4d0b84 `VIBE_CharAction_GossipBroadcast`, bribe math @0x4d0ce3:
```
v11 = (double)wealth * flt_61EAD4;   // flt_61EAD4 @0x61ead4 = 0x3C23D70A == 0.00999999977f
VIBE_Coord_ConvertX();               // @0x5c6b08: fldcw 0x1F (RC=11 truncate) + frndint -> trunc st0
v20 = (int)v11;
v13 = (int)v11 * (RandomModulo(...) + 2);
```
get_bytes 0x61ead4 = `0a d7 23 3c` -> float 0.01 (NOT exact). With wealth=1000, rnd=0:
trunc(1000.0 * 0.00999999977) = trunc(9.9999997) = **9**; 9*(0+2) = **18**.
The test comment wrongly assumed exact 0.01 (-> 10 -> 20). Source already truncates
correctly (static_cast<int> of the float-widened double). Golden fixed 20 -> 18.

## 2. charaction_steps6_test — InitSpionageAlreadySpawnedNoOp (golden was wrong)

Test wrote the "already spawned" flag at byte **+121**; gate reads **+120**.

gilde.exe 0x4e2e58, @0x4e2e68: `mov ah,[eax+78h]; test ah,4` — reads flag byte at
+0x78 == +120 (`He_Flags`), bit 0x04. Source reads `He_Flags(h) & 0x04` (+120) — correct.
Golden fixed: write `at<u8>(120) = 0x04` (was 121).

## 3. charaction_steps6_test — RunSpionageTerminalStateBails (golden was wrong)

Test expected `ret == -2` (the state). Binary returns the FreeHandlerEntry result.

gilde.exe 0x4e3164, terminal path @0x4e317a..0x4e31b4: for state -1/-2 it does the
optional Single49/Pair33 cleanup, then `mov eax,ebp; call VIBE_He_FreeHandlerEntry`
and returns THAT (eax), not the state. Source returns
`freeHandlerEntry(h)` — correct; matches sibling test `EinstellenTerminalFrees` which
expects the leaf's `freeRet` (7). Golden fixed -2 -> 7.

## 4. sim_charaction_misc_e2e_test — IdleNoNeighbourClearsDirtyMesh (golden was wrong)

Test expected `flagsA & 0x08 == 0` after an eligible idle scan found no neighbour.

gilde.exe 0x405148 `VIBE_Character_Update`, idle branch: the `*(ch+140) &= ~8u` clear
fires ONLY at 0x4054a9, inside the NOT-eligible branch (scene mismatch / +140&2 /
+141<0 / +140&0x20 / +292). The eligible path (FindNearbyInRadius @0x405418) has NO
`&= ~8` — on a no-neighbour scan the function just falls through. So the dirty-mesh bit
is PRESERVED. `character_social.cpp` (not an owned file) already implements this
correctly. Split the bad test into two binary-correct cases:
- `IdleEligibleNoNeighbourKeepsDirtyMesh`: eligible + no neighbour -> +140&8 stays set.
- `IdleIneligibleClearsDirtyMesh`: sitting (+140&0x20) -> ineligible branch clears +140&8.

## 5. charaction_steps4_e2e_test — SEGFAULT (test missing hook installs)

Crash: null call at charaction_steps4.cpp:276 `k.randomModulo(8)` in `DuelArmCombatant`
(gilde.exe 0x4cfab4). The e2e `Install()` reset the hook struct to `{}` and never set
`randomModulo` (also unset: `resolveEntityById`, `queueRequest16`). Added `ERnd`,
`EResolve`, `EReq16` stubs and wired all three. The unconditional RandomModulo draw at
0x4cfb7b (arg eax==8 left from `mov eax,8` before ConvertX) is faithful — it MUST run to
keep the RNG stream in sync; source already does it.

## 6. charaction_steps6_itest — SEGFAULT + follow-on golden

(a) SEGFAULT: `InitSpionageAlreadySpawnedSkipsRng` wrote the spawned flag at byte +121
(should be +120, see #2). The gate missed, execution fell through to the LABEL_19
`queueRequestEntity29` leaf hook which the itest never installed -> null call. Fixed the
test byte 121 -> 120.

(b) Once the crash was gone, `InitSpionageSeedsFromRealRng` failed at the stride check.
The itest's local `kStride[16]` mirror was wrong. gilde.exe @0x4e2ed8
`mov eax, dword_478450[eax*4]` — get_bytes 0x478450 (16 dwords) =
`1,3,5,7,0xb,0xd,0x11,0x13,0xed,0xef,0xf3,0xf5,0xf9,0xfb,0xfd,0xff` i.e.
`1,3,5,7,11,13,17,19,237,239,243,245,249,251,253,255`. Source `kSpyStrideTable`
(charaction_steps6.cpp:438) already matches the binary exactly. Fixed the itest mirror
to the binary bytes (was `1,3,5,...,31`).

---

## Evidence index (addresses)
- 0x4d0b84 GossipBroadcast; flt_61EAD4 @0x61ead4 = 0x3C23D70A; ConvertX @0x5c6b08 (RC=11 trunc)
- 0x4e2e58 InitSpionage; flag gate @0x4e2e68 (+0x78 & 4); stride table dword_478450 @0x478450
- 0x4e3164 RunSpionage; terminal free @0x4e317a..0x4e31b4; FreeHandlerEntry @0x4c6144
- 0x405148 VIBE_Character_Update idle branch; clear @0x4054a9 (NOT-eligible only)
- 0x4cfab4 DuelArmCombatant; RNG draw @0x4cfb7b

No source logic changed (all five sources were binary-correct). Changes were limited to
owned test files: golden values, a flag-byte offset, a stride-table mirror, and two
missing hook installs. NO git commands run. progress/INDEX.md untouched.
