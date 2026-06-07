// Unit tests for sim/npcaction2 (plague behaviour) and sim/npctarget (combat/move
// target selection). Seeded CRT RNG + recording mock leaves; golden values are
// hand-computed from the LCG (state = state*1103515245 + 12345; RandNext returns
// (state>>16)&0x7FFF) and the recovered constants/tables.
#include "tests/framework/test.h"

#include "sim/npcaction2.h"
#include "sim/npctarget.h"
#include "sim/npcaction.h"   // SetNpcClock / NpcClock
#include "sim/he.h"
#include "crt/rand.h"

#include <cstring>
#include <vector>
#include <string>

using namespace guild::sim;
using guild::i32;
using guild::u8;

// ---------------------------------------------------------------------------
// Shared recording state for the NpcAction2 (plague) hooks.
// ---------------------------------------------------------------------------
namespace {

struct Recorder {
    // object-array model: aiType per slot, susceptible per slot, id = 1000+slot.
    std::vector<int> aiType = std::vector<int>(256, 0);
    std::vector<char> susceptible = std::vector<char>(256, 0);
    std::vector<int> entity29Args;   // sequence of QueueRequestEntity29 args
    int entity29Counter = 0;         // returns an increasing handle
    int op73Calls = 0;
    int slotResetCalls = 0;
    int outbreakObj = -777;
    int spreadDoneObj = -777;
    int infectCalls = 0;
    std::vector<int> named53Obj;     // obj ids passed to QueueRequestNamedObject53
    std::vector<int> pair33Persons;
    bool allPresent = true;          // personPresent default
    bool allNearDoor = true;         // personNearDoor default
    bool resolveOk = true;           // resolveObjectId default
    int infectReturn = 1;
};

Recorder g_rec;

i32 hk_entity29(int arg, HeRecord*) { g_rec.entity29Args.push_back(arg); return ++g_rec.entity29Counter; }
i32 hk_status(i32 handle) { return handle >= 0 ? 1 : 0; }
i32 hk_seq(i32 handle) { return handle + 5000; }
i32 hk_free(HeRecord*) { return 42; }
u8  hk_objKind(int) { return 0; }
u8  hk_objAi(int slot) { return slot >= 0 && slot < 256 ? (u8)g_rec.aiType[slot] : 0; }
i32 hk_objId(int slot) { return 1000 + slot; }
bool hk_objSusc(int slot) { return slot >= 0 && slot < 256 && g_rec.susceptible[slot]; }
bool hk_resolve(i32) { return g_rec.resolveOk; }
bool hk_present(i32) { return g_rec.allPresent; }
bool hk_nearDoor(i32, i32) { return g_rec.allNearDoor; }
void hk_single49(i32) {}
void hk_named53(i32, i32 objId, int, int) { g_rec.named53Obj.push_back(objId); }
void hk_pair33(i32 pid, int) { g_rec.pair33Persons.push_back(pid); }
i32  hk_op73(i32) { return 200 + (g_rec.op73Calls++); }
void hk_slotReset(i32) { g_rec.slotResetCalls++; }
void hk_outbreak(i32 obj) { g_rec.outbreakObj = obj; }
void hk_spreadDone(i32 obj) { g_rec.spreadDoneObj = obj; }
int  hk_infect(i32) { g_rec.infectCalls++; return g_rec.infectReturn; }

NpcAction2Hooks MakeHooks() {
    NpcAction2Hooks h{};
    h.queueRequestEntity29 = hk_entity29;
    h.packetStatus = hk_status;
    h.packetSeq = hk_seq;
    h.freeHandlerEntry = hk_free;
    h.objectKindAt = hk_objKind;
    h.objectAiPlayerType = hk_objAi;
    h.objectId = hk_objId;
    h.objectSusceptible = hk_objSusc;
    h.resolveObjectId = hk_resolve;
    h.personPresent = hk_present;
    h.personNearDoor = hk_nearDoor;
    h.queueSingle49 = hk_single49;
    h.queueNamedObject53 = hk_named53;
    h.queuePair33 = hk_pair33;
    h.requestBuildOp73Str = hk_op73;
    h.beginSlotResetPacket = hk_slotReset;
    h.broadcastOutbreak = hk_outbreak;
    h.broadcastSpreadDone = hk_spreadDone;
    h.plagueInfectNearby = hk_infect;
    return h;
}

void ResetRec() {
    g_rec = Recorder{};
    guild::crt::Srand(1);
    GameTime t{};
    t.day = 10; t.hour = 8; t.minute = 0; t.second = 0;
    SetNpcClock(t);
}

HeRecord MakeHe() {
    HeRecord h{};
    std::memset(&h, 0, sizeof(h));
    return h;
}

} // namespace

