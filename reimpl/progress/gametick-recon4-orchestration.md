# GameTick recon4 — turn / universe / shutdown orchestration

Files:
- `src/play/gametick_recon4_orchestration.h`
- `src/play/gametick_recon4_orchestration.cpp`
- `tests/unit/gametick_recon4_orchestration_test.cpp` (26 tests, 320 golden checks, all pass)

Namespace `guild::play`. Engine leaves routed through `GameTickRecon4Hooks`
(inert defaults); SEQUENCE / state-transitions / teardown ORDER reconstructed 1:1.

## Reconstructed (addr — name)
| addr | name | what |
|------|------|------|
| 0x4c0750 | RunAdvanceGameDialog | flag(8) gate, open panel, frame-loop, close (DispatchPanelEvent 0xB,0 / 0xB,1) |
| 0x579530 | RequestStartTurn | zero phase, stamp turn tag 1685283436, flush BuildOp86 |
| 0x57957c | AdvanceTurnTimer | phase!=1 early-out; forced-event path (89/78/80 suppress via He handler); normal accumulate `rand*rate+base+accum` vs threshold; category dispatch (0->kind80, 1/2/3->weight tables A/B/C +1 -> 89/78/80); counter++; category<-pending |
| 0x5b3030 | Universe_DisplayLogAndCleanup | slot SUSPEND: bit1->floor free, bit0->scene free-mesh; suspendByte=flag |
| 0x5b3410 | Universe_InitLogAndInflate | slot RESUME: bit1->floor alloc, bit0->scene inflate-geom; suspendByte=~flag&3; terrain build iff inv==0 && heightmap && !floor |
| 0x5b3628 | Universe_RunObjectScriptPass | walk(first=1), walk(first=0), upload textures, refresh lights |
| 0x5b5050 | Universe_DestroySlot | >=64 ->0; ==active ->ResetCurrentSlot; !allocated ->1; else switch-in, dispose objs, free render nodes, free floor, destroy sky, free slot mem, clear suspend, switch-back |
| 0x5278cc | Game_ShutdownSubsystems | inner teardown order (gfx, banks, script, cutscene, sound x3 groups, text, gameobj, charaction, cmdqueue) |
| 0x52794c | Game_ShutdownAllSubsystems | inner-first then outer (widget, gfx, gamestate, switch slot0, lightmaps, render, input, timer, vfs, mempool, memtrack, errlog, plugin, window) |
| 0x52f44c | Game_ShutdownWorldAndSubsystems | world teardown order (status/hud, unreg proc, script, mesh, voice/sound, 6 world banks, history, objreset, he, panel, groundplan, building, person/owner tables, chars, snow, rain, 6 sky layers + destroy, ambient, spawned, inv, animal, cmd, net x2, drag) |

## Weight tables (exact bytes)
- `kEventWeightTableA` 0x577954 = {0,0,1,1,1,1,1,1,2,2,2,2,2,2,2,2}
- `kEventWeightTableB` 0x577994 = {0,0,0,0,0,0,0,1,1,2,2,2,2,2,2,2}
- `kEventWeightTableC` 0x5779d4 = {0,0,0,0,0,0,0,1,1,1,1,1,1,1,2,2}

## Deferred / omitted
- **0x52f66c BeginRound (main_RundenBeginn)** — OMITTED body. Reason: it is a
  per-object round-init *economics* pass over the 268-byte-stride person/building
  table (word_12CE910), firing AP-events keyed on building category / occupant /
  workstation sums via ~12 tightly-coupled engine leaves
  (GetCategoryForObject, ComputeRankWithinGroup, PopulateOccupantList,
  RegisterApEvent, SumWorkstationByCategory, Person query iterators,
  RequestBuildOp90). This is per-entity simulation, not turn/round *state
  machinery*; faithfully reconstructing it requires the building/person record
  layouts and the AP-event subsystem, which are out of this cluster. Per rule 8,
  not half-translated. The turn-flow neighbors (timer/start/dialog) ARE done; the
  existing `src/sim/turn_driver.*` already drives the per-round pass enumeration.

## Rule-6 flags
- NONE. The "Inflate" in InitLogAndInflate / VIBE_Object_InflateGeometry
  (0x5b30d4) / VIBE_Floor_AllocInflateBuffers (0x5bce10) is **in-engine geometry
  expansion** ("d3:InflateObject(allpolys/allpoints/mat)"), not a zlib/LZ codec.
  No third-party compression library is involved; routed as a normal hook leaf.

## Wiring notes (real callers)
- AdvanceTurnTimer / RequestStartTurn are the per-turn timer driver; sibling of
  the per-round pass in `src/sim/turn_driver.*`.
- DisplayLogAndCleanup / InitLogAndInflate are slot suspend/resume used around
  active-slot switches (already referenced from `src/sim/character_render2.*`
  hooks `initLogAndInflate` / `displayLogAndCleanup`).
- ShutdownAllSubsystems is the app-exit teardown (callee of the main loop exit);
  ShutdownWorldAndSubsystems is the level/world teardown invoked on
  game-state transition out of play.
- DestroySlot frees a universe slot during world/level changes.
```
```
