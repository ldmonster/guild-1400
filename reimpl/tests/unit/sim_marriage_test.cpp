// Unit golden-vector tests for the marriage / courtship / family-formation cluster
// (sim/marriage.cpp). Each test installs a PersonRelCtx (He pool finders +
// eligibility) and/or a MarriageCtx with inline leaf stubs and asserts the
// record-walk / counting / spouse-selection / command-emission logic against
// hand-computed golden values.
#include "test.h"

#include "sim/marriage.h"
#include "sim/entity.h"

#include <array>
#include <cstring>
#include <initializer_list>
#include <vector>

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

// --- a tiny in-test He handler pool routed through PersonRelCtx --------------
struct FakeHe {
    int kind;
    HeRecord rec;
};
std::vector<FakeHe>* g_pool = nullptr;
int g_iterKind = -1;
size_t g_iterPos = 0;

HeRecord* PoolFindNext() {
    if (!g_pool) return nullptr;
    while (g_iterPos < g_pool->size()) {
        FakeHe& f = (*g_pool)[g_iterPos++];
        if (f.kind == g_iterKind)
            return &f.rec;
    }
    return nullptr;
}
HeRecord* PoolFindFirst(int kind) {
    g_iterKind = kind;
    g_iterPos = 0;
    return PoolFindNext();
}

// Build a relatives (filter-44) handler with up to 6 relative ids + a date.
FakeHe MakeRelHandler(i32 handlerId, std::initializer_list<i32> relIds, i32 date) {
    FakeHe f{};
    f.kind = 44;
    std::memset(&f.rec, 0, sizeof f.rec);
    f.rec.id = handlerId;
    static const int offs[6] = {172, 176, 180, 184, 188, 192};
    int i = 0;
    for (i32 id : relIds) {
        if (i >= 6) break;
        *reinterpret_cast<i32*>(reinterpret_cast<u8*>(&f.rec) + offs[i]) = id;
        ++i;
    }
    std::memcpy(reinterpret_cast<u8*>(&f.rec) + 197, &date, sizeof date);
    return f;
}

// Build a filter-71 (spouse) handler keyed (k172, k176).
FakeHe MakeSpouseHandler(i32 k172, i32 k176) {
    FakeHe f{};
    f.kind = 71;
    std::memset(&f.rec, 0, sizeof f.rec);
    *reinterpret_cast<i32*>(reinterpret_cast<u8*>(&f.rec) + 172) = k172;
    *reinterpret_cast<i32*>(reinterpret_cast<u8*>(&f.rec) + 176) = k176;
    return f;
}

// Build a filter-24 (business partner) handler with partner id @ +196.
FakeHe MakePartnerHandler(i32 partnerId) {
    FakeHe f{};
    f.kind = 24;
    std::memset(&f.rec, 0, sizeof f.rec);
    *reinterpret_cast<i32*>(reinterpret_cast<u8*>(&f.rec) + 196) = partnerId;
    return f;
}

int EligStub(u16 /*ref*/, u16 cand, int /*filter*/) { return 5000 + cand; }

PersonRelCtx MakeCtx(std::vector<FakeHe>& pool, i32 gameDate = 0) {
    g_pool = &pool;
    PersonRelCtx c;
    c.currentPlayerIndex = 0;
    c.gameDateLow = gameDate;
    c.heFindFirst = &PoolFindFirst;
    c.heFindNext  = &PoolFindNext;
    c.evaluateEligibility = &EligStub;
    return c;
}

} // namespace