// ===========================================================================
// PlagueSelectTarget — success path (object at start slot 198 is type 7).
// ===========================================================================
TEST(SimNpcAction2, PlagueSelectTargetSuccess) {
    ResetRec();
    NpcAction2Hooks hk = MakeHooks();
    SetNpcAction2Hooks(&hk);

    // seed 1: RandomModulo(256) draw #1 == 198. Make slot 198 AiPlayer type 7.
    g_rec.aiType[198] = 7;

    HeRecord h = MakeHe();
    NpcAction2_PlagueSelectTarget(&h);

    // Found immediately at slot 198 -> object id 1000+198 = 1198.
    CHECK_EQ(He_PlagueSource(&h), 1198);
    // Cursor (+176) := draw #2 == 126 ; step (+180) := scanSteps[draw#3 % 16].
    CHECK_EQ(He_TargetObjId(&h), 126);
    // draw #3 = RandNext()#3 = 10113, %16 = 1 -> scanSteps[1] = 3.
    CHECK_EQ(He_ScanStep(&h), 3);
    // 4 op73 "PEST" packets into the member array (handles 200..203).
    CHECK_EQ(g_rec.op73Calls, 4);
    CHECK_EQ(He_PacketId(&h, 0), 200);
    CHECK_EQ(He_PacketId(&h, 3), 203);
    // QueueRequestEntity29(0) issued; handle stored at +132.
    CHECK_EQ(g_rec.entity29Args.size(), (size_t)1);
    CHECK_EQ(g_rec.entity29Args[0], 0);
    CHECK_EQ(He_ReqHandle(&h), g_rec.entity29Counter);
    // outbreak broadcast for the chosen object.
    CHECK_EQ(g_rec.outbreakObj, 1198);
    // appointment advanced +1 minute from 08:00.
    CHECK_EQ(He_ApptTime(&h).hour, (guild::u16)8);
    CHECK_EQ(He_ApptTime(&h).minute, 1);
}

// ===========================================================================
// PlagueSelectTarget — failure path (no object is type 7): queues (-1), no setup.
// ===========================================================================
TEST(SimNpcAction2, PlagueSelectTargetFailure) {
    ResetRec();
    NpcAction2Hooks hk = MakeHooks();
    SetNpcAction2Hooks(&hk);
    // leave all aiType == 0 -> never finds type 7 -> 256 probes fail.

    HeRecord h = MakeHe();
    NpcAction2_PlagueSelectTarget(&h);

    CHECK_EQ(g_rec.entity29Args.size(), (size_t)1);
    CHECK_EQ(g_rec.entity29Args[0], -1);
    CHECK_EQ(g_rec.op73Calls, 0);
    CHECK_EQ(g_rec.outbreakObj, -777);
    CHECK_EQ(He_ReqHandle(&h), 1);
}

// ===========================================================================
// PlagueSpreadStep — teardown path (state -1, flag 0x02 cures the members).
// ===========================================================================
TEST(SimNpcAction2, PlagueSpreadTeardown) {
    ResetRec();
    NpcAction2Hooks hk = MakeHooks();
    SetNpcAction2Hooks(&hk);

    HeRecord h = MakeHe();
    He_State(&h) = -1;
    He_Flags(&h) = 0x02;
    for (int i = 0; i < 4; ++i) He_PacketId(&h, i) = 500 + i;

    NpcAction2_PlagueSpreadStep(&h);

    // Each present member gets a pair33 "cured".
    CHECK_EQ(g_rec.pair33Persons.size(), (size_t)4);
    CHECK_EQ(g_rec.pair33Persons[0], 500);
    CHECK_EQ(g_rec.pair33Persons[3], 503);
}

