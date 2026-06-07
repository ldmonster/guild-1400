// Integration: drive npcaction9's evaluators against the REAL RNG sibling chain —
// VIBE_Math_RandomModulo (guild::util::RandomModulo, 0x58b89c), which itself
// consumes the REAL CRT LCG (guild::crt::RandNext/Srand). This is exactly the live
// wiring: EvaluateArrest (0x474c4c) seeds its target-search radius from
// RandomModulo(0x20)+32, and the loyalty/jail gates roll RandomModulo(0x40). We
// forward npcaction9's `randomModulo` hook into the genuine reconstructed function
// (no stub) and assert the cross-module value the binary would compute by replaying
// the same REAL CRT LCG sequence.
//
// The deterministic pure cores (TruncToInt / ArrestBounty / ShootTooExpensive /
// RivalBounty / LoyaltyRollPasses) need no hooks and are golden-checked alongside,
// with the loyalty gate fed a roll drawn from the REAL RandomModulo.
#include "test.h"

#include "sim/npcaction9.h"
#include "util/math_random.h"
#include "crt/rand.h"

#include <cstring>

using namespace guild;
using namespace guild::sim;

namespace {

// The REAL sibling, wired exactly as the engine indirection forwards it.
u16 RealRandomModulo(u16 n) { return static_cast<u16>(util::RandomModulo(n)); }

// Capture the search radius EvaluateArrest derives from RandomModulo(0x20)+32.
float g_capturedMaxR = -1.0f;
int g_findCalls = 0;
int CaptureFindNearest(const void* /*record*/, int /*kind*/, void* /*reqBlock*/,
                       float /*minR*/, float maxR, int* outId) {
    g_capturedMaxR = maxR;
    g_findCalls++;
    if (outId) *outId = 0x4242;
    return 1;   // "found" so the body proceeds past the search to the city check
}

// The arrest path requires guild-rank-level-2 == 1 and a wealth/affordability pass.
int RankIsOne(const void*) { return 1; }
int WealthZero(int, const void*) { return 0; }       // wealth 0 -> bounty floor 160
int CurrencyHigh(int, u8) { return 100000; }         // budget huge -> bounty <= budget
void ResolveById(void** out, int, int, int) { if (out) *out = nullptr; }

} // namespace

// EvaluateArrest's target-search radius is RandomModulo(0x20)+32, drawn from the
// REAL CRT LCG. We seed the LCG, run the evaluator with the REAL RandomModulo
// wired, capture the radius the body passed to FindNearestEntity, then replay the
// same REAL sequence and confirm they match.
TEST(NpcAction9Itest, ArrestRadiusFromRealCrtLcg) {
    NpcAction9Hooks hk{};
    hk.randomModulo = RealRandomModulo;          // REAL util::RandomModulo
    hk.amtCheckGuildRankLevel2 = RankIsOne;
    hk.personTotalWealth = WealthZero;
    hk.personCurrencyAmount = CurrencyHigh;
    hk.findNearestEntity = CaptureFindNearest;
    hk.resolveEntityById = ResolveById;
    SetNpcAction9Hooks(&hk);

    crt::Srand(12345);
    g_capturedMaxR = -1.0f; g_findCalls = 0;

    i16 record[64];
    std::memset(record, 0, sizeof(record));
    record[0] = 0x10;   // self id
    unsigned char reqA[kNpc9ReqBlockSize]; std::memset(reqA, 0, sizeof(reqA));

    // With an empty city-id table the success path is not reached (returns 0), but
    // the radius roll is consumed BEFORE that check — exactly the live ordering.
    int code = NpcAction9_EvaluateArrest(/*prevResult*/ 0, reqA, /*relFlag*/ 0, record);
    SetNpcAction9Hooks(nullptr);

    CHECK_EQ(code, 0);            // inert city table -> not-applicable
    CHECK_EQ(g_findCalls, 1);     // the search ran (radius was consumed)

    // Replay the REAL sibling sequence: the only RandomModulo draw before the
    // FindNearestEntity call is the radius roll RandomModulo(0x20).
    crt::Srand(12345);
    float expected = static_cast<float>(util::RandomModulo(0x20) + 32);
    CHECK_EQ(g_capturedMaxR, expected);
    // The radius is always in [32, 63].
    CHECK(g_capturedMaxR >= 32.0f && g_capturedMaxR <= 63.0f);
}

