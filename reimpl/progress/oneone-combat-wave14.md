# Wave-14 1:1 fidelity audit — sim combat cluster (W14-COMBAT)

Scope: `src/sim/combat*.{h,cpp}` + their unit/integration/e2e tests. MCP was DOWN,
so this is an in-tree 1:1 audit: provenance inventory, golden-value pinning,
internal-consistency check, confidence map, and a NEEDS-LIVE-MCP binary-diff queue.
Builds against `build/` (GUILD_BACKEND=OFF). All combat unit tests green.

## Method

Extracted every `// gilde.exe 0xXXXX — ...` provenance header across the cluster,
cross-checked each function's recovered constants/offsets/control-flow against its
provenance comments and the wave-12 hardening doc, and confirmed each recovered
1:1 VALUE is asserted by a golden test. Added the one missing golden suite
(`combat_strength`, which had NO test file). No source edits were needed (no
evidence-backed drift found; every constant already matches its provenance note).

## Inventory (function -> gilde.exe address)

Every reconstructed function in the cluster carries a provenance address. NO
address-less functions are reached by the cluster (no RED FLAGS).

### combat.cpp / combat.h
- 0x58b89c Math_RandomModulo
- 0x4ac9e8 CutsceneRng::RandInt        (LCG 1103515245*s+12345; ((s>>16)%0x7FFF)%n)
- 0x4aca48 CutsceneRng::RandFloat      (same LCG; *2^-15)
- 0x486430 CombatField::FindUnitById   (linear scan, bound 17152)
- 0x4bfab4 ApplyMeleeHit               (hp -= RandomModulo(60)+40)
- 0x48c790 ApplyUnitDeath
- 0x4864a0 DistanceToTargetXZ
- 0x48ad54 FindNearestEnemyUnit

### combat_action.cpp/.h
- 0x490a80 PerformAttackAction (decision core)
- 0x48c96c ResolveMeleeHit (rules core)

### combat_battle.cpp/.h
- 0x58b910 RandomFloatScaled
- 0x485dc0 GetSoundRangeScale (weapon-type weight switch v8)
- 0x48be60 ScoreUnitForRole
- 0x48bba8 ComputeUnitBalanceWeights / ComputeBalanceIndex (index derivation)
- 0x48bfb0 AssignUnitsToRoles
- 0x48c24c BuildOrderForUnit
- 0x491688 (state 2 LABEL_46) attack range gate + hit-chance + dmg
- 0x48cf6c EvaluateBattleOutcome
- 0x490014 auto-resolve branch (dword_6315BC fast path)

### combat_drivers.cpp/.h
- 0x48e5b9.. ValidateRoster (unit-counts-when-id rule)
- 0x48e4e4 / 0x48e641 RunResultScreen winner pick (forcedWinner dword_6311F0)
- 0x48e93b/0x48e9df loser-rank classification
- 0x490014 RunBattleSetup auto-resolve score accum
- 0x48dab0 BuildDeploymentScreen AI decision
- 0x48e23a human offer-pick path
- 0x48c15c IssueOrdersForTeam, 0x4882c0 UpdateOrderSlots refresh
- 0x488874 AssignSelectedTarget
- **0x48980c SetUnitFormationMode** -> FindFirstFreeFormationSlot
- **0x489820/0x489b69 FormationModeOpcode** (mode 1->342, mode 2|3->344, else -1)
- 0x48c400 UpdatePursuitTargets -> DrivePursuitTargets

### combat_escape.cpp/.h
- 0x4865ec Coord_Distance3D, 0x486524 DistanceObjectToTarget
- 0x48ae54 ComputeTileThreatScore
- 0x48b01c FindSafestTileInRange, 0x48b1b4 FindMostThreatenedTile
- 0x48b350 EscapeTileCallback, 0x48b384 FindNearestEscapeTile

### combat_loop.cpp/.h
- 0x4905bc TickBattleState, 0x492c28 RunBattleLoop
- 0x489ba8 LoadScenarioAssets, 0x48a7b4 UnloadScenario
- 0x48dab0 BuildDeploymentScreen, 0x48e4e4 RunResultScreen (mercenary-payout flags&4)

