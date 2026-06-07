// Unit tests for the interaction2 panel/drag-drop event state machine.
// Golden vectors hand-derived from the gilde.exe pseudocode (interaction2.h lists
// the addresses). Each test installs its own building-kind table + panel and asserts
// the exact transition / verdict.
#include "sim/interaction2.h"
#include "test.h"

using namespace guild;
using namespace guild::sim;

namespace {

// A simple building-kind table the BuildingKindOf hook reads.
u8 g_kindTable[256];
u8 KindHook(u8 idx) { return g_kindTable[idx]; }

// Reset everything and install a fresh panel + kind table.
PanelObject g_panel;
PanelHandler g_handler;

void Setup() {
    ResetInteraction2State();
    ResetInteraction2Hooks();
    for (auto& k : g_kindTable) k = 0;
    g_panel = PanelObject{};
    g_handler = PanelHandler{};
    g_panel.handlerSlot = &g_handler;
    g_i2.panel = &g_panel;
    g_i2.panelActive = true;
    g_i2.eventTs = 777;
    g_i2Hooks.buildingKindOf = &KindHook;
}

InteractionInputEvent Ev(u8 type, DropTarget* t = nullptr, int phase = 0,
                         int code = 0) {
    InteractionInputEvent e;
    e.type = type;
    e.target = t;
    e.phase = phase;
    e.code = code;
    return e;
}

} // namespace

// --- panel-active predicates --------------------------------------------------
TEST(Interaction2, IsPanelModeTwo_gates) {
    Setup();
    g_panel.panelMode = 2;
    CHECK(IsPanelModeTwo());
    g_panel.panelMode = 1;
    CHECK(!IsPanelModeTwo());
    g_i2.panelActive = false;
    g_panel.panelMode = 2;
    CHECK(!IsPanelModeTwo());
}

TEST(Interaction2, IsPanelActive_gates) {
    Setup();
    g_i2.panelActive = false;
    CHECK(IsPanelActive());                  // no panel → allowed
    g_i2.panelActive = true;
    g_panel.panelMode = 0; g_panel.state = 0;
    CHECK(IsPanelActive());
    g_panel.panelMode = 2;
    CHECK(!IsPanelActive());
    g_panel.panelMode = 0; g_panel.state = 5;
    CHECK(!IsPanelActive());
}

TEST(Interaction2, InvokeHandlerSlot60_paths) {
    Setup();
    g_i2.panelActive = false;
    CHECK_EQ(InvokeHandlerSlot60(0, 0, 0, 0), 1);
    Setup();
    g_panel.handlerSlot = nullptr;
    CHECK_EQ(InvokeHandlerSlot60(0, 0, 0, 0), 1);
    Setup();
    g_i2.suppress = true;
    CHECK_EQ(InvokeHandlerSlot60(0, 0, 0, 0), 0);
    Setup();
    g_handler.hasSlot60 = false;
    CHECK_EQ(InvokeHandlerSlot60(0, 0, 0, 0), 1);
    Setup();
    g_handler.hasSlot60 = true;
    g_i2Hooks.handlerSlot60 = [](char a, int b, int c, int d) {
        return (int)a + b + c + d + 100;
    };
    // arg order: (a, b, d, c) → 1 + 2 + 4 + 3 + 100 = 110
    CHECK_EQ(InvokeHandlerSlot60(1, 2, 3, 4), 110);
}

