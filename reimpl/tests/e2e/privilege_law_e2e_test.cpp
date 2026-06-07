// E2E: a full office-candidacy presentation -> confirmation flow across the
// privilege_law module. We model a small candidate roster, drive the actor
// collection, lay out the candidate card list, render the requirement text, and
// finally confirm a candidacy — asserting the state threaded through every step.
#include "test.h"

#include "world/privilege_law.h"

#include <vector>
#include <cstring>

using namespace guild;
using namespace guild::world;

namespace {

u16 RmHalf(u32 n) { return static_cast<u16>(n / 2); }

int ReqBand(u8, i16* aMin, i16* aMax, u8* mMin, u8* mMax) {
    if (aMin) *aMin = 18;
    if (aMax) *aMax = 40;
    if (mMin) *mMin = 2;
    if (mMax) *mMax = 8;
    return 1;
}

} // namespace

TEST(PrivilegeLawE2E, CandidacyPresentationToConfirmFlow) {
    // ---- step 1: collect the session actors owned by guild #7 ---------------
    // actors 0,3,5 are owned by guild 7; the rest are unowned/other.
    auto ownerOf = [](int idx) -> i32 {
        if (idx == 0 || idx == 3 || idx == 5) return 7;
        if (idx == 1) return 4;
        return 0;
    };
    static i32 (*s_owner)(int) = ownerOf;
    i32 actors[40] = {};
    int actorCount = OfficeCollectActorsByOwner(7, [](int i){ return s_owner(i); },
                                                actors, 40);
    CHECK_EQ(actorCount, 3);
    // 1-based write quirk preserved.
    CHECK_EQ(actors[1], 0);
    CHECK_EQ(actors[2], 3);
    CHECK_EQ(actors[3], 5);

    // ---- step 2: present the candidate card list for those actors -----------
    static int g_enabled = 0; static int g_cards = 0;
    g_enabled = 0; g_cards = 0;
    PrivilegeLawHooks h{};
    h.pageWidth = [](int) { return 600; };
    h.contentWidth = []() { return 100; };
    h.buildPersonCard = [](int, i16 y, int, CandidateCard* slot) -> int {
        g_cards++; slot->objectId = static_cast<i16>(2000 + y); return slot->objectId;
    };
    h.setEnabled = [](i16, int) { g_enabled++; };
    PrivilegeLawSetHooks(h);

    std::vector<CandidateCard> cards(actorCount);
    std::memset(cards.data(), 0, cards.size() * sizeof(CandidateCard));
    int shown = OfficeShowCandidateCardList(1 /*variant B*/, 10, 11, 12,
                                            actorCount, cards.data());
    CHECK_EQ(shown, 1);
    CHECK_EQ(g_cards, 3);
    CHECK_EQ(g_enabled, 3);
    // x = (600-100)/2 = 250 for every card; y = 40,152,264.
    CHECK_EQ(static_cast<int>(cards[0].x), 250);
    CHECK_EQ(static_cast<int>(cards[0].y), 40);
    CHECK_EQ(static_cast<int>(cards[1].y), 152);
    CHECK_EQ(static_cast<int>(cards[2].y), 264);
    CHECK_EQ(static_cast<int>(cards[2].objectId), 2000 + 264);

    // ---- step 3: render the requirement text for the target rank ------------
    // band 18..40 / 2..8, RmHalf -> target = 2 + (8+1-2)/2 = 2 + 3 = 5; v2=
    // randMod(12)=6 -> variant=7; parity seeds randMod(2)=1, flips to 0.
    static int g_reqTarget = 0; static u8 g_variant = 0; g_reqTarget = 0;
    h.rankReq = ReqBand;
    h.randMod = RmHalf;
    h.enqueueObjInteraction = [](int, int target, u8 variant, int) -> int {
        g_reqTarget = target; g_variant = variant; return 1;
    };
    PrivilegeLawSetHooks(h);
    u8 parity = 2;
    int rendered = OfficeRenderRequirementText(5, &parity);
    CHECK_EQ(rendered, 1);
    CHECK_EQ(g_reqTarget, 5);
    CHECK_EQ(static_cast<int>(g_variant), 7);
    CHECK_EQ(static_cast<int>(parity), 0);

    // ---- step 4: confirm the candidacy --------------------------------------
    // age band 18..40 RmHalf -> 18 + (40+1-18)/2 = 18 + 11 = 29.
    // money band countA..countB = 2..8 -> 2 + (8+1-2)/2 = 2 + 3 = 5.
    static int g_qCalls = 0; static i32 g_qAge = 0; static u8 g_qMoney = 0;
    static int g_applyCalls = 0; g_qCalls = 0; g_applyCalls = 0;
    h.queueCandidacy = [](i32, i32 age, u8 money, u8) {
        g_qCalls++; g_qAge = age; g_qMoney = money;
    };
    h.applyForCandidacy = [](i32, u8) -> int { g_applyCalls++; return 1; };
    PrivilegeLawSetHooks(h);

    CandidacyCommit commit{};
    int confirmed = OfficeConfirmCandidacyDialog(actors[1], 5, 19, &commit);
    CHECK_EQ(confirmed, 1);
    CHECK(commit.eligible == true);
    CHECK_EQ(commit.ageRoll, 29);
    CHECK_EQ(static_cast<int>(commit.moneyRoll), 5);
    CHECK_EQ(g_qCalls, 1);
    CHECK_EQ(g_qAge, 29);
    CHECK_EQ(static_cast<int>(g_qMoney), 5);
    CHECK_EQ(g_applyCalls, 1);

    PrivilegeLawResetHooks();
}
