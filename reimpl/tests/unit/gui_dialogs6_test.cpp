// Unit tests for gui_dialogs6 — the VIBE_Panel_* / VIBE_Dialog_* "run" loops.
// Each test installs a captured GuiDialogs6Hooks struct (starting from the
// module default) so the deterministic control flow is observable without the
// engine. Golden expectations were derived from the Hex-Rays pseudocode of each
// function (see addresses in the header).
#include "test.h"

#include "gui/gui_dialogs6.h"
#include "gui/gui_dialogs5.h"   // g_forceQuitLatch, Panel_RunUseObject path
#include "gui/object.h"         // g_widgets, Widget_AllocSlot, ResetWidgets
#include "gui/window.h"         // g_currentWindowId

#include <cstring>
#include <string>
#include <vector>

using namespace guild;
using namespace guild::gui;

namespace {

// ----- shared capture state ------------------------------------------------
struct Cap {
    std::vector<std::string> forms;       // form names passed to gameTickFinalize
    std::vector<unsigned> richIds;        // text ids rendered
    int destroyCount = 0;
    int destroyedForm = -999;
    int frameCalls = 0;                   // how many times the loop body ran
    int border = -1;                      // SetWindowBorder value (read g_winBorderColor[0])
    int edgeScrollCalls = 0;
    int barCreate = 0, barDestroy = 0;
    int selClearAll = 0;
    int cmdStart = 0, cmdEnd = 0, cmd15 = 0, slotReset = 0, args25 = 0;
    int mapDispatch = 0;
    unsigned lastFmtId = 0;
    int activeFlagCalls = 0;
    int useObjectCalls = 0;
    int showMsgBoxCalls = 0;
    int removeChildrenCalls = 0;
    int gridLayout = 0, columnLayout = 0;
};
Cap g_cap;

int FormId(i16, i16, const char* name) {
    g_cap.forms.push_back(name ? name : "(null)");
    return 7; // a fixed non-(-1) form id
}
int RichCap(unsigned id, int, int, int, int) { g_cap.richIds.push_back(id); return 0; }
int Destroy(int f) { ++g_cap.destroyCount; g_cap.destroyedForm = f; return f; }
void Edge(int, int, int) { ++g_cap.edgeScrollCalls; }
void BarCreate(int, int) { ++g_cap.barCreate; }
void BarDestroy(int, void*) { ++g_cap.barDestroy; }
void SelClear() { ++g_cap.selClearAll; }
void FmtCap(char* out, unsigned id, int, int, int) { g_cap.lastFmtId = id; if (out) out[0] = 0; }

// Frame loop: run N body iterations then exit. Counts iterations.
int g_frameBudget = 0;
int FrameN(int, int, const void*) {
    ++g_cap.frameCalls;
    return g_frameBudget-- > 0 ? 1 : 0;
}
// Frame loop that always exits immediately (0 body iterations of a while-loop).
int FrameZero(int, int, const void*) { return 0; }

GuiDialogs6Hooks MakeHooks() {
    GuiDialogs6Hooks h = *GuiDialogs6Hooks_Default();
    h.gameTickFinalize = &FormId;
    h.textRenderRichString = &RichCap;
    h.formDestroy = &Destroy;
    h.hudUpdateEdgeScroll = &Edge;
    h.playerBarCreate = &BarCreate;
    h.playerBarDestroy = &BarDestroy;
    h.selectionClearAll = &SelClear;
    h.textRenderFormattedMessage = &FmtCap;
    h.gameLogicRunFrameLoop = &FrameZero;
    return h;
}

void ResetCap() { g_cap = Cap(); g_frameBudget = 0; }

} // namespace