TEST(Interaction2, TestHandlerFlag_dword_word) {
    Setup();
    g_handler.flagDword = 0x0F;
    g_handler.flagWord = 0x30;
    CHECK(TestHandlerFlagDword(0x01));      // 0x01 & 0x0F
    CHECK(!TestHandlerFlagDword(0x10));     // no overlap
    CHECK(TestHandlerFlagWord(0x20));
    CHECK(!TestHandlerFlagWord(0x40));
    // no panel → true
    g_i2.panelActive = false;
    CHECK(TestHandlerFlagDword(0x10));
    // panelMode 2 → false
    Setup();
    g_handler.flagDword = 0xFF;
    g_panel.panelMode = 2;
    CHECK(!TestHandlerFlagDword(0x01));
    // suppress → false
    Setup();
    g_handler.flagDword = 0xFF;
    g_i2.suppress = true;
    CHECK(!TestHandlerFlagDword(0x01));
    // no handler slot → true
    Setup();
    g_panel.handlerSlot = nullptr;
    CHECK(TestHandlerFlagDword(0x01));
}

// --- drop-target predicates ---------------------------------------------------
TEST(Interaction2, AllowDropOnShop_kind18_code116) {
    Setup();
    DropTarget t; t.recordIndex = 5;
    g_kindTable[5] = 18;
    g_i2.curEventCode = 116;
    auto e = Ev(26, &t);
    CHECK_EQ(AllowDropOnShop(&e), 0);       // reject
    g_i2.curEventCode = 117;
    CHECK_EQ(AllowDropOnShop(&e), 1);       // wrong code → allow
    g_kindTable[5] = 17; g_i2.curEventCode = 116;
    CHECK_EQ(AllowDropOnShop(&e), 1);       // wrong kind → allow
    auto e2 = Ev(25, &t);
    CHECK_EQ(AllowDropOnShop(&e2), 1);      // wrong type → allow
}

TEST(Interaction2, CheckTargetState_open_variants) {
    Setup();
    DropTarget t; t.recordIndex = 3;
    auto e = Ev(25, &t);
    g_kindTable[3] = 15; CHECK_EQ(CheckTargetStateOpen(&e), 1);
    g_kindTable[3] = 2;  CHECK_EQ(CheckTargetStateOpen(&e), 0);
    g_kindTable[3] = 2;  CHECK_EQ(CheckTargetStateOpenOrTwo(&e), 1);
    g_kindTable[3] = 18; CHECK_EQ(CheckTargetStateOpenOrTwo(&e), 0);
    g_kindTable[3] = 18; CHECK_EQ(CheckTargetStateOpenTwoOr18(&e), 1);
    g_kindTable[3] = 7;  CHECK_EQ(CheckTargetStateOpenTwoOr18(&e), 0);
    // non-25 event → always allow
    auto e2 = Ev(99, &t);
    CHECK_EQ(CheckTargetStateOpen(&e2), 1);
    // null target → allow
    auto e3 = Ev(25, nullptr);
    CHECK_EQ(CheckTargetStateOpen(&e3), 1);
}

TEST(Interaction2, MatchObjectDropTarget_branches) {
    Setup();
    DropTarget t; t.recordIndex = 1;
    g_kindTable[1] = 15;
    auto e25 = Ev(25, &t, 0, 270);
    CHECK_EQ(MatchObjectDropTarget(&e25), 1);
    e25.code = 271;
    CHECK_EQ(MatchObjectDropTarget(&e25), 0);
    // event 26: returns kind!=15 || curEventCode!=270
    g_i2.curEventCode = 270;
    auto e26 = Ev(26, &t);
    CHECK_EQ(MatchObjectDropTarget(&e26), 0);   // kind15 && code270 → false
    g_i2.curEventCode = 271;
    CHECK_EQ(MatchObjectDropTarget(&e26), 1);
    // other types: !=27
    auto e27 = Ev(27);
    CHECK_EQ(MatchObjectDropTarget(&e27), 0);
    auto e10 = Ev(10);
    CHECK_EQ(MatchObjectDropTarget(&e10), 1);
}

