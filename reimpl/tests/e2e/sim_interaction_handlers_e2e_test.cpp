// End-to-end flow for the interaction RULES cores. Stages one actor and a context
// menu with several entries (some eligible, some not), dispatches each entry through
// the ContextAction executors and the Perform* command path, and verifies which
// interactions fire + the emitted (mock) commands against a hand-computed reference.
#include "tests/framework/test.h"

#include <cstdint>
#include <cstring>
#include <vector>
#include <string>

#include "sim/interaction_handlers.h"
#include "sim/entity.h"

using namespace guild;
using namespace guild::sim;

namespace {

// A context-menu entry: a label, the handler it dispatches to, and the event mode.
struct MenuEntry {
    const char* label;
    char (*handler)(ContextActor*, InteractionEventRec*);
    u8 mode;
};

// Capture all privilege-leaf and command emissions across the whole flow.
struct FlowLog {
    std::vector<int> leaves;          // privilege leaf ids fired (in order)
    std::vector<std::string> commands; // command tags emitted
};
FlowLog g_log;

int FlowPrivilege(int leafId, ContextActor*, InteractionEventRec*) {
    g_log.leaves.push_back(leafId);
    return 0;
}
void FlowCommand(const char* tag, int /*code*/) {
    g_log.commands.push_back(tag ? tag : "");
}

ContextActor MakeActor() {
    ContextActor a; std::memset(&a, 0, sizeof(a)); return a;
}
InteractionEventRec MakeEvent(u8 mode) {
    InteractionEventRec ev; std::memset(&ev, 0, sizeof(ev)); ev.mode = mode; return ev;
}

} // namespace

// Scenario: a guildmaster's lieutenant (player char, kind 6) of rank 4 with
// profession 22 (a "spy"-class role) is right-clicked. The context menu offers:
//   Promote      → rank>=3 so PromoteRank returns 4 (advance, not shown as 2)
//   Train4       → rank==4 exact → fires (2), dialog leaf
//   Train5       → rank>=5? no (rank 4) → reject (1)
//   Demote       → rank>=3 → fires (2), build-cmd leaf
//   Equip        → rank>=3 && !(457&4) → fires (2), simple-cmd leaf
//   AttackOrSteal→ profession 22 accepted → fires (2), charm leaf
//   Patrol       → profession 15/21/27? no → reject (1)
//   OpenInventory→ kind 6 → fires (|2), change-prof leaf
TEST(SimIntHandlersE2E, ContextMenuDispatch) {
    SetPrivilegeLeafHook(FlowPrivilege);
    g_log = FlowLog{};

    ContextActor actor = MakeActor();
    actor.kind = 6;
    actor.rank = 4;
    actor.profession = 22;

    const MenuEntry menu[] = {
        {"Promote",       ContextPromoteRank,    kModeActivate},
        {"Train4",        ContextTrainRank4A,    kModeActivate},
        {"Train5",        ContextTrainRank5A,    kModeActivate},
        {"Demote",        ContextDemoteIfRank3,  kModeActivate},
        {"Equip",         ContextEquipIfRank3,   kModeActivate},
        {"AttackOrSteal", ContextAttackOrSteal,  kModeActivate},
        {"Patrol",        ContextCommandPatrol,  kModeActivate},
        {"OpenInventory", ContextOpenInventory,  kModeActivate},
    };

    std::vector<int> verdicts;
    for (const auto& m : menu) {
        auto ev = MakeEvent(m.mode);
        verdicts.push_back((int)m.handler(&actor, &ev));
    }

    // Hand-computed reference verdicts:
    //   Promote 4, Train4 2, Train5 1, Demote 2, Equip 2, Attack 2, Patrol 1, Inv 2
    const int expect[] = {4, 2, 1, 2, 2, 2, 1, 2};
    for (size_t i = 0; i < verdicts.size(); ++i)
        CHECK_EQ(verdicts[i], expect[i]);

    // Leaves fired (only the eligible "activate" entries that reached a panel):
    //   Train4 ShowDialog, Demote SendBuildCmd, Equip SendSimpleCmd,
    //   Attack CharmConfirm, OpenInventory ChangeProfession.
    // (Promote returned 4 before any leaf; Train5 & Patrol rejected.)
    const int expectLeaves[] = {
        kPrivShowDialog, kPrivSendBuildCmd, kPrivSendSimpleCmd,
        kPrivCharmConfirm, kPrivChangeProfession,
    };
    CHECK_EQ(g_log.leaves.size(), sizeof(expectLeaves)/sizeof(int));
    for (size_t i = 0; i < g_log.leaves.size() && i < 5; ++i)
        CHECK_EQ(g_log.leaves[i], expectLeaves[i]);

    SetPrivilegeLeafHook(nullptr);
}

