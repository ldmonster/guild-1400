// Integration: drive privilege_law's candidacy flow against a REAL reconstructed
// sibling — guild::world::OfficeGetRankRequirements (office.cpp, gilde.exe
// 0x47fb30), wired exactly as the live engine forwards it. The original
// ConfirmCandidacyDialog / RenderRequirementText call VIBE_Office_GetRankRequirements
// to obtain the requirement block, then roll within its recovered bands. Here the
// PrivilegeLawHooks.rankReq is forwarded into that genuine sibling (a thin adapter
// surfacing the four scalars the rollers consume: ageMin/ageMax and the two byte
// counts), and we independently predict the roll the binary would draw from the
// real office-definition table, then assert the cross-module candidacy commit.
//
// The remaining leaves (the lockstep command queue, ApplyForCandidacy, the HUD
// widget builders) have no fully reconstructed target on this path; those hooks
// stay inert / use captors, as the live wiring would.
#include "test.h"

#include "world/privilege_law.h"
#include "world/office.h"   // REAL OfficeGetRankRequirements + RankRequirements

using namespace guild;
using namespace guild::world;

namespace {

// Adapter forwarding the privilege_law rankReq hook into the REAL sibling.
// The original's age roll uses [ageMin,ageMax]; its byte "money/standing" roll
// uses the two byte counts (countA/countB) of the requirement block.
int RealRankReq(u8 rank, i16* ageMin, i16* ageMax, u8* mMin, u8* mMax) {
    RankRequirements req{};
    int ok = OfficeGetRankRequirements(rank, &req);
    if (!ok)
        return 0;
    if (ageMin) *ageMin = req.ageMin;
    if (ageMax) *ageMax = req.ageMax;
    if (mMin)   *mMin   = req.countA;
    if (mMax)   *mMax   = req.countB;
    return 1;
}

// Deterministic randMod stub that picks the midpoint, so we can predict exactly.
u16 RmHalf(u32 n) { return static_cast<u16>(n / 2); }

// Find a rank for which the real requirement table yields a requirement block.
int FindValidRank() {
    RankRequirements req{};
    for (int r = 0; r < 37; ++r) {
        if (OfficeGetRankRequirements(static_cast<u8>(r), &req))
            return r;
    }
    return -1;
}

} // namespace

TEST(PrivilegeLawItest, ConfirmUsesRealRankRequirements) {
    int rank = FindValidRank();
    CHECK(rank >= 0);
    if (rank < 0) return;

    // Independently compute the expected band straight from the REAL sibling.
    RankRequirements req{};
    int ok = OfficeGetRankRequirements(static_cast<u8>(rank), &req);
    CHECK_EQ(ok, 1);

    // Predicted rolls with RmHalf: age = ageMin + (ageMax+1-ageMin)/2;
    //                              money = countA + (countB+1-countA)/2.
    int expAge = req.ageMin + (req.ageMax + 1 - req.ageMin) / 2;
    int expMoney = req.countA + (req.countB + 1 - req.countA) / 2;

    static int g_qCalls = 0; static i32 g_qAge = 0; static u8 g_qMoney = 0;
    static int g_applyCalls = 0; static i32 g_applyPerson = 0;
    g_qCalls = 0; g_applyCalls = 0;

    PrivilegeLawHooks h{};
    h.rankReq = RealRankReq;          // <-- REAL sibling wiring
    h.randMod = RmHalf;
    h.queueCandidacy = [](i32, i32 age, u8 money, u8) {
        g_qCalls++; g_qAge = age; g_qMoney = money;
    };
    h.applyForCandidacy = [](i32 p, u8) -> int {
        g_applyCalls++; g_applyPerson = p; return 1;
    };
    PrivilegeLawSetHooks(h);

    CandidacyCommit out{};
    int rc = OfficeConfirmCandidacyDialog(1234, static_cast<u8>(rank), 19, &out);
    CHECK_EQ(rc, 1);
    CHECK(out.eligible == true);
    CHECK_EQ(out.ageRoll, expAge);
    CHECK_EQ(static_cast<int>(out.moneyRoll), expMoney);

    // The cross-module flow fired exactly once and carried the band-derived rolls.
    CHECK_EQ(g_qCalls, 1);
    CHECK_EQ(g_qAge, expAge);
    CHECK_EQ(static_cast<int>(g_qMoney), expMoney);
    CHECK_EQ(g_applyCalls, 1);
    CHECK_EQ(g_applyPerson, 1234);
    PrivilegeLawResetHooks();
}

TEST(PrivilegeLawItest, RenderRequirementUsesRealBandTarget) {
    int rank = FindValidRank();
    CHECK(rank >= 0);
    if (rank < 0) return;

    RankRequirements req{};
    OfficeGetRankRequirements(static_cast<u8>(rank), &req);

    // RenderRequirementText's enqueue target = countA + RandomModulo(countB+1-countA).
    // With RmHalf -> countA + (countB+1-countA)/2.
    int expTarget = req.countA + (req.countB + 1 - req.countA) / 2;

    static int g_target = 0; static int g_calls = 0; g_calls = 0;
    PrivilegeLawHooks h{};
    h.rankReq = RealRankReq;          // <-- REAL sibling wiring
    h.randMod = RmHalf;
    h.enqueueObjInteraction = [](int, int target, u8, int) -> int {
        g_calls++; g_target = target; return 1;
    };
    PrivilegeLawSetHooks(h);

    u8 parity = 2;
    int rc = OfficeRenderRequirementText(static_cast<u8>(rank), &parity);
    CHECK_EQ(rc, 1);
    CHECK_EQ(g_calls, 1);
    CHECK_EQ(g_target, expTarget);
    PrivilegeLawResetHooks();
}

TEST(PrivilegeLawItest, InvalidRankRejectedByRealSibling) {
    // A rank past the table (>=37) makes the REAL OfficeGetRankRequirements
    // return 0, so the candidacy gate rejects end to end.
    PrivilegeLawHooks h{};
    h.rankReq = RealRankReq;
    PrivilegeLawSetHooks(h);
    CandidacyCommit out{};
    CHECK_EQ(OfficeConfirmCandidacyDialog(7, 200 /*rank>=37*/, 19, &out), 0);
    CHECK(out.eligible == false);
    PrivilegeLawResetHooks();
}
