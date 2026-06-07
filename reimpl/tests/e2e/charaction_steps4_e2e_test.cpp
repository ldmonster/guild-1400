// e2e flow for charaction_steps4 — drives a full master-exam dialog through its
// two-step lifecycle and a duel through arm -> dispatch -> resolve, observing the
// cross-cluster effects via the installed hook tables. Suite prefix:
// CharActionW_E2E.
#include "test.h"

#include "sim/charaction_steps4.h"
#include "sim/he.h"
#include "sim/npcaction.h"

#include <cstdint>
#include <cstring>
#include <vector>

using namespace guild;
using namespace guild::sim;

namespace {

struct BigRec {
    unsigned char bytes[512];
    BigRec() { std::memset(bytes, 0, sizeof(bytes)); }
    HeRecord* get() { return reinterpret_cast<HeRecord*>(bytes); }
    template <class T> T& at(int off) { return *reinterpret_cast<T*>(bytes + off); }
};

GameTime Clock() { GameTime t{}; t.day = 30; t.hour = 12; t.minute = 0; t.second = 0; return t; }

struct Rec {
    int frees = 0, cmd29 = 0, args25 = 0, cmd15 = 0, panelCreate = 0, panelDestroy = 0;
    int buildAll = 0; std::vector<int> buildOps;
    i32 dlgWin = 0, dlgRes = kDialogNone;
    int wealth = 0;
    HeRecord* person = nullptr;
    HeRecord* findFirst = nullptr;
    i32 statusRet = 0;
    std::vector<HeRecord*> byId;
} g;

i32 EFree(HeRecord*) { ++g.frees; return 0; }
i32 ECmd29(int, HeRecord*) { ++g.cmd29; return 100 + g.cmd29; }
i32 EStatus(i32) { return g.statusRet; }
HeRecord* EPerson(i32 id) {
    if (id >= 0 && (size_t)id < g.byId.size() && g.byId[id]) return g.byId[id];
    return g.person;
}
HeRecord* EFindFirst(int, int) { return g.findFirst; }
int EWealth(u16, HeRecord*) { return g.wealth; }
i32 ECityPid(u16) { return 555; }
void EArgs25(i32, int, int, int, int) { ++g.args25; }
void ECmd15(i32, i32, int, u8) { ++g.cmd15; }
void ECoord27(i32, i32, int) {}
void EHighlight(HeRecord*, int) {}
int  EProd(HeRecord*, int) { return 0; }
int  EOffice(HeRecord*, int*) { return 0; }
void ESendMsg(i32, int) {}
void EBuildAll(u16, int op) { ++g.buildAll; g.buildOps.push_back(op); }
void EPanelCreate(HeRecord*) { ++g.panelCreate; }
void EPanelDestroy(HeRecord*) { ++g.panelDestroy; }
void EDialogLine(HeRecord*, int, int) {}
i32 EDlgWin() { return g.dlgWin; }
i32 EDlgRes() { return g.dlgRes; }

void Install() {
    g = Rec{};
    static NpcLeafHooks leaf; leaf = NpcLeafHooks{};
    leaf.freeHandlerEntry = EFree;
    leaf.queueRequestEntity29 = ECmd29;
    leaf.packetStatus = EStatus;
    SetNpcLeafHooks(&leaf);

    static CharActionStep4Hooks s4; s4 = CharActionStep4Hooks{};
    s4.findPersonById = EPerson;
    s4.findFirstByFilter = EFindFirst;
    s4.personWealth = EWealth;
    s4.cityPersonId = ECityPid;
    s4.queueArgs25 = EArgs25;
    s4.enqueueCmd15 = ECmd15;
    s4.queueCoord27 = ECoord27;
    s4.highlightGuildMembers = EHighlight;
    s4.productionRating = EProd;
    s4.officeHolder = EOffice;
    s4.sendEntityMessage = ESendMsg;
    s4.buildingQueueAll = EBuildAll;
    s4.eventPanelCreate = EPanelCreate;
    s4.eventPanelDestroy = EPanelDestroy;
    s4.renderDialogLine = EDialogLine;
    s4.dialogWindow = EDlgWin;
    s4.dialogResult = EDlgRes;
    SetCharActionStep4Hooks(&s4);

    SetNpcClock(Clock());
}

} // namespace

TEST(CharActionW_E2E, MasterExamPromptLifecycle) {
    Install();
    BigRec r;
    // Deadline already past so the dialog opens immediately.
    He_SavedTime(r.get()) = Clock(); He_SavedTime(r.get()).day = 1;
    He_State(r.get()) = 0;
    g.wealth = 2000;

    // Tick 1: open the prompt dialog, advance to state 1.
    MasterExamPromptStep(r.get());
    CHECK_EQ(g.panelCreate, 1);
    CHECK_EQ(He_State(r.get()), 1);
    CHECK_EQ(g.frees, 0);

    // Tick 2: the player accepts -> fee charged (cmd15) + building broadcast 8, free.
    Cas4_Slot116(r.get()) = 0xABCD;
    g.dlgWin = 0xABCD; g.dlgRes = kDialogAccept;
    MasterExamPromptStep(r.get());
    CHECK_EQ(g.cmd15, 1);
    CHECK_EQ(g.buildOps[0], 8);
    CHECK_EQ(g.panelDestroy, 1);
    CHECK_EQ(g.frees, 1);
}

TEST(CharActionW_E2E, DuelArmThenResolve) {
    Install();
    BigRec h, opp, self;
    opp.at<u8>(2) = 6; opp.at<i32>(4) = 0x1; self.at<i32>(4) = 0x2;

    // 1) Arm a combatant: disarm both, queue the entity request.
    DuelArmCombatant(h.get(), opp.get(), self.get());
    CHECK_EQ(g.args25, 2);
    CHECK_EQ(g.cmd29, 1);

    // 2) The resolve coroutine, case-3 state, re-arms once the packet is applied.
    BigRec r;
    He_ReqHandle(r.get()) = -1;     // no pending packet
    He_State(r.get()) = 1;          // +2 = 3 -> rearm, no free
    He_Flags(r.get()) = 2;          // disarm + rearm active
    g.byId.assign(8, nullptr);
    g.person = self.get();          // both combatants resolve
    i32 before = g.cmd29;
    DuelResolveStep(r.get());
    CHECK_EQ(g.cmd29, before + 1);  // re-armed
    CHECK_EQ(g.frees, 0);           // case 3 does not free

    // 3) The terminal resolve (state -2 -> case 0) disarms, re-arms and frees.
    BigRec r2;
    He_ReqHandle(r2.get()) = -1;
    He_State(r2.get()) = -2;
    He_Flags(r2.get()) = 2;
    DuelResolveStep(r2.get());
    CHECK_EQ(g.frees, 1);

    SetCharActionStep4Hooks(nullptr);   // restore inert defaults
}
