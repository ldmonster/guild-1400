// End-to-end flow: a small "household + office succession" scenario exercised
// through all four person_relations collectors with a single PersonRelCtx that
// wires the He pool (handler_entry), the person id lookup (entity), the building
// output bucket, and the AI favorability + eligibility leaves together — the same
// composition the office-succession / family UI pass drives in the shipping game.
#include "test.h"

#include "sim/person_relations.h"
#include "sim/entity.h"
#include "sim/handler_entry.h"

#include <cstring>

using namespace guild::sim;
using guild::u8;
using guild::u16;
using guild::u32;
using guild::i16;
using guild::i32;

namespace {

void ResetPersons() {
    for (int i = 0; i < kPersonCapacity; ++i) {
        std::memset(&g_persons[i], 0, sizeof(Person));
        g_persons[i].marker = -1;
        g_personIds[i] = 0;
    }
}
void PB(Person* p, int off, u8 v)  { reinterpret_cast<u8*>(p)[off] = v; }
void PW(Person* p, int off, i16 v) { std::memcpy(reinterpret_cast<u8*>(p) + off, &v, 2); }
void PD(Person* p, int off, i32 v) { std::memcpy(reinterpret_cast<u8*>(p) + off, &v, 4); }

Person* MakePerson(int idx, i32 id, u8 kind = 4) {
    Person* p = &g_persons[idx];
    std::memset(p, 0, sizeof(Person));
    p->marker = static_cast<i16>(idx);
    PB(p, 2, kind);
    PD(p, 4, id);
    g_personIds[idx] = id;
    return p;
}

HandlerTable* g_table = nullptr;
HeRecord* BridgeFindFirst(int kind) {
    return reinterpret_cast<HeRecord*>(g_table->FindFirstHandlerByFilter(1, 0, kind));
}
HeRecord* BridgeFindNext() {
    return reinterpret_cast<HeRecord*>(g_table->FindNextMatchingHandler());
}
void NoopInit(HandlerRecord*) {}
i32  NoopRun(HandlerRecord*) { return 0; }
HandlerRecord* AllocHandler(HandlerTable& t, u8 kind, i32 ownerKey, i32 relId) {
    HeRecord desc; std::memset(&desc, 0, sizeof desc);
    u8* d = reinterpret_cast<u8*>(&desc);
    d[4] = kind;
    *reinterpret_cast<i32*>(d + 8) = -1;
    *reinterpret_cast<i32*>(d + 88) = ownerKey;
    *reinterpret_cast<i32*>(d + 92) = relId;
    return t.AllocHandlerEntry(&desc);
}

float g_favor[768];
float FavorByMarker(int /*a*/, int b, int /*mode*/) {
    return (b >= 0 && b < 768) ? g_favor[b] : 0.0f;
}
float BuildOut(Person* p) {
    // building output keyed by the person's slot index (marker).
    return 100.0f * static_cast<float>(static_cast<u16>(p->marker));
}
int Elig(u16 /*ref*/, u16 cand, int filter) { return filter * 100 + cand; }

} // namespace

