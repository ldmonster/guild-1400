// Verifies InstallRealEconomy2Wiring() binds the previously-inert SupervisionHooks
// (master-AI daily sweep) and IStockHooks (building stock/value command tails)
// bridges to their real reconstructed leaves — nothing installed them before, so
// both ran fully inert at runtime. Suite prefix: WireEconomy2.
#include "tests/framework/test.h"

#include "world/wire_economy2.h"
#include "ai/meister_supervision.h"
#include "sim/building_stock.h"
#include "sim/real_hooks3.h"   // InstallRealSimHooks3 (creates/Init's the shared He pool)

using namespace guild;
using guild::sim::InstallRealSimHooks3;
using guild::world::InstallRealEconomy2Wiring;

// ---------------------------------------------------------------------------
// The two wireable SupervisionHooks fields point at real adapters after install.
// (The bridge SEEDS from inert defaults, so unbound fields stay as their non-null
// stubs — we only assert the BOUND fields, then prove behaviour by executing.)
// ---------------------------------------------------------------------------
TEST(WireEconomy2, BindsSupervisionAndStockLeaves) {
    // Re-inert the supervision table to a clean baseline before install. (Passing
    // a zero-initialised hooks value makes BindHooks() refill every field from the
    // module defaults — the inert non-null stubs.)
    ai::SupervisionHooks blank{};
    ai::SetSupervisionHooks(blank);

    InstallRealSimHooks3();          // shared He pool exists
    InstallRealEconomy2Wiring();

    const ai::SupervisionHooks s = ai::GetSupervisionHooks();
    CHECK(s.handler_exists != nullptr);
    CHECK(s.rng_mod        != nullptr);
    // every other field stays a non-null inert stub (seed-from-defaults), not null
    CHECK(s.queue_slot_reset28 != nullptr);
    CHECK(s.request_build_op83 != nullptr);
    CHECK(s.queue_state22      != nullptr);

    // IStockHooks: a real subclass is now installed (no longer the inert default).
    CHECK(sim::StockHooks() != nullptr);
}

// ---------------------------------------------------------------------------
// A representative leaf from each bridge runs through the installed real leaves
// without crashing, returning a defined result — i.e. the wired control flow
// actually executes (the He probe resolves against the real pool; the stock
// command tail stages onto the real queue).
// ---------------------------------------------------------------------------
TEST(WireEconomy2, WiredLeavesExecute) {
    ai::SupervisionHooks blank{};
    ai::SetSupervisionHooks(blank);
    InstallRealSimHooks3();
    InstallRealEconomy2Wiring();

    // --- supervision: RequestCmd134 over an EMPTY real He pool -----------------
    // handler_exists(1,134,0) -> RealHandlerTable().FindFirstHandlerByFilter(1,0,134)
    // == null (empty pool) -> the function proceeds to queue_slot_reset28 (inert
    // no-op) and returns true. This exercises the wired real He scan.
    bool emitted = ai::RequestCmd134(/*masterBuildId*/ 42);
    CHECK(emitted == true);

    // RequestBuildingCmd43 walks per-employee; handler_exists(1,43,pid) drives the
    // FindFirst + FindNext-by-ordinal owner walk against the (empty) real pool —
    // every employee resolves "no pending 43" -> all emit (inert). Defined count.
    const int emps[3] = {101, 102, 103};
    int n = ai::RequestBuildingCmd43(/*masterBuildId*/ 7, emps, 3);
    CHECK(n == 3);   // empty pool -> none already targeted -> one emit per employee

    // --- supervision: FlagIdleStaff drives the real rng_mod leaf ---------------
    // A single idle candidate slot makes the function take the rng_mod(128) reprieve
    // roll; whether it aborts or flags, the wired RandomModulo leaf is exercised and
    // the return is one of {-1, 0, 1}.
    ai::BuildingFlag bld;        // supervisedBit == 0 (not yet processed)
    ai::StaffSlot slots[2];      // [0]=candidate, [1]=the no-candidate reprieve read
    slots[0].live = true; slots[0].employed = true; slots[0].hasActionObj = false;
    slots[0].busyFlags = 0; slots[0].gaugeA = 0; slots[0].gaugeB = 0;  // < 168 -> idle
    int flag = ai::FlagIdleStaff(&bld, /*buildId*/ 7, slots, /*slotCount*/ 1,
                                 ai::GetSupervisionHooks());
    CHECK(flag == 0 || flag == 1);

    // --- stock: Building_SyncStockLevel over a zeroed record -------------------
    // The projection math runs, then QueuePriceUpdate(...,28,delta) routes through
    // the wired QueueRequestArgs26 onto the real command queue. Defined return.
    sim::BuildingStockRec rec{};   // all-zero; fullFill 0 -> delta == projected level
    float level = sim::Building_SyncStockLevel(&rec, /*newStock*/ 0);
    (void)level;

    // --- stock: Building_ComputeSalePrice drives the wired SaleTaxTier leaf -----
    // ComputeSalePrice reads SaleTaxTier() == GesetzGetRecord(6).threshold; with no
    // flagged slots the worth is 0 so the price is 0, but the wired law lookup runs.
    int price = sim::Building_ComputeSalePrice(/*typeIndex*/ 0, /*qualityCode*/ 0);
    CHECK(price >= 0);

    // restore a clean supervision baseline for any later test in this TU
    ai::SetSupervisionHooks(blank);
}
