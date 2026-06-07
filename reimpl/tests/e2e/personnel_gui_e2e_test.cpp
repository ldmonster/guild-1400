// E2E tests for guild::gui personnel_gui — drive the full dialog bodies through
// the installable hooks, scripting a frame loop's worth of input and asserting the
// returned result codes and the queued side effects.
#include "test.h"

#include "gui/personnel_gui.h"

#include <cstring>

using namespace guild;
using namespace guild::gui;

namespace {

// A scriptable hook environment: a queue of "last-clicked" object ids, one per
// frame, and counters for the queued side effects.
struct Env {
    int clicks[8];
    int clickCount;
    int clickIdx;
    int frameBudget;     // how many frames gameLogicRunFrameLoop allows
    int frame;
    int hireQueued;      // queueHireRequest calls
    int msgBoxes;        // dialogShowMessageBox calls
    int destroys;        // formDestroy calls
    int sumCurrency;     // value returned by sumCurrencyHeld
    int recruitCost;     // value returned by computeRecruitmentCost
    int lastValueShown;  // last objectSetValueOrText value
};
Env g_env;

int  E_GameTick(i16, i16, const char*) { return 7; }
void E_Center(int) {}
void E_Select(int, int) {}
void E_Destroy(int) { g_env.destroys++; }
void E_SetVis(int, int) {}
int  E_GetChild(int, int, int b) { return 100 + b; }  // distinct button ids
int  E_Text(unsigned, unsigned, int) { return 0; }
void E_SetValue(int, int, int v, int) { g_env.lastValueShown = v; }
void E_CheckRes(int, int) {}
void E_MsgBox(const char*, int, int) { g_env.msgBoxes++; }

int  E_FrameLoop(int, int, const void*) {
    if (g_env.frame >= g_env.frameBudget) return 0;
    g_env.frame++;
    return 1;
}
int  E_LastClick() {
    if (g_env.clickIdx < g_env.clickCount) return g_env.clicks[g_env.clickIdx++];
    return -1;
}
int  E_CancelEdge() { return 0; }
int  E_QueueHire(int) { g_env.hireQueued++; return 55; }
int  E_PacketDone(int) { return 1; }
void E_Refresh() {}
int  E_Cost(int) { return g_env.recruitCost; }
int  E_Sum(int) { return g_env.sumCurrency; }
int  E_Rng(int n) { return n / 2; }

void InstallEnv() {
    PersonnelGuiHooks h{};
    h.gameTickFinalize        = E_GameTick;
    h.formCenterChildWindows  = E_Center;
    h.formSelectWindow        = E_Select;
    h.formDestroy             = E_Destroy;
    h.formSetChildrenVisible  = E_SetVis;
    h.formGetChildObjectId    = E_GetChild;
    h.textRenderRichString    = E_Text;
    h.objectSetValueOrText    = E_SetValue;
    h.dialogCheckResourceAmount = E_CheckRes;
    h.dialogShowMessageBox    = E_MsgBox;
    h.gameLogicRunFrameLoop   = E_FrameLoop;
    h.readLastClickedObject   = E_LastClick;
    h.readCancelEdge          = E_CancelEdge;
    h.queueHireRequest        = E_QueueHire;
    h.commandGetPacketStatus  = E_PacketDone;
    h.refreshGuildState       = E_Refresh;
    h.computeRecruitmentCost  = E_Cost;
    h.sumCurrencyHeld         = E_Sum;
    h.randomModulo            = E_Rng;
    SetPersonnelGuiHooks(h);
}

void ResetEnv() {
    std::memset(&g_env, 0, sizeof(g_env));
    InstallEnv();
}

} // namespace

// Pay-worker dialog: gate false -> immediate 0, no frame/destroy.
TEST(PersonnelGuiE2E, PayWorkerGateClosed) {
    ResetEnv();
    int r = RunPayWorkerDialog(/*resourceOk*/ false, /*player*/ 3);
    CHECK_EQ(r, 0);
    CHECK_EQ(g_env.destroys, 0);  // early-returns before form creation
    ResetPersonnelGuiHooks();
}

// Pay-worker dialog: confirm click on the first frame -> dataPtr becomes 1, the
// display shows clamp(currency)/32, and the form is destroyed once.
TEST(PersonnelGuiE2E, PayWorkerConfirm) {
    ResetEnv();
    g_env.frameBudget = 2;
    g_env.sumCurrency = 4800 + 100;   // clamps to 4800 -> 150
    g_env.clicks[0] = 1210;           // confirm
    g_env.clickCount = 1;
    int r = RunPayWorkerDialog(true, 3);
    CHECK_EQ(r, 1);
    CHECK_EQ(g_env.lastValueShown, 150);
    CHECK_EQ(g_env.msgBoxes, 1);
    CHECK_EQ(g_env.destroys, 1);
    ResetPersonnelGuiHooks();
}

