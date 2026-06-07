// =============================================================================
// guild::play — REAL NPC PATHFINDING + MOVEMENT BRIDGE implementation.
// See wire_npc_movement.h. Additive: drives the REAL reconstructed path-follow
// leaves (sim::PathBuildWaypointList = VIBE_Path_BuildWaypointList 0x43bd70 over
// the bidirectional A* VIBE_Path_FindRoute 0x43be20; the per-tick advance mirrors
// VIBE_CharAction_WalkStep 0x4093b0's `++waypointIdx; load next waypoint`). No
// owned file is edited; the per-tick step is exposed as StepNpcMovement() for the
// caller's loop to invoke.
// =============================================================================
#include "play/wire_npc_movement.h"

#include "sim/map.h"        // MapGrid, MapGridFromHeightmap
#include "sim/path.h"       // PathBuildWaypointList, PathStep, PathFindRoute
#include "sim/entity.h"     // g_objects, g_persons, BuildingFindById, PersonFindRecordById
#include "sim/types.h"      // ObjectRec, Person, kObjectCapacity, kPersonCapacity

#include <cstring>

namespace guild::play {

namespace {

// -- process-static state -----------------------------------------------------
bool                g_installed = false;
sim::MapGrid        g_grid{0, nullptr};
NpcMovementTallies  g_tally{};

constexpr int kMaxWaypoints = 512;

// ---------------------------------------------------------------------------
// Raw record-pad accessors. The order-apply path (DefaultApplyOrder) and this
// bridge address the live record by the SAME unaligned byte offsets the engine
// uses, so the fields land inside the digest-folded record bytes.
// ---------------------------------------------------------------------------
inline i32 ReadI32(guild::u8* base, int off) {
    i32 v;
    std::memcpy(&v, base + off, sizeof v);
    return v;
}
inline void WriteI32(guild::u8* base, int off, i32 v) {
    std::memcpy(base + off, &v, sizeof v);
}

// Resolve a live entity id to its record bytes (object first, then person —
// the order-apply id-resolution order).
guild::u8* ResolveRecord(i32 id) {
    if (sim::ObjectRec* o = sim::BuildingFindById(id))
        return reinterpret_cast<guild::u8*>(o);
    if (sim::Person* p = sim::PersonFindRecordById(id))
        return reinterpret_cast<guild::u8*>(p);
    return nullptr;
}

// Advance ONE live record one waypoint toward its destination along the real A*
// path. Returns 1 if it moved, 0 otherwise; sets *arrived when it reaches dest.
int AdvanceRecord(guild::u8* rec, int* arrived) {
    *arrived = 0;
    if (ReadI32(rec, kMoveStateOff) != 1)
        return 0;                                  // no active path

    int curX = ReadI32(rec, kMoveCurXOff);
    int curZ = ReadI32(rec, kMoveCurZOff);
    int dstX = ReadI32(rec, kMoveDestXOff);
    int dstZ = ReadI32(rec, kMoveDestZOff);

    // Already there -> clear the active flag (arrival).
    if (curX == dstX && curZ == dstZ) {
        WriteI32(rec, kMoveStateOff, 0);
        *arrived = 1;
        return 0;
    }

    // Build the REAL path from the current tile to the destination tile.
    sim::PathStep steps[kMaxWaypoints];
    int n = sim::PathBuildWaypointList(g_grid, curX, curZ, dstX, dstZ,
                                       /*profile=*/0, steps, kMaxWaypoints);
    if (n <= 0)
        return 0;                                  // no route (blocked / OOB)
    ++g_tally.pathsBuilt;
    g_tally.lastPathLen = n;

    // The WalkStep per-tick advance: steps[0] is the current tile (start), so the
    // next waypoint is steps[1]. Advance one tile per tick (== ++waypointIdx).
    int ni = (n > 1) ? 1 : 0;
    WriteI32(rec, kMoveCurXOff, steps[ni].x);
    WriteI32(rec, kMoveCurZOff, steps[ni].y);

    // Arrived?
    if (steps[ni].x == dstX && steps[ni].y == dstZ) {
        WriteI32(rec, kMoveStateOff, 0);
        *arrived = 1;
    }
    return 1;
}

} // namespace

// ---------------------------------------------------------------------------
// Install / uninstall.
// ---------------------------------------------------------------------------
void InstallNpcMovement()   { g_installed = true; }
void UninstallNpcMovement() { g_installed = false; }
bool NpcMovementInstalled() { return g_installed; }

// ---------------------------------------------------------------------------
// Grid binding.
// ---------------------------------------------------------------------------
void SetNpcMovementGrid(const sim::MapGrid& grid) { g_grid = grid; }
void SetNpcMovementGridFromHeightmap(const guild::render::Heightmap* hm) {
    g_grid = sim::MapGridFromHeightmap(hm);
}

// ---------------------------------------------------------------------------
// Destination assignment.
// ---------------------------------------------------------------------------
bool SetEntityDestination(i32 id, int destX, int destZ, int startX, int startZ) {
    guild::u8* rec = ResolveRecord(id);
    if (!rec) return false;
    // Seed the current tile only if the entity has no active path yet.
    if (ReadI32(rec, kMoveStateOff) != 1) {
        WriteI32(rec, kMoveCurXOff, startX);
        WriteI32(rec, kMoveCurZOff, startZ);
    }
    WriteI32(rec, kMoveDestXOff, destX);
    WriteI32(rec, kMoveDestZOff, destZ);
    WriteI32(rec, kMoveStateOff, 1);
    return true;
}

NpcMovePos GetEntityMovePos(i32 id) {
    NpcMovePos p;
    guild::u8* rec = ResolveRecord(id);
    if (!rec) return p;
    p.found = true;
    p.curX  = ReadI32(rec, kMoveCurXOff);
    p.curZ  = ReadI32(rec, kMoveCurZOff);
    p.destX = ReadI32(rec, kMoveDestXOff);
    p.destZ = ReadI32(rec, kMoveDestZOff);
    p.active = ReadI32(rec, kMoveStateOff) == 1;
    return p;
}

// ---------------------------------------------------------------------------
// Per-tick step over all live entities with an active path.
// ---------------------------------------------------------------------------
int StepNpcMovement() {
    if (!g_installed || !g_grid.entries || g_grid.size <= 0)
        return 0;

    int moved = 0;
    int arrived = 0;

    // Live objects (g_objects, stride 169; alive byte @+0).
    for (int i = 0; i < sim::kObjectCapacity; ++i) {
        if (!sim::g_objects[i].alive) continue;
        guild::u8* rec = reinterpret_cast<guild::u8*>(&sim::g_objects[i]);
        int m = AdvanceRecord(rec, &arrived);
        if (m) ++moved;
        if (arrived) ++g_tally.arrivals;
    }

    // Live persons (g_persons, stride 536; marker @+0, -1 == free slot).
    for (int i = 0; i < sim::kPersonCapacity; ++i) {
        if (sim::g_persons[i].marker == -1) continue;
        guild::u8* rec = reinterpret_cast<guild::u8*>(&sim::g_persons[i]);
        int m = AdvanceRecord(rec, &arrived);
        if (m) ++moved;
        if (arrived) ++g_tally.arrivals;
    }

    g_tally.entitiesMoved += moved;
    return moved;
}

const NpcMovementTallies& GetNpcMovementTallies() { return g_tally; }
void ResetNpcMovementTallies() { g_tally = NpcMovementTallies{}; }

} // namespace guild::play
