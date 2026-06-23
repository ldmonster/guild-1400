# Living city — persons MOVE in the session (wave 4, agent W4-B)

Status: **landed** (2026-06-11). Live `sim::g_persons` walk through the 3D city
session (`guild_run --play`, `RunSdlSession` with `city3d + persons`): the
day-start daily-routine director assigns destinations, the REAL A* path-follow
advances each person one tile per WORLD-clock commit, and the person pass draws
them at their live tile over the REAL terrain ground height. Additive: a
session whose persons get NO destination renders byte-identically to wave 3
(pinned by the untouched `IntegratedCity3DSession` e2e — same traces, same
hashes).

Modules:
* `src/play/session_npc_daily.{h,cpp}` — **NEW**: the day-start destination pass.
* `src/play/session_persons3d.{h,cpp}` — extended: the documented WALKING
  placement handoff (movement tile -> world) + the per-frame position update.
* `src/play/sdl_session.{h,cpp}` — wiring (config/trace additive).
* `tests/unit/session_living_city_test.cpp`,
  `tests/e2e/playable_flow_e2e_test.cpp` (TEST I `LivingCityPersonsMove`).

## 1. WHEN the original moves NPCs (the cadence evidence, in-tree)

* The walk integration itself is **per-frame**: `VIBE_CharAction_WalkUpdate`
  @0x40a0b8 / the `ch_WalkOnPath` monolith @0x40a4d4 (reconstructed 1:1 in
  `src/sim/charaction_walk.*`) scale rotation/movement by the elapsed
  master-tick delta `dword_62D008 - dword_62D004`, bracketed per frame by
  Character_Update; the master tick is the 14 ms winmm TimeBase
  (`StartTimer(0xE,0)` @0x527e52, `src/sim/game_clock_tick.h`).
* The **world-time cascade** rides the opcode-30 commit: RunFrameLoop @0x4c0b8c
  queues the master clock (`QueueRequestPerm30` @0x494a50) once per 7-tick
  command window; `VIBE_Command_ExAdvanceGameTick` @0x498954 commits the WORLD
  clock `qword_13CE852` when newer and runs the per-tick world cascade (He
  handlers, character needs, Meister AI, production timers —
  `src/sim/command_apply6.cpp`). The master clock itself advances on the
  994 ms clock proc (interval 71 x 14 ms, @0x527778).
* The session binding: `wire_npc_movement`'s motion model is **tile-grain**
  (one `WalkStep` @0x4093b0 waypoint advance per step), so the session steps
  `StepNpcMovement()` **once per opcode-30 world-clock commit**
  (`SessionTick::FrameResult::timeSyncCommits`) — the committed tick that runs
  the engine's own per-tick world updates. Without the continuous clock, the
  SPACE game-day is the tick (the `wire_npc_movement_e2e` cadence).

**Named cadence gap:** the original's *continuous* sub-tile walk rate (the walk
anim's `+92` segment duration / `+96` playback speed, integrated per frame by
WalkUpdate over the avatar mesh) needs the live charaction/avatar runtime,
which is not attached to the session persons. The tile advance per world-clock
commit is the deterministic, digest-visible binding of the same reconstructed
step; the exact world-units-per-tick speed is NOT claimed.

## 2. What runs at day start (destinations without player orders)

`play::SessionNpcDailyAssign(view, hm)` — run at session start (after
`SessionTick::SyncClocksToDayStart`, so the director reads the real 06:00) and
again after every day rollover (the continuous-clock `dayEnded` branch and the
SPACE day):

1. `sim::SetNpcClock(sim::g_tickClock)` — the director reads the world clock
   image (`qword_13CE852`).
2. The REAL director step: `sim::NpcDaily_DailyRoutineStep` @0x4e7e88 (He
   state 0 = the morning work-dispatch sweep `BeginPlayerRound` @0x533188
   runs), through `play::InstallRealNpcActions` (live `g_persons` columns
   +356/+357/+364/+368/+388/+456, `Building_IsProductionKind`, the real
   `Command_*` packet builders). 06:00 is inside every season's morning window
   (`flt_6476FC` {8,7,8,9} + `flt_61F968` -1).
