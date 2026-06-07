// real_hooks4 — fourth-wave cross-module "real wiring" installer. Glue only:
// binds the command-apply2 object-stock / building free-remove leaves and the
// command-apply3 person-create / object-resolve leaves to their real
// reconstructed module targets (the object module's g_sceneNodes node-array
// mutators, the building lifecycle's g_buildingPersons RemoveAndCleanup, the
// person create module's g_persons allocator, and the shared entity resolver).
// See real_hooks4.h for the full hook -> target table and the remaining-stub list.
//
// This file contains NO module logic — only the indirection that connects the
// already-translated modules to each other.
#include "sim/real_hooks4.h"

#include "sim/command_apply2.h"   // Set{Remove,Add,Decrement}ObjektHook / SetBuilding{Free,Remove}Hook
#include "sim/command_apply3.h"   // SetPersonCreateHook / SetObjectFindHook
#include "sim/object.h"           // GameObject{Remove,Add,Decrement}* (g_sceneNodes mutators)
#include "sim/building_lifecycle.h" // Building_RemoveAndCleanup / BuildingPersonAt
#include "sim/person_create.h"    // Person_CreateAndSpawn / PersonSpawnArgs
#include "sim/entity.h"           // GameObjectResolveEntityById (shared record probe)

namespace guild::sim {

namespace {

// =========================================================================
// object stock mutation -> real object module (operates on shared g_sceneNodes).
// command_apply2.h ObjStockOpFn is `int(*)(i32 containerId, i32 proto, i32 amount)`
// returning nonzero on success. The handler hands us the RESOLVED container entity
// id (RemapIdAt), the proto key (low byte of the type word) and the amount.
//
//   SetRemoveObjektHook    (0x0F src side)  -> VIBE_GameObject_RemoveObjektAmount
//   SetAddObjektHook       (0x0F/0x10 dst)  -> VIBE_GameObject_AddObjektToParent
//   SetDecrementObjektHook (0x10 src side)  -> VIBE_GameObject_DecrementObjektStock
//
// The object module's AddObjektToParent returns the affected node index (>=0) or
// -1; RemoveObjektAmount/DecrementObjektStock return 1 on success / 0 on over-draw
// or miss. The hook contract treats "nonzero == success" (the handler short-
// circuits on 0 for the remove/add legs, and ignores the decrement return), so we
// normalise each module return to that boolean shape.
// =========================================================================
int RealRemoveObjekt(i32 containerId, i32 proto, i32 amount) {
    return GameObjectRemoveObjektAmount(containerId, static_cast<i16>(proto), amount)
               ? 1
               : 0;
}

int RealAddObjekt(i32 containerId, i32 proto, i32 amount) {
    // AddObjektToParent returns the node index (>=0) on success, -1 on failure.
    return GameObjectAddObjektToParent(containerId, static_cast<i16>(proto), amount) >= 0
               ? 1
               : 0;
}

int RealDecrementObjekt(i32 containerId, i32 proto, i32 amount) {
    return GameObjectDecrementObjektStock(containerId, static_cast<i16>(proto), amount)
               ? 1
               : 0;
}

// =========================================================================
// building free / remove -> real building lifecycle (shared g_buildingPersons).
// command_apply2.h BuildingFreeFn is `int(*)(i32 objectId)` returning nonzero on
// FAILURE (ExFreeBuildingOrScene's object leg: `if (g_buildingFree(obj->id)) return
// 1;`). BuildingRemoveFn is `void(*)(i32 sceneId)` (the person leg). The real leaf
// is VIBE_Building_RemoveAndCleanup (building_lifecycle), which indexes
// word_12CE910[268*slot] — i.e. the resolved id IS the building-person slot index
// the original threaded through `eax`. We free the slot (freeSlot=true), reporting
// failure only when the slot index is out of range (no record to free).
// =========================================================================
int RealBuildingFree(i32 objectId) {
    if (!BuildingPersonAt(objectId))
        return 1; // out of range -> failure (matches the "couldn't free" branch)
    Building_RemoveAndCleanup(objectId, /*freeSlot=*/true);
    return 0; // success
}

void RealBuildingRemove(i32 sceneId) {
    if (BuildingPersonAt(sceneId))
        Building_RemoveAndCleanup(sceneId, /*freeSlot=*/true);
}

// =========================================================================
// person create / spawn -> real person create module (shared g_persons).
// command_apply3.h PersonCreateFn is `i32(*)(u8 kind, u8 a, u8 b)` returning the
// new entity id (>=0) or <0 on failure. The real leaf is VIBE_Person_CreateAndSpawn
// (person_create), whose argument bundle is a PersonSpawnArgs; the apply handler
// only carries the kind byte and two spawn-context bytes (a6/a7 -> record +356/
// +357), the rest of the bundle is resolved from the packet's parent id (deferred).
// We fold the three available bytes into the bundle and return the new slot index,
// mapping the 0xFFFF "array full / not loaded" sentinel to -1 (the handler's
// failure code). The allocation mutates the shared g_persons / g_personIds arrays.
// =========================================================================
i32 RealPersonCreate(u8 kind, u8 a, u8 b) {
    PersonSpawnArgs args{};
    args.kind = kind;
    args.parentAId = -1;
    args.ownerWord = 0;
    args.parentBId = -1;
    args.queryRec = 0;
    args.a6 = a;      // record +356 spawn-context byte
    args.a7 = b;      // record +357 spawn-context byte
    args.a8 = 0;      // gender/seed byte
    u16 slot = Person_CreateAndSpawn(args);
    if (slot == 0xFFFF)
        return -1;    // array full / not loaded -> handler failure
    return static_cast<i32>(slot);
}

// =========================================================================
// object resolve-by-id -> real shared entity resolver.
// command_apply3.h ObjectFindFn is `int(*)(i32 id)` returning nonzero on hit
// (ExDeselectObject / the walk handlers gate on it). The default already delegates
// to BuildingFindById; we bind the FULLER VIBE_GameObject_ResolveEntityById probe
// (object OR scene OR person record), matching the original's broader resolve.
// =========================================================================
int RealObjectFind4(i32 id) {
    ObjectRec* obj = nullptr;
    SceneNode* scene = nullptr;
    Person* person = nullptr;
    return GameObjectResolveEntityById(&obj, &scene, id, &person) ? 1 : 0;
}

} // namespace

void InstallRealSimHooks4() {
    // --- object stock mutation -> real object module -------------------------
    SetRemoveObjektHook(&RealRemoveObjekt);
    SetAddObjektHook(&RealAddObjekt);
    SetDecrementObjektHook(&RealDecrementObjekt);

    // --- building free / remove -> real building lifecycle -------------------
    SetBuildingFreeHook(&RealBuildingFree);
    SetBuildingRemoveHook(&RealBuildingRemove);

    // --- person create / object resolve -> real entity modules ---------------
    SetPersonCreateHook(&RealPersonCreate);
    SetObjectFindHook(&RealObjectFind4);
}

} // namespace guild::sim
