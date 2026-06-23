// End-to-end flow across world/court_council2: build a population, tally the
// office categories, collect candidates for an open office, rate them, then run
// a court trial against the standing officeholder. Exercises the candidate
// scanners, the rating bars, and the verdict scorer together.
#include "test.h"

#include "world/court_council2.h"
#include "world/office.h"
#include "sim/entity.h"
#include "sim/types.h"
#include "sim/person_relations.h"

#include <cstring>
#include <cstdint>
#include <vector>

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
void ResetPersons() {
    for (int i = 0; i < kPersonCapacity; ++i) {
        std::memset(&g_persons[i], 0, sizeof(Person));
        g_persons[i].marker = -1;
    }
}
void MakePerson(int slot, std::int16_t marker, std::uint8_t kind, std::uint8_t office360) {
    Person* p = &g_persons[slot];
    std::memset(p, 0, sizeof(Person));
    p->marker = marker;
    PutByte(p, 2, kind);
    PutByte(p, 358, 1);
    PutByte(p, 359, office360);
    PutByte(p, 360, office360);
    PutDword(p, 4, marker);
}

float g_favor[64] = {0};
float FavorHook(int /*a*/, int b, int /*mode*/) {
    return (b >= 0 && b < 64) ? g_favor[b] : 99.0f;
}
int EligibleHook(std::uint16_t /*ref*/, std::uint16_t cand, int /*filter*/) {
    return cand < 50 ? 1 : 0;   // first 50 markers eligible
}

int g_barCalls = 0; int g_barSum = 0;
char SetBarHook(int /*widget*/, int value) { ++g_barCalls; g_barSum += value; return 1; }

// Court building model.
struct TB { bool alive; std::uint16_t owner; std::uint8_t kind; std::uint8_t sec; };
std::vector<TB> g_b;
int g_mask = 0; float g_rating[7] = {0};
int BC() { return (int)g_b.size(); }
bool BS(int i, CourtBuildingSlot* o) {
    if (i < 0 || i >= (int)g_b.size()) return false;
    o->alive = g_b[i].alive; o->ownerWord = g_b[i].owner;
    o->defKind = g_b[i].kind; o->defSecurity = g_b[i].sec; return true;
}
int AN(int c) { return (g_mask >> c) & 1; }
float CR(int c) { return (c >= 0 && c < 7) ? g_rating[c] : 1.0f; }
int EL1(Person*) { return 1; }
int RNG() { return 0; }

} // namespace

TEST(CourtCouncil2E2E, ElectionThenTrialFlow) {
    ResetPersons();
    PersonRelCtx ctx;
    ctx.personFavorability = &FavorHook;
    ctx.evaluateEligibility = &EligibleHook;
    SetPersonRelCtx(&ctx);

    // Population: a reference officeholder (slot 0, office band 9) plus several
    // same-band candidates and some outsiders.
    MakePerson(0, 100, 6, 9);                 // reference holder, kind 6
    MakePerson(1, 1,   5, 9);  g_favor[1] = 12.0f;
    MakePerson(2, 2,   5, 9);  g_favor[2] = 25.0f;
    MakePerson(3, 3,   5, 9);  g_favor[3] = 40.0f;   // above 33 cutoff
    MakePerson(4, 4,   5, 7);  g_favor[4] = 88.0f;    // different band, far favor

    // 1) Tally categories — at least the reference's band is represented.
    //    (0x47fdfc returns the module global histogram; the tally lands there.)
    int* hist = OfficeTallyCategoryCounts();
    int totalTallied = 0;
    for (int b = 0; b < 40; ++b) totalTallied += hist[b];
    CHECK(totalTallied >= 1);

    // 2) Collect same-office successor candidates (capacity 0 -> count).
    std::uint32_t succCount = OfficeCollectGuildSuccessorCandidates(
        &g_persons[0], 0, /*filter*/0, nullptr, nullptr, 0);
    CHECK_EQ(succCount, 3u);   // slots 1,2,3 share band 9

    // 3) Collect the nearest (lowest-favor) candidates, capacity 2.
    PersonRelEntry near[2];
    std::memset(near, 0, sizeof near);
    int kept = OfficeCollectNearestCandidatesByDistance(&g_persons[0], 2, 0, near);
    CHECK_EQ(kept, 2);
    if (kept == 2) {
        CHECK(near[0].person == &g_persons[1]);   // favor 12 lowest
        CHECK(near[1].person == &g_persons[2]);   // favor 25 next
        CHECK_EQ(near[0].relClass, 12);
    }

    // 4) Rate the two kept candidates against the reference holder.
    CourtCouncilHooks bars; bars.setBarValue = &SetBarHook;
    SetCourtCouncilHooks(&bars);
    RatingSlot slots[2];
    std::memset(slots, 0, sizeof slots);
    slots[0].person = near[0].person; slots[0].barWidget = 10;
    slots[1].person = near[1].person; slots[1].barWidget = 11;
    g_barCalls = 0; g_barSum = 0;
    OfficeApplyCandidateRatingBars(&g_persons[0], 2, slots);
    // reference is kind 6, candidates kind 5 -> favorability path (12 and 25).
    CHECK_EQ(g_barCalls, 2);
    CHECK_EQ(g_barSum, 12 + 25);

    // 5) Run a court trial against the standing holder (guilty trace).
    g_b.clear();
    const std::uint16_t accusedId = 5;
    for (int i = 0; i < 10; ++i)
        g_b.push_back({true, std::uint16_t(i < 8 ? accusedId : 999), 18, 1}); // cat1
    for (int i = 0; i < 4; ++i)
        g_b.push_back({true, std::uint16_t(i < 2 ? accusedId : 999), 20, 1}); // cat2
    g_mask = 0x3; g_rating[0] = 0.5f; g_rating[1] = 2.0f;
    CourtCouncilHooks ch;
    ch.buildingSlotCount = &BC; ch.buildingSlot = &BS;
    ch.aiNeedsComputeWeights = &AN; ch.categoryRating = &CR;
    ch.guildEligibility = &EL1; ch.randomMod4 = &RNG;
    SetCourtCouncilHooks(&ch);

    CourtVerdict verdict{};
    Person accused{}; accused.marker = static_cast<std::int16_t>(accusedId);
    char r = EvaluateCourtTrial(0, &accused, &verdict, 0);
    CHECK_EQ(r, 54);
    CHECK(verdict.issued);
    CHECK_EQ(verdict.category, 1);
    CHECK_EQ(verdict.direction, 1);

    SetCourtCouncilHooks(nullptr);
    SetPersonRelCtx(nullptr);
}
