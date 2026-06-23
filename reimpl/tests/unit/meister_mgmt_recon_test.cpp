// Golden-vector unit tests for the MeisterAi management/planner DATA/RULES cores
// (gilde.exe). Each vector is hand-derived from the named decompile so the
// reconstruction stays verifiably 1:1. Self-contained: no game assets, no RNG
// state other than the injected deterministic stub.
#include "tests/framework/test.h"

#include "sim/meister_mgmt_recon.h"

#include <cstring>

using namespace guild;
using namespace guild::sim;

// --------------------------------------------------------------------------
// 0x453638 ResourceRestockTarget
// --------------------------------------------------------------------------
TEST(MeisterMgmtReconStock, SkipFlag0x20) {
    // flags & 0x20 -> not eligible regardless of the price math.
    auto r = ResourceRestockTarget(0x20, 0, 100, 0, 50, 10.0f, 100.0f);
    CHECK(!r.eligible);
    CHECK_EQ(r.target, 0);
}

TEST(MeisterMgmtReconStock, SkipFlag0x200) {
    auto r = ResourceRestockTarget(0x200, 0, 100, 0, 50, 10.0f, 100.0f);
    CHECK(!r.eligible);
}

TEST(MeisterMgmtReconStock, BlockedSkips) {
    auto r = ResourceRestockTarget(0, 1, 100, 0, 50, 10.0f, 100.0f);
    CHECK(!r.eligible);
}

TEST(MeisterMgmtReconStock, CapacityGate) {
    // slotCapacity/4 == 25, reserve 25 -> (25 <= 25) -> not eligible.
    auto r = ResourceRestockTarget(0, 0, 100, 25, 50, 10.0f, 100.0f);
    CHECK(!r.eligible);
    // reserve 24 -> (25 > 24) passes the capacity gate.
    auto r2 = ResourceRestockTarget(0, 0, 100, 24, 50, 10.0f, 100.0f);
    CHECK(r2.eligible);
}

TEST(MeisterMgmtReconStock, RatioCutoffRejectsHighPrice) {
    // ratio = 90/100 = 0.9 >= 0.8 -> not eligible (price too close to fresh).
    auto r = ResourceRestockTarget(0, 0, 100, 0, 50, 90.0f, 100.0f);
    CHECK(!r.eligible);
}

TEST(MeisterMgmtReconStock, RatioCutoffAcceptsLowPrice) {
    // ratio = 10.0f/100.0f < 0.8 -> eligible. The restock target uses the exact
    // single-precision ratio promoted to double, matching the original FPU math:
    //   ratio  = 10.0f/100.0f = 0.100000001490116...   (nearest float to 0.1)
    //   1.0 - ratio = 0.899999998509884...
    //   50 * that   = 44.99999992...  -> (int) truncates to 44 (NOT 45).
    // This truncation is the faithful behaviour; a naive 0.1 would give 45.
    auto r = ResourceRestockTarget(0, 0, 100, 0, 50, 10.0f, 100.0f);
    CHECK(r.eligible);
    CHECK_EQ(r.target, 44);
}

TEST(MeisterMgmtReconStock, ExactQuarterRatioTarget) {
    // ratio = 25.0f/100.0f = 0.25 exactly (representable). target =
    // (int)(80 * (1.0 - 0.25)) = (int)60.0 = 60.
    auto r = ResourceRestockTarget(0, 0, 1000, 0, 80, 25.0f, 100.0f);
    CHECK(r.eligible);
    CHECK_EQ(r.target, 60);
}

TEST(MeisterMgmtReconStock, TargetClampsToBaseQty) {
    // ratio = 0 (cached price 0) -> target = (int)(50 * 1.0) = 50, clamp -> 50.
    auto r = ResourceRestockTarget(0, 0, 100, 0, 50, 0.0f, 100.0f);
    CHECK(r.eligible);
    CHECK_EQ(r.target, 50);
}

TEST(MeisterMgmtReconStock, CutoffBitsMatchDecompile) {
    // The exact boundary: a ratio whose bit pattern equals 0x3F4CCCCD (0.8f,
    // byte-verified at disasm 0x4536db `cmp ..., 3F4CCCCDh`) is NOT < cutoff, so
    // it must be rejected; one bit below must be accepted.
    CHECK_EQ((int)kRatioCutoffBits, 0x3F4CCCCD);
    float atCutoff;
    u32 bits = kRatioCutoffBits;  // 0x3F4CCCCD
    std::memcpy(&atCutoff, &bits, sizeof(atCutoff));
    // construct prices yielding exactly that ratio: cached/fresh == atCutoff.
    auto r = ResourceRestockTarget(0, 0, 100, 0, 50, atCutoff, 1.0f);
    CHECK(!r.eligible);
    u32 below = kRatioCutoffBits - 1;
    float belowF;
    std::memcpy(&belowF, &below, sizeof(belowF));
    auto r2 = ResourceRestockTarget(0, 0, 100, 0, 50, belowF, 1.0f);
    CHECK(r2.eligible);
}

