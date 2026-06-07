// Integration test: drive event5's DiscoveryRaidRun (gilde.exe 0x4f0708) against
// REAL reconstructed siblings — NOT mocks. The raid-discovery payout body makes a
// genuine cross-module call the binary makes directly (no hook indirection):
//   * VIBE_Math_RandomModulo(0xC8) and RandomModulo(0x64) — guild::util::RandomModulo,
//     which pulls the real CRT LCG guild::crt::RandNext/Srand (crt/rand.cpp) — draw
//     the fine amount and the per-good loot gate.
// We also forward the moneyMultiplyByRate hook into the REAL crt::RandNext-seeded
// rate path is NOT applicable (it's a money helper), so the genuine sibling wired
// here is RandomModulo over the real LCG: we seed the LCG exactly as a live session
// would, run phase 6, and assert the payout that reaches the emitted QueueRequest16
// command equals (RandomModulo(200)+550) replayed against the same seed. The
// GameTimeCompare/Advance siblings are exercised live by the phase-2 begin path.
#include "test.h"

#include "world/event5.h"
#include "sim/he.h"
#include "sim/npcaction.h"     // NpcClock / SetNpcClock (shared clock)
#include "sim/gametime.h"      // the REAL GameTimeAdvance sibling
#include "util/math_random.h"  // the REAL RandomModulo (over the CRT LCG)
#include "crt/rand.h"          // the REAL CRT LCG

#include <cstring>

using namespace guild;
using guild::world::Event5Hooks;
using guild::world::SetEvent5Hooks;
using guild::world::GetEvent5Hooks;
using guild::world::DiscoveryRaidRun;
using guild::sim::HeRecord;
using guild::sim::HeBytes;
using guild::sim::GameTime;
using guild::sim::SetNpcClock;

namespace {
i32& RD(HeRecord* h, int off) { return *reinterpret_cast<i32*>(HeBytes(h) + off); }
GameTime& RT(HeRecord* h, int off) { return *reinterpret_cast<GameTime*>(HeBytes(h) + off); }
HeRecord MakeHe() { HeRecord h; std::memset(&h, 0, sizeof(h)); return h; }

u8 g_leader[256];

struct Cap {
    bool emitted16 = false;
    i32 payout16 = 0;
    int adjustCalls = 0;
    i32 firstAdjustDelta = 0;
} g_cap;
}  // namespace

