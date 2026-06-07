// Unit tests for world/court_council2 — candidate collection, category tally,
// candidate rating bars, and the court-trial verdict scorer. Golden vectors are
// computed against the reconstructed office-def table (OfficeDefBookCat oracle)
// and hand-derived FSM traces.
#include "test.h"

#include "world/court_council2.h"
#include "world/office.h"        // OfficeDefBookCat (oracle), OfficePerson
#include "sim/entity.h"          // g_persons, kPersonCapacity
#include "sim/types.h"           // Person, kPersonStride
#include "sim/person_relations.h" // PersonRelCtx, PersonRelEntry

#include <cstring>
#include <cstdint>

using namespace guild::world;
using guild::sim::g_persons;
using guild::sim::kPersonCapacity;
using guild::sim::Person;
using guild::sim::PersonRelEntry;
using guild::sim::PersonRelCtx;
using guild::sim::SetPersonRelCtx;

namespace {

inline void PutByte(Person* p, int off, std::uint8_t v) {
    reinterpret_cast<std::uint8_t*>(p)[off] = v;
}
inline void PutDword(Person* p, int off, std::int32_t v) {
    std::memcpy(reinterpret_cast<std::uint8_t*>(p) + off, &v, sizeof v);
}

// Clear the whole person array to "free" (marker == -1, all other bytes 0).
void ResetPersons() {
    for (int i = 0; i < kPersonCapacity; ++i) {
        std::memset(&g_persons[i], 0, sizeof(Person));
        g_persons[i].marker = -1;
    }
}

// A live person: marker (id-ish) + kind (+2) + office bytes (+359 = held office
// for the tally; +360 = office band for the collectors).
void MakePerson(int slot, std::int16_t marker, std::uint8_t kind,
                std::uint8_t office359, std::uint8_t office360) {
    Person* p = &g_persons[slot];
    std::memset(p, 0, sizeof(Person));
    p->marker = marker;
    PutByte(p, 2, kind);
    PutByte(p, 358, 1);             // gate byte for CategoryMatched
    PutByte(p, 359, office359);
    PutByte(p, 360, office360);
    PutDword(p, 4, marker);         // id == marker for simplicity
}

// Favorability hook capture for deterministic scoring.
float g_favorTable[8] = {0};
int   g_favorRef = -1, g_favorCand = -1;
float FavorHook(int a, int b, int /*mode*/) {
    g_favorRef = a; g_favorCand = b;
    return (b >= 0 && b < 8) ? g_favorTable[b] : 99.0f;
}
int EligibleHook(std::uint16_t /*ref*/, std::uint16_t cand, int /*filter*/) {
    return 1000 + cand;             // deterministic, distinguishable
}

PersonRelCtx MakeCtx() {
    PersonRelCtx c;
    c.personFavorability = &FavorHook;
    c.evaluateEligibility = &EligibleHook;
    return c;
}

} // namespace