// Hire-confirm dialog: confirm click queues a hire and returns 1.
TEST(PersonnelGuiE2E, HireConfirmYes) {
    ResetEnv();
    g_env.frameBudget = 3;
    g_env.recruitCost = 250;
    g_env.clicks[0] = 1210;
    g_env.clickCount = 1;
    int r = RunHireConfirmDialog(/*candidate*/ 42);
    CHECK_EQ(r, 1);
    CHECK_EQ(g_env.hireQueued, 1);
    CHECK_EQ(g_env.destroys, 1);
    ResetPersonnelGuiHooks();
}

// Hire-confirm dialog: cancel click -> 0, no hire queued.
TEST(PersonnelGuiE2E, HireConfirmCancel) {
    ResetEnv();
    g_env.frameBudget = 2;
    g_env.clicks[0] = 1155;
    g_env.clickCount = 1;
    int r = RunHireConfirmDialog(42);
    CHECK_EQ(r, 0);
    CHECK_EQ(g_env.hireQueued, 0);
    ResetPersonnelGuiHooks();
}

// Candidate-pick window: no click -> returns 2 (default, no hire).
TEST(PersonnelGuiE2E, CandidatePickNoSelection) {
    ResetEnv();
    g_env.frameBudget = 2;
    const int cands[2] = { 11, 22 };
    i8 r = RunCandidatePickWindow(cands, 2);
    CHECK_EQ(static_cast<int>(r), 2);
    CHECK_EQ(g_env.destroys, 1);
    ResetPersonnelGuiHooks();
}

// Candidate-pick window: a click drives RunHireConfirmDialog; if the confirm
// succeeds the window returns -110 (0x92).
TEST(PersonnelGuiE2E, CandidatePickHire) {
    ResetEnv();
    g_env.frameBudget = 4;
    // frame 1: a click in the pick window; the nested confirm sees its own click.
    g_env.clicks[0] = 500;    // pick-window click (opens confirm)
    g_env.clicks[1] = 1210;   // confirm click inside RunHireConfirmDialog
    g_env.clickCount = 2;
    const int cands[1] = { 11 };
    i8 r = RunCandidatePickWindow(cands, 1);
    CHECK_EQ(static_cast<int>(r), -110);
    CHECK_EQ(g_env.hireQueued, 1);
    ResetPersonnelGuiHooks();
}

// Recruitment-offer window: no handler + empty candidate pool -> 32.
TEST(PersonnelGuiE2E, OfferNoHandlerEmpty) {
    ResetEnv();
    i8 r = RunRecruitmentOfferWindow(/*entity*/ 1, /*handlerFound*/ false, 0, 0,
                                     /*cands*/ nullptr, /*count*/ 0);
    CHECK_EQ(static_cast<int>(r), 32);
    ResetPersonnelGuiHooks();
}

// Recruitment-offer window: no handler but a candidate pool -> mode 1 forwards to
// the candidate-pick window, which returns 2 with no selection.
TEST(PersonnelGuiE2E, OfferNoHandlerForwardsToPick) {
    ResetEnv();
    g_env.frameBudget = 2;
    const int cands[2] = { 11, 22 };
    i8 r = RunRecruitmentOfferWindow(1, false, 0, 0, cands, 2);
    CHECK_EQ(static_cast<int>(r), 2);
    ResetPersonnelGuiHooks();
}

// Recruitment-offer window: mode 4 (handler offer-pending). A click on the decline
// button (slot 3, id 100+0x18F8...) routes through the decline branch with a msgbox;
// returns 2 and destroys the form.
TEST(PersonnelGuiE2E, OfferMode4DeclineButton) {
    ResetEnv();
    g_env.frameBudget = 2;
    // button ids are 100 + (rich-string-id of each button). E_GetChild returns
    // 100 + b where b is textRenderRichString(...) == 0, so all four ids collide at
    // 100. RecruitOfferFindSlotIndex returns 0 for a click of 100 -> bribe slot 0.
    g_env.clicks[0] = 100;
    g_env.clickCount = 1;
    i8 r = RunRecruitmentOfferWindow(1, /*handlerFound*/ true, 0, /*byte186*/ 1,
                                     nullptr, 0);
    CHECK_EQ(static_cast<int>(r), 2);
    CHECK_EQ(g_env.msgBoxes, 1);   // bribe slot showed a result box
    CHECK_EQ(g_env.destroys, 1);
    ResetPersonnelGuiHooks();
}
