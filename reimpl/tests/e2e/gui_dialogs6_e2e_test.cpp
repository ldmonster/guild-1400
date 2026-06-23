// End-to-end flow across gui_dialogs6: a player opens the building round-end
// stats panel, then the gelage (party) panel and confirms it, then the office
// session (empty -> force-quit). We drive the whole sequence through one captured
// hooks struct and assert the cross-function form lifecycle and command queueing.
#include "test.h"

#include "gui/gui_dialogs6.h"
#include "gui/gui_dialogs5.h"   // g_forceQuitLatch

#include <cstring>
#include <string>
#include <vector>

using namespace guild;
using namespace guild::gui;

namespace {

struct Flow {
    std::vector<std::string> forms;
    int created = 0, destroyed = 0;
    int partyStart = 0, partyEnd = 0, cmd15 = 0, slotReset = 0;
    int frameBudget = 0;
};
Flow g_flow;

int E_Form(i16, i16, const char* n) { ++g_flow.created; g_flow.forms.push_back(n ? n : ""); return 11; }
int E_Destroy(int) { ++g_flow.destroyed; return 11; }
int E_Frame(int, int, const void*) { return g_flow.frameBudget-- > 0 ? 1 : 0; }

GuiDialogs6Hooks Hooks() {
    GuiDialogs6Hooks h = *GuiDialogs6Hooks_Default();
    h.gameTickFinalize = &E_Form;
    h.formDestroy = &E_Destroy;
    h.gameLogicRunFrameLoop = &E_Frame;
    h.cmdEnqueueBuildingActionStart = [](const char*) { ++g_flow.partyStart; };
    h.cmdEnqueueBuildingActionEnd   = []() { ++g_flow.partyEnd; };
    h.cmdEnqueueCmd15               = [](int, int, int, int) { ++g_flow.cmd15; };
    h.cmdQueueRequestSlotReset28    = [](void*, int) { ++g_flow.slotReset; };
    return h;
}

} // namespace

TEST(GuiDialogs6E2E, ThreePanelSession) {
    g_flow = Flow();
    ResetGuiDialogs6();
    GuiDialogs6Hooks h = Hooks();

    // 1) building round-end stats panel: opens, renders, closes (default frame
    //    loop exits immediately).
    h.buildingValueComputeWorth = [](const void*, unsigned short, int* out) {
        std::memset(out, 0, 14 * sizeof(int)); out[0] = 50;
    };
    const GuiDialogs6Hooks* prev = SetGuiDialogs6Hooks(&h);

    char rec[600]; std::memset(rec, 0, sizeof(rec));
    int f1 = Panel_RunBuildingRoundEnd(rec, /*city*/1);
    CHECK_EQ(f1, 11);

    // 2) gelage party panel with a confirming click on this frame.
    g_flow.frameBudget = 1;
    static int gci = 0; gci = 0;
    h.formGetChildObjectId = [](int, int, int) { return gci++ == 0 ? 20 : 30; };
    h.readLastClickedId = []() { return 1210; };
    h.readHoverObject   = []() { return 30; };  // == the v37 confirm widget
    SetGuiDialogs6Hooks(&h);
    int f2 = Panel_RunGelage(/*buildingRec*/0);
    CHECK_EQ(f2, 11);
    CHECK_EQ(g_flow.partyStart, 1);
    CHECK_EQ(g_flow.partyEnd, 1);
    CHECK_EQ(g_flow.cmd15, 1);
    CHECK_EQ(g_flow.slotReset, 1);

    // 3) empty office session -> force-quit, destroy.
    h.readLastClickedId = []() { return -1; };
    h.readHoverObject   = []() { return -1; };
    SetGuiDialogs6Hooks(&h);
    unsigned short* who = Panel_RunOfficeSession(/*a1*/0, 0, 0, 0, nullptr);
    CHECK(who == nullptr);

    // Every opened form was closed.
    CHECK_EQ(g_flow.created, 3);
    CHECK_EQ(g_flow.destroyed, 3);
    CHECK_EQ((int)g_flow.forms.size(), 3);
    if (g_flow.forms.size() == 3) {
        CHECK(g_flow.forms[0] == "Runden\\Spielerrunde_Ende_geb");
        CHECK(g_flow.forms[1] == "special\\gelage");
        CHECK(g_flow.forms[2] == "special\\amt2");
    }
    SetGuiDialogs6Hooks(prev);
}
