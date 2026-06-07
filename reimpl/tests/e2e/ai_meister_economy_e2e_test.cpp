// End-to-end: run a full MeisterAi player turn on a synthetic faction with known
// buildings/workers/crafts and verify the ordered sequence of emitted (mock)
// commands and the resulting accumulator against a hand-computed reference.
//
// The turn consumes the CRT RNG (mood-decay roll per building; confrontation
// rolls per low-relation worker). The reference replays the SAME draw order on a
// freshly-seeded generator so the golden is exact.
#include "tests/framework/test.h"

#include "ai/meister_economy.h"
#include "ai/meisterai.h"
#include "crt/rand.h"
#include "util/math_random.h"

#include <vector>

using namespace guild;

namespace {

// Build a deterministic faction: 4 buildings (one of each interesting kind), 2
// workers (one normal, one confrontation-bound), 1 craft building.
ai::MeisterFaction MakeFaction() {
    ai::MeisterFaction f;
    f.playerId = 7;

    // b0: a security-sweep building (kind 4) with 2 objects: one over-heat with a
    // guarding building (clamp), one cool (Quad43).
    ai::MeisterBuilding b0;
    b0.kind = 4; b0.ownerPlayer = 7; b0.id = 100; b0.needsCmd25 = true;
    b0.securityObjects.push_back({10 /*heat*/, 2 /*sec*/, 1001});  // heat>5 -> clamp 10-4=6
    b0.securityObjects.push_back({3 /*heat*/, -1, 1002});          // heat<=5 -> Quad43
    f.buildings.push_back(b0);

    // b1: banker (kind 5), multiplier 2 -> tax 32000.
    ai::MeisterBuilding b1;
    b1.kind = 5; b1.ownerPlayer = 7; b1.id = 200; b1.aiMultiplier = 2;
    f.buildings.push_back(b1);

    // b2: guard building (kind 11), guard target 0x2A. Not owned by player check
    // skip: owner 7. (no supervision reset for guard kinds.)
    ai::MeisterBuilding b2;
    b2.kind = 11; b2.ownerPlayer = 7; b2.id = 300; b2.guardTargetHi = 0x2A;
    f.buildings.push_back(b2);

    // b3: a non-owned building (skipped entirely).
    ai::MeisterBuilding b3;
    b3.kind = 4; b3.ownerPlayer = 99; b3.id = 400;
    f.buildings.push_back(b3);

    // workers: w0 normal (small delta, no confrontation); w1 confrontation-bound.
    ai::MeisterWorker w0;
    w0.personId = 5000; w0.attitudeA = 100; w0.attitudeB = 100; w0.relation = 0;
    w0.profession = 0; w0.itemValue = 1234; w0.workerEntityId = 5001;
    f.workers.push_back(w0);

    ai::MeisterWorker w1;
    // attitudeA=70 (delta -10), relation -30 -> newRelation -40 < -26 -> confront.
    w1.personId = 6000; w1.attitudeA = 70; w1.attitudeB = 100; w1.relation = -30;
    w1.profession = 2; /* craft profession -> profession coord */
    w1.itemValue = 5678; w1.workerEntityId = 6001;
    f.workers.push_back(w1);

    // craft building: both worth gates set.
    ai::MeisterCraftBuilding c;
    c.ownerPersonId = 7000; c.hasWorthA = true; c.productSumA = 4000; c.productIdA = 11;
    c.hasWorthB = true; c.productSumB = 2500; c.productIdB = 12;
    f.crafts.push_back(c);

    return f;
}

} // namespace

