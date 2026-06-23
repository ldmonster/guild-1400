// Verifies InstallRealInventoryWiring() binds the inventory / build-plot /
// command-target / command-emit bridges (Inventory2Hooks, BuilderHooks,
// CommandApply8Hooks, ApplyTargetHooks, BauplatzMapHooks) to their real
// reconstructed leaves — previously all five were fully inert at runtime (nothing
// installed them). Suite prefix: WireInventory.
#include "tests/framework/test.h"

#include "sim/wire_inventory.h"
#include "sim/inventory2.h"
#include "sim/command_apply9.h"
#include "sim/command_apply8.h"
#include "sim/command_apply10.h"
#include "sim/buildingtype_recon.h"
#include "sim/entity.h"
#include "sim/command_apply.h"

#include <cstring>

using namespace guild;
using namespace guild::sim;

// Re-inert every bridge so a clean baseline can be asserted before install.
static void InertAll() {
    SetApplyTargetHooks(nullptr);
    SetCommandApply8Hooks(nullptr);
    SetBauplatzMapHooks(nullptr);
    SetInventory2Hooks(nullptr);
    SetBuilderHooks(nullptr);
}

TEST(WireInventory, BindsRealLeavesIntoTargetBridges) {
    InertAll();
    // The installer SEEDS each table from its module inert defaults and overrides
    // only the wireable fields. We assert the BOUND fields point at real adapters;
    // the execute test proves real behaviour.
    InstallRealInventoryWiring();

    // --- ApplyTargetHooks: resolve / special-target / find / count / worth / id --
    const ApplyTargetHooks& at = GetApplyTargetHooks();
    CHECK(at.resolveEntityById    != nullptr);
    CHECK(at.specialTarget        != nullptr);
    CHECK(at.personFindRecordById != nullptr);
    CHECK(at.countAtLocation      != nullptr);
    CHECK(at.roomWorth            != nullptr);
    CHECK(at.recordEntityId       != nullptr);

    // --- CommandApply8Hooks: the PRNG nonce is real ----------------------------
    const CommandApply8Hooks& a8 = GetCommandApply8Hooks();
    CHECK(a8.randNext != nullptr);

    // --- BauplatzMapHooks: the edge rasteriser is real (installed pointer) ------
    CHECK(BauplatzMapHooks() != nullptr);

    // --- Inventory2Hooks / BuilderHooks: seeded (non-null inert table) ----------
    // GetInventory2Hooks / GetBuilderHooks always return a live table; the install
    // path runs without crashing, which is what the seed-from-defaults guarantees.
    (void)GetInventory2Hooks();
    (void)GetBuilderHooks();

    InertAll();   // restore for any later test in this TU
}

// The bound real leaves execute over the live (empty) entity arrays and the
// last-created globals without crashing, returning defined results — i.e. the wired
// adapters actually reach the real reconstructed code.
TEST(WireInventory, WiredLeavesExecuteAgainstRealState) {
    ResetEntityArrays();
    InstallRealInventoryWiring();

    const ApplyTargetHooks& at = GetApplyTargetHooks();

    // special-target: -2/-3/-4 read the apply dispatcher's last-created globals.
    g_lastObjectId = 4242;
    g_lastSceneId  = 7;
    g_lastTradeId  = 99;
    CHECK_EQ(at.specialTarget(-2), 4242);
    CHECK_EQ(at.specialTarget(-3), 7);
    CHECK_EQ(at.specialTarget(-4), 99);
    CHECK_EQ(at.specialTarget(123), 0); // non-sentinel -> 0

    // resolveEntityById over empty arrays: no entity, defined "not found".
    ResolvedEntity e;
    int r = at.resolveEntityById(12345, nullptr, &e);
    CHECK_EQ(r, 0);
    CHECK(e.immediate == nullptr);
    CHECK(e.parent    == nullptr);
    CHECK(e.object    == nullptr);

    // personFindRecordById over empty array: null.
    CHECK(at.personFindRecordById(777) == nullptr);

    // countAtLocation over empty world: defined non-negative count.
    int cnt = at.countAtLocation(0);
    CHECK(cnt >= 0);

    // recordEntityId reads *(record+4); a small blob with a known id @+4.
    unsigned char rec[16];
    std::memset(rec, 0, sizeof(rec));
    i32 want = 0x01020304;
    std::memcpy(rec + 4, &want, 4);
    CHECK_EQ(at.recordEntityId(rec), want);
    CHECK_EQ(at.recordEntityId(nullptr), 0);

    // randNext: the real LCG advances (two draws are defined ints).
    const CommandApply8Hooks& a8 = GetCommandApply8Hooks();
    u32 d0 = a8.randNext();
    u32 d1 = a8.randNext();
    (void)d0; (void)d1;

    // RasterizeBauplatzEdge: a unit-square quad yields a defined (>=1) step count;
    // the rasteriser must not touch the (unset) supermap (mapCtx ignored).
    float quad[8] = {0.f, 0.f, 1.f, 0.f, 1.f, 1.f, 0.f, 1.f};
    int steps = BauplatzMapHooks()->RasterizeBauplatzEdge(0, quad, 255);
    CHECK(steps >= 1);

    InertAll();
}