// The loyalty-roll gate (jail/recruit) is the pure comparison byteTop < roll+37,
// with `roll` drawn from the REAL RandomModulo(0x40). Drive both sides from the
// same REAL LCG state and confirm the gate decision the binary makes.
TEST(NpcAction9Itest, LoyaltyRollGateFromRealRng) {
    crt::Srand(777);
    int roll = util::RandomModulo(0x40);          // [0,63]
    CHECK(roll >= 0 && roll <= 63);

    // A loyalty top byte just below roll+37 passes; just at/above fails.
    int threshold = roll + 37;
    CHECK(NpcAction9_LoyaltyRollPasses(threshold - 1, roll));
    CHECK(!NpcAction9_LoyaltyRollPasses(threshold, roll));
    CHECK(!NpcAction9_LoyaltyRollPasses(threshold + 5, roll));
}

// The RNG-independent deterministic cores: golden values for the arrest/shoot
// bounty arithmetic that the evaluators above feed into their affordability gates.
TEST(NpcAction9Itest, DeterministicBountyCores) {
    // ArrestBounty: wealth*0.01f <= 160 -> floor 160; else trunc(wealth*0.01f).
    // kArrestWealthMul is a FLOAT (0.01f), so promoted to double it is slightly
    // under 0.01 — the trunc lands one below the naive decimal (a faithful quirk).
    CHECK_EQ(NpcAction9_ArrestBounty(0), 160);
    CHECK_EQ(NpcAction9_ArrestBounty(16000), 160);     // 159.99.. <= 160 -> floor 160
    CHECK_EQ(NpcAction9_ArrestBounty(50000), 499);     // 50000*0.01f -> 499.9999.. -> 499

    // TruncToInt truncates toward zero (x87 (int)double semantics).
    CHECK_EQ(NpcAction9_TruncToInt(3.99), 3);
    CHECK_EQ(NpcAction9_TruncToInt(-3.99), -3);

    // ShootTooExpensive: (int)((wSelf+wTarget)*0.005) > (int)(currency*0.22).
    // (1000+1000)*0.005 == 10; currency 100 -> 100*0.22 == 22 -> 10 > 22 false.
    CHECK(!NpcAction9_ShootTooExpensive(1000, 1000, 100));
    // (100000+100000)*0.005 == 1000; currency 100 -> 22 -> 1000 > 22 true.
    CHECK(NpcAction9_ShootTooExpensive(100000, 100000, 100));
}

// The fully inert default-hook path: with no hooks, every evaluator takes its
// earliest not-applicable exit (empty world) and returns 0 — proving the module's
// inert defaults are wired and the control flow is faithful with no siblings.
TEST(NpcAction9Itest, InertWorldEvaluatorsReturnZero) {
    SetNpcAction9Hooks(nullptr);

    u16 rec[64]; std::memset(rec, 0, sizeof(rec));
    i16 irec[64]; std::memset(irec, 0, sizeof(irec));
    unsigned char a[kNpc9ReqBlockSize], b[kNpc9ReqBlockSize];
    std::memset(a, 0, sizeof(a)); std::memset(b, 0, sizeof(b));

    CHECK_EQ(NpcAction9_EvaluateShoot(rec, 0, a, b), 0);
    CHECK_EQ(NpcAction9_EvaluateEnterBuilding(rec, a, 0, b), 0);
    CHECK_EQ(NpcAction9_EvaluateArrest(0, a, 0, irec), 0);
    CHECK_EQ(NpcAction9_EvaluateShopInteract(0, a, 0, rec, b), 0);
    // relFlag set short-circuits regardless of world.
    CHECK_EQ(NpcAction9_EvaluateShoot(rec, 1, a, b), 0);
}
