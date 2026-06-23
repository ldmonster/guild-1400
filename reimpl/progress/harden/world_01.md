# Wave-H1 1:1 Hardening — chunk world_01 (rollup)

Chunk = 22 .cpp files under `src/world/` (election / guild / event / exchange / estate /
family / gesetz / groundplan). Every provenance-bearing function was MCP-decompiled and
diffed line-for-line against gilde.exe (disasm used wherever Hex-Rays collapsed
`__usercall` args / strides / float→int sites). Per-cluster detail lives in the sibling
files listed below; this is the rollup.

## Cluster reports (full per-function detail + evidence)
- `world_01_election.md`   — election_candidacy, election, election_form, guild_election
- `world_01_guild.md`      — guild, guild_assignment, guild_rank
- `world_01_eventcore.md`  — event, event2, event_bindings, event_effects, event_fire
- `world_01_event34.md`    — event3, event4
- `world_01_event5.md`     — event5
- `world_01_exchange.md`   — exchange, exchange_loop, estate_transfer, family_query
- `world_01_gesetz.md`     — gesetz_flow, gesetztable_law_recon, groundplan_recon

## Totals
- Functions audited: ~106 (incl. helpers).
- VERIFIED-1:1: ~80
- FIXED (divergence corrected to the binary): 29
- BOUNDARY (rule 8 hook / data not in tree): documented per cluster.

## FIXED — headline divergences (cite addr + evidence; details in cluster files)

election:
- CollectGuildCandidates 0x480e4c — incumbent id read from entry **+4** (not +20); vacant
  winner seed **0** (not INT_MIN); rank-distance gate `abs(sar 24) < 6` recovered; member
  exclusion added. (evidence: VIBE_Office_TransferHoldership 0x47e870, loc_4810C6 xor.)
- ElectGuildMasterFull / CalcZunftElection 0x481228 / 0x4813d0 — winner-pick must seed from
  the **incumbent's** ComputeTotalWealth (or 0), replace only on strictly-greater; recon
  seeded cand[0] so it always reported a winner and installed candidates poorer than the
  incumbent. Goldens corrected (winnerId → -1 when incumbent is wealthiest).

guild:
- AssignGuildMembers 0x480634 — category-key table corrected to **{6,5,4,3,2,1}** (aliases
  byte run @0x47DE70 via qmemcpy spill); member-branch gate was inverted (secondary vs
  primary). PersonHasOfficeObject 0x480abc — scans CollectByCategory holder buffer +4, not
  the office-object table. HasOccupiedOffice 0x480cb4 — budget decremented only on a vacant
  (city==-1) seat. ComputeGuildAssignment 0x47ff5c — WIN per-voter loop guarded by
  RecordById non-null. AssignSlotData 0x56ea50 — missing `+16 dword = -1` write added.

eventcore:
- HelpTextPlaybackRun / HelpAdviceLoopRun 0x4f1ac4 / 0x4f1c2c — He+172 cursor is a 32-bit
  dword (`++*(_DWORD*)`), was read/written via a u16 accessor (truncation).
- FireRaidDurationFactor 0x4ee804 — scaled distance round-trips through a 32-bit float.
- Descriptor table @0x63CD48 (1152 B), name table aNone_0 @0x64A7FC, all fire floats/doubles
  confirmed byte-exact.

event3/4:
- AllocKillPlayer 0x4efcdc — control flow was inverted + operating on wrong record (free
  SELF on match / reset SELF on no-match). WorkActionRun 0x4f2614 — rate is float; member
  loop 8 slots @+140; drain factor `(2·+20 + +24)`. HarvestWageRun 0x4f3b34 — objId is
  unsigned HIWORD. UpdateBuildingHeState 0x4f52f8 — no restore() on non-{1,2} segment counts.
  UpdateListenerFromActor 0x4f5270 — extended-precision float add.

event5:
- RunGebaeudeBauen — per-step divisor byte **+0x243**; model handle byte **+97** (4 sites);
  case-9 div/mod reads `*(a1+192)`. ReArmEntity29 — removed spurious `++Dword(h,112)`.
  RunProduktion — favorability loop 8 members. SlotProcessRun — `shr` (unsigned >>2);
  `__int16*` field strides (bytes 2/2/14, not 4/4/28); final id compares **source+1**;
  owner-chain `&& !FindOwnerChain` branch restored (hook return type changed void→i32).
  RNG draw order in DiscoveryRaid + loot ConvertX truncation verified 1:1.

exchange/estate:
- ExchangeCourier 0x51cbdc / ExchangeGoodsTrade 0x51bb4c — span/fee/value are 32-bit floats;
  min-A branch re-reads PriceByRate(1, **take** currency). ConvertX (RC=11, round-toward-zero)
  truncation confirmed at every net leg. dbl_6220D0=0.03 / dbl_622068=0.01 byte-confirmed.
- PersonTransferEstateOwnership 0x58c4a8 — family-link fixup compares **fromPtr[0x50] vs
  toPtr[0x50]** (recon had always-false self-compare); ComputeRankWithinGroup called exactly
  **twice** (was three).

gesetz/groundplan:
- GesetzLoadState 0x4c28d8 — clear-remainder loop clears per-record {+0,+18,+22:-1; +37,
  +26(word):0}; recon cleared +33 (target, never touched by binary) and missed +18. Golden
  added. All other gesetz/groundplan funcs (0x4c247c/4c24f8/4c258c/4c25e0, 0x5384d0,
  0x4ae824/4af464/4ae59c) VERIFIED-1:1 incl. grid stride 536, table stride 24.

## Build / verification
- All 22 chunk .cpp files compile clean (`g++ -std=c++17 -fsyntax-only -Isrc -Iinclude -I.
  -Ishim -Itests/framework`).
- All touched tests compile clean (unit + election itest/e2e).
- No git commit. progress/INDEX.md untouched.

## Handoffs (out-of-chunk, do not own — flagged for owners)
- `src/sim/he.h:91` types `He_Counter` (He+172) as `u16&`; eventcore confirmed this field is a
  32-bit dword in HelpText/AdviceLoop. Our files use `Dword(h,172)` locally; the shared
  accessor should be widened by the sim owner.
- Full `libguild` link is currently blocked by an unrelated WIP error in
  `src/gui/widget_layout.cpp` (`Widget::ld<>` missing) and an ambiguous `EvaluateAttack`
  overload in `src/sim/combat_battle.cpp` — both outside this chunk; flagged, untouched. Our
  files were verified by standalone syntax-check / object link.
