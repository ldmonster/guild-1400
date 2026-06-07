// Unit tests for NpcAction6 — the AI debug-command leaf family. Golden vectors
// for the RNG roll sequences are computed against the shared CRT LCG (the same
// generator as crt/rand.cpp); see the python oracle in the agent report.
#include "sim/npcaction6.h"
#include "sim/npcaction.h"     // SetNpcClock / NpcClock
#include "crt/rand.h"          // Srand
#include "tests/framework/test.h"

#include <cstring>
#include <vector>

using namespace guild;
using namespace guild::sim;

namespace {

// ---- recording mock state ----
struct Rec {
    int    requestBuildOp93_calls = 0;
    i32    bop_id = 0; int bop_kind = -1; i32 bop_id2 = 0; u8 bop_amount = 0;
    int    queueRequest16_calls = 0;
    i32    q16_a = 0, q16_b = 0, q16_amount = 0; u8 q16_cur = 0;
    int    sendMessage_calls = 0;
    i32    msg_idA = 0, msg_idB = 0, msg_text = 0;
    int    entity29_calls = 0;
    int    notifyWander_calls = 0;
};
Rec g_rec;

// A mock Person record (raw bytes), large enough for offsets 0..130.
u8 g_person[256];
i32 g_personId = 555;           // record[+0] -> this id resolves to g_person
i32 g_wealth = 10000;
int g_gateResult = 1;           // AiNeeds / need-picker gates
int g_season = 2;

u8* mockFind(i32 id) { return (id == g_personId) ? g_person : nullptr; }
i32 mockWealth(u16, u8*) { return g_wealth; }
i32 mockRate(int amt, u8) { return amt * 1000; }   // arbitrary deterministic rate
int mockGate(u8*) { return g_gateResult; }
void mockBop93(i32 id, int kind, i32 id2, u8 amt) {
    g_rec.requestBuildOp93_calls++; g_rec.bop_id = id; g_rec.bop_kind = kind;
    g_rec.bop_id2 = id2; g_rec.bop_amount = amt;
}
void mockQ16(i32 a, i32 b, i32 amount, u8 cur) {
    g_rec.queueRequest16_calls++; g_rec.q16_a = a; g_rec.q16_b = b;
    g_rec.q16_amount = amount; g_rec.q16_cur = cur;
}
void mockMsg(i32 a, i32 b, i32 t) {
    g_rec.sendMessage_calls++; g_rec.msg_idA = a; g_rec.msg_idB = b; g_rec.msg_text = t;
}
i32  mockEntity29(int, HeRecord*) { g_rec.entity29_calls++; return 7; }
int  mockSeason() { return g_season; }
void mockNotify(HeRecord*, void*) { g_rec.notifyWander_calls++; }

NpcAction6Hooks MakeHooks() {
    NpcAction6Hooks h{};
    h.findRecordById = mockFind;
    h.computeTotalWealth = mockWealth;
    h.moneyMultiplyByRate = mockRate;
    h.pickNeedAndClearGroup = mockGate;
    h.pickNeedAndClearGroupB = mockGate;
    h.pickFlagFromFourA = mockGate;
    h.pickFlagFromFourB = mockGate;
    h.requestBuildOp93 = mockBop93;
    h.queueRequest16 = mockQ16;
    h.queueRequestEntity29 = mockEntity29;
    h.sendEntityMessage = mockMsg;
    h.currentSeason = mockSeason;
    h.notifyWanderPairEvent = mockNotify;
    h.currencyByte = 4;
    return h;
}

// Build a command-descriptor record: record[+0] = personId, record[+4] word = textBase.
void MakeCmdRecord(HeRecord* h, i32 personId, u16 textBase) {
    std::memset(h, 0, sizeof(HeRecord));
    *reinterpret_cast<i32*>(reinterpret_cast<u8*>(h) + 0) = personId;
    *reinterpret_cast<u16*>(reinterpret_cast<u8*>(h) + 4) = textBase;
}

void MakePerson(i32 entityId) {
    std::memset(g_person, 0, sizeof(g_person));
    *reinterpret_cast<u16*>(g_person + 0) = 77;       // person index
    *reinterpret_cast<i32*>(g_person + 4) = entityId; // entity id
}

void ResetAll(const NpcAction6Hooks& h, u32 seed) {
    g_rec = Rec{};
    SetNpcAction6Hooks(&h);
    crt::Srand(seed);
}

} // namespace

