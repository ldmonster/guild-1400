# Harden sweep — sim group 7 (character_path.cpp + character_query.cpp)

MCP-driven 1:1 verification of every provenance-tagged function in:
- `src/sim/character_path.cpp` (+ `.h`)
- `src/sim/character_query.cpp` (+ `.h`)

Each function decompiled AND disassembled; constants confirmed via `get_bytes`.
Tests built/run in isolation (the unified build currently breaks on an unrelated
untracked file `src/gui/widget_layout.cpp` — `w.ld<i32>` not a member; NOT mine).

Result: **84 checks / 0 failures** (sim_character_core_test, incl. new gate test);
**555 checks / 0 failures** (character_path_test).

---

## character_path.cpp

| addr | function | verdict |
|------|----------|---------|
| 0x402254 | AllocSlot | VERIFIED-1:1 |
| 0x4022c8 | AllocSlotAtIndex | VERIFIED-1:1 |
| 0x406e68 | FindNearbyWide | **FIXED** (output cap) |
| 0x4073f0 | CreateMapNode | VERIFIED-1:1 |
| 0x4525fc | CountActiveByTurn | VERIFIED-1:1 (He-ring tally = BOUNDARY) |
| 0x45277e | NearestTargetRankWeight | VERIFIED-1:1 |
| 0x452917 | NearestTargetWalkSteps | VERIFIED-1:1 |
| 0x408862 | ClampTileCoord | VERIFIED-1:1 |
| 0x4062c0 | WaitSlotCallback | VERIFIED-1:1 |
| 0x406344 | PickWaitAnimation | VERIFIED-1:1 |
| 0x4087f4 | ResolvePathEndpoints (FindWaypointsToPoint core) | VERIFIED-1:1 |

### FIXED — FindNearbyWide output cap (0x406eee)
- Disasm 0x406ee3 `cmp ecx,0x800` (512 slots) ; 0x406eeb `cmp esi,0x40`.
  `esi` is a **byte offset** (`add esi,4` per hit at 0x406edb), so the real
  output cap is `0x40/4 == 16` entries, NOT 64.
- Before: `while (... && out < kNearbyWideMax)` where `out` is an index (stride 1)
  and `kNearbyWideMax==64` → allowed **64** outputs.
- After: `while (... && out * 4 < kNearbyWideMax)` → caps at **16** entries, exactly
  matching `esi < 0x40`. `kNearbyWideMax` kept at 64 (also the safe out-buffer size;
  header comment updated to document the byte-stride semantics).

### Notes
- AllocSlot: original uses two counters (`eax` byte-offset bounded `<0x800`, `ecx`
  index bounded `<0x200`); reimpl's single-index loop is behavior-identical incl. the
  overflow path. VERIFIED.
- CountActiveByTurn: the tally `edx` only increments when `byte[handler+0xF1]==0`
  (0x452611). That predicate + the He-handler ring are not in the tree, so the tally
  is supplied via `heActiveTally` (BOUNDARY). Arithmetic
  `heTurnMultiplier*(u8)heTurnByte + tally` matches `imul/add` exactly.
- NearestTargetWalkSteps: the original else-branch re-loads the *unclamped* byte
  (var_24) and recomputes `min(v, hi)`; net result equals the reimpl's clamped value.
  Verified arm-by-arm (0x452920 jge / 0x452932 jle / 0x452a08 / 0x452a0f).
- Constants flt_619138..144 = {0.5, 0.10000000149011612, 10.0, 34.0} confirmed via
  get_bytes @0x619138 (`00 00 00 3f / cd cc cc 3d / 00 00 20 41 / 00 00 08 42`).
- PickWaitAnimation: RNG path `idiv (count&0xFFFF)`, remainder `mov ax,dx` ==
  `(u16)((int)r % (u16)count)`. Draw count = 1, matches reimpl. The `!(u16)count`
  early-return (buf[0]) and zero-count return-0 arms verified.
- FindWaypointsToPoint/WalkPathActionUpdate full bodies remain the caller's
  orchestration; only the shared deterministic endpoint resolution + tile clamp
  (ResolvePathEndpoints/ClampTileCoord) are reconstructed and verified.