// --- simple state-transition handlers -----------------------------------------
TEST(Interaction2, HandleEvent13SetState11) {
    Setup();
    DropTarget t; t.recordIndex = 2; g_kindTable[2] = 10;
    auto e = Ev(13, &t);
    CHECK_EQ(HandleEvent13SetState11(&e), 1);
    CHECK_EQ((int)g_panel.state, 11);
    CHECK_EQ((int)g_panel.subState, 11);
    CHECK_EQ(g_panel.lastEventTs, 777);
    // wrong kind
    Setup(); g_kindTable[2] = 9;
    auto e2 = Ev(13, &t);
    CHECK_EQ(HandleEvent13SetState11(&e2), 3);
    // wrong type
    auto e3 = Ev(14, &t);
    CHECK_EQ(HandleEvent13SetState11(&e3), 3);
}

TEST(Interaction2, HandleEvent23Or24SetState) {
    Setup();
    auto e23 = Ev(23);
    CHECK_EQ(HandleEvent23Or24SetState(&e23), 1);
    CHECK_EQ((int)g_panel.state, 10);
    Setup();
    auto e24 = Ev(24);
    CHECK_EQ(HandleEvent23Or24SetState(&e24), 1);
    CHECK_EQ((int)g_panel.state, 11);
    auto e0 = Ev(22);
    CHECK_EQ(HandleEvent23Or24SetState(&e0), 3);
}

TEST(Interaction2, HandleEvent25Codes) {
    Setup();
    DropTarget t; t.subCode = 270;
    auto e = Ev(25, &t);
    CHECK_EQ(HandleEvent25Code270(&e), 1);
    CHECK_EQ((int)g_panel.state, 11);
    t.subCode = 272; Setup();
    CHECK_EQ(HandleEvent25Code272(&e), 1);
    t.subCode = 116; Setup();
    CHECK_EQ(HandleEvent25Code116(&e), 1);
    t.subCode = 272; Setup();
    auto e26 = Ev(26, &t);
    CHECK_EQ(HandleEvent26Code272(&e26), 1);
    // mismatch
    t.subCode = 999; Setup();
    CHECK_EQ(HandleEvent25Code270(&e), 3);
}

TEST(Interaction2, HandleEvent27Or28State13) {
    Setup();
    DropTarget t; t.recordIndex = 4; g_kindTable[4] = 13;
    g_panel.subState = 4;
    auto e27 = Ev(27, &t);
    CHECK_EQ(HandleEvent27Or28State13(&e27), 1);
    CHECK_EQ((int)g_panel.state, 10);
    Setup(); g_kindTable[4] = 13;
    auto e28 = Ev(28, &t);
    CHECK_EQ(HandleEvent27Or28State13(&e28), 1);
    CHECK_EQ((int)g_panel.state, 11);
    // event 27 requires subState 4
    Setup(); g_kindTable[4] = 13; g_panel.subState = 9;
    CHECK_EQ(HandleEvent27Or28State13(&e27), 3);
}

TEST(Interaction2, HandleEvent25Or26Code19) {
    Setup();
    DropTarget t; t.subCode = 19;
    g_panel.subState = 4;
    auto e25 = Ev(25, &t);
    CHECK_EQ(HandleEvent25Or26Code19(&e25), 1);
    CHECK_EQ((int)g_panel.state, 10);
    Setup();
    auto e26 = Ev(26, &t);
    CHECK_EQ(HandleEvent25Or26Code19(&e26), 1);
    CHECK_EQ((int)g_panel.state, 11);
    t.subCode = 20; Setup();
    g_panel.subState = 4;
    CHECK_EQ(HandleEvent25Or26Code19(&e25), 3);
}