// ---------------------------------------------------------------------------
// TallyCategoryCounts (0x47fdfc): histogram by held-office book category.
// ---------------------------------------------------------------------------
TEST(CourtCouncil2Tally, BucketsByBookCategoryOracle) {
    ResetPersons();
    // kinds: in-range [2,7] count; out-of-range are skipped.
    MakePerson(0, 10, 5, 0,    0);   // office359==0 -> bucket 0
    MakePerson(1, 11, 2, 3,    0);   // office 3 -> OfficeDefBookCat(3)
    MakePerson(2, 12, 7, 3,    0);   // office 3 -> same bucket as slot1
    MakePerson(3, 13, 4, 0x25, 0);   // office >=0x25 -> OfficeDefBookCat(0)
    MakePerson(4, 14, 1, 3,    0);   // kind 1 (<2) -> SKIPPED
    MakePerson(5, 15, 8, 3,    0);   // kind 8 (>7) -> SKIPPED
    MakePerson(6, 16, 6, 0,    0);   // office 0 -> bucket 0

    int hist[256];
    OfficeTallyCategoryCounts(hist);

    // Oracle: recompute the same way the implementation must.
    int expect[256] = {0};
    for (int i = 0; i < kPersonCapacity; ++i) {
        std::uint8_t kind = reinterpret_cast<std::uint8_t*>(&g_persons[i])[2];
        if (kind >= 2 && kind <= 7) {
            std::uint8_t off = reinterpret_cast<std::uint8_t*>(&g_persons[i])[359];
            if (off) {
                std::uint8_t b = (off < 0x25) ? OfficeDefBookCat(off) : OfficeDefBookCat(0);
                ++expect[b];
            } else {
                ++expect[0];
            }
        }
    }
    for (int b = 0; b < 40; ++b)
        CHECK_EQ(hist[b], expect[b]);

    // Specifically: slots 0 and 6 land in bucket 0 (office==0); slots 1,2 share
    // OfficeDefBookCat(3); slot 3 lands in OfficeDefBookCat(0).
    std::uint8_t b3 = OfficeDefBookCat(3);
    std::uint8_t b0 = OfficeDefBookCat(0);
    int office0Count = 2 + (b0 == 0 ? 1 : 0);    // slots 0,6 plus slot3 if b0==0
    int b3Count = (b3 == 0 ? 0 : 2) + ((b3 == b0) ? 1 : 0)
                  + (b3 == 0 ? 2 : 0);
    (void)office0Count; (void)b3Count;
    CHECK(hist[b3] >= 2);            // slots 1,2 at least
}

TEST(CourtCouncil2Tally, EmptyArrayAllZero) {
    ResetPersons();
    int hist[256];
    int* r = OfficeTallyCategoryCounts(hist);
    CHECK_EQ(r, hist);
    for (int b = 0; b < 40; ++b) CHECK_EQ(hist[b], 0);
}

// ---------------------------------------------------------------------------
// CollectCategoryMatchedCandidates (0x555d5c).
// ---------------------------------------------------------------------------
TEST(CourtCouncil2CatMatch, CountModeMatchesOfficeBand) {
    ResetPersons();
    PersonRelCtx ctx = MakeCtx();
    SetPersonRelCtx(&ctx);
    // refRec at slot 0 with +358 gate set.
    MakePerson(0, 100, 5, 0, 0);
    PutByte(&g_persons[0], 358, 1);
    // Candidates: office360 == 4 matches list code 4.
    MakePerson(1, 1, 5, 0, 4);
    MakePerson(2, 2, 5, 0, 4);
    MakePerson(3, 3, 5, 0, 7);   // code 7 not in list -> not counted
    MakePerson(4, 4, 5, 0, 0);   // office 0 -> skipped

    const std::uint8_t list[1] = {4};
    // capacity 0 -> count persons whose +360 in {4}: slots 1,2.
    std::uint32_t cnt = OfficeCollectCategoryMatchedCandidates(
        &g_persons[0], 0, /*filter*/0, nullptr, list, 1);
    CHECK_EQ(cnt, 2u);
    SetPersonRelCtx(nullptr);
}

TEST(CourtCouncil2CatMatch, FillModeWritesEntries) {
    ResetPersons();
    PersonRelCtx ctx = MakeCtx();
    SetPersonRelCtx(&ctx);
    MakePerson(0, 100, 5, 0, 0);
    PutByte(&g_persons[0], 358, 1);
    MakePerson(5, 50, 5, 0, 4);
    MakePerson(9, 90, 5, 0, 4);

    const std::uint8_t list[1] = {4};
    PersonRelEntry out[4];
    std::memset(out, 0, sizeof out);
    std::uint32_t w = OfficeCollectCategoryMatchedCandidates(
        &g_persons[0], 4, /*filter*/0, out, list, 1);
    CHECK_EQ(w, 2u);
    if (w == 2) {
        CHECK_EQ(out[0].person, &g_persons[5]);
        CHECK_EQ(out[1].person, &g_persons[9]);
        CHECK_EQ(out[0].relClass, 4);
        // eligibility stamped via hook: 1000 + candidate marker.
        CHECK_EQ(out[0].eligibility, 1000 + 50);
        CHECK_EQ(out[1].eligibility, 1000 + 90);
    }
    SetPersonRelCtx(nullptr);
}

