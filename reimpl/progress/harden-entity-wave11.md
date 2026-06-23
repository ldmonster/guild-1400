# Wave-11 hardening — ENTITY cluster (W11-ENTITY)

MCP DOWN → hardening only, no new 1:1 reconstruction. Goal: ASAN+UBSAN over the
entity/person/building/gametime/character-factory sources, add malformed/boundary
input tests, and FIX memory-safety bugs (OOB reads/writes, leaks). Golden values
kept byte-identical; the in-bounds/valid-input path is unchanged.

## Scope (owned)
`src/sim/entity.{h,cpp}`, `person_create.{h,cpp}`, `character_factory.{h,cpp}`,
`building*.{h,cpp}` (building_type / buildingtype_recon / bauplatz),
`gametime*.{h,cpp}`, plus the matching tests.

## Build / run
```
cmake -S . -B build-asan-w11 -DCMAKE_BUILD_TYPE=Debug -DGUILD_BACKEND=OFF \
  -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-sanitize-recover=all -g"
cmake --build build-asan-w11 --target <test> -j$(nproc)
```

## Fixes (memory-safety; faithful — the original did not corrupt memory)

### 1. entity.cpp — `GameObjectResolveEntityById` scene scan OOB  (0x583b44)
The scene-node scan walked the flat array with a raw index `idx` bounded only by
the *examined-occupied* count `v13`. Empty slots (`type==0`) advance `idx` but not
`v13`, so a SPARSE scene array (few occupied nodes, large `sceneNodeCount`, or a
declared count > capacity) ran `idx` past `kSceneNodeCapacity` (512) → OOB read of
`g_sceneNodes[idx].type`. The original walked raw heap with the same loop shape; in
the flat model this overruns the array.
**Fix:** added a top-of-loop `if (idx >= kSceneNodeCapacity) return 0;` fail-safe
(same pattern already used by the DFS iterator `GameObjectIterNext`). In-bounds /
valid scenes are unaffected.
**Verified load-bearing:** removing the guard makes UBSAN report
`index 512 out of bounds for type 'SceneNode [512]'` on
`SimEntityHarden.ResolveSceneCountOverCapacityNoOverrun`; restoring it is clean.

### 2. character_factory.cpp — name-field copies overrun the 516-byte record  (0x4029c4)
`CreateMesh` copies the model name → record+5, the base token → record+304, and the
prefix token → record+368 with `std::strcpy` (unbounded). The character record is a
516-byte (`0x204`) heap block (`AllocSlot` / `kCharRecordSize`). An overlong /
NUL-less / malformed model name (the classic StrChr/strstr-probe case) made the
strcpy run past the field span and overflow the heap block.
**Fix:** added a bounded `WriteStrField(base, off, spanEnd, s)` helper that copies
into the field's own span ([+5,+40) model name, [+304,+368) base, [+368,+416)
prefix), always NUL-terminates, and never writes past the 516-byte record. For
valid (short) model names it writes byte-identical output → goldens unchanged.

### 3. buildingtype_recon.cpp — `Building_PickName` survivor-array OOB  (0x504a54)
The survivor index list `survivors[64]` was write-guarded (`if (survivorCount < 64)`)
but `survivorCount` kept incrementing past 64. A `count` larger than 64 surviving
candidates then made `pick = RandomModulo(survivorCount)` (and `survivors[pick]`)
index past the array.
**Fix:** cap `survivorCount` at the array capacity (64). The original's survivor
array holds ≤ 12 templates, so faithful inputs (count ≤ 12) never reach 64 → no-op
for valid data; only a malformed oversized `count` is affected (it would have read
OOB otherwise).

## Tests added (all ASAN+UBSAN clean; goldens unchanged)

- **sim_entity_test.cpp** (+6 `SimEntityHarden`): person/building lookup at the LAST
  slot (767 / 255) — the 411648 / 43264 byte-cursor bounds; sparse scene array +
  declared-count-over-capacity scene scans (drive fix #1); owner-no-match full
  256-slot iterator scan; person query hitting only the last object slot.
  `93 checks, 0 failures`.
- **character_factory_test.cpp** (+5 + teardown): empty / NULL / overlong-token name
  decomposition; `CreateMesh` with 300-char prefix+base tokens (drives fix #2 —
  fields stay ≤ their spans inside the record); plus a final teardown test +
  `FreeLiveSlots()` in `ResetAll()` to release the `AllocSlot`-malloc'd 516-byte
  records (fixed a PRE-EXISTING ASAN leak: 8 records / 4128 B leaked → now clean).
  `591 checks, 0 failures`.
- **buildingtype_recon_test.cpp** (+1 `PickOversizedCountNoOverrun`): 200 valid
  candidates, modulo arg clamped to 64, index stays in bounds (drives fix #3).
  `89 checks, 0 failures`.
- **gametime_recon_test.cpp** (+1 `AllOnesRecordZeroExtendsBytes`): a fully
  degenerate (all-0xFF) packed record — bytes zero-extend, dword passes through,
  unpack reads only the 12-byte struct (no OOB; faithful year-bias wrap to -1401).
  `31 checks, 0 failures`.
- **person_create_harden_test.cpp** (NEW, +4 `PersonCreateHarden`): array-FULL after
  768 creates returns 0xFFFF with no write past `g_persons[767]`; last-free-slot
  fill keeps the parallel id column in lockstep; parent building with the type table
  UNLOADED → class 0, no column write, no table read; parent building type byte 255
  (last valid `g_buildingTypes[256]` index) stays in bounds. `1546 checks, 0 failures`.

## Reviewed — no OOB found (faithful, in-bounds)
- `PersonFindRecordById` / `BuildingFindById` (0x58bc6c / 0x587b20): read slot `i`
  then `++i >= cap` before re-reading; max index is cap-1. Safe.
- `BuildingTypeDefAt(u8)` (0x...): u8 index ∈ [0,255] always within
  `g_buildingTypes[256]`; null-guarded when table unloaded. Safe.
- `building_type.cpp` group/category/rank/variant mappers: pure `switch` on a u8,
  no array indexing. Safe.
- `GameTimeUnpackFromRecord` / `GameTimeInitDefault` (0x58334c / 0x58320c): read only
  the fixed 12 / 14-byte structs; degenerate records produce faithful wrapped
  values, never OOB.
- `DecomposeModelName`: scratch buffer is `strncpy(...,255)`-bounded and force-NUL'd;
  the out buffers are 256 B with `strncpy(...,255)`. Safe (the record overflow was
  downstream in `CreateMesh`, fixed above).

## BEHAVIORAL — needs MCP (NOT changed)
None encountered. Every fix is a memory-safety guard on a degenerate/malformed
input; no observable output or control flow on VALID input changed, so no 1:1
question was raised. (If a future reviewer wants the engine's exact behavior on a
scene array whose declared count exceeds the live allocation, that is a raw-heap
question for the original — documented here, not guessed.)

## Status
ASAN+UBSAN: all owned test targets green, zero leaks. Normal `build/`: green
(re-ran person_create_harden / sim_entity / character_factory / buildingtype_recon /
gametime_recon + consumers living_city_verify / buildingtype_callers / sim_building).
Did not edit `progress/INDEX.md`, any non-owned cluster file, or commit.
