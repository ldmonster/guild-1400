#include "tests/framework/test.h"
#include "sim/command_recon4_resolve.h"

#include <cstring>
#include <vector>

using namespace guild;
using namespace guild::sim;

// ---------------------------------------------------------------------------
// Synthetic 768-slot selection table + leaf stubs driving the resolvers.
// ---------------------------------------------------------------------------
namespace {

struct Table {
    Recon4Record slot[768];
    std::vector<i32> selected;   // entity ids that ARE in the selection list
    std::vector<u16> randomSeq;  // queued RandomModulo returns
    size_t randIdx = 0;
    u16 lastRenderWord = 0xFFFF;
    int renderCount = 0;
    // wealth by slot index
    i32 wealth[768];
};

Table* g_t = nullptr;

bool ReadRecord(int i, Recon4Record* out) {
    if (i < 0 || i >= 768) return false;
    *out = g_t->slot[i];
    return true;
}
i32 ComputeWealth(u16 i) { return (i < 768) ? g_t->wealth[i] : -1; }
int IsNotInSel(i32 id) {
    for (i32 s : g_t->selected) if (s == id) return 0;
    return 1;
}
int Rand(u16 n) {
    u16 v = (g_t->randIdx < g_t->randomSeq.size()) ? g_t->randomSeq[g_t->randIdx++] : 0;
    return (n ? v % n : 0);
}
void ParseTokens1Mode0(const char*, int* tc, i32* v) { *tc = 1; *v = 0; } // mode 0
void ParseTokens1Mode1(const char*, int* tc, i32* v) { *tc = 1; *v = 1; } // mode 1
void ParseTokensNoTok(const char*, int* tc, i32* v) { *tc = 0; *v = 0; }  // -> mode 1
void Render(char*, const char*, u16 w) { g_t->lastRenderWord = w; g_t->renderCount++; }

void install(void (*parse)(const char*, int*, i32*)) {
    Recon4ResolveHooks h{};
    h.readRecord = &ReadRecord;
    h.computeTotalWealth = &ComputeWealth;
    h.isNotInSelectionList = &IsNotInSel;
    h.randomModulo = &Rand;
    h.parseTokens = parse;
    h.renderMessage = &Render;
    // findRecordById / recordFlag / recordTypeWord left default (kind==1 paths).
    SetRecon4ResolveHooks(&h);
}

void clearTable(Table& t) {
    for (int i = 0; i < 768; ++i) {
        t.slot[i] = Recon4Record{};
        t.slot[i].typeWord = -1; // empty by default
        t.wealth[i] = -1;
    }
    t.selected.clear();
    t.randomSeq.clear();
    t.randIdx = 0;
    t.lastRenderWord = 0xFFFF;
    t.renderCount = 0;
}

// Make slot i a live person with given fields.
void mkPerson(Table& t, int i, i16 word, i32 entId, u8 kind = 1) {
    t.slot[i].typeWord = word;
    t.slot[i].entityId = entId;
    t.slot[i].alive = 1;
    t.slot[i].kind = kind;
}

} // namespace

// ---------------------------------------------------------------------------
// Clergy: rank prof79 in [0x1E,0x21], mode 0 active people, random start.
// ---------------------------------------------------------------------------
TEST(CommandRecon4, ClergyMode0PicksRankInRange) {
    Table t; clearTable(t); g_t = &t;
    // slot 10: clergy rank 0x20 (valid). slot 5: rank 0x10 (out of range).
    mkPerson(t, 5, 500, 5000); t.slot[5].prof79 = 0x10;
    mkPerson(t, 10, 510, 5100); t.slot[10].prof79 = 0x20;
    t.randomSeq = {0}; // start scan at 0
    install(&ParseTokens1Mode0);

    u8 params[64] = {0};
    int rc = ResolveTargetClergy(/*kind*/0, params, "x", /*index*/1, nullptr);
    CHECK_EQ(rc, 1);
    CHECK_EQ((int)t.lastRenderWord, 510);   // picked the in-range clergy slot
    i32 written; std::memcpy(&written, params + 8 * 1 + 4, 4);
    CHECK_EQ(written, 5100);                 // back-wrote the chosen entity id
}

TEST(CommandRecon4, ClergyNoCandidateReturnsZero) {
    Table t; clearTable(t); g_t = &t;
    mkPerson(t, 3, 300, 3000); t.slot[3].prof79 = 0x05; // out of range
    t.randomSeq = {0};
    install(&ParseTokens1Mode0);
    u8 params[64] = {0};
    CHECK_EQ(ResolveTargetClergy(0, params, "x", 1, nullptr), 0);
    CHECK_EQ(t.renderCount, 0);
}