// --------------------------------------------------------------------------
// 0x45379c IdleWorkerShouldQueue
// --------------------------------------------------------------------------
TEST(MeisterMgmtReconIdle, QueuesWhenFreeAndFlagClear) {
    CHECK(IdleWorkerShouldQueue(0, 0));
    CHECK(IdleWorkerShouldQueue(0, 0x4));   // unrelated flag bits ignored
}
TEST(MeisterMgmtReconIdle, SkipsWhenBlocked) {
    CHECK(!IdleWorkerShouldQueue(1, 0));
}
TEST(MeisterMgmtReconIdle, SkipsWhenBit8Set) {
    CHECK(!IdleWorkerShouldQueue(0, 0x8));
    CHECK(!IdleWorkerShouldQueue(0, 0x18));
}

// --------------------------------------------------------------------------
// 0x45d4c4 CancelTaskFlagMask / TaskMatchesOrder
// --------------------------------------------------------------------------
TEST(MeisterMgmtReconCancel, FlagMaskByFilter) {
    CHECK_EQ((int)CancelTaskFlagMask(4), 0x8);
    CHECK_EQ((int)CancelTaskFlagMask(3), 0x8);
    CHECK_EQ((int)CancelTaskFlagMask(8), 0x10);
    CHECK_EQ((int)CancelTaskFlagMask(0), 0);
    CHECK_EQ((int)CancelTaskFlagMask(7), 0);
}
TEST(MeisterMgmtReconCancel, TaskMatch) {
    // order/handler building ids both 5 (in HIWORD), order flag bit clear.
    int orderKey = (5 << 16) | 0x1234;
    int handlerKey = (5 << 16) | 0x9999;
    CHECK(TaskMatchesOrder(orderKey, handlerKey, 0x0, 0x8));
    // flag bit set -> no match.
    CHECK(!TaskMatchesOrder(orderKey, handlerKey, 0x8, 0x8));
    // different building id -> no match.
    int handlerKey2 = (6 << 16);
    CHECK(!TaskMatchesOrder(orderKey, handlerKey2, 0x0, 0x8));
}

// --------------------------------------------------------------------------
// 0x45cfac AiSlotDeficit
// --------------------------------------------------------------------------
TEST(MeisterMgmtReconSlots, BothShortAffordable) {
    // capWorker 3, used 1 -> workerShort 2; capGuard 2, used 0 -> guardShort 2.
    auto p = AiSlotDeficit(/*usedWorkers*/1, /*usedGuards*/0,
                           /*capWorker*/3, /*capGuard*/2, /*cash*/30000);
    CHECK_EQ(p.workerShort, 2);
    CHECK_EQ(p.guardShort, 2);
    CHECK(p.fire);
    CHECK_EQ(p.slotCount, 2);     // both nonzero
    CHECK_EQ(p.cost, 16000);      // 8000 * 2
    CHECK(p.affordable);          // 30000 >= 24000
}
TEST(MeisterMgmtReconSlots, NoDeficitNoFire) {
    auto p = AiSlotDeficit(3, 2, 3, 2, 30000);
    CHECK_EQ(p.workerShort, 0);
    CHECK_EQ(p.guardShort, 0);
    CHECK(!p.fire);
    CHECK_EQ(p.slotCount, 0);
}
TEST(MeisterMgmtReconSlots, OneShortNotAffordable) {
    // only worker short by 1; cash below 24000 gate.
    auto p = AiSlotDeficit(0, 2, 1, 2, 23999);
    CHECK_EQ(p.workerShort, 1);
    CHECK_EQ(p.guardShort, 0);
    CHECK(p.fire);
    CHECK_EQ(p.slotCount, 1);
    CHECK_EQ(p.cost, 8000);
    CHECK(!p.affordable);
}
TEST(MeisterMgmtReconSlots, ExactAffordabilityBoundary) {
    auto p = AiSlotDeficit(0, 0, 1, 0, 24000);
    CHECK(p.affordable);          // 24000 >= 24000
    auto q = AiSlotDeficit(0, 0, 1, 0, 23999);
    CHECK(!q.affordable);
}

// --------------------------------------------------------------------------
// 0x45d618 DispatchOrderCount
// --------------------------------------------------------------------------
TEST(MeisterMgmtReconDispatch, FirstPassFoldsRemainder) {
    // divisor 3, total 10, first pass (passIndex 0), already 0.
    // count = 10%3 + 10/3 + 0 = 1 + 3 = 4.
    CHECK_EQ(DispatchOrderCount(3, 10, 0, 0), 4);
}
TEST(MeisterMgmtReconDispatch, LaterPassDropsRemainder) {
    // passIndex nonzero -> count = 10/3 + 0 = 3.
    CHECK_EQ(DispatchOrderCount(3, 10, 1, 0), 3);
}
TEST(MeisterMgmtReconDispatch, DivisorOneNoRemainderFold) {
    // divisor <= 1 -> count = total/divisor + already (no remainder fold).
    CHECK_EQ(DispatchOrderCount(1, 10, 0, 2), 12);
}
TEST(MeisterMgmtReconDispatch, AlreadyAccumulates) {
    CHECK_EQ(DispatchOrderCount(4, 9, 0, 5), 9 % 4 + 9 / 4 + 5); // 1+2+5 = 8
    CHECK_EQ(DispatchOrderCount(4, 9, 0, 5), 8);
}

