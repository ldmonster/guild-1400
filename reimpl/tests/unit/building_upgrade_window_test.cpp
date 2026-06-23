// Golden tests for gilde.exe 0x594100 VIBE_Building_OpenUpgradeTreeWindow.
// Drives the real RoadComputeNetworkLayout (reused) through the boundary hooks and
// asserts: the .form-open / select / title call order, the empty-tree -> Form_Destroy
// -> -1 path, and the edge-draw geometry (parent-link condition + the exact
// Paintbox_DrawLine coordinate offsets incl. the +0x34 cross-record Y aliasing).
#include "sim/building_upgrade_window.h"
#include "sim/building_lifecycle.h"
#include "world/road_network.h"
#include "tests/framework/test.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace guild;
using namespace guild::sim;

namespace {

// The binary-faithful synthetic type tables (mirrors world_road_network_test).
struct SyntheticCity {
    u8 flag[65 * 256];
    u8 table[589 * 4];
    SyntheticCity() { std::memset(flag, 0, sizeof(flag)); std::memset(table, 0, sizeof(table)); }
    u8* rec(int type) { return table + 589 * type; }
    void setCount(int type, int n) { rec(type)[34] = (u8)n; }
    void setEntry(int type, int idx, u16 id) { *(u16*)(rec(type) + 35 + 2 * idx) = id; }
    void setLink(int type, int idx, u16 parentFrom, u16 childType) {
        *(u16*)(rec(type) + 163 + 4 * idx) = parentFrom;
        *(u16*)(rec(type) + 163 + 4 * idx + 2) = childType;
    }
    void setCategory(u16 id, u8 cat) { flag[65 * id] = cat; }
};

struct DrawRec { int x1, y1, x2, y2, color, style, w; };

struct Harness {
    SyntheticCity city;
    int width = 800, height = 600;
    int openedHandle = 4242;
    bool destroyed = false;
    std::vector<std::string> calls;          // ordered boundary call log
    std::vector<DrawRec> draws;
};
Harness* g = nullptr;

int  h_open(const char* name)      { g->calls.push_back(std::string("open:") + name); return g->openedHandle; }
void h_center(int hnd)             { g->calls.push_back("center:" + std::to_string(hnd)); }
int  h_ctx()                       { g->calls.push_back("ctx"); return 7; }
void h_select(int, int i)          { g->calls.push_back("select:" + std::to_string(i)); }
void h_title(int t)                { g->calls.push_back("title:" + std::to_string(t)); }
void h_extent(int* w, int* hh)     { *w = g->width; *hh = g->height; }
const u8* h_typeTable()            { return g->city.table; }
const u8* h_typeFlag()             { return g->city.flag; }
void h_build(const i8*, const i16*){ g->calls.push_back("build"); }
void h_destroy(int hnd)            { g->destroyed = true; g->calls.push_back("destroy:" + std::to_string(hnd)); }
void h_draw(int x1, int y1, int x2, int y2, int c, int s, int w) {
    g->draws.push_back({x1, y1, x2, y2, c, s, w});
}

UpgradeWindowHooks MakeHooks() {
    UpgradeWindowHooks h;
    h.formOpen = h_open;
    h.formCenter = h_center;
    h.contextSnapshot = h_ctx;
    h.formSelectWindow = h_select;
    h.renderRichTitle = h_title;
    h.layoutExtent = h_extent;
    h.typeTableBase = h_typeTable;
    h.typeFlagBase = h_typeFlag;
    h.buildUpgradeTree = h_build;
    h.drawLine = h_draw;
    h.formDestroy = h_destroy;
    return h;
}

// The golden 4-node tree from world_road_network_test (ids 11/12/13/14).
void buildGoldenTree(SyntheticCity& c) {
    c.setCount(0, 5);
    c.setEntry(0, 0, 10);  // target
    c.setEntry(0, 1, 11);  // node id 11 (root)
    c.setEntry(0, 2, 12);
    c.setEntry(0, 3, 13);
    c.setEntry(0, 4, 14);
    c.setLink(0, 1, 0,  100);
    c.setLink(0, 2, 11, 101);
    c.setLink(0, 3, 11, 102);
    c.setLink(0, 4, 12, 103);
}

} // namespace