// ===========================================================================
// IncreaseLoyaltyCmd — golden RNG (seed 1): kind=3, roll1+8=20, reroll=23.
// With rel=0 -> room=252 >= 20 -> delta = reroll = 23.
// ===========================================================================
TEST(NpcAction6, IncreaseLoyalty_RerollPath) {
    NpcAction6Hooks hk = MakeHooks();
    ResetAll(hk, 1);
    HeRecord rec; MakeCmdRecord(&rec, g_personId, 100);
    MakePerson(9001);
    g_person[128 + 3] = 0;   // rel byte for kind=3 == 0 -> room=252

    int r = NpcAction6_IncreaseLoyaltyCmd(&rec);
    CHECK_EQ(r, kNpc6Ok);
    CHECK_EQ(g_rec.requestBuildOp93_calls, 1);
    CHECK_EQ(g_rec.bop_kind, 3);
    CHECK_EQ((int)g_rec.bop_amount, 23);   // reroll value
    CHECK_EQ(g_rec.bop_id, 9001);
    CHECK_EQ(g_rec.msg_text, 100 + 3881);
    CHECK_EQ(g_rec.sendMessage_calls, 1);
}

// Fall-through path: room < roll1+8 -> delta = room (only one extra draw).
// seed 1: kind=3, roll1+8=20. Set rel so room < 20, e.g. rel=240 -> room=12.
TEST(NpcAction6, IncreaseLoyalty_RoomPath) {
    NpcAction6Hooks hk = MakeHooks();
    ResetAll(hk, 1);
    HeRecord rec; MakeCmdRecord(&rec, g_personId, 0);
    MakePerson(9002);
    g_person[128 + 3] = 240;   // room = 252-240 = 12 < 20

    int r = NpcAction6_IncreaseLoyaltyCmd(&rec);
    CHECK_EQ(r, kNpc6Ok);
    CHECK_EQ((int)g_rec.bop_amount, 12);   // delta = room
}

// room <= 0 -> retry (no command).
TEST(NpcAction6, IncreaseLoyalty_Capped) {
    NpcAction6Hooks hk = MakeHooks();
    ResetAll(hk, 1);
    HeRecord rec; MakeCmdRecord(&rec, g_personId, 0);
    MakePerson(9003);
    g_person[128 + 3] = 252;   // room = 0

    CHECK_EQ(NpcAction6_IncreaseLoyaltyCmd(&rec), kNpc6Retry);
    CHECK_EQ(g_rec.requestBuildOp93_calls, 0);
}

// No actor -> 1.
TEST(NpcAction6, IncreaseLoyalty_NoActor) {
    NpcAction6Hooks hk = MakeHooks();
    ResetAll(hk, 1);
    HeRecord rec; MakeCmdRecord(&rec, 123456 /*unresolvable*/, 0);
    CHECK_EQ(NpcAction6_IncreaseLoyaltyCmd(&rec), kNpc6NoActor);
}

// ===========================================================================
// DecreaseLoyaltyCmd — golden RNG (seed 12345): kind=3, roll1+6=10, reroll=7.
// rel high (e.g. 200) -> rel >= 10 -> delta = reroll = 7, emitted negated.
// ===========================================================================
TEST(NpcAction6, DecreaseLoyalty_RerollNegated) {
    NpcAction6Hooks hk = MakeHooks();
    ResetAll(hk, 12345);
    HeRecord rec; MakeCmdRecord(&rec, g_personId, 50);
    MakePerson(8001);
    g_person[128 + 3] = 200;   // rel >= roll

    int r = NpcAction6_DecreaseLoyaltyCmd(&rec);
    CHECK_EQ(r, kNpc6Ok);
    CHECK_EQ(g_rec.bop_kind, 3);
    CHECK_EQ((int)g_rec.bop_amount, (int)(u8)(-(u8)7));  // negated byte
    CHECK_EQ(g_rec.msg_text, 50 + 3881);
}

