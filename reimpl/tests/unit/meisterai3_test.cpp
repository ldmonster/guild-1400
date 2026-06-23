// Unit tests for src/ai/meisterai3.cpp — AiMethod scorers + session orchestrator
// + MeisterAi event aggregators. Golden vectors computed with python (float32 /
// trunc-toward-zero semantics matching VIBE_Coord_ConvertX).
#include "ai/meisterai3.h"
#include "ai/aimethod2.h"
#include "tests/framework/test.h"

#include <vector>

using namespace guild;
using guild::ai::MethodEnv;
using guild::ai::Meister3Hooks;

namespace {

// Deterministic rng_mod stub: replays a fixed sequence (clamped to n) so the
// RNG-driven branches are golden-testable without touching the LCG.
const int* g_rngSeq = nullptr;
int        g_rngLen = 0;
int        g_rngPos = 0;
int RngStub(u16 n) {
    int raw = (g_rngPos < g_rngLen) ? g_rngSeq[g_rngPos] : 0;
    ++g_rngPos;
    return n ? (raw % n) : 0;
}
void ResetRng(const int* seq, int len) { g_rngSeq = seq; g_rngLen = len; g_rngPos = 0; }

MethodEnv EnvWithRng() {
    MethodEnv e = guild::ai::DefaultMethodEnv();
    e.rng_mod = RngStub;
    return e;
}

} // namespace

TEST(Meisterai3Request, SlotResetEmitters) {
    static int lastSlot = -1;
    static int lastKind = -1;
    static int emitCount = 0;
    lastSlot = -1; lastKind = -1; emitCount = 0;
    Meister3Hooks h;
    h.queue_slot_reset28 = [](int slot, u8 kind, int, int) {
        lastSlot = slot; lastKind = kind; ++emitCount;
    };
    Meister3Hooks prev = guild::ai::SetMeister3Hooks(h);

    // handler already exists -> no emission.
    CHECK_EQ(guild::ai::RequestCmd107(/*exists*/ true, /*master*/ 5), false);
    CHECK_EQ(emitCount, 0);

    // no handler -> emit type 107 tagged with the master build id.
    CHECK_EQ(guild::ai::RequestCmd107(/*exists*/ false, /*master*/ 5), true);
    CHECK_EQ(emitCount, 1);
    CHECK_EQ(lastSlot, 5);
    CHECK_EQ(lastKind, 107);

    CHECK_EQ(guild::ai::RequestCmd122(/*exists*/ false, /*master*/ 9), true);
    CHECK_EQ(lastKind, 122);
    CHECK_EQ(lastSlot, 9);
    CHECK_EQ(emitCount, 2);

    // person cmd: invalid person or matching handler -> no emission.
    CHECK_EQ(guild::ai::RequestPersonCmd34(/*valid*/ false, /*match*/ false, 3), false);
    CHECK_EQ(guild::ai::RequestPersonCmd34(/*valid*/ true, /*match*/ true, 3), false);
    CHECK_EQ(emitCount, 2);
    // valid + no match -> emit type 34 indexed by the slot word.
    CHECK_EQ(guild::ai::RequestPersonCmd34(/*valid*/ true, /*match*/ false, 12), true);
    CHECK_EQ(lastSlot, 12);
    CHECK_EQ(lastKind, 34);
    CHECK_EQ(emitCount, 3);

    guild::ai::SetMeister3Hooks(prev);
}

TEST(Meisterai3Social, EarlyOuts) {
    MethodEnv e = EnvWithRng();
    CHECK_EQ(guild::ai::EvalSocialInteraction(2.0f, 0, 0, 0.0f, 0, 0, e), 0);   // gauge<=3
    CHECK_EQ(guild::ai::EvalSocialInteraction(20.0f, 0, 0, 0.0f, 0, 0, e), 1);  // gauge>=18
}