// ===========================================================================
// PlagueSpreadStep — phase 0 refreshes member packet ids and re-arms phase 1.
// ===========================================================================
TEST(SimNpcAction2, PlagueSpreadPhase0) {
    ResetRec();
    NpcAction2Hooks hk = MakeHooks();
    SetNpcAction2Hooks(&hk);

    HeRecord h = MakeHe();
    He_State(&h) = 0;
    He_ReqHandle(&h) = -1;          // not pending
    for (int i = 0; i < 4; ++i) He_PacketId(&h, i) = 10 + i;

    NpcAction2_PlagueSpreadStep(&h);

    // status==1 for handles >=0 -> packet id := seq == handle+5000.
    CHECK_EQ(He_PacketId(&h, 0), 5010);
    CHECK_EQ(He_PacketId(&h, 3), 5013);
    // QueueRequestEntity29(1) re-arm.
    CHECK_EQ(g_rec.entity29Args.size(), (size_t)1);
    CHECK_EQ(g_rec.entity29Args[0], 1);
}

// ===========================================================================
// PlagueSpreadStep — phase 1 finds a susceptible object and sends carriers there.
// ===========================================================================
TEST(SimNpcAction2, PlagueSpreadPhase1FindsTarget) {
    ResetRec();
    NpcAction2Hooks hk = MakeHooks();
    SetNpcAction2Hooks(&hk);

    HeRecord h = MakeHe();
    He_State(&h) = 1;
    He_ReqHandle(&h) = -1;
    He_TargetObjId(&h) = 50;        // cursor
    He_ScanStep(&h) = 3;            // step
    for (int i = 0; i < 4; ++i) He_PacketId(&h, i) = 600 + i;
    g_rec.susceptible[50] = 1;      // immediate hit at the cursor

    NpcAction2_PlagueSpreadStep(&h);

    // target object id = 1000 + 50.
    CHECK_EQ(He_PlagueTarget(&h), 1050);
    // all 4 members present -> 4 named53 to the target, delta = 5 (someone present).
    CHECK_EQ(g_rec.named53Obj.size(), (size_t)4);
    CHECK_EQ(g_rec.named53Obj[0], 1050);
    CHECK_EQ(He_ApptTime(&h).minute, 5);
    // re-arm phase 2.
    CHECK_EQ(g_rec.entity29Args.size(), (size_t)1);
    CHECK_EQ(g_rec.entity29Args[0], 2);
    // cursor advanced one step past the hit: (50 + 3) % 256 == 53.
    CHECK_EQ(He_TargetObjId(&h), 53);
}

// ===========================================================================
// PlagueSpreadStep — phase 2, all at door -> infect, counter decrements, re-arm 1.
// ===========================================================================
TEST(SimNpcAction2, PlagueSpreadPhase2Infect) {
    ResetRec();
    NpcAction2Hooks hk = MakeHooks();
    SetNpcAction2Hooks(&hk);

    HeRecord h = MakeHe();
    He_State(&h) = 2;
    He_ReqHandle(&h) = -1;
    He_PlagueTarget(&h) = 1050;
    He_Counter172(&h) = 3;          // remaining iterations
    g_rec.allNearDoor = true;

    NpcAction2_PlagueSpreadStep(&h);

    CHECK_EQ(g_rec.infectCalls, 1);
    CHECK_EQ(He_Counter172(&h), 2);     // decremented, still > 0
    CHECK_EQ(g_rec.entity29Args.size(), (size_t)1);
    CHECK_EQ(g_rec.entity29Args[0], 1); // re-arm phase 1
}