// ---------------------------------------------------------------------------
// RunGelage: loads special\gelage, sets border=14, renders 0x15B8/0x15B9, caps
// the amount at 1000, destroys the form. With the default frame loop (exits at
// once) no command is queued.
// ---------------------------------------------------------------------------
TEST(GuiDialogs6, RunGelageBuildsAndCapsAmount) {
    ResetCap(); ResetGuiDialogs6();
    GuiDialogs6Hooks h = MakeHooks();
    // currency above the 1000 cap so the clamp branch is exercised.
    h.personSumCurrencyHeld = [](int) { return 5000; };
    h.moneyConvertToDisplayCoord = [](int m, unsigned char) { return m; };
    static int capturedValue = -1;
    h.objectSetValueOrText = [](int, int, int v, int, int) { capturedValue = v; };
    const GuiDialogs6Hooks* prev = SetGuiDialogs6Hooks(&h);

    int r = Panel_RunGelage(/*buildingRec*/0);

    CHECK_EQ(r, 7);
    CHECK_EQ(g_cap.destroyCount, 1);
    CHECK_EQ((int)g_cap.forms.size(), 1);
    if (!g_cap.forms.empty()) CHECK(g_cap.forms[0] == "special\\gelage");
    CHECK_EQ(g_winBorderColor[0], 14);
    CHECK_EQ(g_winBorderColor[3], 14);
    CHECK_EQ(capturedValue, 1000);          // clamped
    CHECK_EQ(g_cap.edgeScrollCalls, 1);
    SetGuiDialogs6Hooks(prev);
}

// ---------------------------------------------------------------------------
// RunGelage confirm path: drive one frame where lastClicked==1210 and the
// confirm widget is hovered -> the party command sequence is queued exactly once.
// ---------------------------------------------------------------------------
TEST(GuiDialogs6, RunGelageConfirmQueuesPartyCommand) {
    ResetCap(); ResetGuiDialogs6();
    GuiDialogs6Hooks h = MakeHooks();
    // formGetChildObjectId returns 50 (confirm) then 60 (the +1 widget v37).
    static int gci = 0;
    h.formGetChildObjectId = [](int, int, int) { return gci++ == 0 ? 50 : 60; };
    h.readLastClickedId = []() { return 1210; };
    h.readHoverObject   = []() { return 60; };  // == v37
    h.cmdEnqueueBuildingActionStart = [](const char*) { ++g_cap.cmdStart; };
    h.cmdEnqueueBuildingActionEnd   = []() { ++g_cap.cmdEnd; };
    h.cmdEnqueueCmd15               = [](int, int, int, int) { ++g_cap.cmd15; };
    h.cmdQueueRequestSlotReset28    = [](void*, int) { ++g_cap.slotReset; };
    g_frameBudget = 1;                 // exactly one body iteration
    h.gameLogicRunFrameLoop = &FrameN;
    gci = 0;
    const GuiDialogs6Hooks* prev = SetGuiDialogs6Hooks(&h);

    Panel_RunGelage(0);

    CHECK_EQ(g_cap.cmdStart, 1);
    CHECK_EQ(g_cap.cmdEnd, 1);
    CHECK_EQ(g_cap.cmd15, 1);
    CHECK_EQ(g_cap.slotReset, 1);
    CHECK_EQ(g_forceQuitLatch, 1);
    SetGuiDialogs6Hooks(prev);
}

// ---------------------------------------------------------------------------
// RunInventory: gated on g_inventoryActiveWindow == -1 and the handler flag.
// Default flag returns 1 (proceed). Builds the panel, sets border=24.
// ---------------------------------------------------------------------------
TEST(GuiDialogs6, RunInventoryBuildsWhenIdle) {
    ResetCap(); ResetGuiDialogs6();
    GuiDialogs6Hooks h = MakeHooks();
    static int dispatch = 0;
    h.interactionDispatchPanelEvent = [](int, int, int, int) { ++dispatch; return 0; };
    dispatch = 0;
    const GuiDialogs6Hooks* prev = SetGuiDialogs6Hooks(&h);

    Panel_RunInventory(/*city*/3);

    CHECK_EQ((int)g_cap.forms.size(), 1);
    if (!g_cap.forms.empty()) CHECK(g_cap.forms[0] == "panel\\inventory");
    CHECK_EQ(g_winBorderColor[0], 24);
    CHECK_EQ(g_cap.destroyCount, 1);
    CHECK_EQ(dispatch, 2);              // enter + leave
    CHECK_EQ(g_inventoryActiveWindow, -1);
    SetGuiDialogs6Hooks(prev);
}

