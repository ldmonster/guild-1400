// Integration test for world/wire_privilege_panels_b — proves the SET-B panels are
// reachable through the live dispatcher seam (sim::InvokePrivilegeLeaf ->
// g_privilegeHook) AND that installing SET-B after SET-A keeps BOTH batches
// reachable via the single shared hook slot (the chaining contract).
#include "test.h"

#include "world/wire_privilege_panels_b.h"
#include "sim/interaction_handlers.h"

using namespace guild;
using namespace guild::sim;

namespace {
// A stand-in "SET-A" hook: handles one A leaf-id, returns 0 for everything else
// (the same posture the real SET-A adapter has).
int FakeSetAHook(int leafId, ContextActor*, InteractionEventRec*) {
    if (leafId == 0x563000) return 42;  // a SET-A leaf -> distinctive value
    return 0;
}
} // namespace

TEST(WirePrivB, SetBLeafSetMembership) {
    CHECK(world::IsPrivilegeSetBLeaf(0x561bb4)); // EnactLaw
    CHECK(world::IsPrivilegeSetBLeaf(0x562334)); // Embezzlement
    CHECK(world::IsPrivilegeSetBLeaf(0x5651bc)); // Miracle
    CHECK(world::IsPrivilegeSetBLeaf(0x571218)); // ShowDialog
    CHECK(world::IsPrivilegeSetBLeaf(0x565b88)); // EvidenceDetails
    CHECK(!world::IsPrivilegeSetBLeaf(0x563000)); // a SET-A leaf
    CHECK(!world::IsPrivilegeSetBLeaf(0xdead));
}

TEST(WirePrivB, InstallReachableThroughSeam) {
    ResetInteractionLeafTrace();
    // Install B with no provider -> inert hooks; Miracle kind 7 -> 0.
    int n = world::InstallPrivilegePanelsB(nullptr, nullptr);
    CHECK_EQ(n, 10);

    ContextActor actor{};
    actor.kind = 7;
    InteractionEventRec ev{};
    ev.mode = kModeActivate;
    // With NO provider the actor/event views stay zero-filled (kind 0), so Miracle
    // takes the non-office direct arm with rank 0 -> 32 (the documented inert
    // posture: a live provider binds the real actor fields). The point under test is
    // REACHABILITY: the seam routes the SET-B leaf into B's dispatcher.
    int r = InvokePrivilegeLeaf(0x5651bc /*kPrivMiracle addr*/, &actor, &ev);
    CHECK_EQ(r, 32);
    SetPrivilegeLeafHook(nullptr);
}

TEST(WirePrivB, ChainsToPriorHookForNonBLeaves) {
    // Install the fake SET-A hook first, then SET-B. B must DELEGATE the SET-A
    // leaf-id back to the prior hook (chaining), not clobber it.
    SetPrivilegeLeafHook(&FakeSetAHook);
    world::InstallPrivilegePanelsB(nullptr, /*chainTo*/ nullptr); // auto-captures prior

    ContextActor actor{};
    InteractionEventRec ev{};
    // A SET-A leaf -> delegated to FakeSetAHook -> 42 (chaining preserves SET-A).
    CHECK_EQ(InvokePrivilegeLeaf(0x563000, &actor, &ev), 42);
    // A SET-B leaf (Miracle) -> handled by B (inert view, rank 0) -> 32.
    actor.kind = 7; ev.mode = kModeActivate;
    CHECK_EQ(InvokePrivilegeLeaf(0x5651bc, &actor, &ev), 32);
    // An unknown leaf -> chained to FakeSetAHook -> 0.
    CHECK_EQ(InvokePrivilegeLeaf(0xbeef, &actor, &ev), 0);
    SetPrivilegeLeafHook(nullptr);
}

TEST(WirePrivB, ExplicitChainArg) {
    SetPrivilegeLeafHook(nullptr);
    world::InstallPrivilegePanelsB(nullptr, &FakeSetAHook); // explicit chain
    ContextActor actor{};
    InteractionEventRec ev{};
    CHECK_EQ(InvokePrivilegeLeaf(0x563000, &actor, &ev), 42); // chained
    SetPrivilegeLeafHook(nullptr);
}