// ===========================================================================
// PlagueSpreadStep — phase 2 last iteration -> sends carriers home, re-arm 3.
// ===========================================================================
TEST(SimNpcAction2, PlagueSpreadPhase2Done) {
    ResetRec();
    NpcAction2Hooks hk = MakeHooks();
    SetNpcAction2Hooks(&hk);

    HeRecord h = MakeHe();
    He_State(&h) = 2;
    He_ReqHandle(&h) = -1;
    He_PlagueTarget(&h) = 1050;
    He_PlagueSource(&h) = 1198;
    He_Counter172(&h) = 1;          // last iteration -> 0
    g_rec.allNearDoor = true;

    NpcAction2_PlagueSpreadStep(&h);

    CHECK_EQ(He_Counter172(&h), 0);
    CHECK_EQ(g_rec.infectCalls, 1);
    // members sent home to the source object.
    CHECK_EQ(g_rec.named53Obj.size(), (size_t)4);
    CHECK_EQ(g_rec.named53Obj[0], 1198);
    CHECK_EQ(g_rec.entity29Args.back(), 3); // re-arm phase 3
}

// ===========================================================================
// PlagueSpreadStep — phase 2 not yet at door -> just re-arms phase 2 (no infect).
// ===========================================================================
TEST(SimNpcAction2, PlagueSpreadPhase2Waiting) {
    ResetRec();
    NpcAction2Hooks hk = MakeHooks();
    SetNpcAction2Hooks(&hk);

    HeRecord h = MakeHe();
    He_State(&h) = 2;
    He_ReqHandle(&h) = -1;
    He_PlagueTarget(&h) = 1050;
    He_Counter172(&h) = 3;
    g_rec.allNearDoor = false;      // member hasn't reached the door

    NpcAction2_PlagueSpreadStep(&h);

    CHECK_EQ(g_rec.infectCalls, 0);
    CHECK_EQ(He_Counter172(&h), 3);     // unchanged
    CHECK_EQ(He_State(&h), 2);
    CHECK_EQ(g_rec.entity29Args.back(), 2);
}

// ===========================================================================
// PlagueSpreadStep — gating: pending packet (+132) with status 0 -> no-op.
// ===========================================================================
TEST(SimNpcAction2, PlagueSpreadPendingGate) {
    ResetRec();
    NpcAction2Hooks hk = MakeHooks();
    // make status return 0 for a specific pending handle.
    hk.packetStatus = [](i32 handle) -> i32 { return handle == 99 ? 0 : 1; };
    SetNpcAction2Hooks(&hk);

    HeRecord h = MakeHe();
    He_State(&h) = 1;
    He_ReqHandle(&h) = 99;          // pending

    NpcAction2_PlagueSpreadStep(&h);

    CHECK_EQ(g_rec.entity29Args.size(), (size_t)0);  // nothing happened
}

// ===========================================================================
// NpcTarget — shared recording state.
// ===========================================================================
namespace {

struct PMock {
    u8 kind;
    i32 id;
    u8 catA, catB, catC, sub;
};

// A synthetic person scene keyed by handle.
PMock* P(PersonHandle p) { return reinterpret_cast<PMock*>(p); }

u8  t_kind(PersonHandle p) { return P(p)->kind; }
i32 t_id(PersonHandle p) { return P(p)->id; }
u8  t_catA(PersonHandle p) { return P(p)->catA; }
u8  t_catB(PersonHandle p) { return P(p)->catB; }
u8  t_catC(PersonHandle p) { return P(p)->catC; }
u8  t_sub(PersonHandle p) { return P(p)->sub; }

// favorability: the original passes (self.kind, candidate.kind); we return the
// candidate's kind so tests control each candidate's score via its kind byte.
double t_fav(guild::u16 /*a*/, guild::u16 b, int) { return (double)b; }

struct TScene {
    std::vector<PMock> people;
    std::vector<PersonHandle> succ;   // returned by collectSuccessors
    std::vector<i32> catIds;          // returned by collectByCategory
    int nearestIndex = -1;            // returned by findNearestVisible
    bool nearestHit = false;
    PMock* byIndex = nullptr;
    bool officeOk = true;
    u8 officeRankVal = 4;             // (4-1)/3 == 1 -> tier 1
};
TScene g_sc;

int t_succ(u8, int maxn, PersonHandle* out) {
    int n = (int)g_sc.succ.size(); if (n > maxn) n = maxn;
    for (int i = 0; i < n; ++i) out[i] = g_sc.succ[i];
    return n;
}
int t_cat(u8, int maxn, i32* out) {
    int n = (int)g_sc.catIds.size(); if (n > maxn) n = maxn;
    for (int i = 0; i < n; ++i) out[i] = g_sc.catIds[i];
    return n;
}
PersonHandle t_find(i32 id) {
    for (auto& m : g_sc.people) if (m.id == id) return &m;
    return nullptr;
}
u8 t_oRank(u8, bool* ok) { *ok = g_sc.officeOk; return g_sc.officeRankVal; }
u8 t_oHolder(PersonHandle, u8, bool* ok) { *ok = false; return 0; }
int t_nearest(PersonHandle, double, double, i32* idx) {
    if (g_sc.nearestHit) { *idx = g_sc.nearestIndex; return 1; }
    return 0;
}
PersonHandle t_byIndex(i32) { return g_sc.byIndex; }

NpcTargetHooks MakeTHooks() {
    NpcTargetHooks h{};
    h.personKind = t_kind;
    h.personId = t_id;
    h.officeCatA = t_catA;
    h.officeCatB = t_catB;
    h.officeCatC = t_catC;
    h.subMethod = t_sub;
    h.officeRank = t_oRank;
    h.officeHolderRank = t_oHolder;
    h.collectSuccessors = t_succ;
    h.collectByCategory = t_cat;
    h.findPersonById = t_find;
    h.favorability = t_fav;
    h.findNearestVisible = t_nearest;
    h.personByIndex = t_byIndex;
    return h;
}

} // namespace