// --- pickup/drop transition ---------------------------------------------------
TEST(Interaction2, HandlePickupDropTransition) {
    // type 10, phase 0, subState 4 → state 9
    Setup(); g_panel.subState = 4;
    auto e = Ev(10, nullptr, 0);
    CHECK_EQ(HandlePickupDropTransition(&e), 1);
    CHECK_EQ((int)g_panel.state, 9);
    CHECK_EQ((int)g_panel.subState, 9);
    // type 10, phase 1, subState 9 → state 4
    Setup(); g_panel.subState = 9;
    auto e1 = Ev(10, nullptr, 1);
    CHECK_EQ(HandlePickupDropTransition(&e1), 1);
    CHECK_EQ((int)g_panel.state, 4);
    // type 10, phase 1, subState 10 → state 11
    Setup(); g_panel.subState = 10;
    CHECK_EQ(HandlePickupDropTransition(&e1), 1);
    CHECK_EQ((int)g_panel.state, 11);
    // type 7, target peerKind 5, subState 9 → state 10
    Setup(); g_panel.subState = 9;
    DropTarget t; t.peerKind = 5;
    auto e7 = Ev(7, &t);
    CHECK_EQ(HandlePickupDropTransition(&e7), 1);
    CHECK_EQ((int)g_panel.state, 10);
    // no match → 3
    Setup(); g_panel.subState = 0;
    CHECK_EQ(HandlePickupDropTransition(&e), 3);
}

// --- drop-step machines -------------------------------------------------------
TEST(Interaction2, HandleStorageDropStep_hover_then_commit) {
    Setup();
    g_i2.selectedBuildingId = 42;
    DropTarget t; t.recordIndex = 7; t.buildingId = 42; g_kindTable[7] = 18;
    // hover (event 43, code 12) → dragSubState 1, subState/state 10
    auto hov = Ev(43, &t, 0, 12);
    CHECK_EQ(HandleStorageDropStep(&hov), 3);   // returns 3 even on success
    CHECK_EQ(g_panel.dragSubState, 1);
    CHECK_EQ((int)g_panel.state, 10);
    // commit (event 16, phase 1, dragSubState 1 → subState 11)
    auto com = Ev(16, &t, 1);
    CHECK_EQ(HandleStorageDropStep(&com), 1);
    CHECK_EQ((int)g_panel.state, 11);
    // bad code on hover → 3, no change
    Setup(); g_i2.selectedBuildingId = 42; g_kindTable[7] = 18;
    auto bad = Ev(43, &t, 0, 999);
    CHECK_EQ(HandleStorageDropStep(&bad), 3);
    CHECK_EQ(g_panel.dragSubState, 0);
}

TEST(Interaction2, HandleWorkshopDropStep_commit_phase0) {
    Setup();
    g_i2.selectedBuildingId = 9;
    DropTarget t; t.recordIndex = 3; t.buildingId = 9; g_kindTable[3] = 18;
    // commit event 17 phase 0 dragSubState 0 → subState 9
    auto com = Ev(17, &t, 0);
    CHECK_EQ(HandleWorkshopDropStep(&com), 1);
    CHECK_EQ((int)g_panel.subState, 9);
    CHECK_EQ((int)g_panel.state, 9);
}

TEST(Interaction2, HandleHouseDropStep_kind2) {
    Setup();
    g_i2.selectedBuildingId = 1;
    DropTarget t; t.recordIndex = 6; t.buildingId = 1; g_kindTable[6] = 2;
    auto hov = Ev(43, &t, 0, 13);
    CHECK_EQ(HandleHouseDropStep(&hov), 3);
    CHECK_EQ(g_panel.dragSubState, 1);
    // commit phase 1 dragSubState 1 → 11
    auto com = Ev(16, &t, 1);
    CHECK_EQ(HandleHouseDropStep(&com), 1);
    CHECK_EQ((int)g_panel.state, 11);
}

