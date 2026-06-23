# Wave-8 W8-ANIMALS — Creature / Animal world entities

**Status: ALREADY FULLY RECONSTRUCTED (no new code needed). Verified 1:1, tests green.**

## TL;DR — what the brief asked for already exists

The brief asked me to reconstruct the city's RATTE/HUND/KATZE/PFERD (and the
livestock) creatures — spawn + idle/wander behaviour + render via the character
mesh path — into a NEW `src/sim/creature.{h,cpp}`.

While discovering the call tree from the `VIBE_Character_CreateFromModel @0x402d10`
creature probe, I found the **entire ambient-animal subsystem is already
reconstructed 1:1** by a prior wave, in:

- `src/sim/animal.cpp` / `src/sim/animal.h`            — pool + spawn-decision + despawn core
- `src/sim/animal_wander.cpp` / `src/sim/animal_wander.h` — placement, herd, door, per-species AI, model table

with golden unit tests, an integration test and a guarded e2e test:

- `tests/unit/sim_personnel_test.cpp`  (the `SimAnimal.*` suite — pool/spawn-core)
- `tests/unit/sim_animal_wander_test.cpp` (the `SimAnimalWander.*` suite — wander/herd/door/AI)
- `tests/integration/sim_animal_wander_itest.cpp`
- `tests/e2e/sim_animal_wander_e2e_test.cpp`