3. Every person the director DISPATCHED (its +456 bit 0x100000/0x80000 was set
   by the real sweep) gets its movement destination bound:
   `SetEntityDestination(person, WorldToTileWithHeight(target building's bound
   placement), start = the person's bound seat tile)` —
   `render::WorldToTileWithHeight` @0x5c6644 over the session terrain.

The target provider (the render/bone-chain searches with **no standalone
reconstructed leaf** — `wire_npc_actions.h` FINDINGS) derives everything from
the REAL live records: carry/interaction target = the person's own `workBld`
column (+368) when that building has a real bound placement; `destDoorIds` =
the `destBld` column; `homeHasMesh` = the home building has an owner-matched
scene node; `ownerKind` = `g_persons[homeBld word +39].kind`; `aiPlayerClass`
= `g_buildingTypes[type]` byte 0; `workDistanceOk` = world distance < 960 (the
in-tree FINDINGS note); `characterBudgetOk` = bound persons < 32; tavern
leaves report none. The provider is the deterministic seam already flagged as
NOT 1:1; the director's rules/sweep/writebacks/packet builds are the real code.

**Live-data reality (named, not faked):** a freshly loaded `.cty` + the
new-game commit carry **no populated home/work/dest building columns** (probed
on AUGSBURG: player + 2 parents, all three columns 0; the wave-3 named gap),
so by default the director dispatches NOBODY and the session is visually
unchanged. The e2e stages the columns through the new
`SdlSessionConfig::postWorldLoadHook` (an explicit HOST/TEST seam, documented
as such) — never invented inside the engine path.

## 3. The placement chain (live positions in the 3D view)

```
record pad +0x6C/+0x70 current tile (state +0x74)   wire_npc_movement (WalkStep @0x4093b0 advance,
                                                     PathBuildWaypointList @0x43bd70 over A* @0x43be20)
  -> render::TileToWorld @0x5c65d4 over the session terrain Heightmap
     (the REAL "<scene>_height" grid + BuildTerrainMesh @0x5c5610 scales —
      render/scene_floor.*; real ground height at the tile)
  -> session_persons3d MovePlacement (the CityView3D resolvePersonPlacement hook):
       live tile        -> TileToWorld position, zero euler (dword_577A78)
       no live tile     -> the CAPTURED entrance-dummy seat (the genuine
                           SpawnAtBuildingEntrance @0x57c8f0 default the view
                           itself computed at BindPersons — captured at install)
       neither          -> unplaced (counted; rule 8)
  -> per frame: UpdateSessionPersons3DPositions — compares each bound person's
     live tile against its bound placement; rebinds ONLY when a tile changed.
```

Walkable grid: the terrain-type/collision bytes live in the **unparsed
remainder of the scene floor block** (`VIBE_WorldIo_LoadFloorRegions`
@0x5e78a8 — the render/scene_floor.h named boundary), so the session walks an
OPEN grid over the real heightmap with a blocked border (the
`wire_npc_movement_e2e` precedent, stated in the wiring comment). The
tile<->world geometry (origins/scales/heights) is fully real.

## 4. Per-frame cost + the CityView3D handoff

`UpdateSessionPersons3DPositions` costs a roster scan per frame; a full
person rebind runs ONLY on frames where a tile actually changed (at the
world-commit cadence, ~1/s — never per frame). The rebind recreates the
factory records and restarts the pose phase — the documented stand-in.

**HANDOFF (for the CityView3D owner):** a cheap per-instance move needs ONE
public mutator —

```cpp
// Move an already-bound person instance to a new placement (position+euler),
// keeping its factory record, pose state and material binds intact. Returns
// false when `id` is not bound. (Updates persons_[i].info.place + the
// parallel boundPersons_ copy; l2w stays identity per the zero spawn euler.)
bool CityView3D::MoveBoundPerson(i32 id, const CityPlacement& place);
```

When that lands, `UpdateSessionPersons3DPositions` should call it per moved
person instead of `RebindSessionPersons3D` (one-line swap; the detection logic
stays). A second nicety for the clip gap: a `SetBoundPersonClip(id, gait)` to
flip the pose to the factory-preloaded "bewegung/gehen" while moving.

## 5. Session wiring (sdl_session)