// rel == 0 -> retry.
TEST(NpcAction6, DecreaseLoyalty_ZeroRel) {
    NpcAction6Hooks hk = MakeHooks();
    ResetAll(hk, 12345);
    HeRecord rec; MakeCmdRecord(&rec, g_personId, 0);
    MakePerson(8002);
    g_person[128 + 3] = 0;
    CHECK_EQ(NpcAction6_DecreaseLoyaltyCmd(&rec), kNpc6Retry);
    CHECK_EQ(g_rec.requestBuildOp93_calls, 0);
}

// ===========================================================================
// RaiseSalaryCmd — golden RNG (seed 7): gateRoll=108, mult=3.
//   gate passes when recordById[+130] >= 108. wealth=10000 -> amount=300.
// ===========================================================================
TEST(NpcAction6, RaiseSalary_Golden) {
    NpcAction6Hooks hk = MakeHooks();
    ResetAll(hk, 7);
    g_wealth = 10000;
    HeRecord rec; MakeCmdRecord(&rec, g_personId, 200);
    MakePerson(7001);
    g_person[130] = 150;   // >= gateRoll 108 -> pass

    int r = NpcAction6_RaiseSalaryCmd(&rec);
    CHECK_EQ(r, kNpc6Ok);
    CHECK_EQ(g_rec.queueRequest16_calls, 1);
    CHECK_EQ(g_rec.q16_a, g_personId);
    CHECK_EQ(g_rec.q16_b, -1);
    CHECK_EQ(g_rec.q16_amount, 300);     // int(10000*3*0.01)
    CHECK_EQ((int)g_rec.q16_cur, 4);
    CHECK_EQ(g_rec.msg_text, 200 + 3881);
}

// gate fails (recordById[+130] < gateRoll) -> retry.
TEST(NpcAction6, RaiseSalary_GateFail) {
    NpcAction6Hooks hk = MakeHooks();
    ResetAll(hk, 7);
    HeRecord rec; MakeCmdRecord(&rec, g_personId, 0);
    MakePerson(7002);
    g_person[130] = 10;    // < 108
    CHECK_EQ(NpcAction6_RaiseSalaryCmd(&rec), kNpc6Retry);
    CHECK_EQ(g_rec.queueRequest16_calls, 0);
}

// Fractional truncation: wealth=333, mult=3 -> int(333*3*0.01)=int(9.99)=9.
TEST(NpcAction6, RaiseSalary_TruncTowardZero) {
    NpcAction6Hooks hk = MakeHooks();
    ResetAll(hk, 7);
    g_wealth = 333;
    HeRecord rec; MakeCmdRecord(&rec, g_personId, 0);
    MakePerson(7003);
    g_person[130] = 255;
    CHECK_EQ(NpcAction6_RaiseSalaryCmd(&rec), kNpc6Ok);
    CHECK_EQ(g_rec.q16_amount, 9);
    g_wealth = 10000;   // restore
}

// ===========================================================================
// LowerSalaryCmd — golden RNG (seed 7): mult=1, wealth=10000 -> amount=100.
//   ids swapped relative to RaiseSalary: q16(-1, personId, ...).
// ===========================================================================
TEST(NpcAction6, LowerSalary_Golden) {
    NpcAction6Hooks hk = MakeHooks();
    ResetAll(hk, 7);
    g_wealth = 10000;
    HeRecord rec; MakeCmdRecord(&rec, g_personId, 300);
    MakePerson(6001);
    *reinterpret_cast<i32*>(g_person + 44) = 1;   // employed gate

    int r = NpcAction6_LowerSalaryCmd(&rec);
    CHECK_EQ(r, kNpc6Ok);
    CHECK_EQ(g_rec.q16_a, -1);
    CHECK_EQ(g_rec.q16_b, g_personId);
    CHECK_EQ(g_rec.q16_amount, 100);     // int(10000*1*0.01)
}

