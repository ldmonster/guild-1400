// Verifies InstallRealCombatWiring() binds the one signature-compatible combat-driver
// leaf (CombatDriversHooks.randomModulo -> Math_RandomModulo) into the live bridge,
// leaves every other combat-bridge field at its inert (null) default, and that the
// wired RNG drives DriverRoll over a real modulo stream. UNIQUE prefix WireCombat,
// headless, no main().
#include "tests/framework/test.h"

#include "sim/wire_combat.h"
#include "sim/combat_drivers.h"   // CombatDriversHooks / Set/GetCombatDriversHooks / DriverRoll
#include "sim/combat.h"           // Math_RandomModulo

#include "sim/charaction_brawl.h" // BrawlHooks
#include "sim/combat_slots3.h"
#include "sim/combat_slots4.h"
#include "sim/combat_slots5.h"

using namespace guild;
using namespace guild::sim;

TEST(WireCombat, BindsRandomModuloIntoDriversHooks) {
    // Baseline: a fresh inert table leaves randomModulo null.
    SetCombatDriversHooks(nullptr);
    CHECK(GetCombatDriversHooks().randomModulo == nullptr);

    // After install, randomModulo points at the 1:1 reconstruction.
    InstallRealCombatWiring();
    const CombatDriversHooks& h = GetCombatDriversHooks();
    CHECK(h.randomModulo == &Math_RandomModulo);

    // The other CombatDriversHooks fields stay null -> inert default preserved.
    CHECK(h.soundRangeScale   == nullptr);
    CHECK(h.buildOrderForUnit == nullptr);

    SetCombatDriversHooks(nullptr);  // restore for any later test in this TU
}

// With the RNG wired, DriverRoll(n) runs the real Math_RandomModulo (RandNext % n)
// instead of the inert deterministic 0 — the value is always a defined residue < n.
TEST(WireCombat, WiredDriverRollUsesRealRng) {
    InstallRealCombatWiring();

    // n == 0 -> 0 (the original's `n == 0` guard, preserved in DriverRoll).
    CHECK(DriverRoll(0) == 0);

    // For several moduli the roll is a defined residue in [0, n).
    for (u16 n : {1, 2, 7, 10, 90, 100}) {
        u16 r = DriverRoll(n);
        CHECK(r < n);
    }

    SetCombatDriversHooks(nullptr);
}

// The four sibling combat bridges this installer deliberately leaves inert keep
// their null defaults after the install (they are the host integration test's
// responsibility / mirror the inert GroupInteractHooks posture).
TEST(WireCombat, SiblingBridgesStayInert) {
    // Establish fresh inert defaults, then install.
    SetBrawlHooks(nullptr);
    SetCombatSlots3Hooks(nullptr);
    SetCombatSlots4Hooks(nullptr);
    SetCombatSlots5Hooks(nullptr);
    InstallRealCombatWiring();

    // BrawlHooks: every routed leaf stays null.
    const BrawlHooks& b = GetBrawlHooks();
    CHECK(b.packetStatus           == nullptr);
    CHECK(b.freeHandlerEntry       == nullptr);
    CHECK(b.findPersonById         == nullptr);
    CHECK(b.adjustRelationByMood   == nullptr);
    CHECK(b.registerApEvent        == nullptr);
    CHECK(b.restorePoseAndRequeue  == nullptr);

    // CombatSlots3/4/5: a representative field on each stays null.
    CHECK(GetCombatSlots3Hooks().gameObjectQueryFind == nullptr);
    CHECK(GetCombatSlots4Hooks().spawnBloodPoolMesh  == nullptr);
    CHECK(GetCombatSlots5Hooks().randomModulo        == nullptr);
    CHECK(GetCombatSlots5Hooks().personQueryBegin    == nullptr);

    SetBrawlHooks(nullptr);
    SetCombatSlots3Hooks(nullptr);
    SetCombatSlots4Hooks(nullptr);
    SetCombatSlots5Hooks(nullptr);
}
