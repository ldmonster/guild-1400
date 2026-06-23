// End-to-end flow test for charaction_steps3 — drives a sequence of CharAction
// step leaves across a single He handler record (the way the dispatcher would,
// tick by tick) and verifies the cumulative side effects through recording hooks.
// Suite prefix: CharActionYE2E.
#include "test.h"

#include "sim/charaction_steps3.h"
#include "sim/he.h"
#include "sim/npcaction.h"

#include <cstring>
#include <vector>

using namespace guild;
using namespace guild::sim;

namespace {

struct RecBuf {
    HeRecord rec{};
    RecBuf() { std::memset(&rec, 0, sizeof(rec)); }
    HeRecord* get() { return &rec; }
};

GameTime Clock() {
    GameTime t{};
    t.day = 20; t.hour = 12; t.minute = 0; t.second = 0;
    return t;
}

struct Leaf {
    int freeCalls = 0;
    int cmd29Calls = 0;
    std::vector<int> args;
    i32 handle = 555;
};
Leaf g_leaf;
i32 LFree(HeRecord*) { ++g_leaf.freeCalls; return 0; }
i32 LCmd29(int a, HeRecord*) { ++g_leaf.cmd29Calls; g_leaf.args.push_back(a); return g_leaf.handle; }
i32 LStatus(i32) { return 0; }

struct S3 {
    std::vector<std::pair<i32, HeRecord*>> entities;
    std::vector<HeRecord*> queryResults; size_t queryCursor = 0;
    std::vector<HeRecord*> persons2;  // findPersonById id->rec
    std::vector<std::pair<i32, HeRecord*>> persons;
    std::vector<HeRecord*> scan; size_t scanCursor = 0;
    int args25 = 0; int build73 = 0; i32 build73Person = 0; i32 build73Handle = 808;
    int guard61 = 0; i32 guard61Handle = 909;
    bool fast = false; int minutes = 30;
    int notify = 0;
};
S3 g_s3;
HeRecord* SResolve(i32 id) { for (auto& e : g_s3.entities) if (e.first == id) return e.second; return nullptr; }
HeRecord* SQuery(int, int, int) { return g_s3.queryCursor < g_s3.queryResults.size() ? g_s3.queryResults[g_s3.queryCursor++] : nullptr; }
HeRecord* SFindPerson(i32 id) { for (auto& p : g_s3.persons) if (p.first == id) return p.second; return nullptr; }
HeRecord* SFirst(int, const int*, const int*) { g_s3.scanCursor = 0; return g_s3.scan.empty() ? nullptr : g_s3.scan[g_s3.scanCursor++]; }
HeRecord* SNext() { return g_s3.scanCursor < g_s3.scan.size() ? g_s3.scan[g_s3.scanCursor++] : nullptr; }
i32 SGuard(HeRecord*) { ++g_s3.guard61; return g_s3.guard61Handle; }
void SArgs25(i32, int, int, int) { ++g_s3.args25; }
i32 SBuild73(i32 p) { ++g_s3.build73; g_s3.build73Person = p; return g_s3.build73Handle; }
bool SFast() { return g_s3.fast; }
int SMinutes(i32) { return g_s3.minutes; }
void SNotify(i32, int, u16) { ++g_s3.notify; }

void Install() {
    g_leaf = Leaf{};
    g_s3 = S3{};
    static NpcLeafHooks lh; lh = NpcLeafHooks{};
    lh.freeHandlerEntry = LFree; lh.queueRequestEntity29 = LCmd29; lh.packetStatus = LStatus;
    SetNpcLeafHooks(&lh);
    static CharActionStep3Hooks sh; sh = CharActionStep3Hooks{};
    sh.resolveEntityById = SResolve; sh.personQueryBegin = SQuery; sh.findPersonById = SFindPerson;
    sh.findFirstByFilter = SFirst; sh.findNextMatching = SNext;
    sh.queueRequestGuardTarget61 = SGuard; sh.queueRequestArgs25 = SArgs25;
    sh.requestBuildOp73Sabotage = SBuild73; sh.fastTimeEnabled = SFast;
    sh.targetActionMinutes = SMinutes; sh.sendNotifyMessage = SNotify;
    SetCharActionStep3Hooks(&sh);
    SetNpcClock(Clock());
}

} // namespace