TEST(Interaction2, AllowDropTarget_predicates) {
    Setup();
    g_i2.selectedBuildingId = 5;
    DropTarget t; t.recordIndex = 2; t.buildingId = 5;
    // storage allow: kind18 + code in {12,479}
    g_kindTable[2] = 18;
    auto e = Ev(43, &t, 0, 12);
    CHECK_EQ(AllowStorageDropTarget(&e), 1);
    e.code = 100;
    // not a valid 43 → falls to "is it 25/26/28?" → no → reject
    CHECK_EQ(AllowStorageDropTarget(&e), 0);
    auto e26 = Ev(26, &t);
    CHECK_EQ(AllowStorageDropTarget(&e26), 1);  // type 26 allowed
    // workshop: kind18 + code in {120,121,122} OR AllowDropOnShop
    auto w = Ev(43, &t, 0, 121);
    CHECK_EQ(AllowWorkshopDropTarget(&w), 1);
    auto w2 = Ev(99, &t, 0, 0);
    CHECK_EQ(AllowWorkshopDropTarget(&w2), 1);  // AllowDropOnShop default 1
    // house: kind2 + code in {2,5,8,13}
    g_kindTable[2] = 2;
    auto h = Ev(43, &t, 0, 8);
    CHECK_EQ(AllowHouseDropTarget(&h), 1);
    auto h2 = Ev(43, &t, 0, 7);
    CHECK_EQ(AllowHouseDropTarget(&h2), 0);
}

TEST(Interaction2, HandleEvent32DropOnShop) {
    Setup();
    g_panel.panelMode = 3;
    auto e = Ev(32, nullptr, 0, 341);
    CHECK_EQ(HandleEvent32DropOnShop(&e), 1);
    e.code = 0;
    CHECK_EQ(HandleEvent32DropOnShop(&e), 0);
    g_panel.panelMode = 1; e.code = 341;
    CHECK_EQ(HandleEvent32DropOnShop(&e), 0);
    // non-32 → AllowDropOnShop
    auto e2 = Ev(10);
    CHECK_EQ(HandleEvent32DropOnShop(&e2), 1);
}

TEST(Interaction2, MultiStage_and_confirm_drops) {
    Setup();
    DropTarget t; t.recordIndex = 1; t.buildingId = 0; g_kindTable[1] = 18;
    g_i2.selectedBuildingId = 0;
    // stage 0 → 1
    auto e40 = Ev(40, nullptr, 0, 341);
    CHECK_EQ(HandleMultiStageDrop(&e40), 1);
    CHECK_EQ(g_panel.dragSubState, 1);
    // stage 1 → 2
    auto e39 = Ev(39, nullptr, 0, 464);
    CHECK_EQ(HandleMultiStageDrop(&e39), 1);
    CHECK_EQ(g_panel.dragSubState, 2);
    // stage 2 → 3
    auto e36 = Ev(36, &t);
    CHECK_EQ(HandleMultiStageDrop(&e36), 1);
    CHECK_EQ(g_panel.dragSubState, 3);
    // fallback: type 26 → 1
    auto e26 = Ev(26);
    CHECK_EQ(HandleMultiStageDrop(&e26), 1);
    auto e5 = Ev(5);
    CHECK_EQ(HandleMultiStageDrop(&e5), 0);

    // confirm step
    Setup();
    auto c = Ev(38, nullptr, 0, 464);
    CHECK_EQ(HandleConfirmDropStep(&c), 1);
    CHECK_EQ(g_panel.dragSubState, 1);

    // event 41 shop busy
    Setup(); g_panel.dragSubState = 0;
    auto b = Ev(41, nullptr, 0, 341);
    CHECK_EQ(HandleEvent41ShopBusy(&b), 1);    // dragSubState<=0 → 1
    g_panel.dragSubState = 2;
    CHECK_EQ(HandleEvent41ShopBusy(&b), 1);
    CHECK_EQ(g_panel.dragSubState, 1);

    // sermon step
    Setup();
    auto s37 = Ev(37, nullptr, 0, 341);
    CHECK_EQ(HandleSermonDropStep(&s37), 1);
    CHECK_EQ(g_panel.dragSubState, 1);
    DropTarget st; st.recordIndex = 2; g_kindTable[2] = 10;
    auto s36 = Ev(36, &st);
    CHECK_EQ(HandleSermonDropStep(&s36), 1);
    CHECK_EQ(g_panel.dragSubState, 2);
}

