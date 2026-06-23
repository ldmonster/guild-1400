# Wave-H1 1:1 Hardening — chunk sim_05 (consolidated)

MCP-driven full-tree hardening sweep over the 22 .cpp files of chunk `sim_05`
(plus their headers + golden tests). Every function carrying a `gilde.exe 0xADDR`
provenance was decompiled at its address and diffed line-for-line against the
binary (disasm as reference of record where Hex-Rays was garbled). The chunk was
split across 7 parallel sub-agents (groups A–G); per-group detail lives in
`sim_05_groupA.md` … `sim_05_groupG.md`. This file is the roll-up.

All 22 owned .cpp files pass `g++ -std=c++17 -fsyntax-only` against the project
include roots (`. include src shim`). No commits were made.

## Files by group
- A: command_pending, command_receive, command_recon2_sync, command_recon_syncrange, command_unit_orders
- B: command_recon4_resolve, command_recon4_senders
- C: console_recon, contextaction2, cutscene_auction, cutscene, cutscene_duel
- D: cutscene_misc, cutscene_misc2, cutscene_rand
- E: cutscene_misc3, cutscene_misc4, cutscene_misc5
- F: cutscene_process, cutscene_wedding   (command_receive owned by A)
- G: debugcmd2, debugcmd3

## Totals
- Functions diffed against the binary: ~170 provenance-tagged.
- VERIFIED-1:1: ~147
- FIXED (divergence corrected to the binary; source +/- golden): 26 fixes across ~18 functions
- BOUNDARY (rules 3–5 tech swap, or live engine state not exposed in tree): ~15, documented per group.

| Group | VERIFIED | FIXED | BOUNDARY |
|------|----------|-------|----------|
| A | 9 | 4 (3 fns) | 5 |
| B | 16 | 4 | 1 |
| C | 28 | 3 | inline |
| D | 25 | 3 | 4 |
| E | 28 | 8 | 1 handoff (CutsceneExecution) |
| F | 8 | 5 (3 fns) | 1 |
| G | 30 | 0 | 2 |

## Most important fixes (addr + evidence)

**command_receive / command_recon (A)**
- ExecReceivedCommands @0x494088 — group-close dispatch gate. Binary clears edx
  only when ExecCommandGroup returns 0 (it always returns 1), so the reprocessed
  BEGIN frame IS dispatched. Recon forced gate=0. Source + e2e golden fixed.
- ReassembleReceived @0x49377c — returns dword_11AA478 (reasm_len) + clears each
  consumed fragment's +12 link and the last fragment's +12. Recon fabricated the
  count and dropped the side effects. Source + golden fixed.
- SyncSceneEntryExit @0x500f0c — disasm shows edx=-1 for both EnqObj calls
  (Hex-Rays mislabeled entryEntityId, which is dead). Args corrected.
- SyncCharSlotAssignments @0x501064 — slotByte is a single stack slot hoisted
  above the loop (persists prior iteration); recon reset it each iteration.

**command_recon4 (B)**
- SendEntityActionA @0x5675ac — typeWord indexed by full u16 (`*(u16*)(rec+39)`,
  disasm 0x567745/0x567792), recon truncated to u8 → mis-index for type >= 256.
- ResolveTargetPersonByName @0x4f9518 / ResolveTargetPersonAlt @0x4f989c (mode 2)
  — carried-fallback is first-wins (`v19 || …`), recon was last-wins via a sticky
  flag. Gate changed to `fallback == -1`.
- ResolveTargetRandomCarried @0x4faf54 — inner walk advances index BEFORE the
  count==0 bail (disasm 0x4fb040), so on exhaustion i has wrapped 768× back to
  start; recon advanced after. Reordered.

**cutscene leaves (C/D/E)**
- cutscene_auction lease-split @0x4a94c5 — consts are 32-bit floats flt_61D700
  =0.89999998 / flt_61D704=0.10000000 (not double 0.9/0.1) and the store is
  `fistp` = round-to-nearest-even, not `(int)` truncation. (bid 150 → 135.)
- contextaction2 TrainTier @0x56f710/873/917 — rank-0 false positive; replaced
  the overRank=0 sentinel with explicit kNone/kGreater/kEqual.
- cutscene_duel round order @0x4a5996 — B acts first when B.choice in {2,3};
  matters because duel resolvers draw from the cutscene LCG (stream sync).
- CutsceneDestroySky @0x4aa740 — binary zeroes only mirror dword_64A7C8, leaves
  the sky handle/layer intact. Source + 4 goldens fixed.