---

## character_query.cpp

| addr | function | verdict |
|------|----------|---------|
| 0x401a9c | CountActiveUniverse | VERIFIED-1:1 (pool base = BOUNDARY) |
| 0x401ad4 | CountByOwner | VERIFIED-1:1 |
| 0x401b40 | CountByOwnerInRange | VERIFIED-1:1 |
| 0x401bd8 | CountWithTransport | **FIXED** (inverted mesh-gate) |
| 0x4b99ac | CollectByOwner | **FIXED** (1-based output buffer) |
| 0x401c3c | CollectNearbyAtTile | VERIFIED-1:1 |
| 0x426724 | IndexFromUniverse (IndexFromPointer) | VERIFIED-1:1 |
| 0x4266f4 | FindFreeSlot | **FIXED** (probe bytes +980/+981) |
| 0x402314 | FindByPredicate | VERIFIED-1:1 |
| 0x402360 | FindByMesh | VERIFIED-1:1 |
| 0x5caa4c | BoxWithin (local VectorWithinTolerance) | VERIFIED-1:1 |

### FIXED — CountWithTransport mesh-gate inverted (0x401c1c)
- Disasm: `cmp byte[mesh+533],1; jnz inc` — count when **cull != 1** (gate OPEN),
  identical sense to CountByOwner; then `cmp [+296],0; jnz inc` (action != 0 counts).
- Before: `if (anyMesh || !MeshGateOpen(a->mesh) || a->action)` — `!MeshGateOpen`
  means `cull == 1` → **inverted**.
- After: `if (anyMesh || MeshGateOpen(a->mesh) || a->action)`.
- Existing golden (`CountWithTransport`) only used `anyMesh==1`, so it did not encode
  the wrong behavior and still passes. Added regression test
  `SimCharCore.CountWithTransportMeshGate` exercising `anyMesh==0` (open counts,
  culled w/o action does not, culled w/ action does).
- Field offsets confirmed: transport +0x124(=292), action +0x128(=296), active id
  cmp `dword_649D60`.

### FIXED — FindFreeSlot probes the wrong bytes (0x4266f4)
- Disasm 0x4266f9/0x426702: occupancy is `byte_13ED29C[i*984]` and
  `byte_13ED29D[i*984]` = universe **+980** and **+981** — NOT +0/+1 (an old note in
  `src/sim/types.h:254-255` and the prior reimpl comment both claimed +0/+1).
  Stride 0x3D8(984), bound 0xF600(=984*64). A slot is FREE when BOTH are 0.
- Before: heuristic `id==0 && flags==0 && !meshHandle` (none of which are the probe
  bytes).
- After: added `u8 field0x3D4` (+980) to `Universe`; check
  `field0x3D4==0 && noReload==0` (noReload is the +981 field, confirmed by
  `src/sim/universe.cpp:235` writing `byte_13ED29D`).
- Golden encoded the wrong predicate (set `.id`/`.meshHandle` to occupy). FIXED the
  test to set the real probe bytes; it now also asserts that setting id/meshHandle
  does NOT occupy a slot.

### FIXED — CollectByOwner 1-based result buffer (0x4b99ac)
- Disasm: both branches **pre-increment** the store index before writing
  (`add edi,4` @0x4b99ea then store @0x4b99ee; `add eax,4` @0x4b9a45 then store
  @0x4b9a49). First match → `dword_11BB69C[1]`; index [0] is left cleared (the engine
  zero-fills `dword_11BB6A0 == dword_11BB69C+4` over [1..32] at entry).
- Person stride 0x218(536), live-actor field +0x184(388), owner key `*(a1+1)`,
  SetVisible gate `ecx > 8`, caps 31 matches / 768 scanned — all confirmed.
- Before: 0-based `outPersons[idx++]` (first match at [0]).
- After: `++idx` then `outPersons[idx]` (1-based); guard `idx <= maxOut`; caller
  buffer sized `maxOut+1`.
