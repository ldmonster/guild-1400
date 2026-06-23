// End-to-end flow for charaction_steps5: drive the buy-object coroutine across all
// three of its phases (arm -> wait/queue -> finalize) plus the follow-target arming
// and the office-guard init, exercising the Step5 hook bridge and the shared
// NpcLeafHooks together as a host would. Suite prefix: CharActionV5E2E.
#include "test.h"

#include "sim/charaction_steps5.h"
#include "sim/he.h"
#include "sim/npcaction.h"

#include <cstring>
#include <vector>

using namespace guild;
using namespace guild::sim;

namespace {

struct Rec {
    unsigned char b[512];
    Rec() { std::memset(b, 0, sizeof(b)); }
    HeRecord* get() { return reinterpret_cast<HeRecord*>(b); }
};

// --- host state shared by both hook tables ---------------------------------
struct Host {
    int freeCalls = 0;
    int cmd29Calls = 0;
    i32 packetStatus = 0;
    HeRecord* self = nullptr;
    HeRecord* obj = nullptr;
    i32 req17Ret = 0;
    i32 seqRet = 0;
    int markCalls = 0;
    int quickjumpCalls = 0;
    int finalizeCalls = 0;
    u8 cityCat = 0;
    i32 cityPid = 0;
    int playerActionCalls = 0;
};
Host g_h;

i32 HFree(HeRecord*) { ++g_h.freeCalls; return 1; }
i32 HCmd29(int, HeRecord*) { ++g_h.cmd29Calls; return 77; }
i32 HStatus(i32) { return g_h.packetStatus; }

HeRecord* HQuery(i32, int, int, i32) { return g_h.self; }
HeRecord* HObject(i32, int, int, i32) { return g_h.obj; }
HeRecord* HFindNone(int, int, int, int) { return nullptr; }
HeRecord* HFindNextNone() { return nullptr; }
void HPlayer(HeRecord*, HeRecord*, HeRecord*, u16) { ++g_h.playerActionCalls; }
i32  HReq17(i32, i32, int, int, u8) { return g_h.req17Ret; }
i32  HSeq(i32) { return g_h.seqRet; }
void HMark(i32) { ++g_h.markCalls; }
void HSlot28() {}
void HQuick(i32, i32, int) { ++g_h.quickjumpCalls; }
void HFinalize(i32, i32) { ++g_h.finalizeCalls; }
i32  HPid(u16) { return g_h.cityPid; }
u8   HCat(u16) { return g_h.cityCat; }
int  HRnd(int) { return 0; }
int  HZero() { return 0; }
HeRecord* HPerson(i32) { return nullptr; }

void Install() {
    g_h = Host{};
    GameTime t{}; t.day = 12; t.hour = 10; t.minute = 0; t.second = 0;
    SetNpcClock(t);

    static NpcLeafHooks lf; lf = NpcLeafHooks{};
    lf.freeHandlerEntry = HFree;
    lf.queueRequestEntity29 = HCmd29;
    lf.packetStatus = HStatus;
    SetNpcLeafHooks(&lf);

    static CharActionStep5Hooks s5; s5 = CharActionStep5Hooks{};
    s5.findPersonById = HPerson;
    s5.personQueryBegin = HQuery;
    s5.objectQueryFind = HObject;
    s5.findFirstByFilter = HFindNone;
    s5.findNextMatching = HFindNextNone;
    s5.changePlayerAction = HPlayer;
    s5.queueRequest17 = HReq17;
    s5.packetSeqBase = HSeq;
    s5.markObjectBought = HMark;
    s5.queueSlotReset28 = HSlot28;
    s5.sendQuickjumpMessage = HQuick;
    s5.queuePurchaseFinalize32 = HFinalize;
    s5.cityPersonId = HPid;
    s5.cityCategory = HCat;
    s5.randomModulo = HRnd;
    s5.fastFrameCounter = HZero;
    s5.medFrameCounter = HZero;
    SetCharActionStep5Hooks(&s5);
}

} // namespace

TEST(CharActionV5E2E, BuyObjectFullLifecycle) {
    Install();
    Rec h, self;
    g_h.self = self.get();
    g_h.req17Ret = 4321;
    g_h.cityCat = 6;
    g_h.cityPid = 500;
    g_h.seqRet = 6001;

    He_State(h.get()) = 0;
    Cas5_IdB176(h.get()) = 333;

    // Phase 0: arm.
    u32 r0 = RunBuyObject(h.get());
    CHECK_EQ(r0, 1u);
    CHECK_EQ(He_State(h.get()), 1);

    // Phase 1, not yet due: appt set far in the future.
    He_ApptTime(h.get()) = GameTime{999, 0, 0, 0};
    u32 r1a = RunBuyObject(h.get());
    CHECK_EQ(r1a, 1u);             // future -> compare +1
    CHECK_EQ(He_State(h.get()), 1);

    // Phase 1, now due: queue the buy request.
    He_ApptTime(h.get()) = GameTime{1, 0, 0, 0};
    u32 r1b = RunBuyObject(h.get());
    CHECK_EQ(r1b, 4321u);
    CHECK_EQ(Cas5_IdC180(h.get()), 4321);
    CHECK_EQ(He_State(h.get()), 2);

    // Phase 2, packet pending.
    g_h.packetStatus = 0;
    u32 r2a = RunBuyObject(h.get());
    CHECK_EQ(r2a, 0u);
    CHECK_EQ(g_h.freeCalls, 0);

    // Phase 2, packet applied -> finalize with the market render path.
    g_h.packetStatus = 1;
    u32 r2b = RunBuyObject(h.get());
    CHECK_EQ(r2b, 1u);            // free() return
    CHECK_EQ(g_h.freeCalls, 1);
    CHECK_EQ(g_h.quickjumpCalls, 1);
    CHECK_EQ(g_h.markCalls, 1);
}

TEST(CharActionV5E2E, FollowThenOfficeGuardArm) {
    Install();
    Rec h, leader, self;
    // Make the leader resolvable: route findPersonById through a small table.
    static Rec* s_leader = &leader;
    struct L { static HeRecord* fn(i32) { return s_leader->get(); } };
    static CharActionStep5Hooks s5 = GetCharActionStep5Hooks();
    s5.findPersonById = &L::fn;
    SetCharActionStep5Hooks(&s5);

    g_h.self = self.get();
    Cas5_IdB176(h.get()) = 1;       // leader id
    Cas5_IdC180(h.get()) = -1;      // no follow object

    i32 hod = RunFollowTarget(h.get());
    // clock (12,10,0,0) + 30m -> (12,10,30,0); give-up + 6h -> (12,16,30,0) hod 16.
    CHECK_EQ(g_h.freeCalls, 0);
    CHECK_EQ(g_h.playerActionCalls, 1);
    CHECK_EQ(He_ApptTime(h.get()).hour, 10);
    CHECK_EQ(He_ApptTime(h.get()).minute, 30);
    CHECK_EQ(hod, 16);

    // Now arm an office-guard appointment on a fresh record (hour 10 < 17).
    Rec g;
    InitOfficeGuardState(g.get());
    CHECK_EQ(He_ApptTime(g.get()).hour, 17);
    CHECK_EQ(He_ApptTime(g.get()).minute, 0);
    CHECK_EQ(g_h.cmd29Calls, 1);
}
