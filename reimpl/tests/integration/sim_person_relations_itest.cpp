// Integration tests: person_relations collectors driven against the REAL sibling
// modules — the He handler pool (handler_entry.cpp) and the person id->record
// lookup (entity.cpp / PersonFindRecordById). The He finders are bridged to a
// live HandlerTable so OfficeCollectFamilyHeirCandidates and the filter-111/65
// branches walk real handler records.
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

// A live HandlerTable bridged to the PersonRelCtx He finders. We populate a few
// records by hand at the +172 (owner id key) and +176 (related person id) fields
// using the same offsets person_relations reads (HeMatchKey172 / HeMatchRel176).
HandlerTable* g_table = nullptr;

HeRecord* BridgeFindFirst(int kind) {
    // The collectors only use selector-0 (kind) filter; mirror the original's
    // FindFirstHandlerByFilter(1, 0, kind) call.
    return reinterpret_cast<HeRecord*>(g_table->FindFirstHandlerByFilter(1, 0, kind));
}
HeRecord* BridgeFindNext() {
    return reinterpret_cast<HeRecord*>(g_table->FindNextMatchingHandler());
}

void NoopInit(HandlerRecord*) {}
i32  NoopRun(HandlerRecord*) { return 0; }

// Allocate a real handler entry (bumps high-water through the real Alloc path),
// then stamp the owner key @+172 and related id @+176. Descriptor +88/+92 land at
// record +172/+176 via Alloc's qmemcpy(record+172, desc+88, 0xA0). personId=-1
// skips person resolution.
HandlerRecord* AllocHandler(HandlerTable& t, u8 kind, i32 ownerKey, i32 relId) {
    HeRecord desc;
    std::memset(&desc, 0, sizeof desc);
    u8* d = reinterpret_cast<u8*>(&desc);
    d[4] = kind;                                          // descriptor kind
    *reinterpret_cast<i32*>(d + 8) = -1;                  // personId -1 (no resolve)
    *reinterpret_cast<i32*>(d + 88) = ownerKey;           // -> record +172
    *reinterpret_cast<i32*>(d + 92) = relId;              // -> record +176
    return t.AllocHandlerEntry(&desc);
}

int EligPassthrough(u16 /*ref*/, u16 cand, int /*filter*/) { return 7000 + cand; }

} // namespace

// ===========================================================================
// OfficeCollectFamilyHeirCandidates against the real He pool + FindRecordById.
// ===========================================================================
TEST(SimPersonRelI, HeirCandidatesViaRealHePool) {
    ResetPersons();
    HandlerTable table;
    table.Init();
    table.RegisterHandlerByType(111, &NoopInit, &NoopRun);
    g_table = &table;

    Person* ref = MakePerson(0, 0x500);
    // Two heirs resolvable by id; one handler whose owner key != ref id (ignored).
    Person* h1 = MakePerson(1, 0x501);
    Person* h2 = MakePerson(2, 0x502);
    MakePerson(3, 0x503);  // exists but no matching handler

    // Allocate filter-111 (kind 111) handlers via the real Alloc path.
    AllocHandler(table, 111, /*owner=*/0x500, /*rel=*/0x501);
    AllocHandler(table, 111, /*owner=*/0x999, /*rel=*/0x503); // owner mismatch
    AllocHandler(table, 111, /*owner=*/0x500, /*rel=*/0x502);

    PersonRelCtx c;
    c.currentPlayerIndex = 0;
    c.heFindFirst = &BridgeFindFirst;
    c.heFindNext  = &BridgeFindNext;
    c.evaluateEligibility = &EligPassthrough;
    SetPersonRelCtx(&c);

    // count-only mode.
    u32 found = OfficeCollectFamilyHeirCandidates(ref, 0, 0, nullptr);
    CHECK_EQ(found, 2u);

    // fill mode.
    PersonRelEntry out[4];
    u32 f2 = OfficeCollectFamilyHeirCandidates(ref, 4, /*filter=*/12, out);
    CHECK_EQ(f2, 2u);
    CHECK_EQ(out[0].person, h1);
    CHECK_EQ(out[1].person, h2);
    // eligibility stamped from the real candidate marker (slots 1 and 2).
    CHECK_EQ(out[0].eligibility, 7000 + 1);
    CHECK_EQ(out[1].eligibility, 7000 + 2);

    SetPersonRelCtx(nullptr);
    g_table = nullptr;
}

// ===========================================================================
// CollectRelatedNpcs filter-65 extra-relative branch via the real He pool.
// ===========================================================================
TEST(SimPersonRelI, RelatedNpcsExtraViaFilter65) {
    ResetPersons();
    HandlerTable table;
    table.Init();
    table.RegisterHandlerByType(65, &NoopInit, &NoopRun);
    g_table = &table;

    Person* ref = MakePerson(0, 0x600);
    for (int r = 0; r < 8; ++r) PD(ref, 92 + 4 * r, -1);
    PB(ref, 88, 9);   // distinct class so no household fallback matches
    // The extra related person resolved from a filter-65 handler. Give it a
    // distinct household/class too so ONLY the filter-65 branch can collect it.
    Person* extra = MakePerson(5, 0x605);
    PB(extra, 88, 3);
    AllocHandler(table, 65, /*owner=*/0x600, /*rel=*/0x605);

    PersonRelCtx c;
    c.currentPlayerIndex = 0;
    c.heFindFirst = &BridgeFindFirst;
    c.heFindNext  = &BridgeFindNext;
    c.evaluateEligibility = &EligPassthrough;
    SetPersonRelCtx(&c);

    PersonRelEntry out[4];
    int n = PersonCollectRelatedNpcs(ref, 4, 0, out);
    CHECK_EQ(n, 1);
    CHECK_EQ(out[0].person, extra);
    // class == (extra+9 != 0) + 8 == 8 (unmarried).
    CHECK_EQ(out[0].relClass, 8);

    SetPersonRelCtx(nullptr);
    g_table = nullptr;
}