// --- query / book / townhall --------------------------------------------------
TEST(Interaction2, QueryEventCodeRange146) {
    Setup();
    g_i2.curEventPresent = false;
    CHECK_EQ(QueryEventCodeRange146(), -1);
    g_i2.curEventPresent = true;
    g_i2.curEventCode = 0x92;       // 146
    g_panel.dragSubState = 0;
    CHECK_EQ(QueryEventCodeRange146(), 0);   // code == 146 → 0
    g_i2.curEventCode = 0x93;       // 147
    CHECK_EQ(QueryEventCodeRange146(), 1);
    g_panel.dragSubState = 1;
    CHECK_EQ(QueryEventCodeRange146(), -1);  // dragSubState set → -1
    g_i2.curEventCode = 0x50;       // out of range
    g_panel.dragSubState = 0;
    CHECK_EQ(QueryEventCodeRange146(), -1);
}

TEST(Interaction2, QueryEventStateRange) {
    Setup();
    g_i2.curEventPresent = true;
    g_i2.curEventCode = 146; g_panel.dragSubState = 0;
    CHECK_EQ(QueryEventStateRange(), 0);
    g_i2.curEventCode = 147; g_panel.dragSubState = 0;
    CHECK_EQ(QueryEventStateRange(), 3);
    g_i2.curEventCode = 147; g_panel.dragSubState = 1;
    CHECK_EQ(QueryEventStateRange(), 4);     // sub 1, code != 149 → 4
    g_panel.dragSubState = 2;
    CHECK_EQ(QueryEventStateRange(), 2);
    g_panel.dragSubState = 3;
    CHECK_EQ(QueryEventStateRange(), -1);
    g_i2.curEventCode = 0x10;
    CHECK_EQ(QueryEventStateRange(), -1);
}

TEST(Interaction2, GetDragSubState) {
    Setup();
    g_panel.dragSubState = 5;
    CHECK_EQ(GetDragSubState(), 5);
}

TEST(Interaction2, HandleBookPageTurn) {
    Setup();
    static int fwd = 0, back = 0;
    fwd = back = 0;
    g_i2Hooks.bookTurnForward = [](int) { fwd++; };
    g_i2Hooks.bookTurnBackward = [](int) { back++; };
    g_panel.bookObj = 99;
    g_panel.bookMode = 2;
    CHECK_EQ(HandleBookPageTurn(), 0);
    CHECK_EQ(fwd, 1); CHECK_EQ(back, 0);
    g_panel.bookMode = 3;
    CHECK_EQ(HandleBookPageTurn(), 0);
    CHECK_EQ(back, 1);
    // no book object → no turn
    g_panel.bookObj = 0; g_panel.bookMode = 2;
    CHECK_EQ(HandleBookPageTurn(), 0);
    CHECK_EQ(fwd, 1);   // unchanged
}

TEST(Interaction2, OpenTownHallDialog) {
    Setup();
    // precheck nonzero → returned directly
    CHECK_EQ((int)OpenTownHallDialog(7, 0), 7);
    // hovered kind-18 target → 0, no query
    Setup();
    g_i2.hoverTargetPresent = true; g_i2.hoverTargetIndex = 3; g_kindTable[3] = 18;
    g_i2Hooks.personQueryBegin = [](int, int, int, int) { return 999; };
    CHECK_EQ((int)OpenTownHallDialog(0, 0), 0);
    // full path
    Setup();
    g_i2Hooks.personQueryBegin = [](int, int, int, int) { return 5; };
    g_i2Hooks.gameObjectQueryFind = [](int, int, int, int) { return 8; };
    g_i2Hooks.dialogOpenBuilding = [](int, u8) -> char { return 22; };
    CHECK_EQ((int)OpenTownHallDialog(0, 0), 22);
    // person not found → 0
    g_i2Hooks.personQueryBegin = [](int, int, int, int) { return 0; };
    CHECK_EQ((int)OpenTownHallDialog(0, 0), 0);
}
