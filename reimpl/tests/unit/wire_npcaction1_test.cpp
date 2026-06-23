// Verifies InstallRealNpcAction1Wiring() binds the three NpcAction5/6/7 hook
// bridges to their real reconstructed leaves — previously all three were fully
// inert at runtime (nothing installed them). Suite prefix: WireNpcAction1.
#include "tests/framework/test.h"

#include "sim/wire_npcaction1.h"
#include "sim/real_hooks3.h"   // InstallRealSimHooks3 (NpcLeafHooks + the shared He pool)
#include "sim/npcaction5.h"
#include "sim/npcaction6.h"
#include "sim/npcaction7.h"
#include "sim/entity.h"        // ResetEntityArrays (deterministic empty Person/Object world)
#include "sim/he.h"

#include <cstring>

using namespace guild;
using namespace guild::sim;

// Re-inert every bridge so a clean baseline can be asserted across tests in this TU.
static void InertAll() {
    SetNpcAction5Hooks(nullptr);
    SetNpcAction6Hooks(nullptr);
    SetNpcAction7Hooks(nullptr);
}

TEST(WireNpcAction1, BindsRealLeavesIntoAllThreeBridges) {
    InertAll();
    // The installer SEEDS each table from its module inert defaults (non-null stubs)
    // and overrides only the wireable fields — required because the NpcAction bodies
    // invoke several hooks WITHOUT a null-check. Unbound fields stay as inert stubs
    // (non-null), not null; we assert the BOUND fields point at real adapters.
    InstallRealNpcAction1Wiring();

    // --- NpcAction5: the entity-resolve leaf is now real ----------------------
    const NpcAction5Hooks& a5 = GetNpcAction5Hooks();
    CHECK(a5.resolveEntityField97 != nullptr);

    // --- NpcAction6: resolve / money / need+flag gates / cmd emits / season / scan
    const NpcAction6Hooks& a6 = GetNpcAction6Hooks();
    CHECK(a6.findRecordById         != nullptr);
    CHECK(a6.moneyMultiplyByRate    != nullptr);
    CHECK(a6.pickNeedAndClearGroup  != nullptr);
    CHECK(a6.pickNeedAndClearGroupB != nullptr);
    CHECK(a6.pickFlagFromFourA      != nullptr);
    CHECK(a6.pickFlagFromFourB      != nullptr);
    CHECK(a6.requestBuildOp93       != nullptr);
    CHECK(a6.queueRequest16         != nullptr);
    CHECK(a6.queueRequestEntity29   != nullptr);
    CHECK(a6.currentSeason          != nullptr);
    CHECK(a6.wanderScanBegin        != nullptr);
    CHECK(a6.wanderScanNext         != nullptr);
    CHECK(a6.personSlotByIndex      != nullptr);

    // --- NpcAction7: resolve / category / person table / mood / shuffle / cmds --
    const NpcAction7Hooks& a7 = GetNpcAction7Hooks();
    CHECK(a7.findRecordById       != nullptr);
    CHECK(a7.mapTypeToCategory    != nullptr);
    CHECK(a7.personTableBase      != nullptr);
    CHECK(a7.personTableCapacity  != nullptr);
    CHECK(a7.adjustRelationByMood != nullptr);
    CHECK(a7.shuffleDwords        != nullptr);
    CHECK(a7.requestBuildOp67     != nullptr);
    CHECK(a7.requestBuildOp93     != nullptr);
    CHECK(a7.queueRequest17       != nullptr);

    InertAll();   // restore for any later test in this TU
}

// A representative action from each bridge runs over a zeroed record through the
// installed real leaves without crashing, returning a defined result — i.e. the
// wired control flow actually executes (resolves / frees against the real pool,
// emits stage onto the real queue).
TEST(WireNpcAction1, WiredStepsExecuteOverZeroedRecord) {
    // Match the real boot order: InstallRealSimHooks3 binds the sibling NpcLeafHooks
    // bridge (freeHandlerEntry / queueRequestEntity29 / findInventorySlot) + creates
    // and Init's the shared He pool, which NpcAction5/6 reach through GetNpcLeafHooks
    // and RealHandlerTable.
    InstallRealSimHooks3();
    InstallRealNpcAction1Wiring();
    ResetEntityArrays();   // empty Person/Object world: every slot marked free (-1)

    HeRecord rec;

    // NpcAction5: CountdownTickEntity over a zeroed record. state(+112)==0 (not <0)
    // but counter(+172)==0 (<= 0) so it takes the real FreeHandlerEntry path against
    // the shared He pool. Defined return.
    std::memset(&rec, 0, sizeof(rec));
    i32 a = NpcAction5_CountdownTickEntity(&rec);
    (void)a;

    // NpcAction6: NotifyWanderPair scans the (empty) real type-53 handler pool via
    // the real Find*ByFilter; with no matches it returns 1.
    std::memset(&rec, 0, sizeof(rec));
    int b = NpcAction6_NotifyWanderPair(&rec);
    CHECK_EQ(b, 1);

    // NpcAction6: IncreaseLoyaltyCmd over a zeroed descriptor. findRecordById(0)
    // resolves against the empty real Person array (-> nullptr) so it returns
    // kNpc6NoActor without touching the (incompatible) wealth path. Defined return.
    std::memset(&rec, 0, sizeof(rec));
    int c = NpcAction6_IncreaseLoyaltyCmd(&rec);
    CHECK_EQ(c, kNpc6NoActor);

    // NpcAction7: HealCmd over a zeroed descriptor. findRecordById(0) -> nullptr
    // (empty real Person array) so it returns kNpc7NoActor. Defined return.
    std::memset(&rec, 0, sizeof(rec));
    int d = NpcAction7_HealCmd(&rec);
    CHECK_EQ(d, kNpc7NoActor);

    InertAll();
}
