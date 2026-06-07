// Integration test for guild::gui personnel_gui.
//
// REAL SIBLING WIRING: the recruitment-offer window's bribe-bonus roll
// (VIBE_Recruit_RunRecruitmentOfferWindow @0x55e263) draws its loyalty bonus and
// flavor-string index from VIBE_Math_RandomModulo (0x58b89c). Here we forward the
// personnel_gui randomModulo hook into the REAL reconstructed sibling
// util::RandomModulo (math_random.cpp), which itself draws from the REAL crt LCG
// (crt::RandNext, rand.cpp). We seed the generator with crt::Srand and assert the
// recovered bonus bracket exactly matches the LCG's draws — i.e. the
// gui -> util -> crt RNG chain flowed exactly as it would in the live process.
#include "test.h"

#include "gui/personnel_gui.h"
#include "util/math_random.h"   // RandomModulo (real sibling, 0x58b89c)
#include "crt/rand.h"           // Srand / RandNext (real LCG, 0x5cb8bc)

using namespace guild;
using namespace guild::gui;

namespace {
// Forward the gui hook straight into the real util sibling — exactly the live call.
int RealRandomModulo(int n) { return util::RandomModulo(static_cast<u16>(n)); }

// File-scope state for the mode-4 drive (plain fn-ptr hooks can't capture).
int g_frame = 0;
int g_msgBoxes = 0;
int g_destroys = 0;

int  IT_GameTick(i16, i16, const char*) { return 7; }
void IT_Center(int) {}
void IT_Select(int, int) {}
void IT_SetVis(int, int) {}
int  IT_GetChild(int, int, int) { return 200; }
int  IT_Text(unsigned, unsigned, int) { return 0; }
void IT_SetValue(int, int, int, int) {}
void IT_CheckRes(int, int) {}
int  IT_CancelEdge() { return 0; }
int  IT_QueueHire(int) { return 0; }
int  IT_PacketDone(int) { return 1; }
void IT_Refresh() {}
int  IT_Cost(int) { return 0; }
int  IT_Sum(int) { return 0; }
int  IT_FrameLoop(int, int, const void*) {
    if (g_frame >= 1) return 0;
    g_frame++;
    return 1;
}
int  IT_Click() { return 200; }  // matches the button id -> slot 0
void IT_MsgBox(const char*, int, int) { g_msgBoxes++; }
void IT_Destroy(int) { g_destroys++; }
} // namespace

// RecruitOfferBonusRoll over the REAL util::RandomModulo / crt::RandNext chain.
// Seed 12345 -> first LCG draws 21468, 9988, 22117, ...
//   rank 0: count = 21468 % 3 + 1 = 1 ; idx = 9988 % 5 = 3
TEST(PersonnelGuiIntegration, BonusRollRealRng) {
    crt::Srand(12345);
    OfferBonus b = RecruitOfferBonusRoll(0, RealRandomModulo);
    CHECK_EQ(b.count, 1);      // 21468 % 3 + 1
    CHECK_EQ(b.strIndex, 3);   // 9988 % 5

    // rank 1 consumes one draw for the idx (count is the clamped rank).
    crt::Srand(12345);
    OfferBonus b1 = RecruitOfferBonusRoll(1, RealRandomModulo);
    CHECK_EQ(b1.count, 1);
    CHECK_EQ(b1.strIndex, 21468 % 5 + 5);  // 8

    crt::Srand(12345);
    OfferBonus b2 = RecruitOfferBonusRoll(2, RealRandomModulo);
    CHECK_EQ(b2.count, 2);
    CHECK_EQ(b2.strIndex, 21468 % 4 + 9);  // 9

    crt::Srand(12345);
    OfferBonus b3 = RecruitOfferBonusRoll(5, RealRandomModulo);
    CHECK_EQ(b3.count, 3);                 // clamped
    CHECK_EQ(b3.strIndex, 21468 % 3 + 12); // 12
}

// Drive the whole offer window (mode 4) with the real RNG wired into the hook, and
// assert the cross-module flow reaches the bribe branch and terminates cleanly.
TEST(PersonnelGuiIntegration, OfferWindowRealRngWired) {
    crt::Srand(99);
    g_frame = 0;
    g_msgBoxes = 0;
    g_destroys = 0;

    PersonnelGuiHooks h{};
    h.gameTickFinalize = IT_GameTick;
    h.formCenterChildWindows = IT_Center;
    h.formSelectWindow = IT_Select;
    h.formDestroy = IT_Destroy;
    h.formSetChildrenVisible = IT_SetVis;
    h.formGetChildObjectId = IT_GetChild;
    h.textRenderRichString = IT_Text;
    h.objectSetValueOrText = IT_SetValue;
    h.dialogCheckResourceAmount = IT_CheckRes;
    h.dialogShowMessageBox = IT_MsgBox;
    h.gameLogicRunFrameLoop = IT_FrameLoop;
    h.readLastClickedObject = IT_Click;
    h.readCancelEdge = IT_CancelEdge;
    h.queueHireRequest = IT_QueueHire;
    h.commandGetPacketStatus = IT_PacketDone;
    h.refreshGuildState = IT_Refresh;
    h.computeRecruitmentCost = IT_Cost;
    h.sumCurrencyHeld = IT_Sum;
    h.randomModulo = RealRandomModulo;  // <-- the REAL sibling chain (gui->util->crt)

    SetPersonnelGuiHooks(h);
    i8 r = RunRecruitmentOfferWindow(/*entity*/ 1, /*handlerFound*/ true,
                                     /*byte187*/ 0, /*byte186*/ 1,
                                     /*cands*/ nullptr, /*count*/ 0);
    ResetPersonnelGuiHooks();

    CHECK_EQ(static_cast<int>(r), 2);
    CHECK_EQ(g_msgBoxes, 1);   // the bribe slot fired exactly one result box
    CHECK_EQ(g_destroys, 1);
}
