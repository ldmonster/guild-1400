# Hardening sweep — sim chunk 03 (combat packets / projectile2 / recon_balance)

MCP-driven 1:1 diff of every provenance-carrying function in:
- src/sim/combat_packets.cpp
- src/sim/combat_projectile2.cpp
- src/sim/combat_recon_balance.cpp

## Counts
- VERIFIED-1:1: 11
- FIXED: 1 (one constant-table cell + its golden vector)
- BOUNDARY: 0 (all callbacks/hooks are pre-approved cross-cluster leaves already
  modeled via CombatOrderContext / PhysicsRenderHooks; no new tech swaps)

## combat_packets.cpp

| addr | function | status |
|------|----------|--------|
| 0x495874 | RequestBuildOp80 | VERIFIED-1:1 — opcode 80, a1@+0x10, qmemcpy 44 @+0x14, cut target = `dword_6315C0?*dword_6315C0:-1` @+0x40, EnqueuePacket. |
| 0x488a4c | BuildAttackPacket | VERIFIED-1:1 — null gates (target/attacker/mesh-origin/projection/slot), melee predicate `!ObjectDef || c∈{340,342,344,366,370} || c==0`, switch arms 350/352, 372, 374 (field4/field5/field6 + no-shots banner early-out), no-default fall-through, secondary gate `*(a2+452)∈{1,2,3}`. All field offsets + write order match. |
| 0x488c8c | BuildMoveToPacket | VERIFIED-1:1 — bone-chain transform, world→tile, kind 3, field4=tileX field5=tileZ. |
| 0x488edc | BuildTilePacket | VERIFIED-1:1 — own mesh origin → tile, FindSafestTileInRange(...,8,...), kind 7, field4/5 = safest X/Z. v11 unseeded (=0) modeled exactly. |
| 0x488fa4 | BuildSimplePacket | VERIFIED-1:1 — kind 8, targetId only. |

Plus5 (target+4) wrap modeled in unsigned space (UB-safe; bit-identical to the
binary's 32-bit add). Golden vectors (sim_combat_packets_test, e2e) match the
binary; 105 + 45 checks pass.

## combat_projectile2.cpp  (all extracted from 0x487760 VIBE_Combat_UpdateProjectiles)

| function | status |
|----------|--------|
| ArrowSpawnGateOpen | VERIFIED-1:1 — `dword_631204==*(dword_63159C+4) && (tick % 0x15E)==0`, unsigned modulo. |
| TickProjectileSlot | VERIFIED-1:1 — active iff `count!=0 && count<=4`; `--count`; `count==2 → re-arm 100 (no fire)`, else fire. |
| ResolveKnifeAim | VERIFIED-1:1 — highlight branch → 128; no target → hidden; else `255 - dist*0.01*255` then **truncate**. Confirmed at 0x48798e–0x4879d1: `fsub` → `call ConvertX (0x5c6b08, truncate-toward-zero)` → `fistp` → low byte. `(int)` cast in the recon reproduces the truncation exactly. Constants confirmed: flt_61B208=0x437f0000=255.0, flt_61B204=0x3c23d70a=0.01f, radius 0x42C80000=100.0. |
| NearestKnifeTargetDist | VERIFIED-1:1 — best=999999.0; skip same faction (`*(+364)` equal), skip dead (`*(+8)==0`); in-range `SLODWORD(dist)<100.0f`; min-fold; returns -1 when none. |

Note (no change): the falloff is computed in x87 80-bit in the binary and as 32-bit
`float` in the recon (matching the decompile's `float v19`). The byte result after
truncation is identical for all realistic inputs; faithful 80-bit emulation is out of
scope and would not change any observable transparency byte.

## combat_recon_balance.cpp

| addr | function | status |
|------|----------|--------|
| 0x5d8e80 | Physics_Update | VERIFIED-1:1 — `17*a1` stride; bank gate; `LOWORD(v6)=HIWORD(a3)`, `HIWORD(v6)=velY`, `HIWORD(v7)=velX`; gateB → Velocity_Apply((v6>>16)+6,(v7>>16)+6,...); Animation_Basic(v6>>16,v7>>16,...). Signed arithmetic `>>16` (sar) preserved; UB-safe bitfield assembly. |
| 0x48b744 | InitDefaultParameters | **FIXED** — see below. Pass A (×0.01 + cumulative) and Pass B (dedup) loops, bounds, and return value 840 all match. |

### FIX: kDefaultParams cell mode0/bucket2/col0 (flat index 12, dword_B59C70)

The decompile renders the store as `dword_B59C70 = v0` with `v0` an *uninitialized*
`ecx`, so the prior reconstruction took it as 0. DISASM is the reference of record:

- 0x48b768 `mov ecx, 42A00000h`   (= **80.0**)
- 0x48b76d `call VIBE_Light_SetGrayColorThunk`
- 0x48b79f `mov ds:dword_B59C70, ecx`

VIBE_Light_SetGrayColorThunk (0x5c6af0) does `push ecx … pop ecx; retn`, i.e. it
**preserves ecx** across its FillDword tail-call. So at 0x48b79f `ecx` is still
0x42A00000 = 80.0. (0x42A00000 = 80.0, not 100.0 = 0x42C80000.)

- Source: kDefaultParams row 2 col 0 `0` → `80`.
- Golden (combat_recon_balance_test.cpp kGolden) row 2:
  before `0, 0.2, 0, 0, 0, 0` → after `0.8, 1.0, 0, 0, 0, 0`
  (raw 80,20 → ×0.01 → 0.8,0.2 → cumulative → 0.8,1.0 → dedup: 1.0≠0.8 kept).
  Recomputed independently in float32; only this row changes.

All other 179 cells confirmed byte-exact (get_bytes of the global region is all-zero
in the static IDB — runtime-initialized — so each immediate store was decoded from
the disasm and mapped by `(addr-0xB59C40)/4`). dbl_61B8BC = 0.01 confirmed
(0x3f847ae147ae147b). flt_B59C3C = 0.0 (never read; Pass A's `+=prev` only runs for
col!=0).

## Build / tests
- `cmake --build . --target guild` — clean.
- combat_recon_balance_test: 317 checks, 0 failures (with corrected golden).
- sim_combat_packets_test: 105 checks, 0 failures.
- sim_combat_orders_test (covers projectile2 leaves): 95 checks, 0 failures.
- sim_combat_packets_e2e_test: 45 checks, 0 failures.

No cross-file handoffs. No files outside the chunk + their tests were modified.
(Note: the unit-test target needed a one-time `cmake .` reconfigure to refresh a
stale link-order dependency; not a source issue.)