TEST(CourtCouncil2CatMatch, GateByte358ZeroReturnsZero) {
    ResetPersons();
    MakePerson(0, 100, 5, 0, 0);
    PutByte(&g_persons[0], 358, 0);   // gate clear
    const std::uint8_t list[1] = {4};
    PersonRelEntry out[4];
    CHECK_EQ(OfficeCollectCategoryMatchedCandidates(&g_persons[0], 4, 0, out, list, 1), 0u);
}

// ---------------------------------------------------------------------------
// CollectGuildSuccessorCandidates (0x555ba8).
// ---------------------------------------------------------------------------
TEST(CourtCouncil2Successor, ZeroOfficeReturnsZero) {
    ResetPersons();
    MakePerson(0, 100, 5, 0, 0);   // office360 == 0
    PersonRelEntry out[8];
    CHECK_EQ(OfficeCollectGuildSuccessorCandidates(&g_persons[0], 8, 0, out, nullptr, 0), 0u);
}

TEST(CourtCouncil2Successor, CountSameOfficePersons) {
    ResetPersons();
    PersonRelCtx ctx = MakeCtx();
    SetPersonRelCtx(&ctx);
    MakePerson(0, 100, 5, 0, 9);   // refRec office 9
    MakePerson(1, 1,  5, 0, 9);    // same office, other person
    MakePerson(2, 2,  5, 0, 9);    // same office
    MakePerson(3, 3,  5, 0, 5);    // diff office
    // capacity 0 -> poolCount(0, empty pool) + scanCount(<=3 same-office others).
    std::uint32_t cnt = OfficeCollectGuildSuccessorCandidates(
        &g_persons[0], 0, 0, nullptr, nullptr, 0);
    CHECK_EQ(cnt, 2u);   // slots 1,2
    SetPersonRelCtx(nullptr);
}

TEST(CourtCouncil2Successor, FillCapsAtThreeScanned) {
    ResetPersons();
    PersonRelCtx ctx = MakeCtx();
    SetPersonRelCtx(&ctx);
    MakePerson(0, 100, 5, 0, 9);
    for (int s = 1; s <= 5; ++s) MakePerson(s, s, 5, 0, 9); // 5 same-office others
    PersonRelEntry out[8];
    std::memset(out, 0, sizeof out);
    std::uint32_t w = OfficeCollectGuildSuccessorCandidates(
        &g_persons[0], 8, 0, out, nullptr, 0);
    CHECK_EQ(w, 3u);   // scan caps at 3
    if (w == 3) {
        CHECK_EQ(out[0].relClass, 1);   // scanned -> relClass 1
        CHECK_EQ(out[0].person, &g_persons[1]);
        CHECK_EQ(out[0].eligibility, 1000 + 1);
    }
    SetPersonRelCtx(nullptr);
}

// ---------------------------------------------------------------------------
// CollectNearestCandidatesByDistance (0x555378).
// ---------------------------------------------------------------------------
TEST(CourtCouncil2Nearest, CountFavorBelowCutoff) {
    ResetPersons();
    PersonRelCtx ctx = MakeCtx();
    SetPersonRelCtx(&ctx);
    MakePerson(0, 100, 5, 0, 0);   // refRec
    PutDword(&g_persons[0], 4, 7777);
    // candidates with favor values keyed off marker via g_favorTable[marker].
    MakePerson(1, 1, 5, 0, 0);  g_favorTable[1] = 10.0f;  // <=33 counts
    MakePerson(2, 2, 5, 0, 0);  g_favorTable[2] = 33.0f;  // ==33 counts (<=)
    MakePerson(3, 3, 5, 0, 0);  g_favorTable[3] = 34.0f;  // >33 no count
    MakePerson(4, 4, 5, 0, 0);  g_favorTable[4] = 99.0f;
    PutDword(&g_persons[4], 524, 7777);                   // same faction -> counts
    int cnt = OfficeCollectNearestCandidatesByDistance(
        &g_persons[0], 0, 0, nullptr);
    CHECK_EQ(cnt, 3);   // slots 1,2 (favor<=33) + slot4 (same faction)
    SetPersonRelCtx(nullptr);
}