// ---------------------------------------------------------------------------
// PersonByName mode 0: flag9==1 + role byte ok + not-in-selection.
// ---------------------------------------------------------------------------
TEST(CommandRecon4, ByNameRequiresFlag9AndRole) {
    Table t; clearTable(t); g_t = &t;
    // slot 2: flag9=0 (rejected by ByName). slot 7: flag9=1, role=3 (ok).
    mkPerson(t, 2, 200, 2000); t.slot[2].flag9 = 0; t.slot[2].prof76 = 3;
    mkPerson(t, 7, 207, 2070); t.slot[7].flag9 = 1; t.slot[7].prof76 = 3;
    // slot 9: flag9=1 but role==15 (rejected).
    mkPerson(t, 9, 209, 2090); t.slot[9].flag9 = 1; t.slot[9].prof76 = 15;
    t.randomSeq = {0};
    install(&ParseTokens1Mode0);
    u8 params[64] = {0};
    int rc = ResolveTargetPersonByName(0, params, "x", 0, nullptr);
    CHECK_EQ(rc, 1);
    CHECK_EQ((int)t.lastRenderWord, 207);
}

TEST(CommandRecon4, ByNameSkipsSelected) {
    Table t; clearTable(t); g_t = &t;
    mkPerson(t, 4, 204, 2040); t.slot[4].flag9 = 1; t.slot[4].prof76 = 5;
    mkPerson(t, 8, 208, 2080); t.slot[8].flag9 = 1; t.slot[8].prof76 = 5;
    t.selected = {2040}; // slot 4 is selected -> must be skipped
    t.randomSeq = {0};
    install(&ParseTokens1Mode0);
    u8 params[64] = {0};
    CHECK_EQ(ResolveTargetPersonByName(0, params, "x", 0, nullptr), 1);
    CHECK_EQ((int)t.lastRenderWord, 208);
}

// Alt: identical but flag9 == 0.
TEST(CommandRecon4, AltRequiresFlag9Zero) {
    Table t; clearTable(t); g_t = &t;
    mkPerson(t, 6, 206, 2060); t.slot[6].flag9 = 1; t.slot[6].prof76 = 4; // rejected
    mkPerson(t, 11, 211, 2110); t.slot[11].flag9 = 0; t.slot[11].prof76 = 4; // ok
    t.randomSeq = {0};
    install(&ParseTokens1Mode0);
    u8 params[64] = {0};
    CHECK_EQ(ResolveTargetPersonAlt(0, params, "x", 0, nullptr), 1);
    CHECK_EQ((int)t.lastRenderWord, 211);
}

// Scoped: kind==2 rejects.
TEST(CommandRecon4, ScopedKind2Rejects) {
    Table t; clearTable(t); g_t = &t;
    install(&ParseTokens1Mode0);
    u8 params[64] = {0};
    CHECK_EQ(ResolveTargetPersonScoped(2, params, "x", 0, nullptr), 0);
}

// ---------------------------------------------------------------------------
// BestRated: linear scan, max wealth wins.
// ---------------------------------------------------------------------------
TEST(CommandRecon4, BestRatedPicksMaxWealth) {
    Table t; clearTable(t); g_t = &t;
    mkPerson(t, 1, 101, 1010); t.wealth[1] = 50;
    mkPerson(t, 2, 102, 1020); t.wealth[2] = 900;  // best
    mkPerson(t, 3, 103, 1030); t.wealth[3] = 300;
    install(&ParseTokens1Mode0); // tc=1,val=0 -> filter inactive, passes guards
    u8 params[64] = {0};
    int rc = ResolveTargetBestRated(0, params, "x", 2, nullptr);
    CHECK_EQ(rc, 1);
    CHECK_EQ((int)t.lastRenderWord, 102);
    i32 written; std::memcpy(&written, params + 8 * 2 + 4, 4);
    CHECK_EQ(written, 1020);
}

TEST(CommandRecon4, BestRatedRejectsBadTokenCount) {
    Table t; clearTable(t); g_t = &t;
    mkPerson(t, 1, 101, 1010); t.wealth[1] = 50;
    install(&ParseTokensNoTok); // tc=0 -> immediate 0
    u8 params[64] = {0};
    CHECK_EQ(ResolveTargetBestRated(0, params, "x", 2, nullptr), 0);
}

// ---------------------------------------------------------------------------
// CraftWorker: profession prof74 in [13,18].
// ---------------------------------------------------------------------------
TEST(CommandRecon4, CraftWorkerProfessionRange) {
    Table t; clearTable(t); g_t = &t;
    mkPerson(t, 4, 304, 3040); t.slot[4].prof74 = 12;  // below range
    mkPerson(t, 9, 309, 3090); t.slot[9].prof74 = 15;  // in range
    mkPerson(t, 12, 312, 3120); t.slot[12].prof74 = 19; // above range
    t.randomSeq = {0};
    install(&ParseTokens1Mode0);
    u8 params[64] = {0};
    CHECK_EQ(ResolveTargetCraftWorker(0, params, "x", 0, nullptr), 1);
    CHECK_EQ((int)t.lastRenderWord, 309);
}

