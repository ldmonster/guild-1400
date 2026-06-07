#pragma once
// =============================================================================
// guild::play — REAL NPC PATHFINDING + MOVEMENT BRIDGE (PLAYABLE_PLAN, "make the
// city alive"). Drives the REAL per-tick path-follow leaf so a live person/object
// with a DESTINATION advances toward it along the REAL A* path each tick, and its
// world position evolves deterministically (folded by play::HashFullWorld).
//
// THE REAL LEAVES (IDA grounding, imagebase 0x400000)
// ---------------------------------------------------------------------------
//   * VIBE_Path_FindRoute            0x43be20  (bidirectional weighted A*)
//   * VIBE_Path_BuildWaypointList    0x43bd70  (route -> (col,row) tile waypoints)
//     -> reconstructed 1:1 in src/sim/path.cpp as sim::PathBuildWaypointList.
//   * VIBE_CharAction_WalkStep       0x4093b0  (per-tick waypoint advance: the
//     `++*(a1+248)` step that loads the NEXT tile waypoint, recomputes the turn
//     angle + segment length, and writes the segment target into the walk-anim)
//   * VIBE_CharAction_WalkUpdate     0x40a0b8  (top-level per-frame walk step)
//     -> reconstructed 1:1 in src/sim/charaction_walk.cpp as sim::WalkStep /
//        sim::WalkUpdate.
// The per-frame NPC step (RunFrameLoop's NPC update) and the per-day director
// (VIBE_GameTick_BeginPlayerRound @0x533188 -> VIBE_NpcAction_DailyRoutineStep
// @0x4e7e88, sim::NpcDaily_DailyRoutineStep) call these path-follow leaves once a
// person has a destination assigned.
//
// THE MOTION MODEL (what advances, and where it is stored)
// ---------------------------------------------------------------------------
// In the engine the live actor's CURRENT world position lives on its avatar mesh
// (Character+20 avatar, avatar[13] object, object+52 mesh, +76/+80/+84 = X/Y/Z;
// the WalkAvatar.posX/Y/Z in charaction_walk.h). That mesh transform is render-
// owned and is NOT in play::HashFullWorld's fold (which folds the raw g_persons
// stride-536 / g_objects stride-169 RECORD arrays). To make the motion DIGEST-
// VISIBLE and deterministic we keep the live TILE position in the entity record's
// own pad — alongside the (kind,destX,destZ) order fields the order-apply path
// already writes there (play::input_command.h kAppliedDestXOff=0x64 /
// kAppliedDestZOff=0x68). The pad layout this bridge owns:
//   +0x60 (u8)   order kind        (written by DefaultApplyOrder — read-only here)
//   +0x64 (i32)  destination tile X (the order's target; the binding source)
//   +0x68 (i32)  destination tile Z
//   +0x6C (i32)  CURRENT tile X     (this bridge: advanced one waypoint per tick)
//   +0x70 (i32)  CURRENT tile Z
//   +0x74 (i32)  movement state     (1 == has an active path this run)
// Each tick the driver runs the REAL A* (PathBuildWaypointList) from the current
// tile to the destination tile and advances the current tile to the NEXT waypoint
// (steps[1]) — exactly the WalkStep `++waypointIdx; load waypoints[idx]` advance.
//
// THE DESTINATION <-> ENTITY BINDING (documented stand-in)
// ---------------------------------------------------------------------------
// The order-apply path (play::IssueWorldClick kConquer -> Op80Handler ->
// DefaultApplyOrder) writes the destination tile into +0x64/+0x68 of the picked
// record. THAT is the real binding this bridge follows. For entities with no
// order issued, SetEntityDestination() assigns one explicitly (tests/e2e). The
// engine's full destination assignment also flows through the NPC daily director's
// destBld (+388) -> building tile resolution, which is render/scene-coupled and
// has no standalone reconstructed leaf (see wire_npc_actions.h FINDINGS); this
// bridge therefore drives the path-follow from the +0x64/+0x68 order field +
// explicit SetEntityDestination, and SAYS SO. The per-tick path-follow itself is
// the real reconstructed A* + WalkStep advance.
//
// INERT DEFAULT
// ---------------------------------------------------------------------------
// Not installed (the default) => StepNpcMovement() is never called by anything =>
// NO position evolves: identical to current behavior. The bridge edits no owned
// file (wiring.cpp / frameloop.cpp / charaction_walk.cpp / pathfind_map.cpp);
// it drives the real leaves through this self-owned module's public API only.
// =============================================================================
#include "guild/common/types.h"

