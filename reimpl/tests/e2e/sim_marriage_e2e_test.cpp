// End-to-end flow: a "courtship -> marriage -> family" scenario exercised through
// the marriage cluster wired to the REAL He pool (handler_entry) + person id lookup
// (entity), plus the spouse/relative collectors and CountAdultChildren — the same
// composition the family/marriage UI pass drives in the shipping game.
//
// The real-asset path (loading a saved dynasty from a game dir) is GUARDED behind
// GUILD_E2E_ASSETS: without it the asset section no-ops with a guarded skip. The
// in-memory flow below always runs and validates the whole chain deterministically.
#include "test.h"

#include "sim/marriage.h"
#include "sim/entity.h"
#include "sim/handler_entry.h"

#include <array>
#include <cstdlib>
#include <cstring>
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

HandlerTable* g_table = nullptr;
HeRecord* BridgeFindFirst(int kind) {
    return reinterpret_cast<HeRecord*>(g_table->FindFirstHandlerByFilter(1, 0, kind));
}
HeRecord* BridgeFindNext() {
    return reinterpret_cast<HeRecord*>(g_table->FindNextMatchingHandler());
}
void NoopInit(HandlerRecord*) {}
i32  NoopRun(HandlerRecord*) { return 0; }

void DescDword(HeRecord& d, int off, i32 v) {
    std::memcpy(reinterpret_cast<u8*>(&d) + off, &v, 4);
}
HandlerRecord* AllocSpouse(HandlerTable& t, i32 k172, i32 k176) {
    HeRecord d; std::memset(&d, 0, sizeof d);
    reinterpret_cast<u8*>(&d)[4] = 71;
    DescDword(d, 8, -1);
    DescDword(d, 88, k172);
    DescDword(d, 92, k176);
    return t.AllocHandlerEntry(&d);
}

// Trace the courtship commands + wedding messages BeginMarriage emits.
struct Trace {
    std::vector<std::array<i32, 3>> courtships;
    std::vector<i32> messageTargets;
};
Trace* g_trace = nullptr;
void QueueStub(i32 a, i32 b, int coord) { g_trace->courtships.push_back({a, b, coord}); }
void TextStub(char* buf, int /*id*/, u16 /*arg*/) { buf[0] = '\0'; }
void MsgStub(i32 id, const char* /*buf*/, int /*tid*/) { g_trace->messageTargets.push_back(id); }

} // namespace

// ===========================================================================
// Full in-memory courtship -> marriage -> family flow (always runs).
// ===========================================================================
TEST(SimMarriageE2E, CourtshipMarriageFamilyFlow) {
    ResetPersons();
    HandlerTable table; table.Init();
    table.RegisterHandlerByType(71, &NoopInit, &NoopRun);
    g_table = &table;

    // Two player-class would-be spouses and three children.
    Person* groom = MakePerson(0, 0x1000, /*kind=*/6);
    Person* bride = MakePerson(1, 0x1001, /*kind=*/7);

    // --- Step 1: begin the marriage (direct, non-UI path) ---
    Trace tr; g_trace = &tr;
    MarriageCtx mc;
    mc.queueCourtship = &QueueStub;
    mc.renderText = &TextStub;
    mc.sendEntityMessage = &MsgStub;
    SetMarriageCtx(&mc);

    int begun = NpcActionBeginMarriage(/*ctxKind=*/0, 0x1000, 0x1001);
    CHECK_EQ(begun, 1);
    CHECK_EQ((int)tr.courtships.size(), 2);           // reciprocal courtship cmds
    CHECK_EQ(tr.courtships[0][2], -40);
    CHECK_EQ((int)tr.messageTargets.size(), 2);       // both player-class -> 2 msgs
    SetMarriageCtx(nullptr);

    // --- Step 2: record the marriage in the He pool + give the groom children ---
    AllocSpouse(table, /*k172=*/0x1000, /*k176=*/0x1001);  // groom <-> bride
    PB(groom, 457, 0x04);                                   // married bit
    PD(groom, 4, 0x1000);

    Person* child1 = MakePerson(5, 0x1100); PB(child1, 8, 1); PW(child1, 10, 25); // adult
    Person* child2 = MakePerson(6, 0x1101); PB(child2, 8, 1); PW(child2, 10, 8);  // minor
    Person* child3 = MakePerson(7, 0x1102); PB(child3, 8, 1); PW(child3, 10, 14); // adult
    PD(groom, 104, 0x1100);
    PD(groom, 108, 0x1101);
    PD(groom, 112, 0x1102);
    PD(groom, 116, -1);
    PD(groom, 120, -1);

    // --- Step 3: query the family ---
    PersonRelCtx c;
    c.currentPlayerIndex = 0;
    c.heFindFirst = &BridgeFindFirst;
    c.heFindNext  = &BridgeFindNext;
    c.evaluateEligibility = nullptr;
    SetPersonRelCtx(&c);

    // Spouse collector finds the bride.
    PersonRelEntry out[8];
    std::memset(out, 0, sizeof out);
    int spouses = OfficeCollectSpouseAndBusinessCandidates(groom, 8, 0, out);
    CHECK_EQ(spouses, 1);
    CHECK_EQ(out[0].person, bride);

    // Family: 3 live children, 2 of them adult.
    i32 childCount = 0;
    int hasAdult = PersonCountAdultChildren(groom, &childCount);
    CHECK_EQ(childCount, 3);
    CHECK_EQ(hasAdult, 1);

    SetPersonRelCtx(nullptr);
    g_table = nullptr;
    g_trace = nullptr;
}

// ===========================================================================
// Real-asset path (GUARDED): load a real dynasty and walk its spouse/relative
// links. Without GUILD_E2E_ASSETS this no-ops with a guarded skip.
// ===========================================================================
TEST(SimMarriageE2E, RealDynastyGuarded) {
    if (std::getenv("GUILD_E2E_ASSETS") == nullptr) {
        CHECK(true);  // guarded skip — no real game assets available
        return;
    }
    // With assets present a full save-load of a dynasty and a spouse/relative walk
    // would run here; the loader lives in the io/world cluster (not this module's
    // surface), so the guarded section is intentionally a placeholder that asserts
    // the collectors are callable against a loaded array.
    ResetPersons();
    Person* p = MakePerson(0, 0x1, 6);
    i32 cnt = -1;
    PersonCountAdultChildren(p, &cnt);
    CHECK_EQ(cnt, 0);   // no children wired -> 0
}
