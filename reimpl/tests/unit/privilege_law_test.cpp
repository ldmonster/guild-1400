// Unit tests for world/privilege_law — golden vectors computed with python3
// (see the commit message / agent report). CHECK/CHECK_EQ do not abort; every
// deref of an out-param is guarded.
#include "test.h"

#include "world/privilege_law.h"

#include <vector>
#include <cstring>

using namespace guild;
using namespace guild::world;

namespace {

// Deterministic randMod stubs (process-global; tests set these explicitly).
u16 RmZero(u32) { return 0; }
u16 RmHalf(u32 n) { return static_cast<u16>(n / 2); }

// rankReq stub returning a fixed band: age 20..30, money 5..9, ok=1.
int ReqBand(u8, i16* aMin, i16* aMax, u8* mMin, u8* mMax) {
    if (aMin) *aMin = 20;
    if (aMax) *aMax = 30;
    if (mMin) *mMin = 5;
    if (mMax) *mMax = 9;
    return 1;
}
int ReqFail(u8, i16*, i16*, u8*, u8*) { return 0; }

} // namespace

// ---- ConfirmCandidacyDialog ------------------------------------------------
TEST(PrivilegeLawConfirm, GateRejectsWrongRecState) {
    PrivilegeLawHooks h{};
    h.randMod = RmHalf; h.rankReq = ReqBand;
    PrivilegeLawSetHooks(h);

    CandidacyCommit out{};
    // recOffice2 must be 19; anything else -> 0, ineligible.
    CHECK_EQ(OfficeConfirmCandidacyDialog(7, 3, 18, &out), 0);
    CHECK(out.eligible == false);
    PrivilegeLawResetHooks();
}

TEST(PrivilegeLawConfirm, RequirementFailReturnsZero) {
    PrivilegeLawHooks h{};
    h.randMod = RmHalf; h.rankReq = ReqFail;
    PrivilegeLawSetHooks(h);

    CandidacyCommit out{};
    CHECK_EQ(OfficeConfirmCandidacyDialog(7, 3, 19, &out), 0);
    CHECK(out.eligible == false);
    PrivilegeLawResetHooks();
}

TEST(PrivilegeLawConfirm, CommitRollsAgeAndMoney) {
    // golden: band age 20..30 money 5..9, RmHalf -> age=20+5=25, money=5+2=7.
    static int g_calls = 0; static i32 g_age = 0; static u8 g_money = 0; static int g_type = 0;
    static i32 g_applyPerson = 0; static int g_applyType = 0; static int g_applyCalls = 0;
    g_calls = 0; g_applyCalls = 0;

    PrivilegeLawHooks h{};
    h.randMod = RmHalf; h.rankReq = ReqBand;
    h.queueCandidacy = [](i32, i32 age, u8 money, u8 type) {
        g_calls++; g_age = age; g_money = money; g_type = static_cast<int>(type);
    };
    h.applyForCandidacy = [](i32 p, u8 t) -> int {
        g_applyCalls++; g_applyPerson = p; g_applyType = static_cast<int>(t); return 1;
    };
    PrivilegeLawSetHooks(h);

    CandidacyCommit out{};
    CHECK_EQ(OfficeConfirmCandidacyDialog(42, 3, 19, &out), 1);
    CHECK(out.eligible == true);
    CHECK_EQ(out.ageRoll, 25);
    CHECK_EQ(static_cast<int>(out.moneyRoll), 7);
    CHECK_EQ(static_cast<int>(out.officeType), 3);
    CHECK_EQ(g_calls, 1);
    CHECK_EQ(g_age, 25);
    CHECK_EQ(static_cast<int>(g_money), 7);
    CHECK_EQ(g_type, 3);
    CHECK_EQ(g_applyCalls, 1);
    CHECK_EQ(g_applyPerson, 42);
    CHECK_EQ(static_cast<int>(g_applyType), 3);
    PrivilegeLawResetHooks();
}

// ---- RenderRequirementText -------------------------------------------------
TEST(PrivilegeLawRender, RejectsWhenNoRequirements) {
    PrivilegeLawHooks h{};
    h.rankReq = ReqFail;
    PrivilegeLawSetHooks(h);
    u8 parity = 2;
    CHECK_EQ(OfficeRenderRequirementText(3, &parity), -1);
    PrivilegeLawResetHooks();
}