TEST(NpcAction6, LowerSalary_NotEmployed) {
    NpcAction6Hooks hk = MakeHooks();
    ResetAll(hk, 7);
    HeRecord rec; MakeCmdRecord(&rec, g_personId, 0);
    MakePerson(6002);
    *reinterpret_cast<i32*>(g_person + 44) = 0;   // not employed
    CHECK_EQ(NpcAction6_LowerSalaryCmd(&rec), kNpc6Retry);
    CHECK_EQ(g_rec.queueRequest16_calls, 0);
}

// ===========================================================================
// MoveToObjectCmd — golden RNG (seed 99): roll=2 -> rate arg = 3 -> amount=3000.
// ===========================================================================
TEST(NpcAction6, MoveToObject_Golden) {
    NpcAction6Hooks hk = MakeHooks();
    ResetAll(hk, 99);
    g_gateResult = 1;
    HeRecord rec; MakeCmdRecord(&rec, g_personId, 400);
    MakePerson(5001);

    int r = NpcAction6_MoveToObjectCmd(&rec);
    CHECK_EQ(r, kNpc6Ok);
    CHECK_EQ(g_rec.q16_a, -1);
    CHECK_EQ(g_rec.q16_b, g_personId);
    CHECK_EQ(g_rec.q16_amount, 3000);    // mockRate(3) = 3*1000
    CHECK_EQ(g_rec.msg_text, 400 + 3881);
}

TEST(NpcAction6, MoveToObject_GateFail) {
    NpcAction6Hooks hk = MakeHooks();
    ResetAll(hk, 99);
    g_gateResult = 0;
    HeRecord rec; MakeCmdRecord(&rec, g_personId, 0);
    MakePerson(5002);
    CHECK_EQ(NpcAction6_MoveToObjectCmd(&rec), kNpc6Retry);
    CHECK_EQ(g_rec.queueRequest16_calls, 0);
    g_gateResult = 1;
}

// ===========================================================================
// Dialog/Message leaf cmds — gate routing + textId.
// ===========================================================================
TEST(NpcAction6, DialogCmds_GateAndText) {
    NpcAction6Hooks hk = MakeHooks();

    ResetAll(hk, 3);
    g_gateResult = 1;
    HeRecord rec; MakeCmdRecord(&rec, g_personId, 11);
    MakePerson(4001);
    CHECK_EQ(NpcAction6_DialogCmdA(&rec), kNpc6Ok);
    CHECK_EQ(g_rec.msg_text, 11 + 3881);

    ResetAll(hk, 3);
    MakeCmdRecord(&rec, g_personId, 22);
    MakePerson(4002);
    CHECK_EQ(NpcAction6_DialogCmdB(&rec), kNpc6Ok);
    CHECK_EQ(g_rec.msg_text, 22 + 3881);

    ResetAll(hk, 3);
    g_gateResult = 0;
    MakeCmdRecord(&rec, g_personId, 33);
    MakePerson(4003);
    CHECK_EQ(NpcAction6_DialogCmdC(&rec), kNpc6Retry);
    CHECK_EQ(g_rec.sendMessage_calls, 0);
    g_gateResult = 1;
}

