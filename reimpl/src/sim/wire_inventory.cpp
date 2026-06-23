// See wire_inventory.h. Binds the inventory / build-plot / command-target /
// command-emit bridges to their real reconstructed cross-cluster leaves. Glue only —
// no module logic.
//
// All id->record resolves go through the SAME real entity arrays the rest of the
// real wiring owns (entity.h g_persons / g_objects / scene tree, object.h count
// scan). The special-target sentinels read the SAME last-created globals the apply
// dispatcher fills (command_apply.h g_lastObjectId/Scene/Trade). Person/ObjectRec/
// SceneNode are raw POD blobs over the record-base byte layout (id @+4), so the
// void* <-> typed-record reinterpret_casts here are byte-faithful, exactly as
// real_hooks3 / wire_charaction already cast between the record views.
#include "sim/wire_inventory.h"

#include "sim/inventory2.h"          // Inventory2Hooks / Set/GetInventory2Hooks
#include "sim/command_apply9.h"      // BuilderHooks / Set/GetBuilderHooks
#include "sim/command_apply8.h"      // CommandApply8Hooks / Set/GetCommandApply8Hooks
#include "sim/command_apply10.h"     // ApplyTargetHooks / Set/GetApplyTargetHooks / ResolvedEntity
#include "sim/buildingtype_recon.h"  // IBauplatzMapHooks / SetBauplatzMapHooks

#include "sim/entity.h"              // GameObjectResolveEntityById / PersonFindRecordById
#include "sim/object.h"             // GameObjectCountAtLocation
#include "sim/building_storage.h"    // BuildingValue_ComputeRoomWorth
#include "sim/building_types.h"      // BuildingRec
#include "sim/pathfind_map.h"        // MapRasterizeBauplatzEdge
#include "sim/command_apply.h"       // g_lastObjectId / g_lastSceneId / g_lastTradeId
#include "crt/rand.h"                // guild::crt::RandNext

#include <cstring>