### combat_orders.cpp/.h
- 0x491688 non-attack order tick (states 1,3,4,5,6,7,8)
- 0x490f18 StandUpUnitAction, 0x490fa8 PickUpFromGroundAction, 0x490f40 PlayCelebrateGesture
- 0x48cf30 CaptureUnitAction
- 0x48b488 WareObjectCallback, 0x48b5a4 ConquerObjectCallback
- 0x48d4d8 ClassifyTileType, 0x486460 GetSelectionFlag
- 0x4897e0 CountActiveSlots, 0x57e4c8 IsTargetUnderfull, 0x57e714 AssignGuardTarget
- 0x57829c SelectBeatingTarget (RNG-driven), 0x486648 SpawnBomb (table-slot alloc)

### combat_orders2.cpp/.h
- 0x491688 UpdateUnitOrders (driver), 0x48aa45 ranged-vs-melee test
- 0x48a9cc BuildDummyTargetUnits, 0x48857c CreateOrderSlotWindows
- 0x48d518 BuildObjectiveIcons, 0x48d7f8 BuildUnitRosterPanel
- 0x490761 slider X-centering expression

### combat_packets.cpp/.h
- 0x495874 RequestBuildOp80 (opcode 80; staging@+0x14; cut target@+0x40)
- **0x488a4c BuildAttackPacket** (weapon-class switch 340/342/344/350/352/366/370/372/374)
- **0x488c8c BuildMoveToPacket** (kind 3), **0x488edc BuildTilePacket** (kind 7),
  **0x488fa4 BuildSimplePacket** (kind 8)

### combat_projectile2.cpp/.h
- 0x487760 arrow-spawn cadence gate, projectile lifetime tick, throwing-knife (370)
  AIM/transparency rule + nearest-enemy scan

### combat_recon_balance.cpp/.h
- **0x48b744 InitDefaultParameters** (the 180-float 6x5x6 balance grid; scale 0.01,
  cumulative pass, dedup pass; returns 840)
- **0x5d8e80 Physics_Update** (17*a1 stride; HIWORD velocity bitfield; the
  negative-velocity arithmetic-shift recovery)

### combat_slots.cpp/.h
- 0x57e76c FindAvailableSquadSlot (768-slot table, stride 536/0x218)
- 0x485ffc FindUnitByEntity, 0x487090 GetUnitTarget (unit+512)
- 0x57e92c ResolveTargetObjekt (typeTable stride 589, signed person-idx multiply)
- 0x57e9a4 PickActiveTargetEntry (256-entry ring, stride 169)

### combat_slots2.cpp/.h
- 0x57eb64 AccumulateThreatStats (cash->tier rounding)
- 0x48b4d4 FindNearestWareObject, 0x48b5d8 FindNearestConquerTarget
- 0x49080c AutoIssueRetreatOrders (retreat morale gate)

### combat_slots3.cpp/.h
- 0x485a54 FindObjectDef, 0x485b1c FindActiveTarget, 0x485b94 ResetObjectHighlights
- 0x4870f0 CloseSelectionWindows, 0x48751c ResetDamageNumberTable, 0x487548 DestroyDamageNumbers
- 0x488828 DestroyOrderSlotWindows, 0x489578 GatherPlayerUnits, 0x48938c ResetSelectionState
- 0x48c15c IssueOrdersForTeam, 0x48c5e8 RunBattleFrameLoop, 0x48c648 RunOrderWaitLoop
- 0x492e6c BeginBattleOrCacheState

### combat_slots4.cpp/.h
- 0x4864a0 DistanceToTargetXZ, 0x485c0c TriggerEscapeAction
- 0x487300 SpawnDamageNumber, 0x48736c UpdateDamageNumbers
- 0x491324 EvalUnitAttackMove, 0x4bfb68 ProcessShotAndBomb
- 0x48c400 UpdatePursuitTargets, 0x57ea8c ResolveTargetEntityRef
- 0x48c708/0x4a4944 blood-pool spawn y-offset

### combat_slots5.cpp/.h
- 0x486430 FindUnitById, 0x57e50c FindNearestEnemyTarget, 0x4872a0 RefreshHealthBars
- 0x4876d8 StartCutscene, 0x48ac70 SelectIntroTrack (music-bucket ladder)
- 0x48ac0c PlayIntroCutscene, 0x48ace8 PlayOutroCutscene
- 0x48cdc8 DropBombAction, 0x48ce88 ThrowBombAction, 0x4897bc RegisterFlagCallback