// ===========================================================================
// CountAdultChildren: counts live-actor children; adult flag iff any age > 11.
// ===========================================================================
TEST(SimMarriage, CountAdultChildrenBasic) {
    ResetPersons();
    Person* parent = MakePerson(0, 0x100);

    // 3 children resolvable; one is not a live actor (skip), one is a toddler.
    Person* c1 = MakePerson(1, 0x101);  PB(c1, 8, 1); PW(c1, 10, 20);  // adult (>11)
    Person* c2 = MakePerson(2, 0x102);  PB(c2, 8, 1); PW(c2, 10, 5);   // child (<=11)
    Person* c3 = MakePerson(3, 0x103);  PB(c3, 8, 0); PW(c3, 10, 30);  // not live actor
    // child id array at parent+104..+120 (5 slots; last two empty/-1).
    PD(parent, 104, 0x101);
    PD(parent, 108, 0x102);
    PD(parent, 112, 0x103);
    PD(parent, 116, -1);
    PD(parent, 120, -1);

    i32 count = -77;
    int adult = PersonCountAdultChildren(parent, &count);
    // c1 + c2 are live actors -> count 2; c3 not live -> skipped. c1 adult -> flag 1.
    CHECK_EQ(count, 2);
    CHECK_EQ(adult, 1);

    // No adult: make c1 a minor too.
    PW(c1, 10, 11);   // 11 is NOT > 11
    adult = PersonCountAdultChildren(parent, &count);
    CHECK_EQ(count, 2);
    CHECK_EQ(adult, 0);

    // null outCount must not crash; still returns the flag.
    PW(c1, 10, 99);
    CHECK_EQ(PersonCountAdultChildren(parent, nullptr), 1);
}

// ===========================================================================
// CollectSpouseAndBusinessCandidates: spouse (filter-71) + partners (filter-24).
// ===========================================================================
TEST(SimMarriage, SpouseAndBusinessCount) {
    ResetPersons();
    Person* ref = MakePerson(0, 0x200);
    PB(ref, 457, 0x04);            // married bit set
    MakePerson(1, 0x201);          // spouse
    MakePerson(2, 0x202);          // partner A
    MakePerson(3, 0x203);          // partner B

    std::vector<FakeHe> pool;
    pool.push_back(MakeSpouseHandler(/*k172=*/0x200, /*k176=*/0x201)); // ref->spouse
    pool.push_back(MakePartnerHandler(0x202));
    pool.push_back(MakePartnerHandler(0x203));
    PersonRelCtx c = MakeCtx(pool);
    SetPersonRelCtx(&c);

    int n = OfficeCollectSpouseAndBusinessCandidates(ref, 0, 0, nullptr);
    // spouse (1) + two partners (2) = 3.
    CHECK_EQ(n, 3);

    // Not married: spouse skipped, only the 2 partners count.
    PB(ref, 457, 0x00);
    CHECK_EQ(OfficeCollectSpouseAndBusinessCandidates(ref, 0, 0, nullptr), 2);

    SetPersonRelCtx(nullptr);
    g_pool = nullptr;
}

TEST(SimMarriage, SpouseAndBusinessFill) {
    ResetPersons();
    Person* ref    = MakePerson(0, 0x200);
    PB(ref, 457, 0x04);
    Person* spouse = MakePerson(1, 0x201);
    Person* pA     = MakePerson(2, 0x202);
    Person* pB     = MakePerson(3, 0x203);

    std::vector<FakeHe> pool;
    // Spouse handler keyed in the reverse direction (k176 == ref id) to exercise
    // the other branch.
    pool.push_back(MakeSpouseHandler(/*k172=*/0x201, /*k176=*/0x200));
    pool.push_back(MakePartnerHandler(0x202));
    pool.push_back(MakePartnerHandler(0x203));
    PersonRelCtx c = MakeCtx(pool);
    SetPersonRelCtx(&c);

    PersonRelEntry out[8];
    std::memset(out, 0, sizeof out);
    int n = OfficeCollectSpouseAndBusinessCandidates(ref, 8, /*filter=*/0, out);
    CHECK_EQ(n, 3);
    CHECK_EQ(out[0].person, spouse);
    CHECK_EQ(out[0].relClass, 0);        // spouse -> class 0
    CHECK_EQ(out[1].person, pA);
    CHECK_EQ(out[1].relClass, 1);        // partner -> class 1
    CHECK_EQ(out[2].person, pB);
    CHECK_EQ(out[2].relClass, 1);
    // eligibility stamped from candidate markers (slots 1,2,3).
    CHECK_EQ(out[0].eligibility, 5000 + 1);
    CHECK_EQ(out[1].eligibility, 5000 + 2);
    CHECK_EQ(out[2].eligibility, 5000 + 3);

    // capacity clamp: only room for spouse + 1 partner.
    std::memset(out, 0, sizeof out);
    int n2 = OfficeCollectSpouseAndBusinessCandidates(ref, 2, 0, out);
    CHECK_EQ(n2, 2);
    CHECK_EQ(out[0].person, spouse);
    CHECK_EQ(out[1].person, pA);

    SetPersonRelCtx(nullptr);
    g_pool = nullptr;
}