namespace guild::sim {

namespace {

// ===========================================================================
// ApplyTargetHooks adapters (command_apply10) -> real entity / value leaves.
// ===========================================================================

// VIBE_GameObject_ResolveEntityById @0x583b44. The recon's ResolvedEntity triple
// aliases the real leaf's three out-pointers (see wire_inventory.h for the full
// derivation from the @0x495cf8 call site):
//   e.immediate <- objOut, e.parent <- sceneOut, e.object <- personOut.
// The original passes all three out-pointers non-null; the real leaf then searches
// Person first (outPerson != null) and returns 1/2/3 (object/scene/person) or 0.
// The hook treats nonzero as "found".
int WiResolveEntityById(i32 id, void* /*caller*/, ResolvedEntity* out) {
    ObjectRec* obj = nullptr;
    SceneNode* scene = nullptr;
    Person* person = nullptr;
    int r = GameObjectResolveEntityById(&obj, &scene, id, &person);
    if (out) {
        out->immediate = obj;
        out->parent    = scene;
        out->object    = person;
    }
    return r;
}

// The -2/-3/-4 special-target sentinels: dword_631288/63128C/631290, which are the
// command_apply.h last-created globals the apply dispatcher maintains.
i32 WiSpecialTarget(i32 sentinel) {
    switch (sentinel) {
        case -2: return g_lastObjectId; // dword_631288
        case -3: return g_lastSceneId;  // dword_63128C
        case -4: return g_lastTradeId;  // dword_631290
        default: return 0;
    }
}

// VIBE_Person_FindRecordById @0x58bc6c.
void* WiPersonFindRecordById(i32 id) {
    return reinterpret_cast<void*>(PersonFindRecordById(id));
}

// VIBE_GameObject_CountAtLocation @0x58f1f4.
i32 WiCountAtLocation(i32 loc) {
    return GameObjectCountAtLocation(loc);
}

// VIBE_BuildingValue_ComputeRoomWorth @0x59116c. The recon caller (ResolveTargetBest-
// Thief) already supplies kind = *(record+89) >> 24 (the type's room mul); the real
// leaf takes (building, mul) and ignores the original's `table` arg (matching the
// recon, which passes table=nullptr). record is a raw building-record blob.
i32 WiRoomWorth(void* record, i32 kind, void* /*table*/) {
    return BuildingValue_ComputeRoomWorth(reinterpret_cast<const BuildingRec*>(record),
                                          kind);
}

// *(record + 4) == the resolved record's 32-bit entity id (Person/ObjectRec id @+4).
i32 WiRecordEntityId(void* record) {
    if (!record) return 0;
    i32 v;
    std::memcpy(&v, static_cast<const u8*>(record) + 4, 4);
    return v;
}

// ===========================================================================
// CommandApply8Hooks adapter (command_apply8) -> the real CRT LCG.
// ===========================================================================

// VIBE_Util_RandNext @0x5cb8bc — the opcode-34 spawn nonce. crt::RandNext returns
// the LCG's bits 16..30 as an int; the hook surfaces it as a u32 (byte-identical).
u32 WiRandNext() {
    return static_cast<u32>(crt::RandNext());
}

// ===========================================================================
// BauplatzMapHooks adapter (buildingtype_recon) -> the real edge rasteriser.
// ===========================================================================

// VIBE_Map_RasterizeBauplatzEdge @0x5776d8. The original's single fill byte `fill`
// drives BOTH a terrain write (when fill != -1) and a cell write (when fill != 255);
// the recon splits it into (fillTerrain, fillCell). The only call site
// (Bauplatz_MapOneToSupermap @0x5774b8) passes fill = 255, so terrain is written and
// the cell write is skipped — reproduced exactly by (fillTerrain=255, fillCell=255).
// The recon returns the per-edge subdivision step count (the original's `result`);
// the actual supermap byte pokes are inert at the recon level (mapCtx is unused).
class WiBauplatzMapHooks final : public IBauplatzMapHooks {
public:
    // WorldToTile stays the base default (inert): the real Coord_WorldToTile
    // (util/coord_worldtile_misc_recon.h) needs a CoordMapRecord materialised from
    // the opaque mapCtx's raw map-record bytes + a pivot transform — the
    // unreconstructed map subsystem (rule 8). Not overridden.
    int RasterizeBauplatzEdge(int /*mapCtx*/, const float quad[8], int fill) override {
        return MapRasterizeBauplatzEdge(quad, /*fillTerrain=*/fill, /*fillCell=*/fill);
    }
};

// ===========================================================================
// Process-lifetime wired hook tables (the global hook ptrs reference these).
// ===========================================================================
Inventory2Hooks    g_inv2{};
BuilderHooks       g_builder{};
CommandApply8Hooks g_apply8{};
ApplyTargetHooks   g_applyTarget{};
WiBauplatzMapHooks g_bauplatzMap{};

} // namespace

void InstallRealInventoryWiring() {
    // --- ApplyTargetHooks (command_apply10) --------------------------------
    // Seed from the module inert defaults, then override the real-bindable leaves.
    g_applyTarget = GetApplyTargetHooks();
    g_applyTarget.resolveEntityById    = &WiResolveEntityById;
    g_applyTarget.specialTarget        = &WiSpecialTarget;
    g_applyTarget.personFindRecordById = &WiPersonFindRecordById;
    g_applyTarget.countAtLocation      = &WiCountAtLocation;
    g_applyTarget.roomWorth            = &WiRoomWorth;
    g_applyTarget.recordEntityId       = &WiRecordEntityId;
    // queryFind / queryIterNext / personQueryBegin / personIterNext (varargs filter,
    // rule 8) / invFreeCapacity / invCarryCapacity / resolveOwnerOrParentB (raw
    // scene/person void* -> typed view) / selectionMatch (global word_12CE910 table):
    // no clean reconstructed target -> inert (documented in wire_inventory.h).
    SetApplyTargetHooks(&g_applyTarget);

    // --- CommandApply8Hooks (command_apply8) -------------------------------
    g_apply8 = GetCommandApply8Hooks();
    g_apply8.randNext = &WiRandNext;
    // localPlayerMoney (dword_12CE914 table) / copyState23/24Blob (dword_11AA3E0/360)
    // / objectTemplate34 (dword_632240 spawn template) / gesetzGetRecord /
    // gesetzRequestApply (law table dword_631EB0): process-global game-state tables,
    // not standalone callable leaves -> inert.
    SetCommandApply8Hooks(&g_apply8);

    // --- BauplatzMapHooks (buildingtype_recon) -----------------------------
    // The interface install stores the pointer directly (null -> module default);
    // our subclass overrides only RasterizeBauplatzEdge, leaving WorldToTile inert.
    SetBauplatzMapHooks(&g_bauplatzMap);

    // --- Inventory2Hooks (inventory2) --------------------------------------
    // Fully inert (seeded): the entire table is the inventory GRID-UI seam (window /
    // object / widget / scene-graph / surface / text / HUD / drag-slot, rules 3-5),
    // plus two engine-math fields (buildingGroup / buildingOutputRatio) that read a
    // raw void* building layout living only in the unreconstructed object world
    // (rule 8). Seed-from-defaults so the slot reconciliation runs against the safe
    // inert stubs without crashing.
    g_inv2 = GetInventory2Hooks();
    SetInventory2Hooks(&g_inv2);

    // --- BuilderHooks (command_apply9) -------------------------------------
    // Fully inert (seeded): op80SnapshotDword (*dword_6315C0) / combatUnitField9
    // (needs the global combat-unit pool) / errorLog (the game error log): all
    // process-global / cross-module with no clean callable leaf.
    g_builder = GetBuilderHooks();
    SetBuilderHooks(&g_builder);
}

} // namespace guild::sim
