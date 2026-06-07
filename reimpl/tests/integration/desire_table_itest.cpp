// Integration tests: ai/desire_table composed against the REAL ai/intrigue module
// (not a mock). Both translate the same family of "execute an intrigue action,
// then return its label code" rules; this proves the two modules' return-code
// conventions and hook discipline compose consistently.
#include "test.h"

#include "ai/desire_table.h"
#include "ai/intrigue.h"

using namespace guild;
using namespace guild::ai;

// The "reject code" the binary's wrappers obtain from VIBE_Interaction_EvalRejectStub.
// In the real flow it is the same value threaded through intrigue's EvalActionLabel /
// EvalSlanderLabel. We model it as a shared constant and assert both modules honor it.
static constexpr u8 kRejectCode = 0; // Interaction_EvalRejectStub returns 0 (cold)

TEST(DesireTableItest, RejectCodesAgreeAcrossModules) {
    // intrigue's EvalActionLabel/EvalSlanderLabel and desire_table's Perform*
    // wrappers all use the SAME accept/reject shape: accept => label, reject =>
    // the reject code. Feed both a reject and confirm they return the reject code.
    CHECK_EQ(EvalActionLabel(false, kRejectCode), kRejectCode);   // intrigue 0x4715d8
    CHECK_EQ(EvalSlanderLabel(false, kRejectCode), kRejectCode);  // intrigue 0x471fa0
    CHECK_EQ(PerformEnterBuilding(false, kRejectCode), kRejectCode);
    CHECK_EQ(PerformOpenDoorLarge(false, kRejectCode), kRejectCode);
    CHECK_EQ(PerformOpenDoorSmall(false, kRejectCode), kRejectCode);
}

TEST(DesireTableItest, LabelCodesAreDistinctOnAccept) {
    // Each accept path returns its own distinct action label byte. Cross-checking
    // against intrigue's known labels (39 action, 45 slander, 41 pamphlet) confirms
    // the new wrappers' labels (40, 43, 44, 42) don't collide with the existing set.
    u8 labels[] = {
        EvalActionLabel(true, kRejectCode),       // 39
        PerformEnterBuilding(true, kRejectCode),  // 40
        PerformOpenDoorLarge(true, kRejectCode),  // 43
        PerformOpenDoorSmall(true, kRejectCode),  // 44
        PerformUseBack(true, 1),                  // 42
        EvalSlanderLabel(true, kRejectCode),      // 45
    };
    CHECK_EQ(labels[0], 39u);
    CHECK_EQ(labels[1], 40u);
    CHECK_EQ(labels[2], 43u);
    CHECK_EQ(labels[3], 44u);
    CHECK_EQ(labels[4], 42u);
    CHECK_EQ(labels[5], 45u);
    // all distinct
    for (int i = 0; i < 6; ++i)
        for (int j = i + 1; j < 6; ++j)
            CHECK(labels[i] != labels[j]);
}

namespace {
int g_pamphletCalls = 0;
int g_useBackCalls = 0;
void PamphletCb(i32) { ++g_pamphletCalls; }
void UseBackCb(i32) { ++g_useBackCalls; }
} // namespace

TEST(DesireTableItest, CommandHooksFireIndependently) {
    // intrigue's EvalPamphlet and desire_table's PerformUseBack both emit a command
    // through their own module-local hook on accept. Wiring both real hooks and
    // exercising the accept path proves the two command boundaries are independent
    // and both only fire on success.
    g_pamphletCalls = 0;
    g_useBackCalls = 0;
    SetPamphletCmdHook(&PamphletCb);   // intrigue 0x471ae0
    SetUseBackCmdHook(&UseBackCb);     // desire_table 0x471cb4

    CHECK_EQ(EvalPamphlet(true, 100), 41u);
    CHECK_EQ(PerformUseBack(true, 200), 42u);
    CHECK_EQ(g_pamphletCalls, 1);
    CHECK_EQ(g_useBackCalls, 1);

    // Reject paths: neither hook should fire.
    CHECK_EQ(EvalPamphlet(false, 100), 0u);
    CHECK_EQ(PerformUseBack(false, 200), 0u);
    CHECK_EQ(g_pamphletCalls, 1);
    CHECK_EQ(g_useBackCalls, 1);

    SetPamphletCmdHook(nullptr);
    SetUseBackCmdHook(nullptr);
}