TEST(PrivilegeLawRender, RollsTargetVariantAndFlipsParity) {
    // golden (RmZero, band 20..30/5..9, computeVariant passthrough returns a):
    //   parity seeds randMod(2)=0; v1=0; v2=0; v4=v2+1=1; v5=0; target=moneyMin=5;
    //   parity = 1-0 = 1.
    static int g_op = 0, g_target = 0; static u8 g_variant = 0; static int g_calls = 0;
    g_calls = 0;
    PrivilegeLawHooks h{};
    h.rankReq = ReqBand; h.randMod = RmZero;
    h.enqueueObjInteraction = [](int op, int target, u8 variant, int) -> int {
        g_calls++; g_op = op; g_target = target; g_variant = variant; return 777;
    };
    PrivilegeLawSetHooks(h);

    u8 parity = 2;
    CHECK_EQ(OfficeRenderRequirementText(3, &parity), 777);
    CHECK_EQ(g_calls, 1);
    CHECK_EQ(g_op, 19);
    CHECK_EQ(g_target, 5);
    CHECK_EQ(static_cast<int>(g_variant), 1);
    CHECK_EQ(static_cast<int>(parity), 1);

    // RmHalf golden: parity seed=randMod(2)=1; v5=randMod(5)=2 -> target=5+2=7;
    //   v2=randMod(12)=6 -> v4=7; parity=1-1=0.
    g_calls = 0;
    h.randMod = RmHalf;
    PrivilegeLawSetHooks(h);
    u8 parity2 = 2;
    CHECK_EQ(OfficeRenderRequirementText(3, &parity2), 777);
    CHECK_EQ(g_target, 7);
    CHECK_EQ(static_cast<int>(g_variant), 7);
    CHECK_EQ(static_cast<int>(parity2), 0);
    PrivilegeLawResetHooks();
}

// ---- ShowCandidateCardList layout ------------------------------------------
TEST(PrivilegeLawCardList, EmptyReturnsZeroNoWork) {
    static int g_labels = 0; g_labels = 0;
    PrivilegeLawHooks h{};
    h.addCenteredLabel = [](int, int, int) { g_labels++; };
    PrivilegeLawSetHooks(h);
    CHECK_EQ(OfficeShowCandidateCardList(0, 1, 2, 3, 0, nullptr), 0);
    CHECK_EQ(g_labels, 0);
    PrivilegeLawResetHooks();
}

TEST(PrivilegeLawCardList, LayoutGoldenVectors) {
    // golden: count=3 pw=640 cw=120 -> x=260, ys=40,152,264; label half=320,2h=640.
    static int g_labelVariant = -1, g_labelX = 0, g_labelW = 0, g_labelCalls = 0;
    static int g_enabled = 0; g_labelCalls = 0; g_enabled = 0;
    PrivilegeLawHooks h{};
    h.pageWidth = [](int) { return 640; };
    h.contentWidth = []() { return 120; };
    h.addCenteredLabel = [](int v, int x, int w) {
        g_labelVariant = v; g_labelX = x; g_labelW = w; g_labelCalls++;
    };
    // buildPersonCard assigns the widget object id deterministically (= 1000+y).
    h.buildPersonCard = [](int, i16 y, int, CandidateCard* slot) -> int {
        slot->objectId = static_cast<i16>(1000 + y); return slot->objectId;
    };
    h.setEnabled = [](i16, int) { g_enabled++; };
    PrivilegeLawSetHooks(h);

    std::vector<CandidateCard> cards(3);
    std::memset(cards.data(), 0, cards.size() * sizeof(CandidateCard));
    CHECK_EQ(OfficeShowCandidateCardList(2 /*variant C tag*/, 5, 6, -1, 3, cards.data()), 1);

    CHECK_EQ(g_labelCalls, 1);
    CHECK_EQ(g_labelVariant, 2);
    CHECK_EQ(g_labelX, 320);
    CHECK_EQ(g_labelW, 640);

    CHECK_EQ(static_cast<int>(cards[0].x), 260);
    CHECK_EQ(static_cast<int>(cards[1].x), 260);
    CHECK_EQ(static_cast<int>(cards[2].x), 260);
    CHECK_EQ(static_cast<int>(cards[0].y), 40);
    CHECK_EQ(static_cast<int>(cards[1].y), 152);
    CHECK_EQ(static_cast<int>(cards[2].y), 264);
    CHECK_EQ(static_cast<int>(cards[0].objectId), 1040);
    CHECK_EQ(static_cast<int>(cards[2].objectId), 1264);
    CHECK_EQ(g_enabled, 3);
    PrivilegeLawResetHooks();
}

