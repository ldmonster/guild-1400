// See wire_objscene.h. Binds the reconstructed object/scene-sync leaves into the
// SceneSyncHooks bridge. Glue only — no module logic.
//
// Of the five bridges this file owns, only SceneSyncHooks has hook fields whose
// exact original leaf is already reconstructed with a 1:1, signature-compatible
// target. The other four bridges (ObjectRecordHooks, UniverseHooks,
// UniverseHiddenToggle, MapLoadCityHooks) are zero-bindable — every field is a
// render/universe/world side-effect leaf or a host boundary that is itself only
// present in the tree as a hook (never as a callable reconstruction), or carries
// a lossy hook signature. Per the wiring contract, a zero-bindable bridge gets
// nothing here; the rationale is recorded in InstallRealObjSceneWiring().
#include "sim/wire_objscene.h"

#include "sim/cutscene_misc5.h"   // SceneSyncHooks / Get/SetSceneSyncHooks
#include "sim/building_type.h"    // Building_MapKindToCategory (0x5878b0)
#include "world/amt.h"            // AmtMoneyMultiplyByRate     (0x58f19c)

#include "guild/common/types.h"

namespace guild::sim {

namespace {

// VIBE_Building_MapTypeToCategory(typeByte) @0x5878b0 — table-driven kind->category.
// SceneSyncHooks.mapTypeToCategory is `int (*)(u8)`; the real leaf returns u8.
int WosMapTypeToCategory(u8 typeByte) {
    return static_cast<int>(Building_MapKindToCategory(typeByte));
}

// VIBE_Money_MultiplyByRate(amount, rate) @0x58f19c — scale `amount` by the
// per-currency rate. SceneSyncHooks.multiplyByRate is `int (*)(int, u8)`; the
// real leaf is i32(i32, u8). In SyncBuildingEntrance the rate byte is byte_6477A1
// (the currency id the original passes), so this is the byte-faithful target.
int WosMultiplyByRate(int amount, u8 rate) {
    return static_cast<int>(guild::world::AmtMoneyMultiplyByRate(amount, rate));
}

// Process-lifetime wired hook table (the global hook ptr references this).
SceneSyncHooks g_sceneSync{};

}  // namespace

void InstallRealObjSceneWiring() {
    // --- SceneSyncHooks (sim/cutscene_misc5.h) -------------------------------
    // Seed from the bridge's current (inert, all-null) defaults, then override
    // only the fields whose exact original leaf is reconstructed 1:1. The
    // scene-sync bodies null-check every field, so unbound fields stay null
    // (inert) — name-matches fail, the frame pump returns 0, loops terminate.
    g_sceneSync = GetSceneSyncHooks();
    g_sceneSync.mapTypeToCategory = &WosMapTypeToCategory;  // 0x5878b0
    g_sceneSync.multiplyByRate    = &WosMultiplyByRate;     // 0x58f19c
    // Inert (no clean 1:1 reconstructed target):
    //   nameMatch          — VIBE_Util_StrCmpNoCase mid-body collector predicate
    //                        (needs the live scene-object name slab; not a leaf).
    //   isProductionType   — VIBE_Building_IsProductionType(rec) @0x587f80 reads
    //                        rec[0] through the 589-stride type table dword_13CE294;
    //                        the reconstructed Building_IsProductionKind takes the
    //                        already-resolved kind byte, NOT the record, so it omits
    //                        the process-global type-table lookup. Not a clean bind.
    //   loadFromStream     — VIBE_Scene_LoadFromStream @0x5e7e38 path/VFS-coupled;
    //                        only the header PARSER (ParseScene) is reconstructed,
    //                        not the path-opening stream loader.
    //   traverseTree       — SceneGraph_TraverseTree render-graph pass.
    //   queueRequest17 / enqueueCmd15 — command builders, but the hook signatures
    //                        DROP the rate/flag args the originals carry, so binding
    //                        a real builder through them would not be byte-faithful.
    //   getSlotCapacity    — Inventory_GetSlotCapacity(obj): the hook takes an object
    //                        handle; the reconstructed InventorySlotCapacity takes
    //                        (type, level), not a handle. Not a clean bind.
    //   spawnChimneySmoke / refreshAllLights (VIBE_Light_RefreshAllObjects 0x5c886c) /
    //   reserveBauplatz / buildTerrainMesh / updateVisualState — render/world
    //                        side-effect leaves that exist in the tree ONLY as hook
    //                        fields, never as callable reconstructions.
    SetSceneSyncHooks(&g_sceneSync);

    // --- ObjectRecordHooks (io/save_serial3.h) — ZERO-BINDABLE ---------------
    // Single field resolveStock: a probe into another module's object slab
    // (rec+136 stock ptr, sub+981 enable byte). No reconstructed resolver exists;
    // the inert default (hasStock=false -> zero templates) is the intended,
    // byte-deterministic behaviour. Nothing to bind.

    // --- UniverseHooks (sim/character_universe.h) — ZERO-BINDABLE -------------
    // moveToUniverse / switchActiveSlot / selectTextureSet / stopSample /
    // placeAtEntryDummy / setVisible: render/universe leaves. They are keyed on
    // this module's own UniverseTransition* and its private g_univActive* mirror
    // globals (separate storage from universe.cpp's active-slot machinery), so the
    // real reconstructed leaves cannot be dropped in faithfully. Inert.

    // --- UniverseHiddenToggle (world_buildingflag_util_recon2.h) — ZERO-BINDABLE
    // walk (SceneGraph_WalkAndInvoke), toggle (Object_ToggleHiddenState), refresh
    // (Light_RefreshAllObjects 0x5c886c): all three are render leaves present in
    // the tree only as hook fields, never as callable reconstructions. Inert.

    // --- MapLoadCityHooks (gui/mission_load_run.h) — ZERO-BINDABLE ------------
    // UniverseSwitchAndReset / SceneEnterCity / SaveWriteGameFile / BuildingResetAll
    // / WorldResetPersonTable / WorldRelinkObjectOwners / ObjectDestroySpawnedEntities
    // / SceneLoadFromStream: world-reset + file-IO + scene-stream host boundaries.
    // The reconstructed counterparts take record-base/out-pointer args (e.g.
    // Building3_ResetAllBuildings(lightRecords, &sel0..)), are only partially
    // reconstructed, or are asset/VFS-coupled — none matches the nullary/path-only
    // hook shape. Left as host boundaries by design (see mission_load_run.h). Inert.
}

}  // namespace guild::sim