// ===========================================================================
// CollectRelativeCandidates: count + most-recent-handler fill.
// ===========================================================================
TEST(SimMarriage, RelativeCandidatesCount) {
    ResetPersons();
    Person* ref = MakePerson(0, 0x300);
    MakePerson(1, 0x301);
    MakePerson(2, 0x302);
    MakePerson(3, 0x303);
    MakePerson(4, 0x304);
    MakePerson(5, 0x305);

    std::vector<FakeHe> pool;
    // A handler whose 6 relative ids include ref (0x300) -> contributes 6.
    pool.push_back(MakeRelHandler(0x900, {0x300, 0x301, 0x302, 0x303, 0x304, 0x305}, 10));
    // A handler that does NOT include ref -> contributes 0 even though all resolve.
    pool.push_back(MakeRelHandler(0x901, {0x301, 0x302, 0x303, 0x304, 0x305, 0x301}, 10));
    PersonRelCtx c = MakeCtx(pool, /*gameDate=*/0);
    SetPersonRelCtx(&c);

    int n = OfficeCollectRelativeCandidates(ref, 0, 0, nullptr);
    CHECK_EQ(n, 6);

    SetPersonRelCtx(nullptr);
    g_pool = nullptr;
}

TEST(SimMarriage, RelativeCandidatesFill) {
    ResetPersons();
    Person* ref = MakePerson(0, 0x300);
    Person* r1  = MakePerson(1, 0x301);
    Person* r2  = MakePerson(2, 0x302);
    Person* r3  = MakePerson(3, 0x303);
    Person* r4  = MakePerson(4, 0x304);
    Person* r5  = MakePerson(5, 0x305);

    // A single qualifying handler (date <= the running min 200). The 6 relatives
    // map to the class table {0,1,2,3,3,4}. (The "most-recent across handlers"
    // refill path the original codes is defeated at runtime by the single global He
    // cursor: PersonResolveStatusFlags' own FindFirst clobbers the outer filter-44
    // walk — faithful here too, since our stub's filter-111/24/69 scans return null
    // and reset the cursor. So a single handler is the meaningful golden vector.)
    std::vector<FakeHe> pool;
    pool.push_back(MakeRelHandler(0xAAA, {0x300, 0x301, 0x302, 0x303, 0x304, 0x305}, 10));
    PersonRelCtx c = MakeCtx(pool, /*gameDate=*/0);
    SetPersonRelCtx(&c);

    PersonRelEntry out[8];
    std::memset(out, 0, sizeof out);
    int n = OfficeCollectRelativeCandidates(ref, 8, /*filter=*/0, out);
    CHECK_EQ(n, 6);
    CHECK_EQ(out[0].person, ref);   // id #1 (+172) -> ref
    CHECK_EQ(out[1].person, r1);
    CHECK_EQ(out[2].person, r2);
    CHECK_EQ(out[3].person, r3);
    CHECK_EQ(out[4].person, r4);
    CHECK_EQ(out[5].person, r5);
    // Class table {0,1,2,3,3,4}.
    CHECK_EQ(out[0].relClass, 0);
    CHECK_EQ(out[1].relClass, 1);
    CHECK_EQ(out[2].relClass, 2);
    CHECK_EQ(out[3].relClass, 3);
    CHECK_EQ(out[4].relClass, 3);
    CHECK_EQ(out[5].relClass, 4);
    (void)r1; (void)r2; (void)r3; (void)r4; (void)r5;

    // capacity clamp: only 2 entries (4*capacity dword bound).
    std::memset(out, 0, sizeof out);
    int n2 = OfficeCollectRelativeCandidates(ref, 2, 0, out);
    CHECK_EQ(n2, 2);
    CHECK_EQ(out[0].person, ref);
    CHECK_EQ(out[1].person, r1);

    SetPersonRelCtx(nullptr);
    g_pool = nullptr;
}