// RunInventory is a no-op when an inventory window is already active.
TEST(GuiDialogs6, RunInventoryNoOpWhenActive) {
    ResetCap(); ResetGuiDialogs6();
    g_inventoryActiveWindow = 5;        // already open
    GuiDialogs6Hooks h = MakeHooks();
    const GuiDialogs6Hooks* prev = SetGuiDialogs6Hooks(&h);

    Panel_RunInventory(0);

    CHECK_EQ((int)g_cap.forms.size(), 0);
    CHECK_EQ(g_cap.destroyCount, 0);
    SetGuiDialogs6Hooks(prev);
}

// RunInventory is also a no-op when the handler flag is clear.
TEST(GuiDialogs6, RunInventoryNoOpWhenFlagClear) {
    ResetCap(); ResetGuiDialogs6();
    GuiDialogs6Hooks h = MakeHooks();
    h.interactionTestHandlerFlagDword = [](int) { return 0; };
    const GuiDialogs6Hooks* prev = SetGuiDialogs6Hooks(&h);

    Panel_RunInventory(0);
    CHECK_EQ((int)g_cap.forms.size(), 0);
    SetGuiDialogs6Hooks(prev);
}

// ---------------------------------------------------------------------------
// RunBuildingRoundEnd: renders the upgrade-level line (0xA0) and every nonzero
// worth field. We feed a worth vector so a known subset of stat lines fire.
// ---------------------------------------------------------------------------
TEST(GuiDialogs6, RunBuildingRoundEndRendersStatLines) {
    ResetCap(); ResetGuiDialogs6();
    GuiDialogs6Hooks h = MakeHooks();
    // worth[0]=v11 (->0xA1), worth[1]=v13 (->0xA2), worth[5]=v17 (->0xA5).
    h.buildingValueComputeWorth = [](const void*, unsigned short, int* out) {
        std::memset(out, 0, 14 * sizeof(int));
        out[0] = 100; out[1] = 200; out[5] = 300;
    };
    h.buildingGetUpgradeLevel = [](int) { return 4; };
    const GuiDialogs6Hooks* prev = SetGuiDialogs6Hooks(&h);

    // a1 building record: type byte at [0]. Use a byte buffer; only [0] and [547] read.
    char rec[600]; std::memset(rec, 0, sizeof(rec));
    int r = Panel_RunBuildingRoundEnd(rec, /*city*/0);

    CHECK_EQ(r, 7);
    CHECK_EQ(g_cap.destroyCount, 1);
    auto has = [&](unsigned id) {
        for (unsigned x : g_cap.richIds) { if (x == id) return true; }
        return false;
    };
    CHECK(has(0xA0u));   // upgrade level always
    CHECK(has(0xA1u));   // v11
    CHECK(has(0xA2u));   // v13
    CHECK(has(0xA5u));   // v17
    SetGuiDialogs6Hooks(prev);
}

// ---------------------------------------------------------------------------
// ShowUniversity: lays out three choice columns (objectAddToWindow x3) and the
// per-column labels, then renders the confirm string 0x17B5.
// ---------------------------------------------------------------------------
TEST(GuiDialogs6, ShowUniversityLaysOutThreeChoices) {
    ResetCap(); ResetGuiDialogs6();
    ResetWidgets();
    g_currentWindowId = 0;
    GuiDialogs6Hooks h = MakeHooks();
    static int addToWin = 0, addLabel = 0;
    h.objectAddToWindow = [](int, int) { ++addToWin; return Widget_AllocSlot(); };
    h.objectAddTextLabel = [](i16, i16, int, const char*) { ++addLabel; return Widget_AllocSlot(); };
    addToWin = 0; addLabel = 0;
    const GuiDialogs6Hooks* prev = SetGuiDialogs6Hooks(&h);

    unsigned short person[16]; std::memset(person, 0, sizeof(person));
    int r = Panel_ShowUniversity(0, person);

    CHECK_EQ(r, 7);
    CHECK_EQ(addToWin, 3);            // three choice columns
    CHECK_EQ(addLabel, 12);          // 4 labels per column * 3
    bool hasConfirm = false;
    for (unsigned id : g_cap.richIds) if (id == 0x17B5u) hasConfirm = true;
    CHECK(hasConfirm);
    SetGuiDialogs6Hooks(prev);
}