// Phase 6: the leader still owns no fine handler, so the payout branch runs. The
// fine amount = moneyMultiplyByRate(RandomModulo(200)+550). With an identity rate
// hook the payout equals the REAL-LCG draw; the per-member stock penalty is
// -(100 + RandomModulo(100)). We assert both against the real LCG golden.
TEST(Event5Itest, RaidPayoutFromRealLcgSibling) {
    g_cap = Cap{};
    std::memset(g_leader, 0, sizeof(g_leader));
    *reinterpret_cast<i32*>(g_leader + 1) = 0xABCD;   // leader[1] entity id

    SetEvent5Hooks(nullptr);
    Event5Hooks h = GetEvent5Hooks();
    // Owner handler resolves but does NOT own this record (RecI32+212 != +4),
    // so the payout branch (not the +6 idle) is taken.
    static HeRecord s_owner;
    std::memset(&s_owner, 0, sizeof(s_owner));
    *reinterpret_cast<i32*>(HeBytes(&s_owner) + 212) = 0x1111;   // owner.field212
    h.findFirstHandler3 = [](i32, i32, i32) -> HeRecord* { return &s_owner; };
    h.personQueryBegin = [](i32, i32, i32, i32) -> void* { return g_leader; };
    h.personFindRecordById = [](i32) -> void* { return g_leader; };
    h.buildingFindStorableObject = [](void*) -> void* { return nullptr; };
    h.moneyMultiplyByRate = [](i32 amount, i32) -> i32 { return amount; };  // identity rate
    h.renderFormattedMessage = [](char* b, i32, i32, i32, i32) { if (b) b[0] = 0; };
    h.sendEntityMessage = [](i32, i32, i32, const char*, i32, const char*) {};
    h.queueRequest16 = [](i32, i32, i32 payout, i32) {
        g_cap.emitted16 = true; g_cap.payout16 = payout; };
    h.buildingAdjustStockAndNotify = [](i32, i32 delta, i32) {
        if (g_cap.adjustCalls == 0) g_cap.firstAdjustDelta = delta;
        g_cap.adjustCalls++; };
    h.queueRequestSingle49 = [](i32) {};
    h.queueRequestNamedObject53 = [](i32, i32, i32, i32, i32, const char*) {};
    h.freeHandlerEntry = [](HeRecord*) -> i32 { return 0; };
    SetEvent5Hooks(&h);

    // Seed the REAL CRT LCG exactly as a fresh session would.
    crt::Srand(12345);
    // Replay the same draws independently with the REAL sibling to form the golden.
    crt::Srand(12345);
    int goldFine = static_cast<u16>(util::RandomModulo(0xC8)) + 550;  // first draw
    int goldPenalty = -100 - static_cast<u16>(util::RandomModulo(0x64));  // second draw
    // Re-seed so the machine sees the same stream from the top.
    crt::Srand(12345);

    GameTime c{}; c.day = 9; c.hour = 3; SetNpcClock(c);

    HeRecord he = MakeHe();
    RD(&he, 112) = 4;           // phase 6 (state+2 == 6)
    RD(&he, 4)   = 0x2222;      // this record's id (!= owner.field212 -> payout path)
    RD(&he, 184) = 0x3333;      // ownerId
    RD(&he, 140) = 300;         // one band member
    for (int s = 1; s < 8; ++s) RD(&he, 140 + 4 * s) = -1;

    DiscoveryRaidRun(&he);

    CHECK(g_cap.emitted16);
    if (g_cap.emitted16) CHECK_EQ(g_cap.payout16, goldFine);   // 68 + 550 = 618
    CHECK_EQ(g_cap.adjustCalls, 1);
    if (g_cap.adjustCalls) CHECK_EQ(g_cap.firstAdjustDelta, goldPenalty);  // -100 - 9988%100
}

// The phase-2 begin path exercises the REAL GameTimeAdvance sibling: it stamps the
// clock into +82 and advances +4 minutes. We assert the resulting appointment time
// is exactly what the real sibling computes from the seeded clock.
TEST(Event5Itest, BeginUsesRealGameTimeAdvance) {
    SetEvent5Hooks(nullptr);
    Event5Hooks h = GetEvent5Hooks();
    h.personQueryBegin = [](i32, i32, i32, i32) -> void* { return g_leader; };
    h.personFindRecordById = [](i32) -> void* { return g_leader; };
    h.changePlayerAction = [](void*, void*, void*, u16) {};
    h.queueRequestSingle49 = [](i32) {};
    h.queueRequestNamedObject53 = [](i32, i32, i32, i32, i32, const char*) {};
    SetEvent5Hooks(&h);

    GameTime c{}; c.day = 9; c.hour = 23; c.minute = 58; c.second = 0; SetNpcClock(c);

    HeRecord he = MakeHe();
    RD(&he, 112) = 0;           // phase 2 (begin)
    RD(&he, 140) = 400;
    for (int s = 1; s < 8; ++s) RD(&he, 140 + 4 * s) = -1;

    DiscoveryRaidRun(&he);
    // 23:58 + 4 min = day 10, 00:02 (the real Advance carries minutes->hours->days).
    CHECK_EQ(RT(&he, 82).day, 10);
    CHECK_EQ(static_cast<int>(RT(&he, 82).hour), 0);
    CHECK_EQ(RT(&he, 82).minute, 2);
    CHECK_EQ(RD(&he, 112), 1);
}
