#include "test.h"
#include "sim/gamelogic_recon5_resolve_stat.h"

#include <cstring>

using namespace guild;
using namespace guild::sim;

// ---------------------------------------------------------------------------
// Test fixtures: static state the hooks read/write.
// ---------------------------------------------------------------------------
namespace {

struct SelFixture {
    void* recBase = nullptr;
    u16   slot = 0;
    i16   typeWord = 0;
    u8    alive = 1;
    u8    kind = 0;
    i32   wealth = 0;
    i32   parsed = 0;
    // captured:
    i32   coordIn = 0;
    u8    coordFlag = 0xFF;
    bool  rendered = false;
    i32   renderedCoord = 0;
};
SelFixture g_sel;

void* SelFind(i32) { return g_sel.recBase; }
u16   SelSlot(void*) { return g_sel.slot; }
i16   SelType(u16) { return g_sel.typeWord; }
u8    SelAlive(u16) { return g_sel.alive; }
u8    SelKind(u16) { return g_sel.kind; }
i32   SelWealth(u16) { return g_sel.wealth; }
i32   SelParse(const char*) { return g_sel.parsed; }
i32   SelConvert(i32 v, u8 f) { g_sel.coordIn = v; g_sel.coordFlag = f; return v + 100000; }
void  SelRender(char*, const char*, i32 c, i32) { g_sel.rendered = true; g_sel.renderedCoord = c; }

void InstallSel() {
    Recon5StatHooks h{};
    h.findRecordById = &SelFind;
    h.recordSlot = &SelSlot;
    h.tableTypeWord = &SelType;
    h.tableAlive = &SelAlive;
    h.tableKind = &SelKind;
    h.computeTotalWealth = &SelWealth;
    h.parseStatPercent = &SelParse;
    h.convertToDisplayCoord = &SelConvert;
    h.renderMessage = &SelRender;
    SetRecon5StatHooks(&h);
}

struct BldFixture {
    void* person = nullptr;
    void* building = nullptr;
    int   childCount = 0;
    u8    cat[8] = {0};
    i32   qty[8] = {0};
    i32   parsed = 0;
    i32   coordIn = 0;
    bool  rendered = false;
    i32   renderedCoord = 0;
};
BldFixture g_bld;

void* BldQuery(const char*) { return g_bld.person; }
i32   BldAnchor(void*) { return 7; }
void* BldFind(i32) { return g_bld.building; }
i32   BldChildAnchor(void*) { return 9; }
int   BldChildCount(void*) { return g_bld.childCount; }
u8    BldCat(void*, int i) { return g_bld.cat[i]; }
i32   BldQty(void*, int i) { return g_bld.qty[i]; }
i32   BldParse(const char*) { return g_bld.parsed; }
i32   BldConvert(i32 v, u8) { g_bld.coordIn = v; return v; }
void  BldRender(char*, const char*, i32 c, i32) { g_bld.rendered = true; g_bld.renderedCoord = c; }

void InstallBld() {
    Recon5StatHooks h{};
    h.queryByGoodType = &BldQuery;
    h.personAnchorId = &BldAnchor;
    h.queryFindBuilding = &BldFind;
    h.buildingChildAnchor = &BldChildAnchor;
    h.buildingChildCount = &BldChildCount;
    h.childCategory = &BldCat;
    h.childQuantity = &BldQty;
    h.parseStatPercent = &BldParse;
    h.convertToDisplayCoord = &BldConvert;
    h.renderMessage = &BldRender;
    SetRecon5StatHooks(&h);
}

} // namespace

// ===========================================================================
// SelectedStat — gating.
// ===========================================================================
TEST(GameLogicRecon5, SelectedStat_RejectsWrongKind) {
    InstallSel();
    char out[8] = {0};
    u8 params[64] = {0};
    CHECK_EQ(ResolveTargetSelectedStat(0, params, "x", 0, out), 0);
    CHECK_EQ(ResolveTargetSelectedStat(1, params, "x", 0, out), 0);
}

TEST(GameLogicRecon5, SelectedStat_RejectsNullParamsAndIndex8) {
    InstallSel();
    char out[8] = {0};
    u8 params[128] = {0};
    CHECK_EQ(ResolveTargetSelectedStat(2, nullptr, "x", 0, out), 0);
    CHECK_EQ(ResolveTargetSelectedStat(2, params, "x", 8, out), 0);
}