TEST(CourtCouncil2Nearest, FillKeepsLowestFavor) {
    ResetPersons();
    PersonRelCtx ctx = MakeCtx();
    SetPersonRelCtx(&ctx);
    MakePerson(0, 100, 5, 0, 0);
    PutDword(&g_persons[0], 4, 1);
    MakePerson(1, 1, 5, 0, 0);  g_favorTable[1] = 5.0f;
    MakePerson(2, 2, 5, 0, 0);  g_favorTable[2] = 20.0f;
    MakePerson(3, 3, 5, 0, 0);  g_favorTable[3] = 30.0f;
    // capacity 2 keeps the two lowest-favor candidates (5 and 20).
    PersonRelEntry out[2];
    std::memset(out, 0, sizeof out);
    int kept = OfficeCollectNearestCandidatesByDistance(&g_persons[0], 2, 0, out);
    CHECK_EQ(kept, 2);
    if (kept == 2) {
        // front-most slot is the lowest score (5), then 20.
        CHECK_EQ(out[0].relClass, 5);
        CHECK_EQ(out[1].relClass, 20);
        CHECK(out[0].person == &g_persons[1]);
        CHECK(out[1].person == &g_persons[2]);
        CHECK_EQ(out[0].eligibility, 1000 + 1);
    }
    SetPersonRelCtx(nullptr);
}

// ---------------------------------------------------------------------------
// ApplyCandidateRatingBars (0x556ba0).
// ---------------------------------------------------------------------------
namespace {
int g_lastBarWidget = -999, g_lastBarValue = -999;
char SetBarHook(int widget, int value) {
    g_lastBarWidget = widget; g_lastBarValue = value;
    return static_cast<char>(value & 0x7f);
}
} // namespace

TEST(CourtCouncil2RatingBars, BothHoldersFlat100) {
    ResetPersons();
    PersonRelCtx ctx = MakeCtx();
    SetPersonRelCtx(&ctx);
    CourtCouncilHooks h; h.setBarValue = &SetBarHook;
    SetCourtCouncilHooks(&h);

    MakePerson(0, 100, 6, 0, 0);   // refPerson kind 6 (holder)
    MakePerson(1, 1,   7, 0, 0);   // candidate kind 7 (holder)

    RatingSlot slots[1];
    std::memset(slots, 0, sizeof slots);
    slots[0].person = &g_persons[1];
    slots[0].barWidget = 42;
    g_lastBarValue = -1;
    OfficeApplyCandidateRatingBars(&g_persons[0], 1, slots);
    CHECK_EQ(g_lastBarWidget, 42);
    CHECK_EQ(g_lastBarValue, 100);   // both holders -> flat 100

    SetCourtCouncilHooks(nullptr);
    SetPersonRelCtx(nullptr);
}

TEST(CourtCouncil2RatingBars, NonHolderUsesFavor) {
    ResetPersons();
    PersonRelCtx ctx = MakeCtx();
    SetPersonRelCtx(&ctx);
    CourtCouncilHooks h; h.setBarValue = &SetBarHook;
    SetCourtCouncilHooks(&h);

    MakePerson(0, 100, 5, 0, 0);   // refPerson kind 5 (not holder)
    MakePerson(1, 2,   5, 0, 0);   // candidate kind 5
    g_favorTable[2] = 73.0f;       // truncated -> 73

    RatingSlot slots[1];
    std::memset(slots, 0, sizeof slots);
    slots[0].person = &g_persons[1];
    slots[0].barWidget = 7;
    OfficeApplyCandidateRatingBars(&g_persons[0], 1, slots);
    CHECK_EQ(g_lastBarValue, 73);

    SetCourtCouncilHooks(nullptr);
    SetPersonRelCtx(nullptr);
}