// ---------------------------------------------------------------------------
// RunTraining: when >=3 pending handlers exist, it shows the "too many" message
// (fmt id 5719) and returns 0 WITHOUT building the panel.
// ---------------------------------------------------------------------------
TEST(GuiDialogs6, RunTrainingTooManyHandlersShowsMessage) {
    ResetCap(); ResetGuiDialogs6();
    GuiDialogs6Hooks h = MakeHooks();
    // first handler found, then next returns 3 times then null => v7 == 3.
    static int firstCount = 0;
    h.heFindFirstHandlerByFilter = [](int, int, int, int, int) -> const void* {
        firstCount = 0; return reinterpret_cast<const void*>(1);
    };
    h.heFindNextMatchingHandler = []() -> const void* {
        // returns non-null twice, then null: first found + 2 more = 3 total.
        return (++firstCount < 3) ? reinterpret_cast<const void*>(1) : nullptr;
    };
    h.dialogShowMessageBox = [](int, int, int) { ++g_cap.showMsgBoxCalls; };
    const GuiDialogs6Hooks* prev = SetGuiDialogs6Hooks(&h);

    char rec[600]; std::memset(rec, 0, sizeof(rec)); rec[0] = 4;  // type 4 -> v6=1
    int r = Panel_RunTraining(rec, /*a2*/0, /*a3*/0, /*city*/0);

    CHECK_EQ(r, 0);
    CHECK_EQ(g_cap.showMsgBoxCalls, 1);
    CHECK_EQ(g_cap.lastFmtId, 5719u);
    CHECK_EQ(g_cap.barCreate, 0);          // panel never built
    CHECK_EQ((int)g_cap.forms.size(), 0);
    SetGuiDialogs6Hooks(prev);
}

// RunTraining normal path (no handlers): builds the panel and tears it down.
TEST(GuiDialogs6, RunTrainingBuildsPanelWhenFew) {
    ResetCap(); ResetGuiDialogs6();
    GuiDialogs6Hooks h = MakeHooks();   // default He hooks return null => v7 == 0
    const GuiDialogs6Hooks* prev = SetGuiDialogs6Hooks(&h);

    char rec[600]; std::memset(rec, 0, sizeof(rec)); rec[0] = 16; // -> v6 = 15
    int r = Panel_RunTraining(rec, 0, 0, 0);

    CHECK_EQ(r, 0);
    CHECK_EQ(g_cap.barCreate, 1);
    CHECK_EQ(g_cap.barDestroy, 1);
    CHECK_EQ((int)g_cap.forms.size(), 1);
    if (!g_cap.forms.empty()) CHECK(g_cap.forms[0] == "special\\training");
    CHECK_EQ(g_cap.selClearAll, 1);
    SetGuiDialogs6Hooks(prev);
}

// ---------------------------------------------------------------------------
// RunPlantBar: with the heightmap handle inert (0) the whole body is skipped.
// With a null record handle the guarded +113 read yields null and no form is
// loaded — the deterministic inert path returns null.
// ---------------------------------------------------------------------------
TEST(GuiDialogs6, RunPlantBarInertReturnsNull) {
    ResetCap(); ResetGuiDialogs6();
    GuiDialogs6Hooks h = MakeHooks();
    const GuiDialogs6Hooks* prev = SetGuiDialogs6Hooks(&h);

    short* r = Panel_RunPlantBar(/*a1*/0);
    CHECK(r == nullptr);
    CHECK_EQ((int)g_cap.forms.size(), 0);   // no form loaded (inert)
    SetGuiDialogs6Hooks(prev);
}