TEST(Meisterai3Social, ThresholdAndFav) {
    static const int seq[] = {1};
    // threshold branch: gauge=8 -> mood 2; trait 2.8 -> adj 0.8 -> 190*2=380 > budget 0.
    {
        MethodEnv e = EnvWithRng();
        ResetRng(seq, 1);
        CHECK_EQ(guild::ai::EvalSocialInteraction(8.0f, 2.8f, 0, 0.0f, 0, 0, e), 1);
    }
    // fav tie -> rng_mod(2): budget huge so threshold not met; favBA=favCA=0 -> tie true.
    {
        MethodEnv e = EnvWithRng();
        ResetRng(seq, 1);
        CHECK_EQ(guild::ai::EvalSocialInteraction(8.0f, 2.5f, 100000, 0.0f, 0.0f, 0.0f, e), 1);
    }
    // fav returns 0: favBA=100, favCA=110 (0.9*1.1 < 1.1*1.0 false-path) -> 0.
    {
        MethodEnv e = EnvWithRng();
        static const int seq9[] = {9};
        ResetRng(seq9, 1);
        CHECK_EQ(guild::ai::EvalSocialInteraction(8.0f, 2.5f, 100000, 0.0f, 100.0f, 110.0f, e), 0);
    }
}

TEST(Meisterai3Purchase, GoldenDesire) {
    MethodEnv e = guild::ai::DefaultMethodEnv();
    // high wealth tier (20.0 -> tier 6), law 0, fav 0 -> +2 -> 8, clamped to 9? 8<9 -> 8.
    // python golden returned 9 because tier 7 path bumps to +2 = 9.
    CHECK_EQ(guild::ai::EvalPurchaseDesire(20.0f, 0, 0.0f, false, 0, e), 9);
    CHECK_EQ(guild::ai::EvalPurchaseDesire(8.0f, 3, 100.0f, true, 1, e), 2);
    // tier-5 collapse when byte358 == false -> 4.
    CHECK_EQ(guild::ai::EvalPurchaseDesire(11.0f, 5, 1000.0f, false, 0, e), 4);
}

TEST(Meisterai3Choice, WeightDistribution) {
    static const int seq[] = {0, 1, 2};
    MethodEnv e = EnvWithRng();
    ResetRng(seq, 3);
    float dist[3] = {10.0f, 20.0f, 40.0f};
    guild::ai::ChoiceWeights w = guild::ai::ComputeChoiceWeights(10000, dist, e);
    // budget = trunc(10000*0.025) = 250; weights = 250 * d/max(40) -> 62,125,250.
    CHECK_EQ(w.weight[0], 62);
    CHECK_EQ(w.weight[1], 125);
    CHECK_EQ(w.weight[2], 250);
    // base bytes (dword_4664B8[k][0]): row0=4, row1=3, row2=2.
    CHECK_EQ(w.base[0], 4);
    CHECK_EQ(w.base[1], 3);
    CHECK_EQ(w.base[2], 2);
    // pick = row[1 + RandomModulo(3)] with rng seq {0,1,2}:
    //   row0[1+0]=0, row1[1+1]=2, row2[1+2]=1.
    CHECK_EQ(w.pick[0], 0);
    CHECK_EQ(w.pick[1], 2);
    CHECK_EQ(w.pick[2], 1);
}

TEST(Meisterai3Weather, ActivityRoll) {
    u8 wb = 0;
    CHECK_EQ(guild::ai::RollWeatherActivity(0.5f, 0.3, false, false, &wb), false);
    CHECK_EQ(wb, (u8)47);
    CHECK_EQ(guild::ai::RollWeatherActivity(0.8f, 0.4, false, false, &wb), true);
    CHECK_EQ(wb, (u8)47);
    CHECK_EQ(guild::ai::RollWeatherActivity(1.5f, 0.0, true, false, &wb), true);
    CHECK_EQ(wb, (u8)13);
    CHECK_EQ(guild::ai::RollWeatherActivity(0.95f, 0.0, false, true, &wb), true);
    CHECK_EQ(wb, (u8)10);
}

