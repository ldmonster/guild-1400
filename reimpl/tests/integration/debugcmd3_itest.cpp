#include "test.h"

// Integration: drive debugcmd3's wealth-scaled "spawn entity" handlers against a
// REAL reconstructed sibling — the CRT LCG (crt/rand.cpp, VIBE_Util_RandNext
// @0x5cb8bc / Srand). No mock RNG: the wave-3 handlers roll their amount through
// DebugCmdRandomModulo, which is `crt::RandNext() % n` (debugcmd.cpp), and feed
// that roll into the real DebugCmdScaledGold wealth formula. Here we seed the
// genuine LCG, let DebugCmdSpawnEntityNearNearest / SpawnEntityFromHandlerList run
// their real RNG -> real scaled-gold pipeline, and assert the gold delivered to
// QueueRequest16 is bit-exactly what the real LCG sequence + the real gold formula
// produce, end to end.
//
// The person lookup / nearest-entity / query-list leaves are local recorders that
// just steer the handler down the gold path; the load-bearing arithmetic (the RNG
// draw and the wealth scaling) is the reconstructed one.
#include "sim/debugcmd3.h"
#include "sim/debugcmd.h"      // DebugCmdHooks / DebugCmdRandomModulo / DebugCmdScaledGold
#include "crt/rand.h"          // REAL crt::Srand / crt::RandNext (0x5cb8bc)

#include <cstring>

using namespace guild;
using namespace guild::sim;

namespace {

constexpr i32 kPersonId = 700;
constexpr i32 kWealth   = 123456;
constexpr u8  kMarket   = 4;

// --- recorded emit -------------------------------------------------------------
i32 g_q16_a1 = 0, g_q16_a2 = 0, g_q16_amount = -1; u8 g_q16_market = 0;
int g_q16_calls = 0;
void RecQueue16(i32 a1, i32 a2, i32 amount, u8 market) {
    g_q16_a1 = a1; g_q16_a2 = a2; g_q16_amount = amount; g_q16_market = market;
    ++g_q16_calls;
}
int g_msgCalls = 0;
void RecSendMsg(i32, i32, i32) { ++g_msgCalls; }

bool RecFindPerson(i32 id, DebugCmdPerson* out) {
    *out = DebugCmdPerson{};
    out->id = id;
    out->wealth = kWealth;
    return true;
}

const DebugCmdHooks kBaseHooks = {
    /*findPerson*/          RecFindPerson,
    /*sendEntityMessage*/   RecSendMsg,
    /*queueRequest16*/      RecQueue16,
    /*requestBuildOp90*/    nullptr,
    /*queueRequestCoord27*/ nullptr,
    /*moneyMultiplyByRate*/ nullptr,
    /*market*/              kMarket,
};

// --- DebugCmd3Hooks: steer NearNearest down the gold path ----------------------
bool RecNearest(const DebugCmdPerson*, i32* outEntityId) {
    if (outEntityId) *outEntityId = 4242;
    return true;
}
DebugCmd3Entity RecResolveValid(i32) {
    DebugCmd3Entity e{};
    e.valid = true;            // resolve succeeds -> proceed to the gold roll
    return e;
}
// queryEntityList for FromHandlerList: return a fixed-count list.
int RecQueryList(const DebugCmdPerson*, i32* out, int cap) {
    int n = (cap < 3) ? cap : 3;
    for (int i = 0; i < n; ++i) out[i] = 1000 + i;
    return n;
}

DebugCmd3Hooks MakeHooks3() {
    DebugCmd3Hooks h{};
    h.heFindFirst       = nullptr;
    h.heFindNext        = nullptr;
    h.resolveEntity     = RecResolveValid;
    h.findNearestEntity = RecNearest;
    h.queryEntityList   = RecQueryList;
    return h;
}

// Replay the real LCG to predict the gold the handler will emit. `rolls` lists the
// (modulus) draws the handler performs, in order; `goldRollIndex` selects which
// draw feeds the scaled-gold formula; `base`/`scale` are the handler's constants.
i32 PredictGold(u32 seed, const int* moduli, int nRolls, int goldRollIndex,
                double base, double scale) {
    crt::Srand(seed);
    int goldRoll = 0;
    for (int i = 0; i < nRolls; ++i) {
        int r = (moduli[i] != 0) ? (crt::RandNext() % moduli[i]) : 0;
        if (i == goldRollIndex) goldRoll = r;
    }
    return DebugCmdScaledGold(kWealth, goldRoll, base, scale);  // REAL formula
}

} // namespace

