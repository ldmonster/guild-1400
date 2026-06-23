// Verifies InstallRealCmdOpsWiring() binds the one byte-faithful reconstructed
// leaf across the command-apply / action-op bridges this agent surveyed:
// OrderDriverHooks's `randomModulo` -> the REAL guild::sim::Math_RandomModulo
// (gilde.exe 0x58b89c, over crt::RandNext). Previously OrderDriverHooks was fully
// inert at runtime (the order-tick attack roll fell back to 0); nothing installed
// the real RNG into the live call tree. Suite prefix: WireCmdOps. Headless: no
// engine, no third-party libs — pure hook-table + a zeroed-slot drive.
//
// The other surveyed bridges (Op85Hooks, Ops4Hooks/FileOps4Hooks, MiscActionHooks,
// KillHooks, Leaves9Hooks/RenderLeaves9Hooks) have ZERO byte-faithful bindable
// fields (raw OS I/O = rule 6; distinct decoded view structs not castable; no
// reconstructed callable leaf; incompatible typed signature) — see wire_cmdops.h.
// They are not installed here; this suite asserts the bound field plus the wired
// state-2 roll actually consuming a real CRT draw.
#include "test.h"

#include "sim/wire_cmdops.h"
#include "sim/combat_orders2.h"   // OrderDriverHooks / Get/SetOrderDriverHooks / UpdateUnitOrders
#include "sim/combat.h"           // REAL Math_RandomModulo
#include "crt/rand.h"             // REAL LCG: crt::Srand

#include <vector>

using namespace guild;
using namespace guild::sim;

namespace {

// A populated attack-order slot (state 2) with the ready packet sentinel so the
// gate passes; the default resolveUnit reports the unit alive, so the state-2
// body runs and consumes exactly one RandomModulo(255) hit-chance draw.
OrderSlot MakeAttackSlot() {
    OrderSlot s;
    s.unitId = 5; s.state = kOrderAttack; s.packetId = 1; s.phase = 0;
    s.tileX = 1; s.tileZ = 2; s.tileAux = 3;
    s.moved = 0; s.firing = 0; s.hitFlag = 0; s.predictedDamage = 0;
    return s;
}

} // namespace

// The installer binds randomModulo (the one byte-faithful leaf). Re-inert first so
// a clean baseline holds, then assert the bound field is non-null after install.
TEST(WireCmdOps, BindsRandomModuloIntoOrderDriver) {
    SetOrderDriverHooks(nullptr);                 // module inert defaults (all null)
    CHECK(GetOrderDriverHooks().randomModulo == nullptr);

    InstallRealCmdOpsWiring();

    const OrderDriverHooks& h = GetOrderDriverHooks();
    CHECK(h.randomModulo != nullptr);
    // The unbound scene/path/command/sink leaves stay inert (no clean target).
    CHECK(h.packetStatus  == nullptr);
    CHECK(h.resolveUnit   == nullptr);
    CHECK(h.onTargetTile  == nullptr);
    CHECK(h.findFreeTile  == nullptr);
    CHECK(h.unitBusy      == nullptr);
    CHECK(h.emit          == nullptr);

    SetOrderDriverHooks(nullptr);                 // restore for later tests in TU
}

// The wired leaf actually drives a REAL CRT draw: with the order driver installed,
// a state-2 attack slot consumes one Math_RandomModulo(255) over crt::RandNext and
// writes the firing flag accordingly. Re-seeding the LCG identically reproduces the
// exact firing decision — proving the live order-tick now rolls the real RNG (not
// the inert 0). seed 12345 is the same fixture the combat_orders2 integration test
// uses (first RandNext 21468 -> non-zero draw -> firing set).
TEST(WireCmdOps, WiredOrderRollConsumesRealLcg) {
    InstallRealCmdOpsWiring();

    // The exact draw the installed leaf will produce (RandomModulo(255)).
    crt::Srand(12345);
    int expected = Math_RandomModulo(0xFF);
    CHECK(expected >= 0 && expected < 255);

    // Re-seed and drive the order tick: the default resolveUnit reports the unit
    // alive, so state 2's body runs and consumes ONE RandomModulo(255) draw through
    // the wired leaf, writing slot.firing = (roll != 0).
    crt::Srand(12345);
    std::vector<OrderSlot> slots = { MakeAttackSlot() };
    int worked = UpdateUnitOrders(slots, /*globalHalt=*/false);
    CHECK_EQ(worked, 1);
    CHECK_EQ(static_cast<int>(slots[0].firing), expected != 0 ? 1 : 0);
    // The gate consumed the ready sentinel and the driver reset the packet id 1:1.
    CHECK_EQ(slots[0].packetId, -1);

    SetOrderDriverHooks(nullptr);
}

// globalHalt no-ops the whole driver (1:1 prelude gate) even with the leaf wired —
// no roll consumed, no slot mutated.
TEST(WireCmdOps, GlobalHaltNoOpsEvenWired) {
    InstallRealCmdOpsWiring();

    std::vector<OrderSlot> slots = { MakeAttackSlot() };
    int worked = UpdateUnitOrders(slots, /*globalHalt=*/true);
    CHECK_EQ(worked, 0);
    CHECK_EQ(static_cast<int>(slots[0].firing), 0);  // untouched
    CHECK_EQ(slots[0].packetId, 1);                  // untouched

    SetOrderDriverHooks(nullptr);
}