// ---------------------------------------------------------------------------
// ByStatGroup: top-5 wealth pool, random pick from survivors.
// ---------------------------------------------------------------------------
TEST(CommandRecon4, ByStatGroupTop5RandomPick) {
    Table t; clearTable(t); g_t = &t;
    // 6 candidates with distinct wealth: top 5 by wealth are slots with
    // wealth {100,90,80,70,60}; slot with 10 should be dropped.
    int idx[6] = {1, 2, 3, 4, 5, 6};
    i32 w[6]   = {60, 100, 10, 90, 80, 70};
    i16 word[6]= {201, 202, 203, 204, 205, 206};
    for (int j = 0; j < 6; ++j) {
        mkPerson(t, idx[j], word[j], 2000 + idx[j]);
        t.wealth[idx[j]] = w[j];
    }
    // mode 1 (val=1) -> IsPersonType predicate. RandomModulo(5) -> pick index 0
    // which after the descending sort is the highest-wealth survivor (word 202).
    t.randomSeq = {0};
    install(&ParseTokens1Mode1);
    u8 params[64] = {0};
    int rc = ResolveTargetByStatGroup(0, params, "x", 1, nullptr);
    CHECK_EQ(rc, 1);
    CHECK_EQ((int)t.lastRenderWord, 202); // pick[0] == top of sorted pool
}

TEST(CommandRecon4, ByStatGroupKind2Rejects) {
    Table t; clearTable(t); g_t = &t;
    install(&ParseTokens1Mode1);
    u8 params[64] = {0};
    CHECK_EQ(ResolveTargetByStatGroup(2, params, "x", 0, nullptr), 0);
}

// ---------------------------------------------------------------------------
// ByProfessionRange: prof74 in [19,69] excluding 31-33,40-45,52-57.
// ---------------------------------------------------------------------------
TEST(CommandRecon4, ByProfessionRangeExclusions) {
    Table t; clearTable(t); g_t = &t;
    mkPerson(t, 1, 401, 4010); t.slot[1].prof74 = 32; t.wealth[1] = 500; // excluded 31-33
    mkPerson(t, 2, 402, 4020); t.slot[2].prof74 = 20; t.wealth[2] = 400; // included
    mkPerson(t, 3, 403, 4030); t.slot[3].prof74 = 42; t.wealth[3] = 600; // excluded 40-45
    t.randomSeq = {0};
    install(&ParseTokens1Mode0);
    u8 params[64] = {0};
    int rc = ResolveTargetByProfessionRange(0, params, "x", 0, nullptr);
    CHECK_EQ(rc, 1);
    CHECK_EQ((int)t.lastRenderWord, 402); // only the included profession survives
}

// ---------------------------------------------------------------------------
// WoundedPerson: status kind == 0, linear scan picks first.
// ---------------------------------------------------------------------------
TEST(CommandRecon4, WoundedPersonPicksFirstWounded) {
    Table t; clearTable(t); g_t = &t;
    mkPerson(t, 2, 502, 5020, /*kind*/3); // not wounded
    mkPerson(t, 5, 505, 5050, /*kind*/0); // wounded -> first match
    mkPerson(t, 8, 508, 5080, /*kind*/0); // wounded too
    install(&ParseTokens1Mode0);
    u8 params[64] = {0};
    int rc = ResolveTargetWoundedPerson(0, params, "x", 1, nullptr);
    CHECK_EQ(rc, 1);
    CHECK_EQ((int)t.lastRenderWord, 505);
}

// ---------------------------------------------------------------------------
// RandomCarried: walks by random stride from {3,5,7,11}, picks carried items.
// ---------------------------------------------------------------------------
TEST(CommandRecon4, RandomCarriedPicksCarried) {
    Table t; clearTable(t); g_t = &t;
    // The original's outer do/while continues while the inner walk's final index
    // i satisfies i+1 < 768, so it only terminates once the walk ends at i==767.
    // Place the carried item at slot 767 and start the walk there so the first
    // pass finds it AND ends at 767 (767+1 < 768 is false -> single pass).
    mkPerson(t, 767, 767, 7670, /*kind*/6);
    t.randomSeq = {0 /*stride idx -> 3*/, 767 /*start*/};
    install(&ParseTokens1Mode0);
    u8 params[64] = {0};
    int rc = ResolveTargetRandomCarried(0, params, "x", 0, nullptr);
    CHECK_EQ(rc, 1);
    CHECK_EQ((int)t.lastRenderWord, 767);
}

TEST(CommandRecon4, RandomCarriedKind2Rejects) {
    Table t; clearTable(t); g_t = &t;
    install(&ParseTokens1Mode0);
    u8 params[64] = {0};
    CHECK_EQ(ResolveTargetRandomCarried(2, params, "x", 0, nullptr), 0);
}