TEST(AiMeisterEconomyE2E, OrderedCommandSequence) {
    ai::MeisterFaction f = MakeFaction();

    const u32 kSeed = 0xC0FFEE;
    crt::Srand(kSeed);
    std::vector<ai::MeisterCmdRecord> cmds;
    int total = ai::ProcessPlayerTurn(f, cmds);

    // --- hand-computed reference, replaying the same RNG draw order ----------
    crt::Srand(kSeed);
    std::vector<ai::MeisterCmd> expect;
    int v3 = 0;

    auto moodDecay = [](u8 kind, bool flag) -> int {
        // RandomModulo(100) drawn unconditionally; then RandomModulo(3) if fired.
        if (static_cast<u16>(util::RandomModulo(0x64)) <= 0x1Eu) return 0;
        if (kind == 13 || kind == 12 || kind == 11 || kind == 16 || flag) return 0;
        int mag = util::RandomModulo(3);
        return -(mag + 2);
    };

    // b0 (kind 4, owner 7): cmd25, then 2 security objects, then supervision
    //   (Quad56 + Reset28 op27; not market-supervised so no op49), then decay.
    expect.push_back(ai::MeisterCmd::kRequestArgs25); ++v3;
    // obj0: heat 10 > 5, securityLevel 2 -> clamp heat command.
    ++v3; expect.push_back(ai::MeisterCmd::kSecuritySweepHeat);
    // obj1: heat 3 <= 5 -> Quad43.
    ++v3; expect.push_back(ai::MeisterCmd::kSecuritySweepQuad43);
    // supervision (kind 4 is not 11/12/13).
    expect.push_back(ai::MeisterCmd::kSuperviseQuad56); ++v3;
    ++v3; expect.push_back(ai::MeisterCmd::kSuperviseReset28);
    {
        int d = moodDecay(4, false);
        if (d != 0) { ++v3; expect.push_back(ai::MeisterCmd::kMoodDecay); }
    }

    // b1 (banker kind 5): State23, tax15 (2 increments), supervision (kind 5 not
    //   guard), then decay.
    expect.push_back(ai::MeisterCmd::kBankerStats23);
    v3 += 2; // the two ++v3 around the banker tax
    expect.push_back(ai::MeisterCmd::kBankerTax15);
    expect.push_back(ai::MeisterCmd::kSuperviseQuad56); ++v3;
    ++v3; expect.push_back(ai::MeisterCmd::kSuperviseReset28);
    {
        int d = moodDecay(5, false);
        if (d != 0) { ++v3; expect.push_back(ai::MeisterCmd::kMoodDecay); }
    }

    // b2 (guard kind 11): guard target, supervision Quad56 (but no Reset28 for
    //   guard kinds), then decay (kind 11 excluded from decay).
    ++v3; expect.push_back(ai::MeisterCmd::kGuardTarget61);
    expect.push_back(ai::MeisterCmd::kSuperviseQuad56); ++v3;
    {
        int d = moodDecay(11, false); // kind 11 -> excluded -> 0 (still draws 100)
        if (d != 0) { ++v3; expect.push_back(ai::MeisterCmd::kMoodDecay); }
    }

    // b3 skipped (owner 99).

    // Phase 2: workers.
    // w0: stock16, mood delta (0 -> no coord), state22, no confrontation.
    expect.push_back(ai::MeisterCmd::kStockRequest16);
    int v29 = v3 + 1;
    i8 d0 = ai::MoodRelationDelta(100, 100, 0); // 0
    if (d0 != 0) { ++v29; expect.push_back(ai::MeisterCmd::kWorkerMoodCoord27); }
    expect.push_back(ai::MeisterCmd::kWorkerState22);
    v3 = v29 + 1;
    {
        int nr = 0 + d0;
        if (nr < -26) { /* not reached */ }
    }

    // w1: stock16, mood delta (-10 -> coord), state22, confrontation rolls.
    expect.push_back(ai::MeisterCmd::kStockRequest16);
    v29 = v3 + 1;
    i8 d1 = ai::MoodRelationDelta(70, 100, -30); // attitudeA 70 -> -10
    if (d1 != 0) { ++v29; expect.push_back(ai::MeisterCmd::kWorkerMoodCoord27); }
    expect.push_back(ai::MeisterCmd::kWorkerState22);
    v3 = v29 + 1;
    {
        int nr = -30 + d1; // -40
        if (nr < -26) {
            int gate = -static_cast<int>(static_cast<u16>(util::RandomModulo(0x4A)));
            if (gate < nr) {
                ++v3;
                if (static_cast<u16>(util::RandomModulo(0x64)) <= 0x32u) {
                    util::RandomModulo(6);
                    expect.push_back(ai::MeisterCmd::kConfront52);
                } else {
                    util::RandomModulo(6);
                    expect.push_back(ai::MeisterCmd::kConfront51);
                }
            }
            // profession 2 -> profession coord update.
            expect.push_back(ai::MeisterCmd::kProfessionCoord); ++v3;
        }
    }

    // Phase 3: craft building, both worth gates.
    ++v3; expect.push_back(ai::MeisterCmd::kProductionCmd15);
    ++v3; expect.push_back(ai::MeisterCmd::kProductionCmd15);

    // --- compare -------------------------------------------------------------
    CHECK_EQ(cmds.size(), expect.size());
    bool ordered = (cmds.size() == expect.size());
    for (size_t i = 0; i < cmds.size() && i < expect.size(); ++i) {
        if (cmds[i].op != expect[i]) { ordered = false; break; }
    }
    CHECK(ordered);
    CHECK_EQ(total, v3);
}

TEST(AiMeisterEconomyE2E, CommandOperandsSpotCheck) {
    // Verify a few load-bearing operands: banker tax = 16000*mult, guard target
    // hi byte, security heat clamp.
    ai::MeisterFaction f = MakeFaction();
    crt::Srand(1);
    std::vector<ai::MeisterCmdRecord> cmds;
    ai::ProcessPlayerTurn(f, cmds);

    bool sawTax = false, sawGuard = false, sawHeat = false;
    for (const auto& r : cmds) {
        if (r.op == ai::MeisterCmd::kBankerTax15) {
            CHECK_EQ(r.c, 32000); // 16000 * multiplier 2
            sawTax = true;
        }
        if (r.op == ai::MeisterCmd::kGuardTarget61) {
            CHECK_EQ(r.c, 0x2A);
            sawGuard = true;
        }
        if (r.op == ai::MeisterCmd::kSecuritySweepHeat) {
            CHECK_EQ(r.b, 6); // heat 10 - 2*2 = 6
            sawHeat = true;
        }
    }
    CHECK(sawTax);
    CHECK(sawGuard);
    CHECK(sawHeat);
}

TEST(AiMeisterEconomyE2E, DeterministicReplay) {
    // Same seed -> identical command sequence and total.
    ai::MeisterFaction f = MakeFaction();
    crt::Srand(555);
    std::vector<ai::MeisterCmdRecord> a;
    int ta = ai::ProcessPlayerTurn(f, a);
    crt::Srand(555);
    std::vector<ai::MeisterCmdRecord> b;
    int tb = ai::ProcessPlayerTurn(f, b);
    CHECK_EQ(ta, tb);
    CHECK_EQ(a.size(), b.size());
    bool same = (a.size() == b.size());
    for (size_t i = 0; i < a.size() && i < b.size(); ++i)
        if (a[i].op != b[i].op) { same = false; break; }
    CHECK(same);
}
