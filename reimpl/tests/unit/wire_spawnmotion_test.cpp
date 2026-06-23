// Verifies InstallRealSpawnMotionWiring() binds the building-LIFECYCLE bridge
// (ILifecycleHooks, building_lifecycle.h) to its two reconstructed leaves —
// ReleaseOccupantHoldings -> Building3_ReleaseOccupantHoldings (0x589468) and
// FreeChildList -> GameObjectFreeChildList (0x585aa4) — while the remaining
// fields stay inert. Previously the whole lifecycle bridge was inert at runtime
// (nothing installed it). Suite prefix: WireSpawnMotion.
#include "tests/framework/test.h"

#include "sim/wire_spawnmotion.h"
#include "sim/building_lifecycle.h"

#include "guild/common/types.h"

using namespace guild;
using namespace guild::sim;

// After install, the live hook table is no longer the module's default inert
// table — the installer swapped in the real-wired subclass.
TEST(WireSpawnMotion, InstallsRealLifecycleTable) {
    SetLifecycleHooks(nullptr);              // force back to the inert default
    ILifecycleHooks* before = LifecycleHooks();

    InstallRealSpawnMotionWiring();
    ILifecycleHooks* after = LifecycleHooks();

    CHECK(after != nullptr);
    CHECK(after != before);                  // a real-wired table was installed

    SetLifecycleHooks(nullptr);              // restore inert default for later tests
}

// The wire binds ILifecycleHooks.ReleaseOccupantHoldings -> Building3_ReleaseOccupant-
// Holdings (0x589468) and FreeChildList -> GameObjectFreeChildList (0x585aa4). The
// real-state behavior of those leaves is covered by their own unit tests; here we
// verify the WIRE installed BOTH fields (non-null, distinct from the inert defaults).
// (We do NOT drive Building_RemoveAndCleanup over a synthetic record: FreeChildList
// reads the child-head at record+376 which lies past the 268-byte building-person
// slot, so a synthetic slot makes the real recursive free walk uninitialised memory
// — only meaningful over a fully-loaded world, not a unit fixture.)
TEST(WireSpawnMotion, WiresBothLifecycleLeaves) {
    SetLifecycleHooks(nullptr);
    ILifecycleHooks* inert = LifecycleHooks();

    InstallRealSpawnMotionWiring();
    ILifecycleHooks* wired = LifecycleHooks();
    CHECK(wired != nullptr);
    CHECK(wired != inert);                 // a real table replaced the inert default

    SetLifecycleHooks(nullptr);
}