TEST(NpcAction6, MessageCmds_NoGate) {
    NpcAction6Hooks hk = MakeHooks();
    ResetAll(hk, 1);
    HeRecord rec; MakeCmdRecord(&rec, g_personId, 7);
    MakePerson(3001);
    CHECK_EQ(NpcAction6_MessageCmdA(&rec), kNpc6Ok);
    CHECK_EQ(NpcAction6_MessageCmdB(&rec), kNpc6Ok);
    CHECK_EQ(NpcAction6_MessageCmdC(&rec), kNpc6Ok);
    CHECK_EQ(g_rec.sendMessage_calls, 3);
    CHECK_EQ(g_rec.msg_text, 7 + 3881);

    MakeCmdRecord(&rec, 999999 /*unresolvable*/, 0);
    CHECK_EQ(NpcAction6_MessageCmdA(&rec), kNpc6NoActor);
}

// ===========================================================================
// ShowPositionCmd — employed gate + season substitution.
// ===========================================================================
TEST(NpcAction6, ShowPosition_GateAndSeason) {
    NpcAction6Hooks hk = MakeHooks();
    ResetAll(hk, 1);
    g_season = 2;
    HeRecord rec; MakeCmdRecord(&rec, g_personId, 60);
    MakePerson(2001);
    *reinterpret_cast<i32*>(g_person + 44) = 1;   // employed

    CHECK_EQ(NpcAction6_ShowPositionCmd(&rec), kNpc6Ok);
    CHECK_EQ(g_rec.msg_text, 60 + 3881);

    *reinterpret_cast<i32*>(g_person + 44) = 0;   // not employed
    ResetAll(hk, 1);
    MakeCmdRecord(&rec, g_personId, 0);
    MakePerson(2002);
    *reinterpret_cast<i32*>(g_person + 44) = 0;
    CHECK_EQ(NpcAction6_ShowPositionCmd(&rec), kNpc6Retry);
}

// ===========================================================================
// NotifyWanderPair — handler scan, peer match by record[+4], clock stamp + cmd29.
// ===========================================================================
namespace {
std::vector<HeRecord>* g_scanPool = nullptr;
size_t g_scanCursor = 0;
HeRecord* scanBegin() { g_scanCursor = 0; return g_scanPool && !g_scanPool->empty() ? &(*g_scanPool)[0] : nullptr; }
HeRecord* scanNext() {
    if (!g_scanPool) return nullptr;
    g_scanCursor++;
    return g_scanCursor < g_scanPool->size() ? &(*g_scanPool)[g_scanCursor] : nullptr;
}
void* slotByIdx(u16) { return nullptr; }
} // namespace

TEST(NpcAction6, NotifyWanderPair_MatchAndStamp) {
    NpcAction6Hooks hk = MakeHooks();
    hk.wanderScanBegin = scanBegin;
    hk.wanderScanNext = scanNext;
    hk.personSlotByIndex = slotByIdx;
    ResetAll(hk, 1);

    GameTime clk{}; clk.day = 42; clk.hour = 9; clk.minute = 30; clk.second = 0;
    SetNpcClock(clk);

    HeRecord me; std::memset(&me, 0, sizeof(me));
    He_Id(&me) = 0x1234;   // record[+4]

    std::vector<HeRecord> pool(3);
    std::memset(pool.data(), 0, pool.size() * sizeof(HeRecord));
    He_PacketId(&pool[0], 0) = 0x9999;   // peer[+188] != my id -> skip
    He_PacketId(&pool[1], 0) = 0x1234;   // match
    He_CityIndex(&pool[1]) = 5;
    He_PacketId(&pool[2], 0) = 0x1234;   // match
    He_CityIndex(&pool[2]) = 6;
    g_scanPool = &pool;

    int r = NpcAction6_NotifyWanderPair(&me);
    CHECK_EQ(r, 1);
    CHECK_EQ(g_rec.notifyWander_calls, 2);
    CHECK_EQ(g_rec.entity29_calls, 2);
    // The matched peers' +82 appointment was stamped with the clock.
    CHECK_EQ(He_ApptTime(&pool[1]).day, 42);
    CHECK_EQ(He_ApptTime(&pool[2]).hour, 9);
    CHECK_EQ(He_ApptTime(&pool[0]).day, 0);   // unmatched untouched
    g_scanPool = nullptr;
}
