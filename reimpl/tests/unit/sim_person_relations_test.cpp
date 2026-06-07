// Unit golden-vector tests for the person social-relation collectors
// (sim/person_relations.cpp). Each test installs a PersonRelCtx with inline leaf
// stubs and asserts the record-walk / classification / scoring logic against
// hand-computed golden values.
#include "test.h"

#include "sim/person_relations.h"
#include "sim/entity.h"

#include <cstring>

using namespace guild::sim;
using guild::u8;
using guild::u16;
using guild::u32;
using guild::i16;
using guild::i32;

namespace {

// Reset the person array to all-free and clear the id column.
void ResetPersons() {
    for (int i = 0; i < kPersonCapacity; ++i) {
        std::memset(&g_persons[i], 0, sizeof(Person));
        g_persons[i].marker = -1;
        g_personIds[i] = 0;
    }
}

// Set a person record's raw byte/word/dword fields.
void PB(Person* p, int off, u8 v)  { reinterpret_cast<u8*>(p)[off] = v; }
void PW(Person* p, int off, i16 v) { std::memcpy(reinterpret_cast<u8*>(p) + off, &v, 2); }
void PD(Person* p, int off, i32 v) { std::memcpy(reinterpret_cast<u8*>(p) + off, &v, 4); }

// Make slot `idx` a live, kind-<10 person with id and marker.
Person* MakePerson(int idx, i32 id, u8 kind = 4) {
    Person* p = &g_persons[idx];
    std::memset(p, 0, sizeof(Person));
    p->marker = static_cast<i16>(idx);   // marker == slot index
    PB(p, 2, kind);                      // kind byte
    PD(p, 4, id);                        // id
    g_personIds[idx] = id;
    return p;
}

// --- shared leaf stubs (no He handlers, no building output) ----------------
int g_eligRef = -1, g_eligCand = -1, g_eligFilter = -1, g_eligCalls = 0;
int EligStub(u16 ref, u16 cand, int filter) {
    g_eligRef = ref; g_eligCand = cand; g_eligFilter = filter; ++g_eligCalls;
    return 1000 + cand;  // distinctive, lets us check it landed in +48
}

float g_favorByMarker[768];
float FavorStub(int /*a*/, int b, int /*mode*/) {
    return (b >= 0 && b < 768) ? g_favorByMarker[b] : 0.0f;
}

PersonRelCtx MakeCtx() {
    PersonRelCtx c;
    c.currentPlayerIndex = 0;
    c.evaluateEligibility = &EligStub;
    return c;
}

} // namespace

// ===========================================================================
// ResolveStatusFlags: flagA from person+12, flagD bucket from building output.
// ===========================================================================
TEST(SimPersonRel, ResolveStatusFlagsOutputBuckets) {
    ResetPersons();
    Person* p = MakePerson(5, 0x100);
    PB(p, 12, 0);     // person+12 == 0  -> flagA = 1597
    PB(p, 458, 0);    // +458 sign byte >= 0 -> skip filter-111 block

    float output = 0.0f;
    PersonRelCtx c = MakeCtx();
    c.buildingCurrentOutput = [](Person*) -> float { return 0.0f; };

    // We can't capture; use a file-scope output via a small struct of stubs.
    struct Stub { static float lowOut(Person*)  { return 50.0f; }
                  static float midOut(Person*)  { return 200.0f; }
                  static float hiMid(Person*)   { return 400.0f; }
                  static float hiOut(Person*)   { return 700.0f; } };

    PersonRelEntry e{};
    e.person = p;

    c.buildingCurrentOutput = &Stub::lowOut;
    SetPersonRelCtx(&c);
    CHECK_EQ(PersonResolveStatusFlags(&e), 1);
    CHECK_EQ(e.flagA, 1597);            // (p+12==0)+1596
    CHECK_EQ(e.flagB, 0);
    CHECK_EQ(e.flagC, 0);
    CHECK_EQ(e.flagD, 1605);           // output < 100

    c.buildingCurrentOutput = &Stub::midOut;  // 200 -> [100,350) -> 1604
    SetPersonRelCtx(&c);
    e = PersonRelEntry{}; e.person = p;
    PersonResolveStatusFlags(&e);
    CHECK_EQ(e.flagD, 1604);

    c.buildingCurrentOutput = &Stub::hiMid;   // 400 -> [350,650) -> 1603
    SetPersonRelCtx(&c);
    e = PersonRelEntry{}; e.person = p;
    PersonResolveStatusFlags(&e);
    CHECK_EQ(e.flagD, 1603);

    c.buildingCurrentOutput = &Stub::hiOut;   // 700 -> >=650 -> 1602
    SetPersonRelCtx(&c);
    e = PersonRelEntry{}; e.person = p;
    PersonResolveStatusFlags(&e);
    CHECK_EQ(e.flagD, 1602);

    (void)output;
    SetPersonRelCtx(nullptr);
}

TEST(SimPersonRel, ResolveStatusFlagsCountMatchingEntities) {
    ResetPersons();
    Person* p = MakePerson(3, 0x77);
    PB(p, 12, 1);     // -> flagA = 1596
    PersonRelCtx c = MakeCtx();
    c.heCountMatchingEntities = [](i32) -> int { return 2; };  // >0 -> flagC=1600
    struct S { static float out(Person*) { return 0.0f; } };
    c.buildingCurrentOutput = &S::out;
    SetPersonRelCtx(&c);

    PersonRelEntry e{}; e.person = p;
    CHECK_EQ(PersonResolveStatusFlags(&e), 1);
    CHECK_EQ(e.flagA, 1596);
    CHECK_EQ(e.flagC, 1600);
    CHECK_EQ(e.flagD, 1605);  // building output 0 -> <100

    e.person = nullptr;       // null person -> returns 0
    CHECK_EQ(PersonResolveStatusFlags(&e), 0);
    SetPersonRelCtx(nullptr);
}