TEST(CourtCouncil2RatingBars, SkipsDeadOrNoWidget) {
    ResetPersons();
    CourtCouncilHooks h; h.setBarValue = &SetBarHook;
    SetCourtCouncilHooks(&h);
    MakePerson(0, 100, 5, 0, 0);
    // candidate with no widget (-1) is skipped.
    RatingSlot slots[1];
    std::memset(slots, 0, sizeof slots);
    slots[0].person = &g_persons[0];
    slots[0].barWidget = -1;
    g_lastBarValue = -55;
    OfficeApplyCandidateRatingBars(&g_persons[0], 1, slots);
    CHECK_EQ(g_lastBarValue, -55);   // hook never called
    SetCourtCouncilHooks(nullptr);
}

// ---------------------------------------------------------------------------
// EvaluateCourtTrial (0x4747dc).
// ---------------------------------------------------------------------------
namespace {
// A simple in-memory building model the court hooks read from.
struct TestBuilding { bool alive; std::uint16_t owner; std::uint8_t defKind; std::uint8_t sec; };
std::vector<TestBuilding> g_buildings;
int g_aiNeedsMask = 0;          // bitmask of categories that participate
float g_catRating[7] = {0};
int g_rng4 = 0;

int SlotCount() { return static_cast<int>(g_buildings.size()); }
bool SlotFetch(int i, CourtBuildingSlot* out) {
    if (i < 0 || i >= (int)g_buildings.size()) return false;
    out->alive = g_buildings[i].alive;
    out->ownerWord = g_buildings[i].owner;
    out->defKind = g_buildings[i].defKind;
    out->defSecurity = g_buildings[i].sec;
    return true;
}
int AiNeeds(int cat) { return (g_aiNeedsMask >> cat) & 1; }
float CatRating(int cat) { return (cat >= 0 && cat < 7) ? g_catRating[cat] : 1.0f; }
int Eligible1(Person*) { return 1; }
int RngHook() { return g_rng4; }

CourtCouncilHooks MakeCourtHooks() {
    CourtCouncilHooks h;
    h.buildingSlotCount = &SlotCount;
    h.buildingSlot = &SlotFetch;
    h.aiNeedsComputeWeights = &AiNeeds;
    h.categoryRating = &CatRating;
    h.guildEligibility = &Eligible1;
    h.randomMod4 = &RngHook;
    return h;
}
} // namespace

TEST(CourtCouncil2Trial, IneligibleReturnsZero) {
    CourtCouncilHooks h = MakeCourtHooks();
    h.guildEligibility = [](Person*) { return 0; };   // not eligible
    SetCourtCouncilHooks(&h);
    CourtVerdict v{};
    Person accused{}; accused.marker = 5;
    CHECK_EQ(EvaluateCourtTrial(0, &accused, &v, 0), 0);
    CHECK(!v.issued);
    SetCourtCouncilHooks(nullptr);
}

TEST(CourtCouncil2Trial, NonzeroA1Returns0) {
    CourtCouncilHooks h = MakeCourtHooks();
    SetCourtCouncilHooks(&h);
    CourtVerdict v{};
    Person accused{}; accused.marker = 5;
    CHECK_EQ(EvaluateCourtTrial(1, &accused, &v, 0), 0);   // a1 != 0
    CHECK_EQ(EvaluateCourtTrial(0, &accused, &v, 1), 0);   // a4 != 0
    SetCourtCouncilHooks(nullptr);
}

TEST(CourtCouncil2Trial, NoCategoriesReturns0) {
    g_buildings.clear();
    g_aiNeedsMask = 0;
    CourtCouncilHooks h = MakeCourtHooks();
    SetCourtCouncilHooks(&h);
    CourtVerdict v{};
    Person accused{}; accused.marker = 5;
    CHECK_EQ(EvaluateCourtTrial(0, &accused, &v, 0), 0);
    CHECK(!v.issued);
    SetCourtCouncilHooks(nullptr);
}

