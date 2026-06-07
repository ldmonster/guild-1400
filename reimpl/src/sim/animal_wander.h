#pragma once
// Animal wander / herd / spawn-placement + per-species AI step + model-handle
// table for the Guild ambient-animal simulation (gilde.exe). EXTENDS animal.cpp
// (the pool + spawn/despawn rules core) with the spatial/behaviour leaves that
// the header of animal.h had listed as DEFERRED.
//
// (namespace guild::sim)
//
// What is translated here (1:1 from the Hex-Rays reference of record):
//   VIBE_Animal_BuildWanderPath       0x484200  (RNG tile-offset wander path)
//   VIBE_Animal_CollectSpawnBuilding  0x48432c  (scene-walk match callback)
//   VIBE_Animal_PickSpawnBuilding     0x484374  (name -> RNG-pick a building anchor)
//   VIBE_Animal_FindHerdGrouping      0x4839f0  (gather nearby same-type pasture pts)
//   VIBE_Animal_FindDoorTarget        0x484160  (RNG-pick a house door anchor)
//   VIBE_Animal_UpdateCat             0x483e34  (pet AI: wander OR sound)
//   VIBE_Animal_UpdateSheep           0x483fd4  (livestock AI: wander OR sound)
//   VIBE_Animal_ResetModelHandles     0x484424  (release the loaded animal meshes)
//   VIBE_Animal_LoadModels            0x484468  (load the 7 animal meshes)
//
// SIBLINGS CALLED DIRECTLY (real translated code, integration-tested):
//   util::RandomModulo (0x58b89c), render::WorldToTileWithHeight (0x5c6644),
//   render::TileToWorld (0x5c65d4), sim::MapTraceLineOfSight (0x406f10),
//   util::VectorWithinTolerance (0x5caa4c), util::PointThroughBoneChain (0x5c8b38).
//
// CROSS-CLUSTER LEAVES (building iteration, scene actor create/sound/action,
// object-by-handle, scene-graph walk, mesh load/release) are routed through the
// IAnimalSceneOps context so the spatial + behaviour logic is testable in
// isolation; a test supplies a mock that records calls / fabricates anchors.
#include "guild/common/types.h"
#include "sim/animal.h"

#include <cstdint>

namespace guild::render { struct Heightmap; }

namespace guild::sim {

// ---------------------------------------------------------------------------
// A "building anchor" the spawn/herd/door logic enumerates. In the original each
// is a scene/person record; the only fields these functions touch are:
//   - a frame pointer at record+97 (the bone-chain root passed to
//     PointThroughBoneChain), which yields a world position, and
//   - whether the record is a production / storage building (excluded).
// We model the enumerable set as records exposing those two via the ops hook.
// `frame` is a 110-float bone-chain frame (PointThroughBoneChain reads [0..21]).
// ---------------------------------------------------------------------------
struct BuildingAnchor {
    float* frame;   // record+97 (0 == skip; the original tests *(rec+97))
    void*  record;  // opaque handle (passed back to ops for door/object lookup)
};

// ---------------------------------------------------------------------------
// Cross-cluster scene/world hook for the wander/herd/door/AI helpers. Defaults
// are inert so isolated tests can drive the spatial math without a real scene.
// ---------------------------------------------------------------------------
struct IAnimalSceneOps {
    virtual ~IAnimalSceneOps() = default;

    // The active scene heightmap (off_649D64+44). BuildWanderPath needs it for the
    // world<->tile mapping. Default null (BuildWanderPath then produces 0 points).
    virtual const render::Heightmap* SceneHeightmap() { return nullptr; }

    // Enumerate candidate spawn/herd buildings near `townRecord` (the original's
    // Person_QueryBegin(a2,1,6) walk, filtered to exclude production/storage
    // buildings and records with a null frame). Writes up to `cap` anchors into
    // `out`; returns the count. Default: none.
    virtual int EnumBuildings(int townRecord, BuildingAnchor* out, int cap) {
        (void)townRecord; (void)out; (void)cap; return 0;
    }

    // Find the named door object ("dummy_TUER") under `buildingRecord` and return
    // its frame pointer, or null if absent (FindDoorTarget then falls back to the
    // building frame). Default: null. (VIBE_Object_FindByHandle @0x5b7be4.)
    virtual float* FindDoorFrame(void* buildingRecord) {
        (void)buildingRecord; return nullptr;
    }

    // Issue the "walk to tile (col,row)" action on `animal`'s actor with the given
    // debug tag (UpdateCat/UpdateSheep). VIBE_CharAction_InsertActionVararg(actor,
    // col, row, 0) + tag copy. Default: no-op.
    virtual void IssueWanderAction(AnimalRec* animal, int col, int row,
                                   const char* tag) {
        (void)animal; (void)col; (void)row; (void)tag;
    }

    // Issue a random animal sound action (VIBE_Character_CreateSoundAction(actor,
    // soundId)). Default: no-op.
    virtual void IssueSoundAction(AnimalRec* animal, int soundId) {
        (void)animal; (void)soundId;
    }