// A gather -> arm -> patrol -> resolve flow on one record.
TEST(CharActionYE2E, GatherArmPatrolFlow) {
    Install();
    RecBuf r;

    // 1) GroupGatherInit: two members present.
    for (int i = 0; i < 24; i += 4)
        *reinterpret_cast<i32*>(HeBytes(r.get()) + 140 + i) = -1;
    *reinterpret_cast<i32*>(HeBytes(r.get()) + 140) = 7;
    *reinterpret_cast<i32*>(HeBytes(r.get()) + 144) = 8;
    int gatherHr = GroupGatherInit(r.get());
    CHECK_EQ(*reinterpret_cast<u8*>(HeBytes(r.get()) + 216), 2);
    CHECK_EQ(g_leaf.freeCalls, 0);
    CHECK_EQ(gatherHr, 12);   // clock hour 12, +2 minutes stays hour 12

    // 2) InitTargetState (duration mode).
    g_s3.minutes = 90;   // +90 minutes -> 13:30
    int tsHr = InitTargetState(r.get());
    CHECK_EQ(tsHr, 13);
    CHECK_EQ(He_ApptTime(r.get()).hour, 13);
    CHECK_EQ(He_ApptTime(r.get()).minute, 30);

    // 3) PatrolFindTarget: flag set, query miss -> two cmd29 arms.
    He_Flags(r.get()) = 2;
    Cas3_Counter(r.get()) = 4;
    PatrolFindTarget(r.get());
    CHECK_EQ(g_leaf.cmd29Calls, 2);
    CHECK_EQ(g_leaf.args[0], -1);
    CHECK_EQ(g_leaf.args[1], 0);

    // 4) FindBeggarTarget: another handler exists -> arm cmd29(-1).
    He_Flags(r.get()) = 0;
    RecBuf other;
    g_s3.scan.push_back(r.get());      // self skipped
    g_s3.scan.push_back(other.get());
    i32 begRet = FindBeggarTarget(r.get());
    CHECK_EQ(begRet, 555);
    CHECK_EQ(g_leaf.cmd29Calls, 3);
    CHECK_EQ(g_leaf.args[2], -1);
}

// A sabotage -> notify flow, verifying the build-op and message emits chain.
TEST(CharActionYE2E, SabotageThenNotifyFlow) {
    Install();
    RecBuf r;

    // 1) InitSabotage: slot empty, query resolves a saboteur.
    RecBuf saboteur;
    {  // +1 is unaligned for i32; store via memcpy (byte-identical, no UB).
        i32 _v = 4321;
        std::memcpy(HeBytes(saboteur.get()) + 1, &_v, sizeof(_v));
    }
    Cas3_Slot16(r.get()) = -1;
    g_s3.queryResults.push_back(saboteur.get());
    i32 sabHandle = InitSabotage(r.get());
    CHECK_EQ(Cas3_Slot16(r.get()), 4321);
    CHECK_EQ(g_s3.build73, 1);
    CHECK_EQ(g_s3.build73Person, 4321);
    CHECK_EQ(sabHandle, 808);
    CHECK_EQ(*reinterpret_cast<i32*>(HeBytes(r.get()) + 200), 808);

    // 2) NotifyMessageInit: needs-flag set, both persons resolve, message sent.
    RecBuf p176, p172;
    He_Flags(r.get()) = 2;
    Cas3_TargetId(r.get()) = 500;
    Cas3_Counter(r.get()) = 600;
    *reinterpret_cast<u8*>(HeBytes(p176.get()) + 2) = 7;
    *reinterpret_cast<i32*>(HeBytes(p176.get()) + 4) = 42;
    *reinterpret_cast<u16*>(p172.get()) = 99;
    g_s3.persons.push_back({500, p176.get()});
    g_s3.persons.push_back({600, p172.get()});
    i32 notifyHandle = NotifyMessageInit(r.get());
    CHECK_EQ(g_s3.notify, 1);
    CHECK_EQ(g_leaf.args.back(), 0);   // armed cmd29(0) after message
    CHECK_EQ(notifyHandle, 555);
    CHECK_EQ(Cas3_Packet(r.get()), -1);   // +132 set to -1 at the top
}