// ===========================================================================
// FindNearestEnemy — picks the lowest-favorability successor candidate.
// ===========================================================================
TEST(SimNpcTarget, FindNearestEnemyLowestScore) {
    g_sc = TScene{};
    NpcTargetHooks hk = MakeTHooks();
    SetNpcTargetHooks(&hk);

    PMock self{ /*kind*/10, /*id*/1, 0, 0, /*catC*/5, 0 };
    PMock a{ 50, 100, 0,0,0,0 };
    PMock b{ 20, 101, 0,0,0,0 };   // lowest fav (kind 20) -> the chosen "enemy"
    PMock c{ 90, 102, 0,0,0,0 };
    g_sc.people = { self, a, b, c };
    g_sc.succ = { &g_sc.people[1], &g_sc.people[2], &g_sc.people[3] };

    PersonHandle e = NpcTarget_FindNearestEnemy(&g_sc.people[0]);
    CHECK(e == &g_sc.people[2]);   // person b, kind 20

    // Reject curve: count(3)*8 + 52 = 76 >= 20 -> NOT rejected, so e stays.
    CHECK(e != nullptr);
}

// ===========================================================================
// FindNearestEnemy — reject when candidate too friendly, fall back to visible.
// ===========================================================================
TEST(SimNpcTarget, FindNearestEnemyRejectFallback) {
    g_sc = TScene{};
    NpcTargetHooks hk = MakeTHooks();
    SetNpcTargetHooks(&hk);

    PMock self{ 10, 1, 0, 0, 5, 0 };
    PMock a{ 200, 100, 0,0,0,0 };   // score 200 ; count 1*8+52 = 60 < 200 -> reject
    g_sc.people = { self, a };
    g_sc.succ = { &g_sc.people[1] };
    g_sc.nearestHit = true;
    g_sc.nearestIndex = 7;
    g_sc.byIndex = &g_sc.people[1];

    PersonHandle e = NpcTarget_FindNearestEnemy(&g_sc.people[0]);
    // candidate rejected -> fall back to nearest visible (personByIndex).
    CHECK(e == &g_sc.people[1]);
}

