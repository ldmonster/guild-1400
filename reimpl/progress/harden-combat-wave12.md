# Wave-12 hardening — sim combat cluster (W12-COMBAT)

Scope: `src/sim/combat*.{h,cpp}` and their unit/integration/e2e tests. MCP was
DOWN for this wave, so NO new 1:1 reconstruction — hardening only (memory-safety /
UB fixes that the original binary did not exhibit, plus boundary/malformed tests).
Goldens and valid-input behavior kept byte-identical.

## Build / method

ASAN+UBSAN build in an isolated dir to avoid racing sibling agents:

```
cmake -S . -B build-asan-combat -DCMAKE_BUILD_TYPE=Debug -DGUILD_BACKEND=OFF \
  -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-sanitize-recover=all -g"
cmake --build build-asan-combat -j$(nproc) --target <combat test targets>
```

All combat unit + integration + e2e targets built and run under ASAN+UBSAN.
Result: green (after the fixes below). The asset-guarded itests/e2e skip cleanly
when `GUILD_GAME_DIR` is absent and pass when present.

## Bugs found & FIXED (faithful — the original did not corrupt memory / invoke UB)

### 1. Signed left-shift of a negative velocity — `Physics_Update` (combat_recon_balance.cpp, gilde.exe 0x5d8e80)
UBSAN: `runtime error: left shift of negative value -2` at
`combat_recon_balance.cpp:234`. The decompile writes the velocity into the HIGH
16 bits of a 32-bit register (`HIWORD(v6) = velY`), a well-defined two's-complement
register/bitfield write. The C++ translation expressed this as
`(i32)velY << 16`, which is signed-overflow UB when `velY` is negative.

Fix: assemble the high half in **unsigned** space and reinterpret to `i32`; the
later arithmetic `v6 >> 16` (signed, implementation-defined arithmetic shift = x86
SAR) recovers the signed velocity, byte-identical to the original. The existing
golden test `PhysicsSignedVelocityArithmeticShift` (velY=-100, velX=-7) already
exercises this exact path and still produces (-100, -7). New test
`PhysicsExtremeSignedVelocityNoUB` pins INT16_MIN/INT16_MAX.

### 2. Signed integer overflow on `target + 4` / `attacker + 4` — packet builders (combat_packets.cpp, gilde.exe 0x488a4c / 0x488c8c / 0x488edc / 0x488fa4)
The Build*Packet front-ends form the wire id field as `*(record + 4)` — a 32-bit
register/pointer add that wraps in two's complement. The translation did `id + 4`
on a signed `i32`, which is signed-overflow UB at the high boundary (e.g. a
near-INT_MAX id). Pure pointer/register adds in the binary never trap.

Fix: a file-local `Plus4(i32) -> i32` that computes the add in `u32` then
reinterprets (well-defined wrap, identical result for all in-range ids). Applied
to all 9 `+4` sites. New test `BuildSimpleHighTargetWraps` pins the wrap
(0x7FFFFFFD -> 0x80000001); `OrderStageAllFieldsMaxNoOverflow` confirms the
44-byte staging block and 153-byte packet are never written out of range.

## Audited — already safe (no change)

- `OrderStage` (combat_packets.h): flat `bytes[44]`; all field accessors write at
  fixed offsets <= 28, `put32` works in byte space. No OOB.
- `CommandPacket` (command.{h,cpp}): owned by the **command** cluster, not this
  one. `combat_packets` only WRITES packets (builder side). The malformed-packet
  **parse** path (`ComputePacketSize`/`EnqueuePacket`, "shorter than declared")
  lives in the command cluster — flagged for that owner; not edited here.
- `combat_drivers.cpp`: every collection is a `std::vector` accessed via `.size()`
  clamps (`ValidateRoster`, `FindSelectedTarget` cap 32, `ResolveTeamRow`,
  `DrivePursuitTargets`, `FindFirstFreeFormationSlot` with both `kTeamSlotCount`
  and `.size()` guards). `FormationModeOpcode(u8)` handles the whole byte range.
- `combat_orders.cpp`: `CountActiveSlots` stops at count>=16 BEFORE the `[i+1]`
  read (no index-16 OOB on a 16-element roster). `SelectBeatingTarget` guards
  `candidates[16]` with `count<16` everywhere and the filter-table index with
  `idx < filterTable25.size()`. `SpawnDroppedBomb` resizes to 32 first.
- `combat_slots.cpp`: `FindAvailableSquadSlot` iterates the fixed 768-slot table;
  `FindUnitByEntity` iterates `activeSquads` blocks of fixed stride; `PickActive-
  TargetEntry` walks exactly 256 ring tries. These mirror the original's fixed
  globals — the table capacity is the caller's contract.
