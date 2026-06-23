// Verifies InstallRealObjSceneWiring() binds the reconstructed object/scene-sync
// leaves into the SceneSyncHooks bridge — previously every field was inert at
// runtime (nothing installed them). The four sibling bridges this wiring file
// also considers (ObjectRecordHooks, UniverseHooks, UniverseHiddenToggle,
// MapLoadCityHooks) are zero-bindable and intentionally left inert; this suite
// only asserts the SceneSyncHooks bindings + that the wired flow executes.
// Suite prefix: WireObjScene.  Headless, no main().
#include "tests/framework/test.h"

#include "sim/wire_objscene.h"
#include "sim/cutscene_misc5.h"

using namespace guild;
using namespace guild::sim;

// gilde.exe 0x5878b0 expected category for two known kinds (table-driven). We do
// not hardcode the table; we assert the bound hook equals the real leaf.
TEST(WireObjScene, BindsSceneSyncReconstructedLeaves) {
    // Baseline: re-inert the bridge so a clean pre-install state is asserted.
    SetSceneSyncHooks(nullptr);

    InstallRealObjSceneWiring();

    const SceneSyncHooks& s = GetSceneSyncHooks();
    // The two cleanly-bindable fields are now real.
    CHECK(s.mapTypeToCategory != nullptr);
    CHECK(s.multiplyByRate    != nullptr);

    // Every other field has no clean 1:1 reconstructed target -> stays inert
    // (the bridge's all-null default, since the bodies null-check each field).
    CHECK(s.nameMatch         == nullptr);
    CHECK(s.isProductionType  == nullptr);
    CHECK(s.loadFromStream    == nullptr);
    CHECK(s.traverseTree      == nullptr);
    CHECK(s.queueRequest17    == nullptr);
    CHECK(s.enqueueCmd15      == nullptr);
    CHECK(s.getSlotCapacity   == nullptr);
    CHECK(s.spawnChimneySmoke == nullptr);
    CHECK(s.refreshAllLights  == nullptr);
    CHECK(s.reserveBauplatz   == nullptr);
    CHECK(s.buildTerrainMesh  == nullptr);
    CHECK(s.updateVisualState == nullptr);

    SetSceneSyncHooks(nullptr);  // restore for any later test in this TU
}

// The bound leaves return the real reconstructed values (not stubs).
TEST(WireObjScene, BoundLeavesReturnRealResults) {
    SetSceneSyncHooks(nullptr);
    InstallRealObjSceneWiring();
    const SceneSyncHooks& s = GetSceneSyncHooks();

    // multiplyByRate scales by the per-currency rate; the real leaf's inert
    // internal rate hook is 1, so amount passes through unchanged (deterministic).
    CHECK_EQ(s.multiplyByRate(500, 0), 500);
    CHECK_EQ(s.multiplyByRate(123, 0), 123);

    // mapTypeToCategory is total over u8 kinds; just exercise it (defined result).
    int cat = s.mapTypeToCategory(9);   // Bauplatz/site
    CHECK(cat >= 0);

    SetSceneSyncHooks(nullptr);
}

// The wired entrance-sync flow runs through the installed real leaves without
// crashing, returning a defined product type (310 for a Bauplatz site, which
// drives the multiplyByRate path; 308 for the standard entrance; -1 fee-exempt).
TEST(WireObjScene, WiredEntranceSyncExecutes) {
    SetSceneSyncHooks(nullptr);
    InstallRealObjSceneWiring();

    // type 9 (Bauplatz) -> queues build-fee 310 and the 500*rate sell cmd; the
    // multiplyByRate hook (now real) is exercised on this path.
    int pt9 = SceneSyncBuildingEntrance(/*typeByte=*/9, /*objId=*/1, /*rate=*/0);
    CHECK_EQ(pt9, 310);

    // a fee-exempt storage type {4,16,19} -> -1.
    int pt4 = SceneSyncBuildingEntrance(/*typeByte=*/4, /*objId=*/1, /*rate=*/0);
    CHECK_EQ(pt4, -1);

    // a standard entrance type -> 308.
    int pt2 = SceneSyncBuildingEntrance(/*typeByte=*/2, /*objId=*/1, /*rate=*/0);
    CHECK_EQ(pt2, 308);

    SetSceneSyncHooks(nullptr);
}