TEST(Meisterai3GroupState, MaskRoll) {
    // mask bit0 always; the four rolls -> {nonzero,nonzero,zero,nonzero}.
    static const int seq[] = {1, 1, 0, 1};  // mod2->1, mod3->1, mod3->0, mod2->1
    MethodEnv e = EnvWithRng();
    ResetRng(seq, 4);
    int mask = guild::ai::RollGroupStateMask(e);
    // 1 | 2 | 4 | 0 | 16 = 23.
    CHECK_EQ(mask, 1 | 2 | 4 | 0 | 16);
}

TEST(Meisterai3GroupState, BroadcastCountsLeaders) {
    static int emitCount = 0;
    static int lastMask = -1;
    emitCount = 0; lastMask = -1;
    Meister3Hooks h;
    h.emit_group_state = [](int, int mask) { ++emitCount; lastMask = mask; };
    Meister3Hooks prev = guild::ai::SetMeister3Hooks(h);

    static const int seq[] = {0, 0, 0, 0,   1, 1, 1, 1};  // first leader rolls 0s, second all 1s
    MethodEnv e = EnvWithRng();
    ResetRng(seq, 8);
    u8 slots[5] = {3, 0, 1, 3, 7};  // two slots with byte==3
    int n = guild::ai::BroadcastGroupState(slots, 5, e);
    CHECK_EQ(n, 2);
    CHECK_EQ(emitCount, 2);
    CHECK_EQ(lastMask, 1 | 2 | 4 | 8 | 16);  // second leader: all rolls nonzero

    guild::ai::SetMeister3Hooks(prev);
}

TEST(Meisterai3Tick, TicksOnlyLiveFlaggedInRange) {
    static int worldPosCalls = 0;
    static int dispatchCalls = 0;
    worldPosCalls = 0; dispatchCalls = 0;
    Meister3Hooks h;
    h.update_handler_worldpos = [](int) { ++worldPosCalls; };
    h.dispatch_handler = [](int, u8) { ++dispatchCalls; };
    Meister3Hooks prev = guild::ai::SetMeister3Hooks(h);

    std::vector<guild::ai::ApHandler> hs(5);
    hs[0] = {1, 0x10, 5};    // live, flagged, type<0x88 -> tick
    hs[1] = {0, 0x10, 5};    // not active -> skip
    hs[2] = {1, 0x00, 5};    // flag missing -> skip
    hs[3] = {1, 0x10, 0x88}; // type >= 0x88 -> skip
    hs[4] = {1, 0x11, 7};    // flag bit set within other bits -> tick

    int n = guild::ai::TickRegisteredEvents(hs.data(), 5);
    CHECK_EQ(n, 2);
    CHECK_EQ(worldPosCalls, 2);
    CHECK_EQ(dispatchCalls, 2);

    guild::ai::SetMeister3Hooks(prev);
}

TEST(Meisterai3Apply, DrinkAndEatEmit) {
    static int op90Amount = 0;
    static int resetField8 = 0;
    static int resetCalls = 0;
    op90Amount = 0; resetField8 = 0; resetCalls = 0;
    Meister3Hooks h;
    h.request_build_op90 = [](int, int amt) { op90Amount = amt; };
    h.queue_slot_reset28 = [](int, u8, int, int f8) { resetField8 = f8; ++resetCalls; };
    Meister3Hooks prev = guild::ai::SetMeister3Hooks(h);

    CHECK_EQ(guild::ai::ApplyDrinkAction(/*build*/ 1, /*cost*/ 250, /*slot*/ 9, /*kind*/ 2), (char)4);
    CHECK_EQ(op90Amount, -250);   // debit
    CHECK_EQ(resetField8, -1);    // drink: field8 = -1

    CHECK_EQ(guild::ai::ApplyEatAction(/*build*/ 1, /*cost*/ 30, /*slot*/ 9, /*kind*/ 2), (char)5);
    CHECK_EQ(op90Amount, -30);
    CHECK_EQ(resetField8, 96);    // eat: field8 = field7 (96)
    CHECK_EQ(resetCalls, 2);

    guild::ai::SetMeister3Hooks(prev);
}