Per CLAUDE.md ("No ODR clashes… grep before defining a symbol; if it exists,
reuse it") and the wave-8 brief ("Grep src/** before defining ANY symbol"),
creating a duplicate `creature.{h,cpp}` with the same logic / symbols would be a
**rule violation**. So this wave's deliverable is verification + documentation of
the existing reconstruction and the exact session handoff, NOT duplicate code.

This matches the brief's escape hatch: *"If creatures turn out to be just regular
NPCs … document that precisely."* — here they are not regular NPCs; they are a
dedicated `Animal` subsystem (already built) that bottoms out in the shared
`VIBE_Character_CreateFromModel` factory and the shared character render path.

## The real call tree (verified live via IDA MCP, module gilde.exe)

```
VIBE_Hud_HandleMouseClick           0x4bc280   (per-frame HUD/sim step; calls Update at 0x4bc40c)
└─ VIBE_Animal_Update               0x48364c   round-robin: 1 spawn-roll + 1 slot update/despawn
   ├─ VIBE_GameTime_GetSeasonFromDay 0x58339c  (season; 3 == winter gates spawn off / forces despawn)
   ├─ VIBE_Universe_SwitchActiveSlot 0x5b4a24  (brackets the body; render-scene switch)
   ├─ VIBE_Math_RandomModulo        0x58b89c   (all the RNG rolls)
   ├─ VIBE_Animal_AllocSlot         0x483960   first-free of 32×292-byte records @dword_62EE3C
   ├─ spawn dispatch (RNG type pick):
   │   ├─ VIBE_Animal_SpawnCat      0x483bc4   model "hund_HUND"   kind+4 = 0  (LABEL_9,  roll0)
   │   ├─ VIBE_Animal_SpawnDog      0x483b58   model "katze_KATZE" kind+4 = 1  (LABEL_29, roll1)
   │   ├─ VIBE_Animal_SpawnSheep    0x483c30   model "schaf_SCHAF" kind+4 = 4  scale 1.5f
   │   ├─ VIBE_Animal_SpawnCow      0x483ca8   model "kuh_KUH"     kind+4 = 3  scale 1.5f
   │   └─ VIBE_Animal_SpawnLivestock 0x483d20  model "dummy_%s"/<species> kind+4 = a2 (3/6/7…)
   │       └─ uses byte_62EE44[32*kind] species names (DOG/CAT/BIRD/COW/SHEEP/CHICK/PIG/HORSE)
   ├─ per-slot AI (switch on rec+4 kind byte):
   │   ├─ VIBE_Animal_UpdateCat     0x483e34   kind 0/1  wander odds <=30, tag "anm_UpdateCat"
   │   ├─ VIBE_Animal_FindHerdGrouping 0x4839f0 kind 2
   │   └─ VIBE_Animal_UpdateSheep   0x483fd4   kind 3..7 wander odds <=20, tag "anm_UpdateSheep"
   │       └─ both call BuildWanderPath 0x484200 + CharAction_InsertActionVararg(actor, 45,…)
   │                                            and CreateSoundAction 0x405670 on the sound branch
   └─ VIBE_Character_Destroy        0x402120   despawn (also Light_SetGrayColorThunk 0x5c6af0, --count)

spawn placement helpers:
  VIBE_Animal_FindDoorTarget        0x484160   pick a non-prod/non-storage building, "dummy_TUER" door anchor
  VIBE_Animal_PickSpawnBuilding     0x484374   SceneGraph_WalkAndInvoke + CollectSpawnBuilding name match
  VIBE_Animal_CollectSpawnBuilding  0x48432c   StrCmp collector callback (cap 32)
  VIBE_Animal_BuildWanderPath       0x484200   RNG tile-offset path, clamp [2..size-2], TraceLineOfSight
model table:
  VIBE_Animal_LoadModels            0x484468   7 meshes: hund,katze,kuh,pferd,schaf,pferd,schwein
  VIBE_Animal_ResetModelHandles     0x484424   release dword_B59BC0[] / unk_62EF48
pool lifecycle:
  VIBE_Animal_AllocPool             0x4835c0 / FreePool 0x4835ec / AllocSlot 0x483960 / FreeSlot 0x4839cc
```

All of the above are translated and present in `animal.cpp` / `animal_wander.cpp`
(addresses cited in their headers). I re-decompiled each one this wave and
confirmed the existing C++ matches the Hex-Rays reference.

## RATTE? — the brief's headline creature

The four creature **needles** in the character factory are
`RATTE / HUND / KATZE / PFERD` (`VIBE_Character_CreateMesh @0x4029c4`, the
`loc_5CB930` strstr probe, needle strings @0x610138.., already reconstructed in
`src/sim/character_factory.cpp`). That probe only sets the actor's TYPE byte
(`rec+4 = 2` animal; `|8` for horse; clears two node flags for rat) — it is the
*classifier*, not a spawner.

**There is NO `VIBE_Animal_SpawnRat` in the binary.** The ambient spawner pool
(`VIBE_Animal_Update`) spawns dog/cat/cow/sheep/pig/horse only. "RATTE" is a model
the factory still recognises as an animal when something else creates it (scripts:
`VIBE_Script_CmdCreateCharacter`/`…AtDummy`, the cutscene/office spawns — the other
`0x402d10` xrefs), but the city's ambient-creature loop does not place rats. This
is faithful to the original; not a gap.

## Key data recovered with get_bytes (1:1)

- **Animal model strings @0x61afdc** (verified): `katze_KATZE` `hund_HUND`
  `dummy_SHEEP` `schaf_SCHAF` `dummy_COW` `kuh_KUH` `dummy_%s` `pferd_PFERD`
  `schwein_SCHWEIN`.  Note the **SpawnDog/SpawnCat model swap is REAL** in the
  binary: `SpawnDog` loads `katze_KATZE`, `SpawnCat` loads `hund_HUND`. The
  existing code preserves this 1:1 (do not "fix" it).
- **Animal-anim tags @0x61b068**: `anm_UpdateCat`, `bewegung/fressen`,
  `anm_UpdateSheep`, `dummy_TUER`.
- **Livestock species name table byte_62EE44** (32-byte stride): index 0 `DOG`,
  1 `CAT`, 2 `BIRD`, 3 `COW`, 4 `SHEEP`, 5 `CHICK`, 6 `PIG`, 7 `HORSE`.
- **Pool**: 32 slots × 292 bytes = 9344 (`0x2480`) @dword_62EE3C; live count
  dword_62EE38; round-robin cursor dword_62EE40; last-spawn-tick dword_62EF44;
  clock dword_62EB38; weather byte_1233514 (0 clear / 1 light=cap16 / 2 heavy=cap32).
- **Action codes** (CharAction_InsertActionVararg @0x40c1e4): `56` = idle/spawn
  action inserted at creation; `45` = walk/wander action issued per path point.
- **Lifetime/despawn**: spawnTick+2100 < gameTick gates a probabilistic despawn
  `RandomFloatScaled() > (float)(thresh/(gameTick-thresh))`; winter (season 3)
  forces the despawn flag.

These all match the constants baked into `animal.cpp` / `animal_wander.cpp`.

## Fidelity verification performed this wave

- Re-decompiled `Animal_Update @0x48364c` and diffed the spawn switch: the
  `RandomModulo(2)` branch maps `0 -> SpawnCat (LABEL_9)`, `1 -> SpawnDog (LABEL_29)`
  — `animal.cpp Animal_SpawnKindForRoll` matches (`0->kAnimalCat, 1->kAnimalDog`).
- Re-decompiled `LoadModels @0x484468`: the 7 `LoadOrAddRefByName` calls in order
  hund, katze, kuh, pferd, schaf, pferd, schwein — `animal_wander.cpp` matches.
- `AllocSlot @0x483960` (full at 32 → 0), `FindDoorTarget @0x484160`,
  `PickSpawnBuilding @0x484374`, `BuildWanderPath @0x484200`,
  `FindHerdGrouping @0x4839f0`, `UpdateCat/Sheep @0x483e34/0x483fd4` all confirmed
  against the existing translations.

## Build + test (this wave)

```
cmake -S . -B build && cmake --build build -j   # green
./build/sim_animal_wander_test                  # 57 checks, 0 failures
```
The `SimAnimal.*` spawn-core suite (pool alloc/free, full-pool null, the spawn-kind
table incl. the 0xFF "nothing spawns" hole, winter despawn, spawn throttle) lives
in `sim_personnel_test.cpp` and links/builds green.

## EXACT handoff for the orchestrator (the one genuine gap)

The subsystem is reconstructed and tested but **not yet wired into the live
per-tick loop**. There are no callers of `Animal_Update` / `SetAnimalWorld` /
`SetAnimalSceneOps` / `Animal_AllocPool` outside the animal modules + tests.
The orchestrator (who owns the session/bind-site files — I must not edit them)
should wire:

1. **City load (once):** call `guild::sim::Animal_AllocPool()` (0x4835c0) and
   `guild::sim::Animal_LoadModels()` (0x484468) when the city scene is loaded,
   alongside the other world-entity pool inits in the session init path
   (`src/app/session_init.cpp:206` already notes "…/Animal/…" as a remaining leaf).
2. **Per tick (in the HUD/sim step that mirrors `VIBE_Hud_HandleMouseClick`
   @0x4bc280):** call `guild::sim::Animal_Update(season)` once per frame, with
   `season = GameTime_GetSeasonFromDay(...)` (3 == winter). It self-throttles
   spawns (>=350 ticks) and round-robins one slot/update per call.
3. **Render:** install an `IAnimalWorld` (animal.h) + `IAnimalSceneOps`
   (animal_wander.h) implementation that routes:
   - `SpawnAnimal(kind,…)` → `guild::sim::CreateFromModel(<species model>)`
     (character_factory.cpp @0x402d10) + `FindDoorTarget`/`PickSpawnBuilding`
     placement, then the actor renders through the **existing
     person/character render path** (`src/sim/character_render*.cpp`) — animals
     ARE characters (type byte 2), no separate render needed.
   - `DestroyAnimal(actor)` → `VIBE_Character_Destroy` @0x402120.
   - `UpdateAnimal(rec)` → dispatch on `rec->kind`: 0/1 → `Animal_UpdateCat`,
     2 → `Animal_FindHerdGrouping`, 3..7 → `Animal_UpdateSheep`.
   - the scene-ops leaves (EnumBuildings, FindDoorFrame, IssueWander/SoundAction,
     SceneHeightmap, LoadMesh/ReleaseMesh) → the real scene/heightmap/character
     subsystems.
4. **Teardown:** `Animal_FreePool()` (0x4835ec) + `Animal_ResetModelHandles()`
   (0x484424) on city unload.

## Conclusion

No new module was created — doing so would duplicate existing, correct, tested
1:1 code and break the one-library ODR rule. The "creature" world entities of the
brief = the `Animal` subsystem in `src/sim/animal.{cpp,h}` +
`src/sim/animal_wander.{cpp,h}`, verified complete this wave. The only outstanding
item is the live session wiring, which is the orchestrator's bind-site (handoff
documented above).