    // Load (or add-ref) the named animal mesh, returning a nonzero handle, or
    // release a previously loaded one. Used by Load/ResetModelHandles.
    virtual int  LoadMesh(const char* name) { (void)name; return 0; }
    virtual void ReleaseMesh(int handle) { (void)handle; }
};
void SetAnimalSceneOps(IAnimalSceneOps* ops);
IAnimalSceneOps* AnimalSceneOps();

// Model-handle table (gilde.exe dword_B59BC0 base, count unk_62EF48). Exposed for
// the loader/reset functions and tests. Capacity is the 7 animal species the
// loader installs (the original walks `count` entries).
constexpr int kAnimalModelCapacity = 16;
extern int g_animalModelHandles[kAnimalModelCapacity];  // dword_B59BC0
extern int g_animalModelCount;                          // unk_62EF48

// Clears the model table + scene ops back to defaults (test/setup helper).
void ResetAnimalWander();

// ===========================================================================
// gilde.exe 0x484200 — VIBE_Animal_BuildWanderPath
//   (__usercall eax=fn(startWorld@eax, count@edx, outWorld@ebx)).
// Generates `count` wander waypoints around `startWorld`. For each step: pick a
// random tile offset in [-4,+5] on each axis from the start tile, clamp to
// [2,size-2], LOS-trace to the nearest walkable tile (maxRings=600, flags=0); on
// success use that tile, else fall back to the start tile; convert to a world
// point written into outWorld[i*4 .. i*4+2]. Returns `count`. `hm` is the resolved
// scene heightmap (the original reads off_649D64+44). Produces nothing if hm null.
// ===========================================================================
int Animal_BuildWanderPath(const render::Heightmap* hm, const float* startWorld,
                           int count, float* outWorld);

// ===========================================================================
// gilde.exe 0x48432c — VIBE_Animal_CollectSpawnBuilding
//   (__usercall al=fn(record@eax, ctx@edx)). The SceneGraph_WalkAndInvoke callback
// used by PickSpawnBuilding: if the building's model name (record+? -> a name) equals
// the wanted name in ctx, append the record pointer to ctx's list (ctx+160 = count,
// ctx+32+4*count = slot). Returns "keep walking" == (ctx.count < 32). Here the
// matching set is modelled directly via the collector below.
// ===========================================================================
struct SpawnBuildingCollector {
    const char* wantedName;          // ctx name (the "dummy_%s" the picker built)
    void* records[32];               // ctx+32: matched record pointers
    int   count;                     // ctx+160: matched count
};
bool Animal_CollectSpawnBuilding(SpawnBuildingCollector* ctx, const char* recordName,
                                 void* record);

// ===========================================================================
// gilde.exe 0x484374 — VIBE_Animal_PickSpawnBuilding
//   (__usercall eax=fn(name@eax, town@ecx)). Walks the scene collecting every
// building whose model name == `name` (up to 32), then RNG-picks one and returns
// its record pointer (0 if none matched). The scene walk is provided by the ops
// hook (EnumBuildings gives the candidate set; the name match is applied here).
// `nameOf` maps a candidate record to its model name for the comparison.
// ===========================================================================
void* Animal_PickSpawnBuilding(int town, const char* name,
                               const char* (*nameOf)(void*));

// ===========================================================================
// gilde.exe 0x4839f0 — VIBE_Animal_FindHerdGrouping (__usercall eax=fn(rec@eax)).
// If the animal's herd count (AnimalRec+8) is uncomputed (-1), gather up to 16
// nearby pasture buildings (within 6000 tolerance of an RNG-chosen anchor) and
// store their world positions in the record's herd buffer; stamp the count.
// Returns the record pointer (the original returns `result` == the record).
// ===========================================================================
AnimalRec* Animal_FindHerdGrouping(AnimalRec* rec);

// Test/integration helper exposing the herd grouping math (HerdGroupFrom) with an
// explicit candidate set + optional out-buffer (the gathered x,y,z point triples).
// Returns the gathered float count (the value stamped into AnimalRec+8).
int Animal_HerdGroupFrom(const BuildingAnchor* cand, int n, float* outPoints);

// ===========================================================================
// gilde.exe 0x484160 — VIBE_Animal_FindDoorTarget
//   (__usercall eax=fn(this@ecx, outPos@eax)). RNG-pick a house among the town's
// non-production/non-storage buildings, locate its door ("dummy_TUER") frame (or
// fall back to the building frame), transform the door anchor into world space and
// write it to `outPos[0..2]`. Returns 1 on success, 0 if no candidate buildings.
// `town` is the Person_QueryBegin token (the original's a2 doubles as outPos).
// ===========================================================================
int Animal_FindDoorTarget(int town, float* outPos);

// ===========================================================================
// gilde.exe 0x483e34 — VIBE_Animal_UpdateCat (__usercall al=fn(rec@eax)).
// gilde.exe 0x483fd4 — VIBE_Animal_UpdateSheep (same shape, different odds/tag).
// One AI tick for a pet/livestock animal. If the actor has no pending action
// (actor+296 == 0): roll d100; on a low roll (<=30 cat / <=20 sheep) issue a 1..3
// step wander (BuildWanderPath then a per-waypoint walk action); otherwise issue a
// random 1..3 animal sound. Returns the last sub-result byte (faithful to the
// original's `al`). `actionPending` reports actor+296 (0 == idle).
// ===========================================================================
char Animal_UpdateCat(AnimalRec* rec, int actionPending);
char Animal_UpdateSheep(AnimalRec* rec, int actionPending);

// ===========================================================================
// gilde.exe 0x484424 — VIBE_Animal_ResetModelHandles. Releases every loaded
// animal mesh (handle != 0) and zeroes the table (the original's off-by-one write
// of dword_B59BBC[i+1] is preserved as a behaviour note; we zero index i).
// ===========================================================================
void Animal_ResetModelHandles();

// ===========================================================================
// gilde.exe 0x484468 — VIBE_Animal_LoadModels. Resets the table then loads the 7
// animal meshes (hund, katze, kuh, pferd, schaf, pferd(again), schwein) into it,
// bumping the count each time. Returns the final count.
// ===========================================================================
int Animal_LoadModels();

}  // namespace guild::sim