- CutsceneLeaseAutoResolve @0x4a9a68 — major retrace (askRent threshold via +ecx,
  no early accept-return, inverted gate, ratio direction, cap, counter). Goldens
  recomputed.
- ComputeProductionTickRate @0x502198 — flt_620D3C @0x620d3c = 0x46BB8000 =
  24000.0f (was 16000).
- CutsceneBirthVoiceBase @0x4a7b5c — father-ill / mother-ill voice indices were
  swapped; CutsceneDeath @0x4a7fdc voice set is {0,2,3,4,5,6} (index 1 skipped).

**cutscene_process (F)**
- ProcessActive @0x4ac31c — re-arm amounts are minutes (+5min/+30min) not seconds;
  the word>=0x16 bump is +9 days not +9 hours; final exec compares windowA not
  windowB (disasm 0x4ac412/0x4ac42c/0x4ac560).
- InitParticipantTable @0x4aa9b4 — copies partIds[i] for all 16 rows when slot
  non-null (no partCount gate). Source + golden fixed.
- CheckMarriageEligible @0x4a7118 — missing kind-6/7 vow speech packet (text 3621,
  cmd28) added in correct order.

## ConvertX / float->int audit (the flagged bug class)
Every float->int site in the chunk was checked against disasm. Confirmed
truncate-toward-zero (== C `(int)`) for: auction ConvertX, ComputeProductionTickRate,
the negated-float AdjustStock in cutscene Execution, DebugCmdScaledGold (ConvertX
@0x5c6b08 sets CW=0x1F00 RC=chop, frndint in place). The ONE round-to-nearest-even
site found and fixed was the auction lease-split `fistp` @0x4a94c5. cutscene_process
and cutscene_wedding have NO float->int sites (integer/GameTime only). debugcmd2/3
constants byte-confirmed (0.01, 2.0, 0.0125, 1/120, 0x3c23d70a).

## RNG draw-order audit
Verified in-sync: cutscene_rand RandInt @0x4ac9e8 (single LCG advance,
1103515245/12345, no advance when range==0); SyncSceneObjectStates rand(3)→rand(8);
EnqueueLawAction rand%8+8 then rand%4; all debugcmd2/3 handler draw counts/orders.

## BOUNDARY / handoffs
- SendMapEntityAction @0x568278 — per-slot scan keys on live owner-ptr column
  dword_12CEA7C / value column byte_12CEAD5 not exposed by current hooks. Needs
  two row-column hooks to restore the full scan.
- CutsceneExecution @0x4a6b90 (group E) — pre-existing major misreconstruction
  (real body loads Richtplatz.ed3 / ArmerDelinquent.mp3, RandInt(3) sky/rain band,
  4 role-template strings, hinrichtung-*.esc switch). kExecutionDurations is dead
  data there. Recommend a dedicated wave (~8 new hooks + 4-record RNG order).
- Render/voice/scene-free pokes throughout cutscene_* remain rules-3-5 (Vulkan/SDL)
  or out-of-tree presentation leaves, documented per group.

## Correction applied during roll-up
Group B created a new test `tests/unit/command_recon4_senders2_test.cpp` that
referenced symbols (`LawActionDef`, `kLawActionTable`, `EnqueueLawAction`,
`EnqueueDuelChallenge`, `CombatSetUnitFormationMode`, `QueueRequestTransform64`)
and addresses (0x4c1810/0x4c2218/0x538224/0x48980c/0x4952b4) that do NOT exist in
`command_recon4_senders.cpp` — those are merely comment-header cross-references to
OTHER modules' work, not provenance of functions in this chunk. The file
(`command_recon4_senders.cpp` actually defines only the five SendEntityAction*
functions) would not build. The bogus test was removed. Group B's legitimate
source fixes to senders.cpp (SendEntityActionA u16 typeWord etc.) and the
pre-existing tracked `command_recon4_senders_test.cpp` are intact and compile.

## Build verification
All 22 chunk .cpp files + all 11 touched/new test files pass
`g++ -std=c++17 -fsyntax-only` against the project include roots (33/33).

## Out-of-chunk note (NOT mine, NOT edited)
`src/gui/widget_layout.cpp` is an untracked WIP file (`git status` shows `??`) that
fails to compile (`Widget::ld<>` / mangled `pD(4)`) and breaks the full `guild`
library link. It is outside chunk sim_05 and was left untouched. Its owner (gui
wave) must repair it; chunk verification was done via isolated `-fsyntax-only`.