namespace guild::render { struct Heightmap; }

namespace guild::sim { struct MapGrid; }

namespace guild::play {

// ---------------------------------------------------------------------------
// Record-pad byte offsets this bridge reads/writes (shared with input_command.h
// for +0x60/+0x64/+0x68). Current tile + movement state are bridge-owned pad.
// ---------------------------------------------------------------------------
enum NpcMoveOffset : int {
    kMoveKindOff   = 0x60,  // order kind byte (DefaultApplyOrder; read-only here)
    kMoveDestXOff  = 0x64,  // destination tile X (i32) — the binding source
    kMoveDestZOff  = 0x68,  // destination tile Z (i32)
    kMoveCurXOff   = 0x6C,  // CURRENT tile X (i32) — advanced one waypoint/tick
    kMoveCurZOff   = 0x70,  // CURRENT tile Z (i32)
    kMoveStateOff  = 0x74,  // movement state (i32; 1 == active path)
};

// ---------------------------------------------------------------------------
// Install / uninstall the per-tick NPC movement driver. Process-static,
// idempotent. While installed, StepNpcMovement() drives the real path-follow over
// the supplied grid. Uninstalled (the default) => no movement (current behavior).
// ---------------------------------------------------------------------------
void InstallNpcMovement();
void UninstallNpcMovement();
bool NpcMovementInstalled();

// ---------------------------------------------------------------------------
// Bind the walkable grid the path-follow searches over. Build it from the real
// render heightmap (sim::MapGridFromHeightmap) or pass a synthetic grid for unit
// tests. The MapGrid is a thin view; the caller owns the backing buffer.
// ---------------------------------------------------------------------------
void SetNpcMovementGrid(const sim::MapGrid& grid);
void SetNpcMovementGridFromHeightmap(const guild::render::Heightmap* hm);

// ---------------------------------------------------------------------------
// Assign a destination tile to the live entity `id` (resolves object first, then
// person, mirroring DefaultApplyOrder). Initialises the current tile to the
// entity's current position if it has none yet (curX/curZ default to the start).
// Returns true if an entity was found. `startX/startZ` seed the current tile when
// the entity has no current position recorded (state != 1). Pass the entity's
// spawn tile; for an already-moving entity startX/startZ are ignored.
// ---------------------------------------------------------------------------
bool SetEntityDestination(i32 id, int destX, int destZ, int startX, int startZ);

// Read back the entity's current tile + whether it still has an active path.
struct NpcMovePos {
    int  curX = 0;
    int  curZ = 0;
    int  destX = 0;
    int  destZ = 0;
    bool active = false;  // state == 1 (has not yet arrived)
    bool found = false;   // an entity with this id exists
};
NpcMovePos GetEntityMovePos(i32 id);

// ---------------------------------------------------------------------------
// Advance every live entity that has an active destination ONE waypoint along its
// real A* path toward the destination (the WalkStep per-tick advance). Returns
// the number of entities that moved this tick. No-op (returns 0) when the driver
// is not installed OR no grid is bound. Run this from the per-tick / per-day loop;
// positions written back to the record pad are folded by play::HashFullWorld.
// ---------------------------------------------------------------------------
int StepNpcMovement();

// Per-tick tallies (read back by tests/report). Reset by ResetNpcMovementTallies.
struct NpcMovementTallies {
    int entitiesMoved   = 0;  // entities advanced one waypoint this step
    int pathsBuilt       = 0;  // PathBuildWaypointList calls that returned a route
    int arrivals         = 0;  // entities that reached their destination
    int lastPathLen      = 0;  // waypoint count of the most recent built path
};
const NpcMovementTallies& GetNpcMovementTallies();
void ResetNpcMovementTallies();

} // namespace guild::play
