// e2e flow for interaction2: drive a full storage drag-drop session through the
// panel event handlers the way the input dispatcher would, plus a town-hall open.
#include "sim/interaction2.h"
#include "test.h"

using namespace guild;
using namespace guild::sim;

namespace {
u8 g_kt[256];
u8 KindHook(u8 i) { return g_kt[i]; }
PanelObject g_p;
PanelHandler g_h;

void Boot() {
    ResetInteraction2State();
    ResetInteraction2Hooks();
    for (auto& k : g_kt) k = 0;
    g_p = PanelObject{};
    g_h = PanelHandler{};
    g_p.handlerSlot = &g_h;
    g_i2.panel = &g_p;
    g_i2.panelActive = true;
    g_i2.eventTs = 1000;
    g_i2Hooks.buildingKindOf = &KindHook;
}

InteractionInputEvent Ev(u8 type, DropTarget* t, int phase, int code) {
    InteractionInputEvent e; e.type = type; e.target = t; e.phase = phase; e.code = code;
    return e;
}
} // namespace

TEST(Interaction2E2E, StorageDragDropSession) {
    Boot();
    g_i2.selectedBuildingId = 77;
    DropTarget store; store.recordIndex = 10; store.buildingId = 77;
    g_kt[10] = 18;  // storage building kind

    // 1. The panel becomes active and accepts events.
    CHECK(IsPanelActive());
    CHECK(!IsPanelModeTwo());

    // 2. Drop predicate accepts a kind-18 / code-12 target at event 43.
    auto allowQ = Ev(43, &store, 0, 12);
    CHECK_EQ(AllowStorageDropTarget(&allowQ), 1);

    // 3. Hover over the storage tile (event 43) advances the drag sub-state.
    auto hover = Ev(43, &store, 0, 12);
    CHECK_EQ(HandleStorageDropStep(&hover), 3);
    CHECK_EQ(GetDragSubState(), 1);
    CHECK_EQ((int)g_p.state, 10);
    CHECK_EQ(g_p.lastEventTs, 1000);

    // 4. Begin (event 16, phase 0) keeps dragging; dragSubState already 1 so
    //    the phase-0 subState=9 write is skipped (dragSubState != 0).
    g_i2.eventTs = 1010;
    auto begin = Ev(16, &store, 0, 0);
    CHECK_EQ(HandleStorageDropStep(&begin), 1);
    CHECK_EQ((int)g_p.state, 10);   // mirrors subState which stayed 10
    CHECK_EQ(g_p.lastEventTs, 1010);

    // 5. Commit (event 16, phase 1) with dragSubState==1 finalizes to state 11.
    g_i2.eventTs = 1020;
    auto commit = Ev(16, &store, 1, 0);
    CHECK_EQ(HandleStorageDropStep(&commit), 1);
    CHECK_EQ((int)g_p.state, 11);
    CHECK_EQ(g_p.lastEventTs, 1020);

    // 6. An unrelated event type now returns the no-op sentinel.
    auto other = Ev(99, &store, 0, 0);
    CHECK_EQ(HandleStorageDropStep(&other), 3);
}

TEST(Interaction2E2E, MultiStageOfficeDropThenConfirm) {
    Boot();
    DropTarget office; office.recordIndex = 4; office.buildingId = 0; g_kt[4] = 18;
    g_i2.selectedBuildingId = 0;

    CHECK_EQ(GetDragSubState(), 0);
    auto s0 = Ev(40, nullptr, 0, 341);
    CHECK_EQ(HandleMultiStageDrop(&s0), 1);
    auto s1 = Ev(39, nullptr, 0, 464);
    CHECK_EQ(HandleMultiStageDrop(&s1), 1);
    auto s2 = Ev(36, &office, 0, 0);
    CHECK_EQ(HandleMultiStageDrop(&s2), 1);
    CHECK_EQ(GetDragSubState(), 3);

    // Confirm step is a separate handler tracking its own progress.
    g_p.dragSubState = 0;
    auto conf = Ev(38, nullptr, 0, 464);
    CHECK_EQ(HandleConfirmDropStep(&conf), 1);
    CHECK_EQ(GetDragSubState(), 1);
}

TEST(Interaction2E2E, TownHallOpenFlow) {
    Boot();
    int beginCalls = 0, findCalls = 0, openCalls = 0;
    static int* pb; static int* pf; static int* po;
    pb = &beginCalls; pf = &findCalls; po = &openCalls;
    g_i2Hooks.personQueryBegin = [](int, int, int, int) { (*pb)++; return 1; };
    g_i2Hooks.gameObjectQueryFind = [](int, int, int, int) { (*pf)++; return 2; };
    g_i2Hooks.dialogOpenBuilding = [](int, u8) -> char { (*po)++; return 3; };

    char r = OpenTownHallDialog(0, 4242);
    CHECK_EQ((int)r, 3);
    CHECK_EQ(beginCalls, 1);
    CHECK_EQ(findCalls, 1);
    CHECK_EQ(openCalls, 1);

    // Pre-check short-circuit: a nonzero precheck returns immediately.
    char r2 = OpenTownHallDialog(9, 4242);
    CHECK_EQ((int)r2, 9);
    CHECK_EQ(beginCalls, 1);   // no new queries
}
