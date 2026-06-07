// Integration tests: the marriage / family-formation collectors driven against the
// REAL sibling modules — the He handler pool (handler_entry.cpp) and the person
// id->record lookup (entity.cpp / PersonFindRecordById). The He finders are bridged
// to a live HandlerTable so the filter-71/24/44 branches walk real handler records
// allocated through the real Alloc path (desc+88..+112 -> record +172..+196).
#include "test.h"

#include "sim/marriage.h"
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

// Set a descriptor dword (desc offset). desc+88+k lands at record+172+k.
void DescDword(HeRecord& d, int off, i32 v) {
    std::memcpy(reinterpret_cast<u8*>(&d) + off, &v, 4);
}

// Allocate a filter-71 spouse handler keyed (k172, k176): record +172/+176.
HandlerRecord* AllocSpouse(HandlerTable& t, i32 k172, i32 k176) {
    HeRecord d; std::memset(&d, 0, sizeof d);
    reinterpret_cast<u8*>(&d)[4] = 71;             // kind
    DescDword(d, 8, -1);                           // personId -1 (no resolve)
    DescDword(d, 88, k172);                         // -> record +172
    DescDword(d, 92, k176);                         // -> record +176
    return t.AllocHandlerEntry(&d);
}
// Allocate a filter-24 business-partner handler with partner id @ record +196.
HandlerRecord* AllocPartner(HandlerTable& t, i32 partnerId) {
    HeRecord d; std::memset(&d, 0, sizeof d);
    reinterpret_cast<u8*>(&d)[4] = 24;
    DescDword(d, 8, -1);
    DescDword(d, 112, partnerId);                   // desc+112 -> record +196
    return t.AllocHandlerEntry(&d);
}
// Allocate a filter-44 relatives handler with 6 relative ids + a date @ record+197.
HandlerRecord* AllocRel(HandlerTable& t, std::initializer_list<i32> rels, i32 date) {
    HeRecord d; std::memset(&d, 0, sizeof d);
    reinterpret_cast<u8*>(&d)[4] = 44;
    DescDword(d, 8, -1);
    int i = 0;
    for (i32 r : rels) {                            // desc+88,+92,... -> record+172,+176,...
        if (i >= 6) break;
        DescDword(d, 88 + 4 * i, r);
        ++i;
    }
    // record +197 = desc +(197-172+88) = desc+113 (unaligned). Write the date there.
    std::memcpy(reinterpret_cast<u8*>(&d) + 113, &date, sizeof date);
    return t.AllocHandlerEntry(&d);
}

int EligPassthrough(u16 /*ref*/, u16 cand, int /*filter*/) { return 9000 + cand; }

} // namespace

// ===========================================================================
// Spouse + business candidates via the real He pool (filter 71 + 24).
// ===========================================================================
TEST(SimMarriageI, SpouseAndPartnersViaRealHePool) {
    ResetPersons();
    HandlerTable table; table.Init();
    table.RegisterHandlerByType(71, &NoopInit, &NoopRun);
    table.RegisterHandlerByType(24, &NoopInit, &NoopRun);
    g_table = &table;

    Person* ref    = MakePerson(0, 0x700);
    PB(ref, 457, 0x04);                       // married bit
    Person* spouse = MakePerson(1, 0x701);
    Person* pA     = MakePerson(2, 0x702);
    Person* pB     = MakePerson(3, 0x703);

    AllocSpouse(table, /*k172=*/0x700, /*k176=*/0x701);  // ref -> spouse
    AllocPartner(table, 0x702);
    AllocPartner(table, 0x703);

    PersonRelCtx c;
    c.currentPlayerIndex = 0;
    c.heFindFirst = &BridgeFindFirst;
    c.heFindNext  = &BridgeFindNext;
    c.evaluateEligibility = &EligPassthrough;
    SetPersonRelCtx(&c);

    CHECK_EQ(OfficeCollectSpouseAndBusinessCandidates(ref, 0, 0, nullptr), 3);

    PersonRelEntry out[8];
    std::memset(out, 0, sizeof out);
    int n = OfficeCollectSpouseAndBusinessCandidates(ref, 8, 7, out);
    CHECK_EQ(n, 3);
    CHECK_EQ(out[0].person, spouse);
    CHECK_EQ(out[0].relClass, 0);
    CHECK_EQ(out[1].person, pA);
    CHECK_EQ(out[2].person, pB);
    CHECK_EQ(out[1].relClass, 1);
    CHECK_EQ(out[0].eligibility, 9000 + 1);   // spouse marker == slot 1

    SetPersonRelCtx(nullptr);
    g_table = nullptr;
}

