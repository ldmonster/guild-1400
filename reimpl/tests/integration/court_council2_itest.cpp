#include "test.h"

// Integration: drive court_council2's VIBE_NpcAction_EvaluateCourtTrial against
// the REAL reconstructed RNG sibling VIBE_Math_RandomModulo (util/math_random.cpp
// 0x58b89c, layered on the real crt LCG crt/rand.cpp). The CourtCouncilHooks
// randomMod4 slot IS that function in the live binary (the header states "the
// shipping game wires this to util::RandomModulo(4)"); we forward it straight into
// the real RandomModulo, exactly as the live wiring, and prove the court verdict's
// ambiguous-case direction is decided by the REAL RNG draw — seeding the real LCG
// makes the cross-module outcome reproducible.
//
// (the building-slot / AiNeeds / eligibility / rating leaves have no reconstructed
// sibling, so they run through this test's own captor hooks feeding the verbatim
// scorer; only randomMod4 is wired to a real src/ function.)
#include "world/court_council2.h"
#include "util/math_random.h"   // REAL reconstructed sibling: RandomModulo (0x58b89c)
#include "crt/rand.h"           // REAL crt LCG behind RandomModulo (Srand / RandNext)
#include "sim/types.h"          // Person

#include <cstring>

using namespace guild;
using guild::sim::Person;

namespace {

// randomMod4 hook -> REAL VIBE_Math_RandomModulo(4).
int RandomMod4Hook() { return util::RandomModulo(4); }

// --- building-slot scenario fed to the verbatim scorer -------------------------
// Two participating categories, both passing the AiNeeds gate with a neutral 1.0
// rating, arranged so the scorer reaches the RNG tie-break (v34 == 0 -> draw):
//   cat 1 (defKind 18, c=0): support=5 total=10  -> the "worst" category v24
//   cat 2 (defKind 20, c=1): support=2 total=10  -> the "best"  category v28
// Four slots: per category one owned by the accused (adds to support) + one owned
// by another (only to total).
struct Slot { bool alive; u16 owner; u8 defKind; u8 sec; };
Slot     g_slots[4];
int      g_slotN = 0;
u16      g_accusedOwner = 0;

int  SlotCountHook() { return g_slotN; }
bool SlotHook(int i, world::CourtBuildingSlot* out) {
    if (i < 0 || i >= g_slotN) return false;
    out->alive       = g_slots[i].alive;
    out->ownerWord   = g_slots[i].owner;
    out->defKind     = g_slots[i].defKind;
    out->defSecurity = g_slots[i].sec;
    return true;
}
// AiNeeds gate fires for the two participating buckets c=0 and c=1.
int   AiNeedsHook(int c)        { return (c == 0 || c == 1) ? 1 : 0; }
float RatingHook(int /*c*/)     { return 1.0f; }   // neutral (== 1.0 threshold)
int   EligibleHook(Person*)     { return 1; }      // accused IS guild-eligible

world::CourtCouncilHooks MakeHooks() {
    world::CourtCouncilHooks h{};
    h.buildingSlotCount     = &SlotCountHook;
    h.buildingSlot          = &SlotHook;
    h.aiNeedsComputeWeights = &AiNeedsHook;
    h.categoryRating        = &RatingHook;
    h.guildEligibility      = &EligibleHook;
    h.randomMod4            = &RandomMod4Hook;   // -> real util::RandomModulo(4)
    return h;
}

void BuildScenario(u16 accusedId) {
    g_accusedOwner = accusedId;
    const u16 other = static_cast<u16>(accusedId + 1);
    // cat1 (defKind 18): support 5 (accused) + 5 (other) => total 10, support 5.
    g_slots[0] = {true, accusedId, 18, 5};
    g_slots[1] = {true, other,     18, 5};
    // cat2 (defKind 20): support 2 (accused) + 8 (other) => total 10, support 2.
    g_slots[2] = {true, accusedId, 20, 2};
    g_slots[3] = {true, other,     20, 8};
    g_slotN = 4;
}

} // namespace