### combat_strength.cpp/.h
- **0x48d160 ComputeAttackerStrength** (raid-loot; misnamed by IDA auto-namer)
- **0x48d318 ComputeDefenderStrength** (raid-loot)

## 1:1 VALUE PINS (the brief's targeted values)

| Value / table | Source | Pinned by |
|---|---|---|
| **Strength/balance grid** (180 floats: mode0 `10,10,0,0,30,50`...) | combat_recon_balance.cpp `kDefaultParams` | `CombatReconBalance.InitGoldenTable` (post-processed `kGolden`), `InitReturnValue`==840, dedup/cumulative tests |
| Balance scale const `dbl_61B8BC`=0.01 | combat_recon_balance.cpp | `InitDedupZerosDuplicateColumns` (row0 .1/.2/...) |
| **Loot raid constants** 0.001/0.07/0.01/0.5 | combat_strength.h | **NEW** `CombatStrength.RecoveredConstants` |
| Attacker base-cash truncation `(int)((roll*.001+.07)*cash)` | combat_strength.cpp | **NEW** `AttackerBaseCashOnly` (700 from roll 0) |
| Attacker per-ware `qty=max(1,(int)((roll+80)*.01*q))` | combat_strength.cpp | **NEW** `AttackerWareQuantityAndMarketAccum` |
| Defender `qty=(q*rate>=1)?(int):1`, no-commander *0.5 + cmd, seller -1 | combat_strength.cpp | **NEW** `Defender*` (4 tests) |
| **Packet wire layout** opcode 80; a1@+0x10; staging@+0x14; cut@+0x40 | combat_packets.cpp | `sim_combat_packets_test` (get32 at exact offsets) |
| **id+4 record refs** (target+4 / attacker+4, two's-comp wrap) | combat_packets.cpp `Plus4` | `sim_combat_packets_test` (0x2004/0x5004/0x6004) + `BuildSimpleHighTargetWraps` (0x7FFFFFFD->0x80000001) |
| **Packet kinds** attack=2 move=3 tile=7 simple=8 (LOBYTE stage[1]@+0x18) | combat_packets.h `OrderPacketKind` | `sim_combat_packets_test` (bytes[0x18] per builder) |
| **Projectile negative-velocity shift** (HIWORD velocity, signed `>>16`) | combat_recon_balance.cpp `Physics_Update` | `PhysicsSignedVelocityArithmeticShift` (-100,-7), `PhysicsExtremeSignedVelocityNoUB` (INT16 min/max) |
| Physics stride `17*a1`, gateB +6 offset | combat_recon_balance.cpp | `PhysicsEmptyBankReturnsByteOffset` (51), `PhysicsVelocityWhenGateBSet` (+6) |
| **Formation-mode handling** (0x48980c) mode1->342, 2|3->344, free-slot scan | combat_drivers.cpp | `CombatDrivers.FormationModeOpcode` + `FormationModeOpcodeFullByteRange` + free-slot tests |
| **Slot/unit record offsets** stride 536, flags 0x2000, +0x164/+0x165/+0x16C | combat_slots.h `SlotRecord` (static_assert 536) | `combat_slots_test` + compile-time static_assert |
| Unit-target offset unit+512 | combat_slots.cpp GetUnitTarget | `combat_slots_test` |
| Ring stride 169 / count 256 / typeTable stride 589 | combat_slots.h | `combat_slots_test` (`PickActiveTargetEntry`) |
| Squad block 215 dw / order-slot stride 11 / base 37 | combat_slots.h | `combat_slots_test` (`FindUnitByEntity`) |
| **Escape/strength math** Distance3D/threat score/spiral sweep | combat_escape.cpp | `sim_combat_escape_test` |
| Pursuit `scaled>=20 OR Roll(100)<=30` | combat_drivers.cpp | `combat_drivers_test` |

## Internal consistency

Checked every recovered constant/offset against its provenance comment and the
wave-12 doc. All match. Specifically verified:
- `kDefaultParams` 180-float block matches the per-mode comments and the `kGolden`
  independently-computed post-processed table (scale 0.01 + cumulative + dedup).
- `Physics_Update` HIWORD-velocity assembly = the wave-12 UB fix; signed `>>16`
  recovery is byte-identical (golden -100/-7 unchanged).
- `Plus4` wrap = the wave-12 packet-builder fix; applied at all `+4` sites.
- `SlotRecord` `static_assert(sizeof == 536)` enforces the 0x218 stride; field
  offsets (0x164/0x165/0x16C/0x1C8) match the comment table.
- Strength constants in the header (0.001/0.07/0.01/0.5) match both the `.cpp`
  usage and the strength.h .rdata recovery note.

No drift found -> NO source edits (per the brief, source edits only for clear
evidence-backed drift).

## Confidence map

**GOLDEN-PINNED** (recovered values asserted by a golden, verified consistent):
- combat_recon_balance (InitDefaultParameters table + Physics_Update velocity)
- combat_packets (all 4 builders + RequestBuildOp80 wire layout + Plus4 wrap)
- combat_strength (NEW this wave — both raid-loot functions + 4 constants)
- combat_drivers (FormationModeOpcode, free-slot scan, pursuit roll)
- combat_slots (record offsets via static_assert + ring/squad strides)
- combat_escape, combat_orders, combat_orders2, combat_slots2/3/4/5,
  sim_combat (CombatField/melee/death), sim_combat_battle (role/balance index),
  wire_combat / wire_combat2 / wire_combat_bomb.

**UNDER-VERIFIED** (translated, golden-covered for observable behavior, but the
full decompile-vs-source diff still wants live MCP): none material — every owned
function has at least one golden. The presentation-coupled bodies (cutscene/HUD/
scene-spawn) are intentionally DEFERRED (rule 8) and documented in each .cpp's
DEFERRED list, not counted as reconstructed-but-unverified.

**NEEDS-LIVE-MCP** (exact decompile targets to byte-diff when MCP returns):
1. 0x48d160 / 0x48d318 ComputeAttacker/DefenderStrength — confirm the exact
   command-emission order (QueueRequest17 vs EnqueueCmd15) and the family-record
   `+7` dword offset against the live decompile (currently from the strength.h
   recovery note; the raid e2e path is not yet asset-driven).
2. 0x48b744 InitDefaultParameters — re-verify the raw 180 float bit-patterns and
   the dword_B59C40 stride math (120/24/4) and flt_B59C3C[-1] non-read.
3. 0x5d8e80 Physics_Update — confirm dword_1406420 slot stride (17) and the a1
   index bound (currently unbounded = the binary's own envelope, per wave-12).
4. 0x488a4c BuildAttackPacket — verify the full weapon-class switch arms
   (350/352/372/374) field set and the *(target+452) in {1,2,3} secondary gate
   and the HUD-banner early-outs (dword_8C6F20/24/28).
5. 0x495874 RequestBuildOp80 — confirm ComputePacketSize(0x50)==68 and the
   *dword_6315C0 cut-target read.
6. 0x48980c SetUnitFormationMode — confirm the per-mode opcode pair (344,352)
   emission and the +128 16-slot array stride.
7. 0x57e76c FindAvailableSquadSlot — re-verify the register byte-lane juggling
   (HIBYTE wantType / >>24 wantFlag) and the -30 relation threshold.
8. 0x57e92c / 0x57e9a4 ResolveTargetObjekt / PickActiveTargetEntry — confirm the
   signed `589*(char)entry[0]` type-table multiply (negative-index envelope) and
   the typeCode set {3,22,15} and the QueryFind kinds {18,19,288}.
9. Combat-strength `+7` family offset and `commanderCash` =
   VIBE_Person_SumCurrencyHeld — confirm against the live person-record layout.

## Changes this wave

- ADDED `tests/unit/sim_combat_strength_test.cpp` (7 tests, 39 checks) — the
  previously-untested combat_strength cluster. Pins the four recovered .rdata
  constants and the documented attacker/defender raid-loot math, the max(1,..)
  quantity clamps, the command-sink emission order, the no-commander 0.5 fraction
  + bulk-cash command + seller=-1, and the family-credit/empty-stockpile guards.
  All values trace to combat_strength.{h,cpp}'s own recovery (no invented values).
- NO source edits (no drift found). All existing goldens kept byte-identical.

## Build status

`build/` (GUILD_BACKEND=OFF): all combat unit tests green, including the new
`sim_combat_strength_test`.