// --------------------------------------------------------------------------
// 0x45c9ac RenovateRoomBudget / RenovateUpgradeAffordable
// --------------------------------------------------------------------------
// Constants byte-verified @0x619940/44/48 (wave-19 correction): scale 0.005f,
// floor/min 128.0f, cashScale 0.75f.
TEST(MeisterMgmtReconRenov, BudgetScalesAndTriggers) {
    // roomWorth 1000 -> scaled = 0.005*(1000*0.005) = 0.025. budget floor = 128.0
    // (128.0 <= 0.025 is false). cash 3000 -> 3000*0.75 = 2250 > 128 -> trigger.
    auto r = RenovateRoomBudget(1000, 3000);
    CHECK_EQ((int)r.budget, 128);
    CHECK(r.trigger);
}
TEST(MeisterMgmtReconRenov, TriggersWhenCashCoversFloor) {
    // budget 128; cash 2000 -> 2000*0.75 = 1500 > 128 -> trigger.
    auto r = RenovateRoomBudget(1000, 2000);
    CHECK(r.trigger);
    // Below the 128 floor: cash 100 -> 75 <= 128 -> no trigger.
    auto rlo = RenovateRoomBudget(1000, 100);
    CHECK(!rlo.trigger);
}
TEST(MeisterMgmtReconRenov, MinBudgetFloor) {
    // roomWorth 0 -> scaled 0.0; floor check (128.0 <= 0.0) false -> budget = 128.
    auto r0 = RenovateRoomBudget(0, 0);
    CHECK_EQ((int)r0.budget, 128);
    // negative worth -> scaled < 0 -> still below floor -> 128.0.
    auto rn = RenovateRoomBudget(-1000, 0);
    CHECK_EQ((int)rn.budget, 128);
}
TEST(MeisterMgmtReconRenov, UpgradeAffordable) {
    // 3*price < cash.
    CHECK(RenovateUpgradeAffordable(100, 301));  // 300 < 301
    CHECK(!RenovateUpgradeAffordable(100, 300)); // 300 < 300 false
    CHECK(!RenovateUpgradeAffordable(100, 299));
}

// --------------------------------------------------------------------------
// 0x45e12c FreeStaffSlotCount
// --------------------------------------------------------------------------
TEST(MeisterMgmtReconStaff, BudgetTally) {
    CHECK_EQ(FreeStaffSlotCount(0), 2);   // none assigned -> 2 free
    CHECK_EQ(FreeStaffSlotCount(1), 1);
    CHECK_EQ(FreeStaffSlotCount(2), 0);   // full -> 0 (no free slot)
    CHECK_EQ(FreeStaffSlotCount(3), -1);  // over-assigned -> negative
}

// --------------------------------------------------------------------------
// 0x4537e8 / 0x4542b8 ProductionSlotShuffle
// --------------------------------------------------------------------------
// Deterministic RNG stubs to verify the Fisher-Yates permutation exactly.
static u16 RngZero(u32) { return 0; }
static u16 RngIdentity(u32) { return 0; }  // alias for clarity below

TEST(MeisterMgmtReconShuffle, IsPermutation) {
    // With a "rng" that always returns the current index we'd get identity; with
    // a fixed-seed stub we just verify the result is a permutation of 0..31.
    static int counter = 0;
    counter = 0;
    auto rng = [](u32 m) -> u16 {
        // simple LCG-ish deterministic stream, reduced mod m.
        counter = counter * 1103515245 + 12345;
        u32 v = static_cast<u32>(counter >> 16);
        return static_cast<u16>(v % m);
    };
    int order[kProductionSlots];
    ProductionSlotShuffle(order, rng);
    bool seen[kProductionSlots] = {false};
    for (int i = 0; i < kProductionSlots; ++i) {
        CHECK(order[i] >= 0 && order[i] < kProductionSlots);
        CHECK(!seen[order[i]]);
        seen[order[i]] = true;
    }
    for (int i = 0; i < kProductionSlots; ++i)
        CHECK(seen[i]);
}

TEST(MeisterMgmtReconShuffle, RngZeroBubblesLastToFront) {
    // rng()==0 every step: for i in 0..31 swap(order[i], order[0]).
    // i=0: swap(0,0) -> unchanged [0,1,2,...].
    // i=1: swap(order[1]=1, order[0]=0) -> [1,0,2,3,...].
    // i=2: swap(order[2]=2, order[0]=1) -> [2,0,1,3,...].
    // After all 32 steps, this is a deterministic permutation; front holds 31.
    int order[kProductionSlots];
    ProductionSlotShuffle(order, RngZero);
    // It is still a permutation, and order[0] ends as 31 (last index rotated in).
    CHECK_EQ(order[0], kProductionSlots - 1);
    bool seen[kProductionSlots] = {false};
    for (int i = 0; i < kProductionSlots; ++i) {
        CHECK(!seen[order[i]]);
        seen[order[i]] = true;
    }
    (void)RngIdentity;
}