// Seed the real LCG, run the trial, and confirm the verdict matches what the REAL
// RandomModulo(4) draw dictates: v34 = draw - 1; a verdict is ISSUED only when
// v34 is -1 or +1 (draws 0 or 2), and SUPPRESSED when v34 is 0 or 2 (draws 1/3).
// We replay the same seed through the real RandomModulo to compute the expectation.
TEST(CourtCouncil2Itest, CourtTrialVerdictDecidedByRealRandomModulo) {
    world::CourtCouncilHooks h = MakeHooks();
    world::SetCourtCouncilHooks(&h);

    int issuedCount = 0;
    // Sweep several seeds so we cover both the issue and the suppress branches via
    // the real RNG (not a stubbed value).
    for (u32 seed = 1; seed <= 12; ++seed) {
        // Expected draw from the REAL sibling for this seed (one RandNext consumed).
        crt::Srand(seed);
        int draw = util::RandomModulo(4);
        int expectV34 = draw - 1;
        bool expectIssued = (expectV34 == -1 || expectV34 == 1);
        int expectCategory = 0 + 1;  // v24 == c0 -> category 1

        // Now run the scorer with the SAME seed; its randomMod4 forwards into the
        // real RandomModulo, consuming the identical draw.
        crt::Srand(seed);
        Person accused{};
        accused.marker = static_cast<i16>(100);   // accused owner id (the trial subject)
        BuildScenario(static_cast<u16>(accused.marker));

        world::CourtVerdict v{};
        char rc = world::EvaluateCourtTrial(/*a1=*/0, &accused, &v, /*a4=*/0);

        if (expectIssued) {
            CHECK_EQ(static_cast<int>(rc), 54);   // 54 == verdict produced
            CHECK(v.issued);
            CHECK_EQ(v.category, expectCategory);
            CHECK_EQ(v.direction, expectV34);     // direction came from the real draw
            ++issuedCount;
        } else {
            CHECK_EQ(static_cast<int>(rc), 0);    // RNG said no actionable verdict
            CHECK(!v.issued);
            CHECK_EQ(v.direction, 0);
        }
    }
    // The real RNG produced BOTH outcomes across the sweep (proves the draw — not a
    // constant — is steering the cross-module verdict).
    CHECK(issuedCount > 0);
    CHECK(issuedCount < 12);

    world::SetCourtCouncilHooks(nullptr);
}

// Early-out paths (no RNG reached): non-eligible accused, or a1/a4 set, return 0
// with no verdict — confirming the guard precedes the real-sibling RNG call.
TEST(CourtCouncil2Itest, NonEligibleAndGatedTrialsShortCircuit) {
    world::CourtCouncilHooks h = MakeHooks();
    world::SetCourtCouncilHooks(&h);

    crt::Srand(1);
    Person accused{};
    accused.marker = 100;
    BuildScenario(100);

    // a1 != 0 -> immediate 0.
    world::CourtVerdict v1{};
    CHECK_EQ(static_cast<int>(world::EvaluateCourtTrial(1, &accused, &v1, 0)), 0);
    CHECK(!v1.issued);

    // a4 != 0 -> immediate 0.
    world::CourtVerdict v2{};
    CHECK_EQ(static_cast<int>(world::EvaluateCourtTrial(0, &accused, &v2, 1)), 0);
    CHECK(!v2.issued);

    world::SetCourtCouncilHooks(nullptr);
}

// With eligibility forced off (its own captor returning 0), the scorer returns 0
// before any slot scan or RNG draw — exercising the inert-style eligibility gate.
TEST(CourtCouncil2Itest, IneligibleAccusedReturnsZero) {
    world::CourtCouncilHooks h = MakeHooks();
    static auto noEligible = [](Person*) { return 0; };
    h.guildEligibility = +noEligible;
    world::SetCourtCouncilHooks(&h);

    crt::Srand(1);
    Person accused{};
    accused.marker = 100;
    BuildScenario(100);

    world::CourtVerdict v{};
    CHECK_EQ(static_cast<int>(world::EvaluateCourtTrial(0, &accused, &v, 0)), 0);
    CHECK(!v.issued);

    world::SetCourtCouncilHooks(nullptr);
}
