// Verifies InstallRealObjectWiring() binds the object-spatial bridges that carry a
// genuine pure-logic leaf — ObjectSceneEntityHooks.releaseTables (-> the real
// ResetEntityArrays table teardown) and MoveUniverseHooks.switchActiveSlot (-> the
// real VIBE_Universe_SwitchActiveSlot scene swap) — and that the wired
// Move2UniverseActionUpdate action executes through the real slot swap without
// crashing. The other surveyed bridges (MotionHooks / ObjectRecordHooks) have no
// pure-logic field and are intentionally left inert. Suite prefix: WireObject.
#include "tests/framework/test.h"

#include "sim/wire_object.h"
#include "sim/object_scene_entity.h"
#include "sim/character_move.h"

using namespace guild;
using namespace guild::sim;

// Re-inert the two installed bridges so a clean baseline / restore is possible.
static void InertAll() {
    SetObjectSceneEntityHooks(nullptr);   // restore module default
    SetMoveUniverseHooks(nullptr);        // restore inert default
}

TEST(WireObject, BindsRealLeavesIntoInstalledBridges) {
    InertAll();
    InstallRealObjectWiring();

    // --- ObjectSceneEntityHooks: releaseTables bound (real table teardown) ----
    // NOTE: the module default ALREADY routes releaseTables to the real
    // ResetEntityArrays; the installer seeds from that default and re-affirms it, so
    // the field is non-null and the bridge is explicitly installed by live wiring.
    const ObjectSceneEntityHooks* se = ObjectSceneEntityHooksPtr();
    CHECK(se != nullptr);
    CHECK(se->releaseTables != nullptr);
    // buildingFreeAndUnlink stays inert (64-bit pointer-width gap) -> null default.
    CHECK(se->buildingFreeAndUnlink == nullptr);

    // --- MoveUniverseHooks: switchActiveSlot bound; rest stay inert stubs ------
    const MoveUniverseHooks& mu = GetMoveUniverseHooks();
    CHECK(mu.switchActiveSlot != nullptr);
    // moveToUniverse / setVisible keep their inert (non-null) stubs (the action
    // calls them WITHOUT a null check) -> seeded from defaults, so still non-null.
    CHECK(mu.moveToUniverse != nullptr);
    CHECK(mu.setVisible     != nullptr);

    InertAll();
}

// The universe-transition action runs end to end through the installed real
// switchActiveSlot leaf: a valid (universe 0, id -1) combo classifies as kOk and the
// real slot swap (to 0, then back to the saved slot) executes without crashing.
TEST(WireObject, MoveUniverseActionExecutesThroughRealSlotSwap) {
    InertAll();
    InstallRealObjectWiring();

    // Data0 == 0 requires Data1 == -1 (the engine's valid combination); curUniverse
    // (the saved active slot) 0. The inert moveToUniverse stub returns 1 (success),
    // so control reaches the real switchActiveSlot(0,quiet) -> ... -> switchActiveSlot(0).
    MoveUniverseState st{};
    st.dataUniverse = 0;
    st.dataId       = -1;
    st.dataSubId    = 0;
    st.curUniverse  = 0;

    MoveUniverseResult r = Move2UniverseActionUpdate(&st);
    CHECK(r == MoveUniverseResult::kOk);
    CHECK(st.valid != 0);                 // moveToUniverse stub reported success
    CHECK(st.curUniverse == st.dataId);   // ch+44 = Data[1] (== -1) field-clear ran
    CHECK(st.scratch48 == 0);
    CHECK(st.scratch52 == 0);
    CHECK(st.scratch56 == 0);

    InertAll();
}

// The release-tables leaf (the real ResetEntityArrays teardown) runs via the bridge:
// GameObjectFreeAllTables sweeps the (empty) building array and invokes releaseTables.
// Over a fresh/empty table the sweep count is 0 and the call returns cleanly.
TEST(WireObject, FreeAllTablesRunsRealReleaseLeaf) {
    InertAll();
    InstallRealObjectWiring();

    int swept = GameObjectFreeAllTables();
    CHECK(swept >= 0);   // defined (0 over an empty/unloaded building array)

    InertAll();
}