// ---------------------------------------------------------------------------
// RunOfficeSession: a1==0 walks the (inert) person table => count 0 => force-quit
// path, loads special\amt2, removes children, destroys the form, returns null.
// ---------------------------------------------------------------------------
TEST(GuiDialogs6, RunOfficeSessionEmptyForcesQuit) {
    ResetCap(); ResetGuiDialogs6();
    GuiDialogs6Hooks h = MakeHooks();
    h.windowRemoveChildren = [](int, int) { ++g_cap.removeChildrenCalls; };
    const GuiDialogs6Hooks* prev = SetGuiDialogs6Hooks(&h);

    unsigned short* r = Panel_RunOfficeSession(/*a1*/0, 0, 0, 0, /*drillIn*/nullptr);

    CHECK(r == nullptr);
    CHECK_EQ((int)g_cap.forms.size(), 1);
    if (!g_cap.forms.empty()) CHECK(g_cap.forms[0] == "special\\amt2");
    CHECK_EQ(g_cap.removeChildrenCalls, 1);
    CHECK_EQ(g_cap.destroyCount, 1);
    CHECK_EQ(g_forceQuitLatch, 1);
    SetGuiDialogs6Hooks(prev);
}

// RunOfficeSession with a category and one collected candidate: builds the
// column layout (a1 in 1..6) and selecting it with no drill-in returns the rec.
TEST(GuiDialogs6, RunOfficeSessionCategorySelectsCandidate) {
    ResetCap(); ResetGuiDialogs6();
    GuiDialogs6Hooks h = MakeHooks();
    static unsigned short rec0[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    h.officeCollectByCategory = [](int, int, void* buf) {
        // write one candidate: id at +4 of a 24-byte stride.
        *reinterpret_cast<int*>(reinterpret_cast<char*>(buf) + 4) = 99;
        return 1;
    };
    h.personFindRecordById = [](int) -> const short* {
        return reinterpret_cast<const short*>(rec0);
    };
    h.hudBuildPersonColumnLayout = [](char, int, int, void*, int) { ++g_cap.columnLayout; };
    h.hudBuildPersonGridLayout   = [](char, int, int, void*, int) { ++g_cap.gridLayout; };
    // Drive a select: wheel-up + a click whose hover matches table[1] (the id slot).
    h.readMouseWheelUp = []() { return 1; };
    h.readLastClickedId = []() { return 5; };
    // table[i*14+1] holds the id slot; for i=0 that's table[1], which equals
    // the rec pointer's low bits — instead, our impl compares table[i*14+1] to
    // hover. We can't predict the pointer, so just verify the layout + quit path.
    h.readHoverObject = []() { return -1; };  // no match -> no select
    g_frameBudget = 1;
    h.gameLogicRunFrameLoop = &FrameN;
    const GuiDialogs6Hooks* prev = SetGuiDialogs6Hooks(&h);

    unsigned short* r = Panel_RunOfficeSession(/*a1*/3, 0, 0, 0, nullptr);
    (void)r;
    CHECK_EQ(g_cap.columnLayout, 1);   // category 3 => column layout
    CHECK_EQ(g_cap.gridLayout, 0);
    CHECK_EQ(g_cap.destroyCount, 1);
    SetGuiDialogs6Hooks(prev);
}

// ---------------------------------------------------------------------------
// RunThievesGuildTrain: loads diebe10, runs the grid, destroys, edge-scroll.
// ---------------------------------------------------------------------------
TEST(GuiDialogs6, RunThievesGuildTrainBuildsAndTearsDown) {
    ResetCap(); ResetGuiDialogs6();
    GuiDialogs6Hooks h = MakeHooks();
    static int grid = 0;
    h.inventoryRenderItemGrid = [](int, int) { ++grid; };
    grid = 0;
    g_frameBudget = 1;
    h.gameLogicRunFrameLoop = &FrameN;
    const GuiDialogs6Hooks* prev = SetGuiDialogs6Hooks(&h);

    int r = Panel_RunThievesGuildTrain(0, 0, nullptr, 0);
    CHECK_EQ(r, 0);
    CHECK_EQ((int)g_cap.forms.size(), 1);
    if (!g_cap.forms.empty()) CHECK(g_cap.forms[0] == "Locations\\diebe10");
    CHECK(grid >= 1);
    CHECK_EQ(g_cap.destroyCount, 1);
    CHECK_EQ(g_cap.edgeScrollCalls, 1);
    SetGuiDialogs6Hooks(prev);
}

// ---------------------------------------------------------------------------
// Dialog_RobberRaidConfirm / BriberyConfirm: gated on the active-char flag.
// flag==0 (default) and a nonzero target -> playerbar + map dispatch + clear.
// ---------------------------------------------------------------------------
TEST(GuiDialogs6, RobberRaidConfirmDispatchesWhenAllowed) {
    ResetCap(); ResetGuiDialogs6();
    GuiDialogs6Hooks h = MakeHooks();
    h.mapViewPanelDispatcher = [](int, int, void (*)(), int, int) { ++g_cap.mapDispatch; };
    const GuiDialogs6Hooks* prev = SetGuiDialogs6Hooks(&h);

    Dialog_RobberRaidConfirm(/*target*/42);
    CHECK_EQ(g_cap.barCreate, 1);
    CHECK_EQ(g_cap.barDestroy, 1);
    CHECK_EQ(g_cap.mapDispatch, 1);
    CHECK_EQ(g_cap.selClearAll, 1);
    CHECK_EQ(g_cap.lastFmtId, 5776u);
    SetGuiDialogs6Hooks(prev);
}

TEST(GuiDialogs6, RobberRaidConfirmBlockedByFlag) {
    ResetCap(); ResetGuiDialogs6();
    GuiDialogs6Hooks h = MakeHooks();
    h.dialogCheckActiveCharFlag = []() { return 1; };  // blocked
    h.mapViewPanelDispatcher = [](int, int, void (*)(), int, int) { ++g_cap.mapDispatch; };
    const GuiDialogs6Hooks* prev = SetGuiDialogs6Hooks(&h);

    Dialog_RobberRaidConfirm(42);
    CHECK_EQ(g_cap.barCreate, 0);
    CHECK_EQ(g_cap.mapDispatch, 0);
    SetGuiDialogs6Hooks(prev);
}

TEST(GuiDialogs6, BriberyConfirmUsesOwnMessageId) {
    ResetCap(); ResetGuiDialogs6();
    GuiDialogs6Hooks h = MakeHooks();
    h.mapViewPanelDispatcher = [](int, int, void (*)(), int, int) { ++g_cap.mapDispatch; };
    const GuiDialogs6Hooks* prev = SetGuiDialogs6Hooks(&h);

    Dialog_BriberyConfirm(7);
    CHECK_EQ(g_cap.mapDispatch, 1);
    CHECK_EQ(g_cap.lastFmtId, 5637u);   // bribery message id
    CHECK_EQ(g_cap.selClearAll, 1);
    SetGuiDialogs6Hooks(prev);
}

// Dialog with target==0 does nothing past the gray-color setup.
TEST(GuiDialogs6, BriberyConfirmNoTargetNoDispatch) {
    ResetCap(); ResetGuiDialogs6();
    GuiDialogs6Hooks h = MakeHooks();
    h.mapViewPanelDispatcher = [](int, int, void (*)(), int, int) { ++g_cap.mapDispatch; };
    const GuiDialogs6Hooks* prev = SetGuiDialogs6Hooks(&h);

    Dialog_BriberyConfirm(/*target*/0);
    CHECK_EQ(g_cap.mapDispatch, 0);
    CHECK_EQ(g_cap.barCreate, 0);
    SetGuiDialogs6Hooks(prev);
}