// ===========================================================================
// PickDirectionSeqA — accepts a direction whose candidate average fav < 66.
// ===========================================================================
TEST(SimNpcTarget, PickDirectionSeqAAccepts) {
    g_sc = TScene{};
    guild::crt::Srand(1);
    NpcTargetHooks hk = MakeTHooks();
    SetNpcTargetHooks(&hk);

    PMock self{ 10, 1, 0, 0, /*catC*/5, 0 };
    // three low-kind candidates -> average == 10 < 66 -> accept the first dir.
    PMock c0{ 10, 200, 0,0,0,0 };
    PMock c1{ 10, 201, 0,0,0,0 };
    PMock c2{ 10, 202, 0,0,0,0 };
    g_sc.people = { self, c0, c1, c2 };
    g_sc.catIds = { 200, 201, 202 };
    g_sc.officeOk = true;
    g_sc.officeRankVal = 4;

    u8 d = NpcTarget_PickDirectionSeqA(&g_sc.people[0], 0);
    CHECK(d >= 1 && d <= 6);   // a valid direction chosen
}

// ===========================================================================
// PickDirectionSeqA — no office (officeRank fails) -> returns 0.
// ===========================================================================
TEST(SimNpcTarget, PickDirectionSeqANoOffice) {
    g_sc = TScene{};
    guild::crt::Srand(1);
    NpcTargetHooks hk = MakeTHooks();
    SetNpcTargetHooks(&hk);

    PMock self{ 10, 1, 0, 0, /*catC*/5, 0 };
    g_sc.people = { self };
    g_sc.officeOk = false;     // office lookup fails

    u8 d = NpcTarget_PickDirectionSeqA(&g_sc.people[0], 0);
    CHECK_EQ((int)d, 0);
}

// ===========================================================================
// PickDirectionSeqB — rejects all (pairwise average <= 33) -> returns 0.
// ===========================================================================
TEST(SimNpcTarget, PickDirectionSeqBRejects) {
    g_sc = TScene{};
    guild::crt::Srand(1);
    NpcTargetHooks hk = MakeTHooks();
    SetNpcTargetHooks(&hk);

    PMock self{ 10, 1, 0, 0, 5, 0 };
    // candidates all kind 10 -> pairwise average 10 <= 33 -> reject every dir.
    PMock c0{ 10, 200, 0,0,0,0 };
    PMock c1{ 10, 201, 0,0,0,0 };
    PMock c2{ 10, 202, 0,0,0,0 };
    g_sc.people = { self, c0, c1, c2 };
    g_sc.catIds = { 200, 201, 202 };
    g_sc.officeRankVal = 4;

    u8 d = NpcTarget_PickDirectionSeqB(&g_sc.people[0], 0);
    CHECK_EQ((int)d, 0);
}

// ===========================================================================
// EvalCombatOrMoveAction — sub 30 attacks the nearest enemy (verb 7, mode 1, 56).
// ===========================================================================
TEST(SimNpcTarget, EvalCombatAttack) {
    g_sc = TScene{};
    NpcTargetHooks hk = MakeTHooks();
    SetNpcTargetHooks(&hk);

    PMock self{ 10, 1, 0, 0, /*catC*/5, /*sub*/30 };
    PMock enemy{ 20, 999, 0,0,0,0 };
    g_sc.people = { self, enemy };
    g_sc.succ = { &g_sc.people[1] };

    NpcActionDesc out{};
    int r = NpcTarget_EvalCombatOrMoveAction(&g_sc.people[0], false, false, 1, &out);
    CHECK_EQ(r, 56);
    CHECK_EQ((int)out.verb, 7);
    CHECK_EQ(out.mode, 1);
    CHECK_EQ(out.target, 999);   // enemy id
}

// ===========================================================================
// EvalCombatOrMoveAction — gated off when busy / wrong rank -> 0.
// ===========================================================================
TEST(SimNpcTarget, EvalCombatGated) {
    g_sc = TScene{};
    NpcTargetHooks hk = MakeTHooks();
    SetNpcTargetHooks(&hk);
    PMock self{ 10, 1, 0, 0, 5, 30 };
    g_sc.people = { self };

    NpcActionDesc out{};
    CHECK_EQ(NpcTarget_EvalCombatOrMoveAction(&g_sc.people[0], true, false, 1, &out), 0);
    CHECK_EQ(NpcTarget_EvalCombatOrMoveAction(&g_sc.people[0], false, true, 1, &out), 0);
    CHECK_EQ(NpcTarget_EvalCombatOrMoveAction(&g_sc.people[0], false, false, 2, &out), 0);
}