// ---------------------------------------------------------------------------
TEST(UpgradeWindow, EmptyTreeDestroysFormReturnsMinusOne) {
    Harness hn; g = &hn;
    // target not present -> RoadComputeNetworkLayout returns 1 (empty).
    hn.city.setCount(0, 1);
    hn.city.setEntry(0, 0, 10);
    UpgradeWindowHooks h = MakeHooks();
    SetUpgradeWindowHooks(&h);
    i8 typeRec[1] = {0};
    i16 target = 999;  // absent
    int rc = Building_OpenUpgradeTreeWindow(typeRec, &target, "techtree\\TechTree");
    CHECK_EQ(rc, -1);
    CHECK(hn.destroyed);
    CHECK(hn.draws.empty());
    // the form was still opened + the title/select sequence ran before the layout.
    CHECK_EQ(hn.calls.front(), std::string("open:techtree\\TechTree"));
    SetUpgradeWindowHooks(nullptr);
}

TEST(UpgradeWindow, OpenSequenceOrder) {
    Harness hn; g = &hn;
    buildGoldenTree(hn.city);
    UpgradeWindowHooks h = MakeHooks();
    SetUpgradeWindowHooks(&h);
    i8 typeRec[1] = {0};
    i16 target = 10;
    int rc = Building_OpenUpgradeTreeWindow(typeRec, &target, "techtree\\TechTree");
    CHECK_EQ(rc, hn.openedHandle);
    // open -> center -> ctx -> select(1) -> title(25) -> select(3) -> build -> ...
    CHECK_EQ(hn.calls[0], std::string("open:techtree\\TechTree"));
    CHECK_EQ(hn.calls[1], std::string("center:4242"));
    CHECK_EQ(hn.calls[2], std::string("ctx"));
    CHECK_EQ(hn.calls[3], std::string("select:1"));
    CHECK_EQ(hn.calls[4], std::string("title:25"));
    CHECK_EQ(hn.calls[5], std::string("select:3"));
    CHECK_EQ(hn.calls[6], std::string("build"));
    CHECK(!hn.destroyed);
    SetUpgradeWindowHooks(nullptr);
}

// The headline geometry golden: the drawn edges + their exact coordinates, computed
// 1:1 against the layout the road solver produced for this same input.
TEST(UpgradeWindow, EdgeGeometryGolden) {
    Harness hn; g = &hn;
    buildGoldenTree(hn.city);

    UpgradeWindowHooks h = MakeHooks();
    SetUpgradeWindowHooks(&h);
    i8 typeRec[1] = {0};
    i16 target = 10;
    Building_OpenUpgradeTreeWindow(typeRec, &target, "techtree\\TechTree");

    // Re-derive the expected edge set with the same condition + offsets the function
    // uses, reading from an independently-computed layout (the road solver is the
    // reference of record). Y(N) = nodes[N+1].coordY (the +0x34 cross-record alias).
    world::RoadLayoutState ref{};
    world::RoadComputeNetworkLayout(ref, hn.city.table, hn.city.flag,
                                    typeRec, &target, 800, 600);
    auto coordY = [&](int n) -> int {
        int nx = n + 1;
        return (nx < ref.nodeCount) ? ref.nodes[nx].coordY : 0;
    };
    std::vector<DrawRec> expected;
    for (int outer = 0; outer < ref.nodeCount; ++outer) {
        for (int inner = 0; inner < outer; ++inner) {
            i32 id = ref.nodes[inner].nodeId;
            if (id == ref.nodes[outer].parentFromId || id == ref.nodes[outer].parentToId) {
                expected.push_back({ref.nodes[inner].cost + 24, coordY(inner) + 48,
                                    coordY(outer) + 24, ref.nodes[outer].cost + 24,
                                    88, 31, 20});
            }
        }
    }

    CHECK_EQ((int)hn.draws.size(), (int)expected.size());
    CHECK((int)expected.size() >= 1);  // the golden tree DOES produce >=1 edge
    for (size_t i = 0; i < hn.draws.size() && i < expected.size(); ++i) {
        CHECK_EQ(hn.draws[i].x1, expected[i].x1);
        CHECK_EQ(hn.draws[i].y1, expected[i].y1);
        CHECK_EQ(hn.draws[i].x2, expected[i].x2);
        CHECK_EQ(hn.draws[i].y2, expected[i].y2);
        CHECK_EQ(hn.draws[i].color, 88);
        CHECK_EQ(hn.draws[i].style, 31);
        CHECK_EQ(hn.draws[i].w, 20);
    }
    SetUpgradeWindowHooks(nullptr);
}

TEST(UpgradeWindow, InertDefaultsNoCrash) {
    SetUpgradeWindowHooks(nullptr);
    i8 typeRec[1] = {0};
    i16 target = 10;
    // No hooks -> formOpen null -> handle 0; layout over null tables -> empty -> -1.
    int rc = Building_OpenUpgradeTreeWindow(typeRec, &target, "x");
    CHECK_EQ(rc, -1);
}