// SpawnEntityNearNearest: the only RNG draw before the gold is RandomModulo(3),
// base 1.0, scale 0.01. Seed the REAL LCG, predict the gold from the real
// sequence, re-seed, run; the emitted gold must match bit-for-bit.
TEST(DebugCmd3Itest, SpawnEntityNearNearestGoldFromRealRng) {
    const u32 seed = 0xC0FFEEu;
    const int moduli[1] = {3};
    i32 expectedGold = PredictGold(seed, moduli, 1, /*goldRollIndex*/ 0, 1.0, 0.01);

    g_q16_calls = 0; g_q16_amount = -1; g_msgCalls = 0;
    SetDebugCmdHooks(&kBaseHooks);
    const DebugCmd3Hooks h3 = MakeHooks3();
    SetDebugCmd3Hooks(&h3);

    crt::Srand(seed);                       // re-seed the REAL LCG identically
    i32 r = DebugCmdSpawnEntityNearNearest(kPersonId);

    CHECK_EQ(r, 0);                         // kDbgHandled
    CHECK_EQ(g_q16_calls, 1);
    CHECK_EQ(g_q16_amount, expectedGold);   // real RNG + real ScaledGold, end to end
    CHECK_EQ(g_q16_a1, -1);                 // QR16(-1, personId, gold, market)
    CHECK_EQ(g_q16_a2, kPersonId);
    CHECK_EQ(static_cast<int>(g_q16_market), static_cast<int>(kMarket));
    CHECK_EQ(g_msgCalls, 1);                // message tail fired

    SetDebugCmd3Hooks(nullptr);
    SetDebugCmdHooks(nullptr);
}

// SpawnEntityFromHandlerList: two RNG draws — RandomModulo(count) pick then
// RandomModulo(5) for the gold (base 2.0, scale 0.01). The gold uses the SECOND
// draw; predicting from the real LCG and matching pins both the draw order and
// the wealth formula as the real sibling's.
TEST(DebugCmd3Itest, SpawnEntityFromHandlerListGoldFromRealRng) {
    const u32 seed = 0x1234ABCDu;
    // RecQueryList returns 3 ids -> pick draw is RandomModulo(3), gold is %5.
    const int moduli[2] = {3, 5};
    i32 expectedGold = PredictGold(seed, moduli, 2, /*goldRollIndex*/ 1, 2.0, 0.01);

    g_q16_calls = 0; g_q16_amount = -1; g_msgCalls = 0;
    SetDebugCmdHooks(&kBaseHooks);
    const DebugCmd3Hooks h3 = MakeHooks3();
    SetDebugCmd3Hooks(&h3);

    crt::Srand(seed);
    i32 r = DebugCmdSpawnEntityFromHandlerList(kPersonId);

    CHECK_EQ(r, 0);
    CHECK_EQ(g_q16_calls, 1);
    CHECK_EQ(g_q16_amount, expectedGold);   // 2nd real draw fed the real formula
    CHECK_EQ(g_q16_a1, kPersonId);          // QR16(personId, -1, gold, market)
    CHECK_EQ(g_q16_a2, -1);
    CHECK_EQ(g_msgCalls, 1);

    SetDebugCmd3Hooks(nullptr);
    SetDebugCmdHooks(nullptr);
}

// Inert-default path (no hooks installed): debugcmd3.cpp's own inert DebugCmd3Hooks
// make the iterators/searches yield nothing, so a NearNearest with no near entity
// reports "nothing eligible" (1024) after a successful person lookup — and never
// touches the real RNG. Exercises the module's real inert default end to end.
TEST(DebugCmd3Itest, InertDefaultNearestReportsIneligible) {
    g_q16_calls = 0;
    SetDebugCmdHooks(&kBaseHooks);          // person resolves...
    SetDebugCmd3Hooks(nullptr);             // ...but the wave-3 leaves are inert

    i32 r = DebugCmdSpawnEntityNearNearest(kPersonId);

    CHECK_EQ(r, 1024);                      // kDbgIneligible (no near entity)
    CHECK_EQ(g_q16_calls, 0);               // no command emitted

    SetDebugCmdHooks(nullptr);
}
