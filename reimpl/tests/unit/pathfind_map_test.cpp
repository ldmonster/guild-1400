// Unit tests for pathfind_map — the untranslated VIBE_Path_*/VIBE_Map_*/
// VIBE_ObjectSearch_* slice. Golden vectors computed with python (see report).
#include "sim/pathfind_map.h"
#include "test.h"

#include <cmath>
#include <cstring>
#include <vector>

using namespace guild;
using namespace guild::sim;

namespace {

// ---- recording state shared by installed hooks ---------------------------
struct PMRec {
    std::vector<int> eligibleIds;   // ids the eligibility hook returns true for
    double favorability = 50.0;     // value the favorability hook returns
    int officeRank = 0;             // value the office-rank hook returns
    int grayCalls = 0;
    std::vector<i32> ringRecords;   // flattened triples [id, floatKeyBits, pad]
    int ringPinnedStore[3] = {0, 0, 0};
    bool usePinned = false;
    size_t ringCursor = 0;
};
PMRec* g_rec = nullptr;

int RecEligible(u16, u16 cand, void*) {
    for (int id : g_rec->eligibleIds) {
        if (id == static_cast<int>(cand)) return 1;
    }
    return 0;
}
double RecFav(int, u16, int) { return g_rec->favorability; }
void RecGray(int, int) { ++g_rec->grayCalls; }
int RecOfficeRank(u16, int) { return g_rec->officeRank; }

const i32* RecRingAdvance() {
    if (g_rec->ringRecords.empty()) return nullptr;
    size_t base = (g_rec->ringCursor % (g_rec->ringRecords.size() / 3)) * 3;
    ++g_rec->ringCursor;
    return &g_rec->ringRecords[base];
}

PathfindMapHooks MakeRecHooks(PMRec& r) {
    g_rec = &r;
    PathfindMapHooks h{};  // start from inert then override the ones we record
    h = PathfindMapGetHooks();  // grab current inert defaults as the base
    h.evaluateEligibility = RecEligible;
    h.computeFavorability = RecFav;
    h.setGrayColor        = RecGray;
    h.computeOfficeRank   = RecOfficeRank;
    h.objectRingAdvance   = RecRingAdvance;
    h.objectRingCount     = r.ringRecords.empty() ? 0 : static_cast<int>(r.ringRecords.size() / 3);
    h.objectRingPinned    = r.usePinned ? r.ringPinnedStore : nullptr;
    return h;
}

i32 fbits(float f) { i32 out; std::memcpy(&out, &f, sizeof(out)); return out; }

}  // namespace

// ---------------------------------------------------------------------------
// Stride table + constants
// ---------------------------------------------------------------------------
TEST(PathfindMapConst, StrideTableValues) {
    const int expect[16] = {1, 5, 7, 11, 13, 17, 19, 23,
                            745, 749, 751, 755, 757, 761, 763, 767};
    for (int i = 0; i < kPaletteStrideCount; ++i) {
        CHECK_EQ(kPaletteStrideTable[i], expect[i]);
    }
}

TEST(PathfindMapConst, StridesCoprimeWith768) {
    auto gcd = [](int a, int b) { while (b) { int t = a % b; a = b; b = t; } return a; };
    for (int i = 0; i < kPaletteStrideCount; ++i) {
        CHECK_EQ(gcd(kPaletteStrideTable[i], kPaletteSize), 1);
    }
}

TEST(PathfindMapConst, TruncTowardZero) {
    CHECK_EQ(PathfindMapTruncToInt(2.9), 2);
    CHECK_EQ(PathfindMapTruncToInt(-2.9), -2);
    CHECK_EQ(PathfindMapTruncToInt(0.0), 0);
}

// ---------------------------------------------------------------------------
// Palette search probe order + eligibility
// ---------------------------------------------------------------------------
TEST(PathfindMapPalette, FindOneHitsFirstEligible) {
    PMRec r;
    // walk from start=10 stride=7 (index 2): visits 10,17,24,31,38,...
    // make 31 the only eligible candidate.
    r.eligibleIds = {31};
    PathfindMapHooks h = MakeRecHooks(r);
    PathfindMapSetHooks(&h);

    u16 ref = 5, out = 0xFFFF;
    int rc = ObjectSearchFindOneByPaletteRange(&ref, nullptr, 0.0f, 100.0f, &out,
                                               /*strideIndex=*/2, /*probeStart=*/10);
    CHECK_EQ(rc, 1);
    CHECK_EQ(out, static_cast<u16>(31));
    CHECK(r.grayCalls >= 1);
    PathfindMapSetHooks(nullptr);
}

TEST(PathfindMapPalette, FindOneMissReturnsZero) {
    PMRec r;  // nobody eligible
    PathfindMapHooks h = MakeRecHooks(r);
    PathfindMapSetHooks(&h);
    u16 ref = 5, out = 7;
    int rc = ObjectSearchFindOneByPaletteRange(&ref, nullptr, 0.0f, 100.0f, &out, 0, 0);
    CHECK_EQ(rc, 0);
    PathfindMapSetHooks(nullptr);
}