Config (additive): `npcMovement` (default true; active only with
`city3d && persons`), `postWorldLoadHook` (host/test seam).
Trace (additive): `npcMovementActive, dailyDispatched, dailyAssigned,
moveSteps, personsMoved, moveArrivals, personsMoving, movePosUpdates,
moverId, moverTileX/Z, moverWorldX/Y/Z` (the first mover's tile + its
TileToWorld draw seat at exit — the e2e's ground-height witness).

Flow: terrain bind -> walkable grid + `InstallNpcMovement` +
`InstallSessionPersonsMovePlacement` -> day-start `SessionNpcDailyAssign` ->
per frame: step per `timeSyncCommits`, `UpdateSessionPersons3DPositions`
before the pose advance; day rollover + SPACE re-run the daily assign; F9
quickload re-binds through `RebindSessionPersonsAfterSimChange` (default-seat
refresh + capture, then the movement bind); exit: tallies into the trace,
`UninstallNpcMovement` + `ClearSessionPersonsMovePlacement`.

## 6. Named gaps (rule 8 — none faked)

* **Continuous walk speed** (cadence): see §1 — tile/commit, not units/frame.
* **Walkable cells**: floor-block remainder @0x5e78a8 unparsed -> open grid +
  border (stated at the wiring site).
* **Provider leaves**: findCarryTarget @0x4e786c, findInteractionTarget
  @0x4e79c0, PickClosestByWeight @0x4e7c3c, destDoorIds (+44/+48),
  homeHasMesh (+97), workDistanceOk (exact float gate), CountByOwner cap,
  SumCurrencyHeld, the state-1 candidate gather (Person_QueryBegin @0x4e8383)
  — all derived from live records / reported none (see §2).
* **Destination point**: the building NODE placement (the IsNearDoorAlt
  @0x4b0ee8 fallback), not the composed "dummy_EINGANG" (CityView3D-internal;
  <= 1 tile difference).
* **Clip selection** for movers (gait vs idle): the NpcAction/charaction
  runtime — the wave-3 gap; the bind plays the factory preload set (idle
  first). Cheap flip is part of the §4 handoff.
* **Walk heading/euler**: zero (dword_577A78); the heading interpolation is
  WalkOnPathRotation @0x409b2c in the charaction runtime, not driven here.
* **Person building columns**: nothing in-tree populates +364/+368/+388 on
  load/new-game (wave-3 gap) — the e2e seeds them via the explicit test seam;
  the engine-path population (ExEnterBuilding @0x495300-family / the office
  cluster) is future reconstruction.
* `GetEntityMovePos`/`SetEntityDestination` resolve object-before-person by
  id (the order-apply order); AUGSBURG person ids (510+) don't collide with
  object ids (1..508) — noted, not a behavior change.

## 7. Tests

* `tests/unit/session_living_city_test.cpp` — suite `SessionLivingCity`,
  **4 tests, 70 checks, 0 failures** (asset-free): movement placement follows
  the live tile (captured-seat fallback, no-move no-rebind, arrival keeps the
  tile), REAL ground height (`TileToWorld` y = heights*scaleY+originY),
  daily assign dispatches the production-homed person + binds the work tile
  (+456 writeback, second-pass stability, full-reset determinism), and the
  assign->walk->arrive end-to-end over the synthetic rig.
* `tests/e2e/playable_flow_e2e_test.cpp` TEST I `LivingCityPersonsMove`
  (guarded, honors `GUILD_GAME_DIR`) — real AUGSBURG, the full integrated
  session (city3d + continuous clock + new-game commit): 3 persons dispatched
  + assigned by the REAL director, 4 commit-steps, **12 waypoint advances**,
  **15 draw-position changes across frames**, 3 still en route, the mover's
  exit seat (tile (84,66), world (2187.3, **220.9**, -658.1)) verified ON the
  REAL ground by an independent floor-block heightmap rebuild, 3 person
  meshes rendered; byte-deterministic across reruns (hashes + every counter).
  Whole suite: **154 checks, 0 failures**.
* Full battery after the change: all related suites green
  (`sdl_session_{itest,e2e}` 17/22, `session_persons3d{,_e2e}` 50/69,
  `wire_npc_movement{,_itest,_e2e}` 37/13/14, `wire_npc_actions*` 14/17/10,
  `session_tick` 69). See the report for the full-tree run.