- `combat_slots3/4/5.cpp`: ring/dmg-number/health-bar slot indices come from
  bounded finders (`RingIndexById`/`FirstFreeRingIndex` return valid-or-(-1));
  `FindNearestEnemyTarget`'s fallback roll is clamped to `[0,count)`;
  `FormatDamageText` uses `snprintf` with the buffer cap.
- `combat_escape.cpp`: `SpiralSweep` clamps every probed (x,z) to `[0,size-1]`
  before `TileType`/`ComputeTileThreatScore`; `TileToWorld` itself is bounds-
  checked. `ComputeTileThreatScore` (public, arbitrary tile) only reaches the
  guarded `TileToWorld`. `steps<=0` early-returns.
- `combat_battle.cpp`: `RoleWeights` is `std::array<float,6>`; the role-pick loop
  indexes 0..5 only. `ScoreUnitForRole` switch has a 0.0 default for any role.
  `AssignUnitsToRoles` nulls a working copy by index `best<size()`.
- `combat.cpp`: `CombatField` arrays fixed `kUnitCapacity`; `Spawn` returns
  nullptr when full; `ApplyUnitDeath` guards the HP-ratio division (`maxHp!=0`).
- `combat_strength.cpp` / `combat_projectile2.cpp`: pure value math over
  `std::vector` ranges; no raw indexing.

## BEHAVIORAL — needs MCP (documented, NOT changed; matches the binary's envelope)

- `Physics_Update(a1, ...)` indexes `table[a1]` with no bound — the original
  addresses the fixed global `dword_1406420` and faults on a degenerate `a1`
  exactly the same way. The per-slot stride (17) and the read are 1:1; bounding
  `a1` here would diverge from the binary on out-of-envelope input.
- `ResolveTargetObjekt` / `PickActiveTargetEntry` use `(char)entry[0]` (a SIGNED
  byte) as a `589 * idx` multiplier into the type table. A negative person index
  reads BEFORE the table base — this is the original's signed multiply
  (`589 * (char)entry[0]`), i.e. the engine's own envelope, not a reconstruction
  bug. The type table is a fixed global in the binary. Left as-is.
- `ComputeBalanceIndex` returns `mode == 6` (one past the 6-row balance table)
  when no battle flag is set. This is a VALUE; the actual table read
  (`dword_B59C40[30*mode + ...]`) is performed by a **non-owned** caller. Flagged
  for that owner; the boundary value is pinned by a test here.
- Malformed/short command-packet parse OOB risk: lives in the **command** cluster
  (`ComputePacketSize`/`EnqueuePacket`), not in `combat_packets` (builder-only).
  Flagged for the command-cluster owner.

## Tests added (boundary / malformed / capacity — all pass under ASAN+UBSAN)

- `combat_recon_balance_test`: `PhysicsExtremeSignedVelocityNoUB` (INT16 extremes),
  `PhysicsMaxValidIndexReadsExactSlot`.
- `sim_combat_packets_test`: `OrderStageAllFieldsMaxNoOverflow`,
  `BuildSimpleHighTargetWraps`.
- `sim_combat_escape_test`: `NoValidTileSweeps` (all-impassable + steps=0),
  `SingleCellGridClampsIndices` (1x1 grid clamp), `EmptyRosterZeroScore`.
- `sim_combat_battle_test`: `ComputeBalanceIndexDefaultModeBoundary` (mode 6 +
  zero-other div guard), `ScoreUnitForRoleOutOfRangeRole`,
  `AssignUnitsToRolesEmpty`, `AssignUnitsToRolesAllWeightsZero`.
- `sim_combat_test`: `FindNearestEnemyUnitEmpty`, `FullFieldFindAndSpawnRefused`
  (0/max units), `ApplyUnitDeathZeroMaxHpNoDivByZero`.
- `sim_combat_orders_test`: `NearestKnifeTargetNoValidTarget` (bad/empty target),
  `SelectBeatingTargetUndersizedFilterTable`, `SelectBeatingTargetSaturatesAtSixteen`.
- `combat_slots_test`: `FindUnitByEntityZeroSquads`, `FindSquadSlotFullTableNoMatch`
  (full 768-slot scan), `ResolveTargetObjektMaxPositivePersonIndex` (table index
  at the positive byte boundary).
- `combat_slots5_test`: `FallbackRollClampedHighAndLow`, `FallbackSingleElement`.
- `combat_drivers_test`: `FirstFreeFormationSlotFull16` (capacity + oversize),
  `FormationModeOpcodeFullByteRange`.

## Cleanup

`build-asan-combat/` removed at end of wave.