TEST(PathfindMapPalette, FavorabilityRangeGate) {
    PMRec r;
    r.eligibleIds = {31};
    r.favorability = 12.0;  // outside [50,90] => rejected
    PathfindMapHooks h = MakeRecHooks(r);
    PathfindMapSetHooks(&h);
    u16 ref = 5, out = 0;
    // range active because minR>0
    int rc = ObjectSearchFindOneByPaletteRange(&ref, nullptr, 50.0f, 90.0f, &out, 2, 10);
    CHECK_EQ(rc, 0);            // 12 not in [50,90]
    r.favorability = 70.0;     // now in range
    rc = ObjectSearchFindOneByPaletteRange(&ref, nullptr, 50.0f, 90.0f, &out, 2, 10);
    CHECK_EQ(rc, 1);
    CHECK_EQ(out, static_cast<u16>(31));
    PathfindMapSetHooks(nullptr);
}

TEST(PathfindMapPalette, FindByRangeCountCapAndMultiSlot) {
    PMRec r;
    // start=0 stride=1 => visits 0,1,2,3,... make 2,3,4 eligible.
    r.eligibleIds = {2, 3, 4};
    PathfindMapHooks h = MakeRecHooks(r);
    PathfindMapSetHooks(&h);
    u16 ref = 9; u16 outs[3] = {0, 0, 0};
    int rc = ObjectSearchFindByPaletteRange(&ref, 3, nullptr, 0.0f, 100.0f, outs, 0, 0);
    CHECK_EQ(rc, 1);
    CHECK_EQ(outs[0], static_cast<u16>(2));
    CHECK_EQ(outs[1], static_cast<u16>(2));  // each slot restarts from probeStart
    CHECK_EQ(outs[2], static_cast<u16>(2));
    PathfindMapSetHooks(nullptr);
}

TEST(PathfindMapPalette, CountTooLargeRejected) {
    PMRec r;
    PathfindMapHooks h = MakeRecHooks(r);
    PathfindMapSetHooks(&h);
    u16 ref = 0; u16 outs[4] = {0};
    CHECK_EQ(ObjectSearchFindByPaletteRange(&ref, 4, nullptr, 0.0f, 100.0f, outs, 0, 0), 0);
    PathfindMapSetHooks(nullptr);
}

TEST(PathfindMapPalette, PinnedSlotZeroFastPath) {
    PMRec r;
    r.usePinned = true;
    r.ringPinnedStore[0] = 88;
    r.eligibleIds = {88, 5};  // pinned 88 eligible -> slot0; then 5 found at start
    PathfindMapHooks h = MakeRecHooks(r);
    PathfindMapSetHooks(&h);
    u16 ref = 1; u16 outs[2] = {0, 0};
    // start=5 stride=1, slot0 = pinned 88, slot1 = 5
    int rc = ObjectSearchFindByPaletteRange(&ref, 2, nullptr, 0.0f, 100.0f, outs, 0, 5);
    CHECK_EQ(rc, 1);
    CHECK_EQ(outs[0], static_cast<u16>(88));
    CHECK_EQ(outs[1], static_cast<u16>(5));
    PathfindMapSetHooks(nullptr);
}

TEST(PathfindMapPalette, PeopleByPaletteTypeAndRankGate) {
    PMRec r;
    r.officeRank = 3;
    // eligible 2 and 4; start=0 stride=1
    r.eligibleIds = {2, 4};
    PathfindMapHooks h = MakeRecHooks(r);
    PathfindMapSetHooks(&h);
    // people-type table: index 4 has type 8 (excluded), index 2 has type 0 (ok).
    std::vector<u8> typeTable(768 * 536, 0);
    typeTable[536 * 4] = 8;  // exclude idx 4
    u16 ref = 1; u16 outs[8] = {0};
    int n = ObjectSearchFindPeopleByPalette(&ref, 8, 0.0f, 100.0f, outs, 0, 0,
                                            typeTable.data());
    CHECK_EQ(n, 1);
    CHECK_EQ(outs[0], static_cast<u16>(2));
    PathfindMapSetHooks(nullptr);
}

// ---------------------------------------------------------------------------
// Object-ring colour search
// ---------------------------------------------------------------------------
TEST(PathfindMapColor, FindMatchingColorHitsInRange) {
    PMRec r;
    // ring: [id=10,key=20.0],[id=20,key=70.0],[id=30,key=200.0]
    r.ringRecords = {10, fbits(20.0f), 0, 20, fbits(70.0f), 0, 30, fbits(200.0f), 0};
    r.eligibleIds = {20};  // only 20 is eligible
    PathfindMapHooks h = MakeRecHooks(r);
    PathfindMapSetHooks(&h);
    u16 ref = 1, out = 0;
    // range [0,100]: id20 key70 in range and eligible
    int rc = ObjectSearchFindMatchingColor(&ref, nullptr, 0.0f, 100.0f, &out);
    CHECK_EQ(rc, 1);
    CHECK_EQ(out, static_cast<u16>(20));
    PathfindMapSetHooks(nullptr);
}