// ===========================================================================
// Relative candidates via the real He pool (filter 44), most-recent handler wins.
// ===========================================================================
TEST(SimMarriageI, RelativeCandidatesViaRealHePool) {
    ResetPersons();
    HandlerTable table; table.Init();
    table.RegisterHandlerByType(44, &NoopInit, &NoopRun);
    g_table = &table;

    Person* ref = MakePerson(0, 0x800);
    Person* r1  = MakePerson(1, 0x801);
    Person* r2  = MakePerson(2, 0x802);
    Person* r3  = MakePerson(3, 0x803);
    Person* r4  = MakePerson(4, 0x804);
    Person* r5  = MakePerson(5, 0x805);

    // Two qualifying handlers (dates 10 and 5). The COUNT pass walks both cleanly
    // (no ResolveStatusFlags in the count path) -> 6 + 6 = 12. The FILL pass fills
    // from the FIRST qualifying handler only: PersonResolveStatusFlags' own He
    // FindFirst clobbers the single global cursor (dword_1229260), ending the outer
    // filter-44 walk after the first fill — faithful to the original.
    AllocRel(table, {0x800, 0x801, 0x802, 0x803, 0x804, 0x805}, /*date=*/10);
    AllocRel(table, {0x800, 0x805, 0x804, 0x803, 0x802, 0x801}, /*date=*/5);

    PersonRelCtx c;
    c.currentPlayerIndex = 0;
    c.gameDateLow = 0;
    c.heFindFirst = &BridgeFindFirst;
    c.heFindNext  = &BridgeFindNext;
    c.evaluateEligibility = &EligPassthrough;
    SetPersonRelCtx(&c);

    // count pass: both handlers include ref and fully resolve -> 6 + 6 = 12.
    CHECK_EQ(OfficeCollectRelativeCandidates(ref, 0, 0, nullptr), 12);

    PersonRelEntry out[8];
    std::memset(out, 0, sizeof out);
    int n = OfficeCollectRelativeCandidates(ref, 8, 3, out);
    CHECK_EQ(n, 6);
    // First handler (date 10): ref, r1, r2, r3, r4, r5 (id order +172..+192).
    CHECK_EQ(out[0].person, ref);
    CHECK_EQ(out[1].person, r1);
    CHECK_EQ(out[2].person, r2);
    CHECK_EQ(out[5].person, r5);
    CHECK_EQ(out[1].relClass, 1);
    CHECK_EQ(out[5].relClass, 4);
    (void)r1; (void)r2; (void)r3; (void)r4; (void)r5;

    SetPersonRelCtx(nullptr);
    g_table = nullptr;
}

// ===========================================================================
// CountAdultChildren resolves children through the REAL PersonFindRecordById scan.
// ===========================================================================
TEST(SimMarriageI, CountAdultChildrenViaRealLookup) {
    ResetPersons();
    Person* parent = MakePerson(0, 0xA00);
    Person* c1 = MakePerson(7, 0xA01);  PB(c1, 8, 1); PW(c1, 10, 40);  // adult
    Person* c2 = MakePerson(9, 0xA02);  PB(c2, 8, 1); PW(c2, 10, 3);   // minor
    (void)c1; (void)c2;
    PD(parent, 104, 0xA01);
    PD(parent, 108, 0xA02);
    PD(parent, 112, -1);
    PD(parent, 116, -1);
    PD(parent, 120, -1);

    i32 count = 0;
    int adult = PersonCountAdultChildren(parent, &count);
    CHECK_EQ(count, 2);
    CHECK_EQ(adult, 1);
}