TEST(GameLogicRecon5, SelectedStat_RejectsEmptyDeadOrNonPerson) {
    char out[8] = {0};
    u8 params[64] = {0};
    int dummy = 0;
    // null record
    g_sel = SelFixture{}; g_sel.recBase = nullptr; InstallSel();
    CHECK_EQ(ResolveTargetSelectedStat(2, params, "x", 0, out), 0);
    // typeWord == -1
    g_sel = SelFixture{}; g_sel.recBase = &dummy; g_sel.typeWord = -1; InstallSel();
    CHECK_EQ(ResolveTargetSelectedStat(2, params, "x", 0, out), 0);
    // not alive
    g_sel = SelFixture{}; g_sel.recBase = &dummy; g_sel.alive = 0; InstallSel();
    CHECK_EQ(ResolveTargetSelectedStat(2, params, "x", 0, out), 0);
    // kind >= 10
    g_sel = SelFixture{}; g_sel.recBase = &dummy; g_sel.kind = 10; InstallSel();
    CHECK_EQ(ResolveTargetSelectedStat(2, params, "x", 0, out), 0);
}

// ===========================================================================
// SelectedStat — golden math: (double)parsed * 0.01 * wealth, truncated.
// ===========================================================================
TEST(GameLogicRecon5, SelectedStat_GoldenScaling) {
    char out[8] = {0};
    u8 params[64] = {0};
    int dummy = 0;
    g_sel = SelFixture{};
    g_sel.recBase = &dummy; g_sel.typeWord = 5; g_sel.alive = 1; g_sel.kind = 3;
    g_sel.wealth = 25000; g_sel.parsed = 50;
    InstallSel();
    CHECK_EQ(ResolveTargetSelectedStat(2, params, "+50", 0, out), 1);
    // 50 * 0.01f * 25000 — flt 0.01f is 0.00999999978 -> 12499.99.. -> 12499
    CHECK_EQ(g_sel.coordIn, 12499);
    CHECK_EQ(g_sel.coordFlag, (u8)0);          // byte_6477A1 == 0
    CHECK(g_sel.rendered);
    CHECK_EQ(g_sel.renderedCoord, 112499);     // 12499 + 100000 from convert hook
}

TEST(GameLogicRecon5, SelectedStat_GoldenTruncationTowardZero) {
    char out[8] = {0};
    u8 params[64] = {0};
    int dummy = 0;
    g_sel = SelFixture{};
    g_sel.recBase = &dummy; g_sel.typeWord = 1; g_sel.alive = 1; g_sel.kind = 0;
    // 3 * 0.01 * 33 = 0.99 -> truncates to 0
    g_sel.wealth = 33; g_sel.parsed = 3;
    InstallSel();
    CHECK_EQ(ResolveTargetSelectedStat(2, params, "+3", 0, out), 1);
    CHECK_EQ(g_sel.coordIn, 0);
}

// ===========================================================================
// BuildingStat — leaf gating.
// ===========================================================================
TEST(GameLogicRecon5, BuildingStat_RejectsNoPerson) {
    g_bld = BldFixture{}; g_bld.person = nullptr; InstallBld();
    char out[8] = {0};
    CHECK_EQ(ResolveTargetBuildingStat("x", out), 0);
}

TEST(GameLogicRecon5, BuildingStat_RejectsNoBuilding) {
    int p = 1;
    g_bld = BldFixture{}; g_bld.person = &p; g_bld.building = nullptr; InstallBld();
    char out[8] = {0};
    CHECK_EQ(ResolveTargetBuildingStat("x", out), 0);
}

// ===========================================================================
// BuildingStat — child scan selects category==9; quantity scaled.
// ===========================================================================
TEST(GameLogicRecon5, BuildingStat_GoldenChildScanAndScale) {
    int p = 1, b = 2;
    g_bld = BldFixture{}; g_bld.person = &p; g_bld.building = &b;
    g_bld.childCount = 4;
    g_bld.cat[0] = 3; g_bld.cat[1] = 7; g_bld.cat[2] = 9; g_bld.cat[3] = 9;
    g_bld.qty[0] = 111; g_bld.qty[1] = 222; g_bld.qty[2] = 4000; g_bld.qty[3] = 5000;
    g_bld.parsed = 25;
    InstallBld();
    char out[8] = {0};
    CHECK_EQ(ResolveTargetBuildingStat("+25", out), 1);
    // first category-9 child is index 2 (qty 4000): 25 * 0.01f * 4000 = 999.99 -> 999
    CHECK_EQ(g_bld.coordIn, 999);
    CHECK(g_bld.rendered);
}

TEST(GameLogicRecon5, BuildingStat_NoCategory9UsesZeroQuantity) {
    int p = 1, b = 2;
    g_bld = BldFixture{}; g_bld.person = &p; g_bld.building = &b;
    g_bld.childCount = 3;
    g_bld.cat[0] = 1; g_bld.cat[1] = 2; g_bld.cat[2] = 3;
    g_bld.qty[0] = 999; g_bld.qty[1] = 888; g_bld.qty[2] = 777;
    g_bld.parsed = 100;
    InstallBld();
    char out[8] = {0};
    CHECK_EQ(ResolveTargetBuildingStat("+100", out), 1);
    // no category-9 child -> v17 stays 0 -> 100 * 0.01 * 0 = 0
    CHECK_EQ(g_bld.coordIn, 0);
}