TEST(PathfindMapColor, FindMatchingColorEmptyRing) {
    PMRec r;  // empty ring
    PathfindMapHooks h = MakeRecHooks(r);
    PathfindMapSetHooks(&h);
    u16 ref = 1, out = 9;
    CHECK_EQ(ObjectSearchFindMatchingColor(&ref, nullptr, 0.0f, 100.0f, &out), 0);
    PathfindMapSetHooks(nullptr);
}

// ---------------------------------------------------------------------------
// Map: rasterize edge step counts (deterministic core)
// ---------------------------------------------------------------------------
TEST(PathfindMapRaster, EdgeStepCountGolden) {
    CHECK_EQ(MapRasterEdgeStepCount(0.0f), 1);
    CHECK_EQ(MapRasterEdgeStepCount(1.0f), 2);
    CHECK_EQ(MapRasterEdgeStepCount(100.0f), 20);
    CHECK_EQ(MapRasterEdgeStepCount(144.0f), 24);
    CHECK_EQ(MapRasterEdgeStepCount(9999.0f), 200);
}

TEST(PathfindMapRaster, RasterizeReturnsLastInnerStepCount) {
    // gilde.exe 0x577927 returns `result`, the inner-loop iteration count of the
    // LAST outer step, UNCONDITIONALLY (the fill==-1 / cell==255 gates only skip
    // the grid writes, not the count). A 10x10 square quad: outer edge len 10 ->
    // outerSteps = 2*(int)(sqrt(100)+0.9) = 20. The cross segment is constant
    // length 10 => innerSteps = 20 on every outer step, so result == 20.
    float quad[8] = {0, 0, 10, 0, 10, 10, 0, 10};
    int withFill = MapRasterizeBauplatzEdge(quad, /*fillTerrain=*/3, /*fillCell=*/0);
    CHECK_EQ(withFill, 20);
    // fill==-1 and cell==255 => writes skipped but the return value is unchanged.
    int noFill = MapRasterizeBauplatzEdge(quad, -1, 255);
    CHECK_EQ(noFill, 20);
}

// ---------------------------------------------------------------------------
// Map: building chain depth + road layout (deterministic)
// ---------------------------------------------------------------------------
TEST(PathfindMapRoad, ChainDepthCachedAndRoot) {
    RoadNetwork net{};
    net.nodeCount = 1;
    net.nodes[0].nodeId = 10;
    net.nodes[0].parentFromId = 0;
    net.nodes[0].parentToId = 0;
    net.nodes[0].depth = static_cast<i16>(0xFFFF);
    // no links => depth 0
    CHECK_EQ(MapComputeBuildingChainDepth(net, 0), 0);
    // cached value short-circuits
    net.nodes[0].depth = 7;
    CHECK_EQ(MapComputeBuildingChainDepth(net, 0), 7);
}

TEST(PathfindMapRoad, LayoutFirstLevelXCoords) {
    RoadNetwork net{};
    net.nodeCount = 2;
    for (int i = 0; i < 2; ++i) {
        net.nodes[i].nodeId = 10 + i;
        net.nodes[i].parentFromId = 0;
        net.nodes[i].parentToId = 0;
        net.nodes[i].depth = static_cast<i16>(0xFFFF);
        net.nodes[i].cost = 0;
    }
    int rc = MapComputeRoadNetworkLayout(net, /*width=*/800, /*height=*/600);
    CHECK_EQ(rc, 0);
    CHECK_EQ(net.depthCount, 1);
    // x = 800/(2*2)=200; step=400; x0=200-24=176, x1=600-24=576
    CHECK_EQ(net.nodes[0].coordX, 176);
    CHECK_EQ(net.nodes[1].coordX, 576);
    // all depth 0 => y row 0 +16 = 16 (single level: y=min(96,584)=96, 96*0/1+16)
    CHECK_EQ(net.nodes[0].coordY, 16);
}

TEST(PathfindMapRoad, EmptyNetworkReturnsOne) {
    RoadNetwork net{};
    net.nodeCount = 0;
    CHECK_EQ(MapComputeRoadNetworkLayout(net, 800, 600), 1);
}

// ---------------------------------------------------------------------------
// Map: dummy-name build + city load orchestration
// ---------------------------------------------------------------------------
TEST(PathfindMapCity, BuildDummyNameUppercases) {
    char out[64];
    MapBuildDummyName("marktplatz", out);
    CHECK(std::strcmp(out, "dummy_MARKTPLATZ") == 0);
    MapBuildDummyName("Tor_A1", out);
    CHECK(std::strcmp(out, "dummy_TOR_A1") == 0);
}