TEST(SimPersonRelE2E, HouseholdAndSuccessionFlow) {
    ResetPersons();
    for (int i = 0; i < 768; ++i) g_favor[i] = 0.0f;

    HandlerTable table;
    table.Init();
    table.RegisterHandlerByType(111, &NoopInit, &NoopRun); // heir handlers
    table.RegisterHandlerByType(65,  &NoopInit, &NoopRun); // extra-relative handler
    g_table = &table;

    // --- scenario ----------------------------------------------------------
    // ref (the office holder), id 0x1000, household 12, class 5.
    Person* ref = MakePerson(0, 0x1000);
    for (int r = 0; r < 8; ++r) PD(ref, 92 + 4 * r, -1);
    PW(ref, 80, 12);
    PB(ref, 88, 5);

    // spouse: relation[0] (class (married)+4 == 5), shares nothing else needed.
    Person* spouse = MakePerson(1, 0x1001);
    PB(spouse, 9, 1);          // married
    PD(ref, 92 + 0, 0x1001);

    // child living in the same household (fallback class (unmarried)+2 == 2).
    Person* child = MakePerson(2, 0x1002);
    PW(child, 80, 12);
    PB(child, 88, 5);
    PB(child, 9, 0);

    // a high-favor ally not related (favor 88) and a friend (favor 80).
    Person* ally   = MakePerson(3, 0x1003, /*kind=*/6);  g_favor[3] = 88.0f;
    Person* friendP = MakePerson(4, 0x1004, /*kind=*/2); g_favor[4] = 80.0f;
    // a low-favor stranger (favor 50, below the 75 cutoff).
    Person* stranger = MakePerson(5, 0x1005, /*kind=*/3); g_favor[5] = 50.0f;

    // heir handlers (filter 111) tying two heirs to ref.
    Person* heir1 = MakePerson(6, 0x1006);
    Person* heir2 = MakePerson(7, 0x1007);
    AllocHandler(table, 111, 0x1000, 0x1006);
    AllocHandler(table, 111, 0x1000, 0x1007);

    PersonRelCtx c;
    c.currentPlayerIndex   = 0;
    c.heFindFirst          = &BridgeFindFirst;
    c.heFindNext           = &BridgeFindNext;
    c.heCountMatchingEntities = [](i32) -> int { return 0; };
    c.buildingCurrentOutput = &BuildOut;
    c.personFavorability    = &FavorByMarker;
    c.evaluateEligibility   = &Elig;
    SetPersonRelCtx(&c);

    // --- 1) related NPCs (relation array + household fallback) --------------
    PersonRelEntry related[16];
    int nRel = PersonCollectRelatedNpcs(ref, 16, /*filter=*/3, related);
    CHECK_EQ(nRel, 2);                       // spouse (relation[0]) + child (household)
    CHECK_EQ(related[0].person, spouse);
    CHECK_EQ(related[0].relClass, 5);        // (married)+4
    CHECK_EQ(related[1].person, child);
    CHECK_EQ(related[1].relClass, 2);        // (unmarried)+2 household fallback
    // status flags + eligibility were resolved on each entry.
    CHECK_EQ(related[0].eligibility, 3 * 100 + 1);  // filter*100 + marker(1)
    CHECK_EQ(related[1].eligibility, 3 * 100 + 2);
    // flagD bucket from BuildOut: spouse marker 1 -> 100 -> [100,350) -> 1604.
    CHECK_EQ(related[0].flagD, 1604);

    // --- 2) top-by-score (favorability) ------------------------------------
    PersonRelEntry top[8];
    int nTop = PersonCollectTopByScore(ref, 8, /*filter=*/4, top);
    CHECK_EQ(nTop, 2);                        // ally(88) + friend(80); stranger<75
    CHECK_EQ(top[0].person, ally);           // sorted descending
    CHECK_EQ(top[0].relClass, 88);
    CHECK_EQ(top[1].person, friendP);
    CHECK_EQ(top[1].relClass, 80);

    // --- 3) heir candidates (He filter-111) --------------------------------
    PersonRelEntry heirs[4];
    u32 nHeir = OfficeCollectFamilyHeirCandidates(ref, 4, /*filter=*/9, heirs);
    CHECK_EQ(nHeir, 2u);
    CHECK_EQ(heirs[0].person, heir1);
    CHECK_EQ(heirs[1].person, heir2);
    CHECK_EQ(heirs[0].eligibility, 9 * 100 + 6);  // marker 6
    CHECK_EQ(heirs[1].eligibility, 9 * 100 + 7);

    // --- 4) count-only consistency -----------------------------------------
    CHECK_EQ(PersonCollectRelatedNpcs(ref, 0, 3, nullptr), nRel);
    CHECK_EQ(PersonCollectTopByScore(ref, 0, 4, nullptr), nTop);
    CHECK_EQ(OfficeCollectFamilyHeirCandidates(ref, 0, 9, nullptr), nHeir);

    SetPersonRelCtx(nullptr);
    g_table = nullptr;
    (void)stranger;
}