- Golden `CollectByOwnerMatchesHomeId` encoded `out[0]==col.back()` (0-based). FIXED:
  now asserts `out[0]==nullptr` and the first match at `out[1]`. Buffers in the unit
  + e2e tests bumped to `[32]` for the 1-based layout. Counts unchanged.

### Notes
- CountByOwner/InRange: universe-slot address arithmetic
  (`shl/add` chain) = `984*ownerUniverse + byte_13ECEC8`, stride **984** confirmed;
  universe +0x88(136), mesh +0x34(52), cull +0x215(533), pos +0x4C(76). `jnz`-on-not-1
  gate (count when cull != 1). Box test uses `ja` to fail (`<= tol` passes).
- CollectNearbyAtTile (float-heavy): all FP sites verified against disasm.
  - Constants flt_61003C = 2.0, dbl_610044 = 3.0 (get_bytes @0x61003C:
    `00 00 00 40` / `00 00 00 00 00 00 08 40`).
  - **RNG coincident case draws 3 times, order dx,dy,dz**, each
    `(int)RandNext() % 2 - 1` — Hex-Rays shows `% v13`/`% v15` (garbage regs); DISASM
    proves `ecx=2` set once (0x401e22) and reused for all three `idiv ecx`. reimpl
    matches exactly.
  - radius: the 100.0 branch is dead (entry gate already returns when action+9==45);
    reimpl preserves the dead branch faithfully → always 50.0 on the reachable path.
  - `dist == 0` test is `(LODWORD(v43) & 0x7FFFFFFF) == 0` (±0); reimpl `dist==0.0f`.
  - falloff clamp `(v29 >= 0) ? v29 : 0`; negate via sign-bit flip then
    VectorNormalize; target writes +84(X)/+88(=0)/+92(Z), Y component (v38) discarded;
    redraw flag +140|1; terrain reject 0 or 13 clears the flag. Heightmap/terrain
    addressing (`24*(col+gridW*row)+base`) = BOUNDARY via the query hook.
  - flag constants kLaRedraw 0x01 / kLaDirtyMesh 0x08 / kLaSitting 0x10 confirmed
    against `src/sim/types.h`.
- IndexFromUniverse: original `div` is UNSIGNED with `ecx=0x3D8`(984); `jl`(<0) and
  `cmp 0x3F; jle` bounds → -1 outside [0,63]. C++ pointer subtraction is faithful for
  all in-tree pointers.
- FindByPredicate: StrCmpNoCase(mesh@+52, name); on equal (==0) returns the current
  slot without incrementing the index. mesh-name-as-+52-string read = hook BOUNDARY.

---

## Handoffs (outside my ownership — not edited)
- `src/sim/types.h:254-255` comment still says FindFreeSlot "probes byte +0 / the
  byte at +1". DISASM (0x4266f9) proves the probe bytes are **+980 / +981**
  (`byte_13ED29C`/`byte_13ED29D`). Comment-only fix; left for the types.h owner.
- Pre-existing unified-build break: untracked `src/gui/widget_layout.cpp` references
  `Widget::ld<i32>(...)` which does not exist. Unrelated to this chunk.

## Boundaries (genuine, kept as hooks per Rule 8)
- He-handler ring + `byte[handler+0xF1]` tally (CountActiveByTurn).
- Action-node pool base `dword_62CEFC` (CountActiveUniverse — caller supplies column).
- Heightmap/terrain probe + addressing (CollectNearbyAtTile, FindWaypointsToPoint).
- Command-delta codec / He / favourability / SetVisible / mesh-name string
  (FindNearestTarget, CollectByOwner, FindByPredicate).
- Person↔live-actor distinction collapsed in CollectByOwner's abstraction (result
  stores the Person ptr in the engine; reimpl stores the actor column entry).

## Counts
- Functions verified: 21 (11 path + 10 query) + BoxWithin helper.
- VERIFIED-1:1: 18
- FIXED: 4 (FindNearbyWide cap; CountWithTransport gate inversion; FindFreeSlot probe
  bytes; CollectByOwner 1-based buffer) — 3 with golden-test corrections.
- Tests: 84 + 555 checks, 0 failures (isolated build).
