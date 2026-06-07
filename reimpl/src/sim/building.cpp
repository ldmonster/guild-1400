#include "sim/building.h"

#include <cstring>

namespace guild::sim {

// ===========================================================================
// Type table + record store.
// ===========================================================================
BuildingTypeDef g_buildingTypes[kBuildingTypeCapacity];
bool g_buildingTypesLoaded = false;

const BuildingTypeDef* BuildingTypeDefAt(u8 typeIndex) {
    if (!g_buildingTypesLoaded)
        return nullptr;
    return &g_buildingTypes[typeIndex];
}

void ResetBuildings() {
    std::memset(g_buildingTypes, 0, sizeof(g_buildingTypes));
    g_buildingTypesLoaded = false;
    SetBuildingCounters(0, 0);
}

// The buildings module reuses the shared object array (sim/entity.cpp). Declared
// there; we view its records as BuildingRec. We forward-declare the storage to
// avoid pulling entity.h's full surface into this header.
extern ObjectRec g_objects[kObjectCapacity];

// gilde.exe 0x587b20 — VIBE_Building_FindById
//   v2 = 0;
//   while (!*(BYTE*)(base+v2) || id != *(DWORD*)(base+v2+1)) {
//       v2 += 169; if (v2 >= 43264) return 0;
//   }
//   return base + v2;
ObjectRec* BuildingFindRecordById(i32 id) {
    for (int i = 0; i < kObjectCapacity; ++i) {
        if (g_objects[i].alive && g_objects[i].id == id)
            return &g_objects[i];
    }
    return nullptr;
}

// ===========================================================================
// Type-table-driven accessors.
// ===========================================================================

// gilde.exe 0x5902ac — VIBE_Building_GetSecurityLevel
//   *(BYTE*)(589 * *a1 + dword_13CE294 + 583)
int Building_GetSecurityLevel(const BuildingRec* b) {
    const BuildingTypeDef* td = BuildingTypeDefAt(static_cast<u8>(b->typeIndex));
    if (!td)
        return 0;
    return td->security;
}

// gilde.exe 0x5902f4 — VIBE_Building_CheckSecurityThreshold
//   SecurityLevel = GetSecurityLevel(a1);
//   v4 = *(BYTE*)(typeBase + 583);   // == the same security byte (mode)
//   if (v4 == 1) return 1 >= SecurityLevel;
//   if (v4 != 2) { if (v4 == 3) a2 = 7; return a2 >= SecurityLevel; }
//   return SecurityLevel <= 2;
bool Building_CheckSecurityThreshold(const BuildingRec* b, int reqLevel) {
    int securityLevel = Building_GetSecurityLevel(b);
    const BuildingTypeDef* td = BuildingTypeDefAt(static_cast<u8>(b->typeIndex));
    u8 mode = td ? td->security : 0;   // *(BYTE*)(v3 + 583)
    if (mode == 1)
        return 1 >= securityLevel;
    if (mode != 2) {
        if (mode == 3)
            reqLevel = 7;
        return reqLevel >= securityLevel;
    }
    return securityLevel <= 2;
}

// gilde.exe 0x58fc84 — VIBE_Building_GetUpgradeLevel
//   *(int*)(a1 + 89) >> 24   (arithmetic shift of the packed dword)
int Building_GetUpgradeLevel(i32 upgradePacked) {
    return upgradePacked >> 24;
}

// gilde.exe 0x589768 / 0x589770 — global build counters.
static int g_counterA = 0;   // dword_647724
static int g_counterB = 0;   // dword_64771C
int  Building_GetCounterA() { return g_counterA; }
int  Building_GetCounterB() { return g_counterB; }
void SetBuildingCounters(int a, int b) { g_counterA = a; g_counterB = b; }

// gilde.exe 0x587f50 — VIBE_Building_IsStorageType (record wrapper)
bool Building_IsStorageType(const BuildingRec* b) {
    const BuildingTypeDef* td = BuildingTypeDefAt(static_cast<u8>(b->typeIndex));
    return td && Building_IsStorageKind(td->kind);
}

// gilde.exe 0x587f80 — VIBE_Building_IsProductionType (record wrapper)
//   The original returns the pointer unchanged when a1 is null; here a null
//   record is simply "not production".
bool Building_IsProductionType(const BuildingRec* b) {
    if (!b)
        return false;
    const BuildingTypeDef* td = BuildingTypeDefAt(static_cast<u8>(b->typeIndex));
    return td && Building_IsProductionKind(td->kind);
}

// gilde.exe 0x5878b0 — VIBE_Building_MapTypeToCategory (record wrapper)
u8 Building_MapTypeToCategory(const BuildingRec* b) {
    const BuildingTypeDef* td = BuildingTypeDefAt(static_cast<u8>(b->typeIndex));
    if (!td)
        return 0;
    return Building_MapKindToCategory(td->kind);
}

// ===========================================================================
// Command-lockstep hook.
// ===========================================================================
static IBuildingCommandSink  g_defaultSink;
static IBuildingCommandSink* g_sink = &g_defaultSink;
void SetBuildingCommandSink(IBuildingCommandSink* sink) {
    g_sink = sink ? sink : &g_defaultSink;
}
IBuildingCommandSink* BuildingCommandSink() { return g_sink; }

// ===========================================================================
// DEFERRED — building functions NOT translated in this module and why.
// ===========================================================================
// These remain to be ported; each is GUI/cutscene/render/script-coupled, drives
// state through the command channel only (mock the sink above), or depends on
// subsystems outside the buildings module (He_* handlers, Inventory_*,
// Transform_*/Universe_* render, the per-building production-SLOT table at
// dword_13C3B50/13C3B5C, Person-array internals, MeisterAi, City/Amt).
//
//  -- UI / window / cutscene (presentation) --
//   0x51defc VIBE_Building_EnterAndDispatch            building-enter UI/dispatch
//   0x51e88c VIBE_Building_EnterForeignShop            shop UI
//   0x51ed98 VIBE_Building_EnterScriptedLocation       .esc script location
//   0x51f0c8 VIBE_Building_RunCameraTransition         camera cutscene
//   0x51db4c VIBE_Building_SelectRoomToEnter           room-select UI
//   0x50de7c VIBE_Building_OpenGebaeudeBauenWindow     build window
//   0x50e784 VIBE_Building_OpenStadtBauenWindow        city-build window
//   0x50ed14 VIBE_Building_HandleSelectionClick        mouse selection
//   0x50f7c0 VIBE_Building_OpenUpgradeWindow           upgrade window
//   0x50febc VIBE_Building_RunUpgradeLoop              upgrade UI loop
//   0x594100 VIBE_Building_OpenUpgradeTreeWindow       upgrade-tree window
//   0x5942b0 VIBE_Building_CloseUpgradeWindow          window close
//   0x589d78 VIBE_Building_DrawProductionGauge         HUD gauge draw
//   0x50f698 VIBE_Building_TeleportPlayerToJail        camera/teleport
//   0x4f6d28 VIBE_Building_GateStateMachine            gate animation FSM (+stubs)
//   0x4f704c/0x4f70a0/0x4f72a0 gate reset/sync/register handlers
//   0x4f73e8/0x4f73ec/0x4f740c/0x4f710c gate callback stubs
//   0x40e2b4 VIBE_Building_Update                      per-frame building update
//
//  -- mesh / placement / render --
//   0x50d01c VIBE_Building_LoadAndAlignGebaeudeModel   model load + terrain align
//   0x50cfd0 VIBE_Building_AlignMeshToTerrain          mesh align
//   0x50cec0 VIBE_Building_ComputePlacementHeight      Universe/Transform render
//   0x50ce78 VIBE_Building_ResetPlacementMarker        render marker
//   0x576ac4 VIBE_Building_BuildGebaeudePath           pathfinding
//   0x50cb4c VIBE_Building_CheckPlotConnectivity       plot graph (render coords)
//   0x50c7b0/0x50c8ec/0x50cac4 bauplatz candidate/filter/reserve (render coords)
//   0x50ccbc/0x50ccfc/0x50cd94/0x50cf24 bauplatz reserve/find/pos
//   0x50ca6c VIBE_Building_TestVorgartenPlot           plot string-match (io)
//
//  -- production-SLOT table + market price + value aggregators: TRANSLATED in
//     src/sim/building_production.{h,cpp} + building_storage.{h,cpp}:
//       0x584ec8 ComputeSlotYield, 0x584d34 ComputeSlotInput,
//       0x584de8 ComputeSlotOutput (Person-iter -> IProductionHooks),
//       0x5851fc FindSlotByProt, 0x585198 GetSlotYieldByProt,
//       0x5847a0 RunProductionTick, 0x583c3c RecalcAllProduction,
//       0x58f3d0 ComputeMarketPrice, 0x583304 GameTime_PackToRecord,
//       0x591658 SumStorageItemWorth, 0x590360 ComputeStockValue,
//       0x59116c ComputeRoomWorth.
//     The recovered byte-exact layouts: ProdBuilding (1988-dword/7952B stride,
//     32-dword slots), ProdSchedule (756B keyframe table), SceneTypeDef (65B).
//
//  -- production-SLOT table — STILL DEFERRED (state machine / net / sync) --
//   0x57ca78 VIBE_Building_UpdateProductionState       state machine
//   0x583d38 VIBE_Building_SyncProductionState         net sync (-> IProductionHooks)
//   0x583c74 VIBE_Building_ComputeSlotStats            (stats columns; QueryFind)
//   0x57d0f8/0x57d1c8 VIBE_Building_SyncStockLevel / _ComputeProjectedStock
//   0x57d3d0 VIBE_Building_ComputeEfficiencyScore
//   0x57d5b4 VIBE_Building_AdjustStockAndNotify        (mutates -> command sink)
//   0x57d83c VIBE_Building_DistributeGoodsToCustomers  (Person iter + commands;
//            heavy command/Amt-keepalive coupling — deferred to command module)
//   0x57dcb4 VIBE_Building_FindMatchingSupplier
//   0x58f6b8 VIBE_Building_LookupCachedMarketPrice
//   0x58fc8c VIBE_Building_ComputeWorkstationFillRatio (Person iter)
//   0x5904fc/0x5905dc workstation sum-by-category / count (Person iter)
//
//  -- value aggregators — partially translated --
//   0x58fe68 VIBE_BuildingValue_ComputeProductionWorth  STILL DEFERRED (walks the
//            whole 768-Person array + 21-dword result frame + He_* handlers)
//   0x590df0 VIBE_BuildingValue_RetConst12   (trivial const stub)
//   0x5913e0 VIBE_Building_SumFlaggedSlotsWorth
//   0x591480 VIBE_Building_ComputeSalePrice
//
//  -- create / destroy / lifecycle (mutating -> command sink) --
//   0x586fb8 VIBE_Building_CreateGebaeude               STILL DEFERRED — operates
//            on the Person array (buildings-as-persons) + scene AddObjekt +
//            8C4788/8C4790 name tables + history; ExCreateGebaeude path.
//   0x5894b0 VIBE_Building_RemoveAndCleanup             TRANSLATED in
//            src/sim/building_lifecycle.{h,cpp} (slot-free + counter + hooks).
//   0x5896fc VIBE_Building_ResetAllBuildings
//   0x586df8 VIBE_Building_EnsureDefaultObjects
//   0x586ed8 VIBE_Building_InitWorkerCapacities
//   0x588988/0x588ce4 storage-room alloc/remove   (scene AddObjekt — deferred;
//            modeled via IStorageHooks in building_storage)
//   0x588554 VIBE_Building_AttachStorageRooms
//   0x58820c VIBE_Building_SetObjectParent
//   0x588d00 VIBE_Building_DetachAndDestroyOccupant
//   0x589468 VIBE_Building_ReleaseOccupantHoldings  (-> ILifecycleHooks)
//   0x5843c8 VIBE_Building_ApplyUpgradeStaff
//   0x584680 VIBE_Building_RandomizeStockTransforms (-> IProductionHooks)
//   0x588798 VIBE_Building_EnqueueBuyBuilding           -> IBuildingCommandSink
//   0x5942bc VIBE_Building_RequestGebaeudeBauen         -> IBuildingCommandSink
//   0x5949b4 VIBE_Building_QueueCommandForAll           -> IBuildingCommandSink
//   0x59361c VIBE_Building_BuildUpgradeTree   STILL DEFERRED — pure UI: builds the
//            upgrade-tree windows (Window_AddChildWindow / Object_AddToWindow /
//            Text_RenderRichString), no game-state mutation.
//
//  -- queries needing Person/scene arrays + transforms --
//   0x587908 VIBE_Building_FindNearestSameType  TRANSLATED in building_lifecycle
//            (Person scan + distance via ILifecycleHooks::BuildingDistanceSq).
//   0x586904 VIBE_Building_FindActiveWorkSlot   STILL DEFERRED (scene QueryFind).
//   0x587a28 VIBE_Building_FindNearestVacantSameType
//   0x5918e0/0x591870 VIBE_Building_CollectOwnedByPerson / _CollectByCityHandle
//   0x59287c VIBE_Building_PopulateOccupantList
//   0x586904 VIBE_Building_FindActiveWorkSlot
//   0x587674/0x5877ac find work-product / storable object
//   0x58faa8/0x58fb30/0x58fbb4 collect object/storable/after-object slots
//   0x5880b4/0x588044 flag-node list build/callback
//   0x5885f0 VIBE_Building_CollectEntrySlots
//   0x588dec VIBE_Building_ComputeSelectionFlags
//   0x58a044/0x58a154 occupant/profession category sync
//   0x58a280/0x58a294/0x58a354 office storage helpers
//   0x58aaf4 VIBE_Building_PickRandomDiseaseEvent
//   0x592a5c VIBE_Building_MapTypeToState
//   0x594cfc VIBE_Building_FindOwnedDungeonSlot
//   0x589d24 VIBE_Building_GetGategoryForObject
//   0x587bfc VIBE_Building_CheckBuildRequirements
//   0x504a54/0x50c738/0x587fcc building-name registration / lookup (io/strings)
//   0x46c97c VIBE_Building_EvalBuyBuilding              (AI eval, MeisterAi)
//
//  -- Bauplatz (plot) mapping (render/supermap coupled) --
//   0x576830 VIBE_Bauplatz_AppendMarker
//   0x577464/0x5774b8 VIBE_Bauplatz_MapAllToSupermap / _MapOneToSupermap
//   0x577628 VIBE_Bauplatz_GetSize   (string table lookup; io-coupled)
//
//  -- BuildingType return-code thunks (inlined into the mappers above) --
//   0x589a32..0x589a44 VIBE_BuildingType_ReturnCodeNN  (epilogue thunks; the
//     return constants are folded into building_type.cpp's switch tables).
//   0x589778/0x5897c8 VIBE_Building_LookupTypeRecordA/B (table base wrappers)

}  // namespace guild::sim