// Golden trace (hand-derived from the FSM):
//   cat c0 (defKind 18 -> category 1): total=10, support=8, rating=0.5
//   cat c1 (defKind 20 -> category 2): total=4,  support=2, rating=2.0
//   score[c0]=(10+1-8)/10*0.5*(16-8)/15 = 0.08
//   score[c1]=(4+1-2)/4*2.0*(16-2)/15  = 1.4
//   v24 (worst, set-first then never re-picked) = 0; v28 (best) = 1.
//   first block skipped (rating[0]<=1 and support[0]!=0); second block taken
//   (2*support[1]+1=5 >= total[1]=4); rating[0]<=1 && support[0] -> v34=+1.
//   verdict: issued, category = v24+1 = 1, direction = +1, return 54.
TEST(CourtCouncil2Trial, GuiltyVerdictGoldenTrace) {
    g_buildings.clear();
    // accused owner == accused.marker (the +39 owner word matches the accused).
    const std::uint16_t accusedId = 5;
    // c0 (cat 1, defKind 18): 10 buildings sec 1; 8 owned by accused.
    for (int i = 0; i < 10; ++i)
        g_buildings.push_back({true, std::uint16_t(i < 8 ? accusedId : 9999), 18, 1});
    // c1 (cat 2, defKind 20): 4 buildings sec 1; 2 owned by accused.
    for (int i = 0; i < 4; ++i)
        g_buildings.push_back({true, std::uint16_t(i < 2 ? accusedId : 9999), 20, 1});

    g_aiNeedsMask = (1 << 0) | (1 << 1);   // categories c0,c1 participate
    g_catRating[0] = 0.5f;
    g_catRating[1] = 2.0f;
    g_rng4 = 0;

    CourtCouncilHooks h = MakeCourtHooks();
    SetCourtCouncilHooks(&h);
    CourtVerdict v{};
    Person accused{}; accused.marker = static_cast<std::int16_t>(accusedId);
    char r = EvaluateCourtTrial(0, &accused, &v, 0);
    CHECK_EQ(r, 54);
    CHECK(v.issued);
    CHECK_EQ(v.category, 1);     // v24 + 1
    CHECK_EQ(v.direction, 1);    // guilty
    SetCourtCouncilHooks(nullptr);
}

// Second golden trace exercising the v24-reselection quirk and the second
// decision block. Categories swapped vs the guilty trace:
//   c0 (cat 1): total=4,  support=2, rating=2.0  -> score 1.4
//   c1 (cat 2): total=10, support=8, rating=0.5  -> score 0.08
//   v24: set to c0 first, then (score[0]=1.4 > score[1]=0.08) re-sets v24=1
//        (the original literally assigns v24=1, not the loop index).
//   v28: stays 0 (score[0]=1.4 is the max).  v24=1, v28=0, distinct.
//   first block skipped (rating[v24=1]=0.5 not >1, support[1]=8 != 0).
//   second block: rating[v28=0]=2.0 >=1 but 2*support[0]+1 = 5 >= total[0]=4
//        -> entered; rating[v24=1]=0.5 <=1 && support[1]!=0 -> v34=+1 -> emit.
//   verdict: issued, category = v24+1 = 2, direction = +1.
TEST(CourtCouncil2Trial, SecondCategoryVerdictGoldenTrace) {
    g_buildings.clear();
    const std::uint16_t accusedId = 5;
    for (int i = 0; i < 4; ++i)
        g_buildings.push_back({true, std::uint16_t(i < 2 ? accusedId : 9999), 18, 1});
    for (int i = 0; i < 10; ++i)
        g_buildings.push_back({true, std::uint16_t(i < 8 ? accusedId : 9999), 20, 1});

    g_aiNeedsMask = (1 << 0) | (1 << 1);
    g_catRating[0] = 2.0f;
    g_catRating[1] = 0.5f;
    g_rng4 = 0;

    CourtCouncilHooks h = MakeCourtHooks();
    SetCourtCouncilHooks(&h);
    CourtVerdict v{};
    Person accused{}; accused.marker = static_cast<std::int16_t>(accusedId);
    char r = EvaluateCourtTrial(0, &accused, &v, 0);
    CHECK_EQ(r, 54);
    CHECK(v.issued);
    CHECK_EQ(v.category, 2);     // v24 (re-selected to 1) + 1
    CHECK_EQ(v.direction, 1);
    SetCourtCouncilHooks(nullptr);
}