// ===========================================================================
// BeginMarriage: direct (non-UI) path queues two reciprocal commands + messages.
// ===========================================================================
struct MarriageTrace {
    std::vector<std::array<i32, 3>> courtships;   // (idA, idB, coord)
    std::vector<std::array<i32, 2>> messages;     // (id, textId)
    std::vector<int> texts;                       // text ids rendered
};
MarriageTrace* g_trace = nullptr;

void QueueCourtshipStub(i32 a, i32 b, int coord) {
    g_trace->courtships.push_back({a, b, coord});
}
void RenderTextStub(char* buf, int textId, u16 /*arg*/) {
    g_trace->texts.push_back(textId);
    buf[0] = '\0';
}
void SendMessageStub(i32 id, const char* /*buf*/, int textId) {
    g_trace->messages.push_back({id, textId});
}

TEST(SimMarriage, BeginMarriageDirectBothPlayers) {
    ResetPersons();
    // Both partners are player-class (kind 6) -> both get a wedding message.
    Person* a = MakePerson(10, 0x401, /*kind=*/6);
    Person* b = MakePerson(11, 0x402, /*kind=*/7);
    (void)a; (void)b;

    MarriageTrace tr;
    g_trace = &tr;
    MarriageCtx mc;
    mc.queueCourtship = &QueueCourtshipStub;
    mc.renderText = &RenderTextStub;
    mc.sendEntityMessage = &SendMessageStub;
    SetMarriageCtx(&mc);

    int r = NpcActionBeginMarriage(/*ctxKind=*/0, 0x401, 0x402);
    CHECK_EQ(r, 1);
    // Two reciprocal courtship commands, coord -40.
    CHECK_EQ((int)tr.courtships.size(), 2);
    CHECK_EQ(tr.courtships[0][0], 0x401);  // A->B
    CHECK_EQ(tr.courtships[0][1], 0x402);
    CHECK_EQ(tr.courtships[0][2], -40);
    CHECK_EQ(tr.courtships[1][0], 0x402);  // B->A
    CHECK_EQ(tr.courtships[1][1], 0x401);
    // Both partners (kind 6 and 7) receive a message.
    CHECK_EQ((int)tr.messages.size(), 2);
    CHECK_EQ(tr.messages[0][0], 0x401);
    CHECK_EQ(tr.messages[0][1], 1418);
    CHECK_EQ(tr.messages[1][0], 0x402);

    SetMarriageCtx(nullptr);
    g_trace = nullptr;
}

TEST(SimMarriage, BeginMarriageUnresolvedReturnsZero) {
    ResetPersons();
    MakePerson(10, 0x401, 6);   // A exists, B does not
    MarriageTrace tr;
    g_trace = &tr;
    MarriageCtx mc;
    mc.queueCourtship = &QueueCourtshipStub;
    mc.renderText = &RenderTextStub;
    mc.sendEntityMessage = &SendMessageStub;
    SetMarriageCtx(&mc);

    int r = NpcActionBeginMarriage(0, 0x401, 0xDEAD);
    CHECK_EQ(r, 0);
    CHECK_EQ((int)tr.courtships.size(), 0);  // nothing queued

    SetMarriageCtx(nullptr);
    g_trace = nullptr;
}

TEST(SimMarriage, BeginMarriageNonPlayerNoMessage) {
    ResetPersons();
    // Neither partner is a player-class person -> no wedding messages, but the
    // courtship commands still fire.
    MakePerson(10, 0x401, /*kind=*/4);
    MakePerson(11, 0x402, /*kind=*/4);
    MarriageTrace tr;
    g_trace = &tr;
    MarriageCtx mc;
    mc.queueCourtship = &QueueCourtshipStub;
    mc.renderText = &RenderTextStub;
    mc.sendEntityMessage = &SendMessageStub;
    SetMarriageCtx(&mc);

    int r = NpcActionBeginMarriage(0, 0x401, 0x402);
    CHECK_EQ(r, 1);
    CHECK_EQ((int)tr.courtships.size(), 2);
    CHECK_EQ((int)tr.messages.size(), 0);

    SetMarriageCtx(nullptr);
    g_trace = nullptr;
}