// Drag-drop flow: dragging a valid spy (profession 25) onto a selectable actor
// (flag457 & 1) fires the drag-apply path (verdict 10, leaf fires); dragging an
// ineligible person (profession 5) is rejected (1).
TEST(SimIntHandlersE2E, DragDropDispatch) {
    SetPrivilegeLeafHook(FlowPrivilege);
    g_log = FlowLog{};

    ContextActor target = MakeActor();
    target.flag457 = 1;   // selectable as drag target

    ContextActor goodSrc = MakeActor(); goodSrc.profession = 25;
    ContextActor badSrc  = MakeActor(); badSrc.profession = 5;

    auto evGood = MakeEvent(kModeDragApply);
    evGood.dragSource = &goodSrc;
    CHECK_EQ((int)ContextAttackOrSteal(&target, &evGood), 10);

    auto evBad = MakeEvent(kModeDragApply);
    evBad.dragSource = &badSrc;
    CHECK_EQ((int)ContextAttackOrSteal(&target, &evBad), 1);

    // exactly one leaf fired (the good drag) — CharmConfirm.
    CHECK_EQ(g_log.leaves.size(), (size_t)1);
    if (!g_log.leaves.empty())
        CHECK_EQ(g_log.leaves[0], (int)kPrivCharmConfirm);

    SetPrivilegeLeafHook(nullptr);
}

// Mutation/command flow: a sabotage interaction against a real building and a
// beating against a real person both resolve through the entity arrays and emit the
// right command tags; the same interactions against unknown ids fire nothing.
TEST(SimIntHandlersE2E, PerformCommandFlow) {
    ResetEntityArrays();
    SetCommandEmitHook(FlowCommand);
    g_log = FlowLog{};

    ContextActor actor = MakeActor();

    // Build entities: building id 700, person id 800.
    g_objects[0].alive = 1; g_objects[0].id = 700;
    g_persons[0].marker = 0; g_persons[0].id = 800; g_personIds[0] = 800;

    // Sabotage on the building (mode 4) → "sabotage", verdict 23.
    auto evSab = MakeEvent(4); evSab.targetId = 700;
    CHECK_EQ((int)PerformSabotage(&evSab, &actor), 23);

    // Beating on the person (mode 7) → "pruegel", verdict 24.
    auto evBeat = MakeEvent(7); evBeat.targetId = 800;
    CHECK_EQ((int)PerformBeating(&evBeat, &actor), 24);

    // Renovate always emits "renovieren".
    CHECK_EQ((int)PerformRenovate(&actor, &actor, &actor), 15);

    // Sabotage against a missing building → no emission, verdict 0.
    auto evMiss = MakeEvent(4); evMiss.targetId = 999;
    CHECK_EQ((int)PerformSabotage(&evMiss, &actor), 0);

    // Reference: exactly three commands, in order.
    CHECK_EQ(g_log.commands.size(), (size_t)3);
    if (g_log.commands.size() == 3) {
        CHECK(g_log.commands[0] == "sabotage");
        CHECK(g_log.commands[1] == "pruegel");
        CHECK(g_log.commands[2] == "renovieren");
    }

    SetCommandEmitHook(nullptr);
    ResetEntityArrays();
}