TEST(PrivilegeLawCardList, SingleCardZeroContentWidth) {
    // golden: count=1 pw=500 cw=0 -> x=250 y=40.
    PrivilegeLawHooks h{};
    h.pageWidth = [](int) { return 500; };
    h.contentWidth = []() { return 0; };
    PrivilegeLawSetHooks(h);
    std::vector<CandidateCard> cards(1);
    std::memset(cards.data(), 0, sizeof(CandidateCard));
    CHECK_EQ(OfficeShowCandidateCardList(0, 1, 2, 3, 1, cards.data()), 1);
    CHECK_EQ(static_cast<int>(cards[0].x), 250);
    CHECK_EQ(static_cast<int>(cards[0].y), 40);
    PrivilegeLawResetHooks();
}

// ---- PrepareCandidatePage --------------------------------------------------
TEST(PrivilegeLawPrepare, AltMinusOneSkipsScrollSetup) {
    static std::vector<int> g_sel; static int g_scroll = 0; static int g_remove = 0;
    g_sel.clear(); g_scroll = 0; g_remove = 0;
    PrivilegeLawHooks h{};
    h.selectWindow = [](int, int w) { g_sel.push_back(w); };
    h.removeChildren = [](int) { g_remove++; };
    h.createScrollButtons = [](int) { g_scroll++; };
    PrivilegeLawSetHooks(h);

    CHECK_EQ(OfficePrepareCandidatePage(1, 7, -1), 0);
    // alt==-1: only the primary window selected once + RemoveChildren, no scroll.
    CHECK_EQ(static_cast<int>(g_sel.size()), 1);
    if (!g_sel.empty()) CHECK_EQ(g_sel[0], 7);
    CHECK_EQ(g_remove, 1);
    CHECK_EQ(g_scroll, 0);
    PrivilegeLawResetHooks();
}

TEST(PrivilegeLawPrepare, AltSetsUpScrollWindow) {
    static std::vector<int> g_sel; static int g_scroll = 0;
    g_sel.clear(); g_scroll = 0;
    PrivilegeLawHooks h{};
    h.selectWindow = [](int, int w) { g_sel.push_back(w); };
    h.createScrollButtons = [](int) { g_scroll++; };
    PrivilegeLawSetHooks(h);

    CHECK_EQ(OfficePrepareCandidatePage(1, 7, 9), 0);
    // primary, alt, primary again -> 3 selects, ending on the primary.
    CHECK_EQ(static_cast<int>(g_sel.size()), 3);
    if (g_sel.size() == 3) {
        CHECK_EQ(g_sel[0], 7);
        CHECK_EQ(g_sel[1], 9);
        CHECK_EQ(g_sel[2], 7);
    }
    CHECK_EQ(g_scroll, 1);
    PrivilegeLawResetHooks();
}

// ---- CollectActorsByOwner --------------------------------------------------
TEST(PrivilegeLawActors, CollectsMatchingOwnersOneBased) {
    // ownerOf returns 5 for actors 2,4,7 and 0/other otherwise.
    auto ownerOf = [](int idx) -> i32 {
        if (idx == 2 || idx == 4 || idx == 7) return 5;
        if (idx == 1) return 3; // different owner
        return 0;
    };
    static i32 (*s_owner)(int) = ownerOf;
    i32 out[40] = {};
    int n = OfficeCollectActorsByOwner(5, [](int i){ return s_owner(i); }, out, 40);
    CHECK_EQ(n, 3);
    // 1-based write quirk: out[0] untouched (0), matches at out[1..3].
    CHECK_EQ(out[0], 0);
    CHECK_EQ(out[1], 2);
    CHECK_EQ(out[2], 4);
    CHECK_EQ(out[3], 7);
}

TEST(PrivilegeLawActors, CapsAt31Matches) {
    // every actor owned by 9 -> capped at 31 matches.
    auto ownerOf = []([[maybe_unused]] int idx) -> i32 { return 9; };
    static i32 (*s_owner)(int) = ownerOf;
    i32 out[64] = {};
    int n = OfficeCollectActorsByOwner(9, [](int i){ return s_owner(i); }, out, 64);
    CHECK_EQ(n, 31);
}