// ===========================================================================
// CollectRelatedNpcs: relation-array classification + count-only mode.
// ===========================================================================
TEST(SimPersonRel, CollectRelatedNpcsRelationClasses) {
    ResetPersons();
    Person* ref = MakePerson(0, 0x10);
    // relation[0]=id 0x11 (slot1), relation[3]=id 0x14 (slot2 married), rest -1.
    for (int r = 0; r < 8; ++r) PD(ref, 92 + 4 * r, -1);
    Person* a = MakePerson(1, 0x11);     // relation[0] -> class (married?)+4
    PB(a, 9, 0);                          // unmarried -> 4
    Person* b = MakePerson(2, 0x14);     // relation[3] -> (married?)+6
    PB(b, 9, 1);                          // married -> 7
    PD(ref, 92 + 0, 0x11);
    PD(ref, 92 + 12, 0x14);

    PersonRelCtx c = MakeCtx();
    SetPersonRelCtx(&c);

    PersonRelEntry out[8];
    g_eligCalls = 0;
    int n = PersonCollectRelatedNpcs(ref, 8, /*filter=*/55, out);
    CHECK_EQ(n, 2);
    // entry order follows array slot order: slot1 (a) then slot2 (b).
    CHECK_EQ(out[0].person, a);
    CHECK_EQ(out[0].relClass, 4);
    CHECK_EQ(out[1].person, b);
    CHECK_EQ(out[1].relClass, 7);
    // eligibility stamped into +48 for each filled entry.
    CHECK_EQ(out[0].eligibility, 1000 + 1);   // cand marker == slot 1
    CHECK_EQ(out[1].eligibility, 1000 + 2);
    CHECK_EQ(g_eligCalls, 2);
    CHECK_EQ(g_eligFilter, 55);

    // count-only mode returns the same count without writing.
    int cnt = PersonCollectRelatedNpcs(ref, 0, 55, nullptr);
    CHECK_EQ(cnt, 2);
    SetPersonRelCtx(nullptr);
    (void)b;
}

TEST(SimPersonRel, CollectRelatedNpcsHouseholdFallback) {
    ResetPersons();
    Person* ref = MakePerson(0, 0x20);
    for (int r = 0; r < 8; ++r) PD(ref, 92 + 4 * r, -1);
    PW(ref, 80, 7);    // household word
    PB(ref, 88, 3);    // class byte
    // candidate not in relation array but shares household + class.
    Person* h = MakePerson(4, 0x21);
    PW(h, 80, 7);
    PB(h, 88, 3);
    PB(h, 9, 1);       // married -> class (married?)+2 == 3

    PersonRelCtx c = MakeCtx();
    SetPersonRelCtx(&c);
    PersonRelEntry out[4];
    int n = PersonCollectRelatedNpcs(ref, 4, 0, out);
    CHECK_EQ(n, 1);
    CHECK_EQ(out[0].person, h);
    CHECK_EQ(out[0].relClass, 3);
    SetPersonRelCtx(nullptr);
}

// ===========================================================================
// CollectTopByScore: favorability cutoff (75.0), descending sort, exclusion.
// ===========================================================================
TEST(SimPersonRel, CollectTopByScoreSortAndCutoff) {
    ResetPersons();
    for (int i = 0; i < 768; ++i) g_favorByMarker[i] = 0.0f;
    Person* ref = MakePerson(0, 0x30);

    // Three kind-2..7 candidates with favor 80/90/74; the 74 is below cutoff.
    Person* p1 = MakePerson(1, 0x31, /*kind=*/3);  g_favorByMarker[1] = 80.0f;
    Person* p2 = MakePerson(2, 0x32, /*kind=*/5);  g_favorByMarker[2] = 90.0f;
    Person* p3 = MakePerson(3, 0x33, /*kind=*/2);  g_favorByMarker[3] = 74.0f;
    // cand+524 (relation/employer id) defaults to 0 != refId -> not excluded.

    PersonRelCtx c = MakeCtx();
    c.personFavorability = &FavorStub;
    SetPersonRelCtx(&c);

    PersonRelEntry out[4];
    int n = PersonCollectTopByScore(ref, 4, /*filter=*/9, out);
    CHECK_EQ(n, 2);                       // p3 below 75.0 cutoff excluded
    // sorted descending by score: p2 (90) then p1 (80).
    CHECK_EQ(out[0].person, p2);
    CHECK_EQ(out[0].relClass, 90);
    CHECK_EQ(out[1].person, p1);
    CHECK_EQ(out[1].relClass, 80);

    // count-only path.
    int cnt = PersonCollectTopByScore(ref, 0, 9, nullptr);
    CHECK_EQ(cnt, 2);
    SetPersonRelCtx(nullptr);
    (void)p3;
}

TEST(SimPersonRel, CollectTopByScoreExcludesRelationId) {
    ResetPersons();
    for (int i = 0; i < 768; ++i) g_favorByMarker[i] = 0.0f;
    Person* ref = MakePerson(0, 0x40);
    Person* p1 = MakePerson(1, 0x41, 4);  g_favorByMarker[1] = 95.0f;
    PD(p1, 524, 0x40);   // cand+524 == refId -> excluded

    PersonRelCtx c = MakeCtx();
    c.personFavorability = &FavorStub;
    SetPersonRelCtx(&c);
    int cnt = PersonCollectTopByScore(ref, 0, 9, nullptr);
    CHECK_EQ(cnt, 0);
    SetPersonRelCtx(nullptr);
    (void)p1;
}
