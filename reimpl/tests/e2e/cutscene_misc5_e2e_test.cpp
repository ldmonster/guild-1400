// E2E: drive a full "enter a building scene" flow across the cutscene_misc5
// scene-sync functions over one installed hook surface, the way the cutscene
// scene loader would: load the city scene, sync each building's entrance fees,
// run the production tick-rate computation for the producing buildings, and
// refresh the building effects (chimney smoke + gate/torch passes). Assert the
// cross-function world-sync the binary produces.
#include "test.h"

#include "sim/cutscene_misc5.h"

#include <cstring>
#include <vector>

using namespace guild;
using namespace guild::sim;

namespace {
struct World {
    int loadResult = 1;
    bool loaded = false;
    std::vector<int> traverseMasks;
    std::vector<int> feeProducts;   // product types queued at entrances
    int cmd15Total = 0;
    int smokeSpawns = 0;
    int categoryFor[256] = {0};     // type byte -> category
};
World g_w;

int   E2ELoad(const char* f) { g_w.loaded = (f != nullptr); return g_w.loadResult; }
void  E2ETraverse(int m) { g_w.traverseMasks.push_back(m); }
void  E2EQ17(i32, i32, int, int product) { g_w.feeProducts.push_back(product); }
void  E2ECmd15(i32, i32 amt) { g_w.cmd15Total += amt; }
int   E2EMul(int amt, u8 rate) { return amt + rate; }  // 500 + rate
int   E2EMap(u8 typeByte) { return g_w.categoryFor[typeByte]; }
void  E2ESmoke(int, int) { g_w.smokeSpawns++; }
}  // namespace

TEST(CutsceneMisc5E2E, EnterCitySceneFullSyncFlow) {
    g_w = World{};
    g_w.categoryFor[9]  = 8;   // Bauplatz
    g_w.categoryFor[7]  = 4;   // special producer
    g_w.categoryFor[8]  = 1;   // production
    g_w.categoryFor[16] = 0;   // storage

    SceneSyncHooks h{};
    h.loadFromStream   = E2ELoad;
    h.traverseTree     = E2ETraverse;
    h.queueRequest17   = E2EQ17;
    h.enqueueCmd15     = E2ECmd15;
    h.multiplyByRate   = E2EMul;
    h.mapTypeToCategory = E2EMap;
    h.spawnChimneySmoke = E2ESmoke;
    SetSceneSyncHooks(&h);

    // 1) Load the city scene.
    char loaded = SceneLoadStadtScene("VENEDIG");
    CHECK_EQ(static_cast<int>(loaded), 1);
    // The successful load runs the particle-emitter init traversal (mask 6).
    CHECK(!g_w.traverseMasks.empty());
    if (!g_w.traverseMasks.empty())
        CHECK_EQ(g_w.traverseMasks[0], 6);

    // 2) Sync four building entrances: a site (9), a producer (7), a producer
    //    (8) and a storehouse (16).
    u8 typeBytes[] = {9, 7, 8, 16};
    i32 ids[]      = {10, 11, 12, 13};
    for (int i = 0; i < 4; ++i)
        SceneSyncBuildingEntrance(typeBytes[i], ids[i], /*rate=*/2);

    // Site (9) -> product 310 + a sell cmd15 (500+2). Storage (16) -> exempt.
    // The other two -> standard fee 308.
    CHECK_EQ(static_cast<int>(g_w.feeProducts.size()), 3);  // 310, 308, 308
    if (g_w.feeProducts.size() == 3) {
        CHECK_EQ(g_w.feeProducts[0], 310);
        CHECK_EQ(g_w.feeProducts[1], 308);
        CHECK_EQ(g_w.feeProducts[2], 308);
    }
    CHECK_EQ(g_w.cmd15Total, 502);  // MultiplyByRate(500,2) == 500+2

    // 3) Production tick rate for the producing building (slots resolved).
    int prices[] = {200, 100};
    u16 counts[] = {3, 2};
    bool valid[] = {true, true};
    // sum = 600 + 200 = 800 -> 16000/800 = 20.
    int rate = SceneComputeProductionTickRate(prices, counts, valid, 2, false);
    CHECK_EQ(rate, 20);

    // 4) Refresh effects: two buildings have chimney data, two don't.
    int data[] = {500, 0, 600, 0};
    int spawns = SceneRefreshBuildingEffects(data, 4, /*a1=*/1);
    CHECK_EQ(spawns, 2);
    CHECK_EQ(g_w.smokeSpawns, 2);
    // Effects pass adds the gate (64) then torch (192) traversals.
    bool sawGate = false, sawTorch = false;
    for (int m : g_w.traverseMasks) { if (m == 64) sawGate = true; if (m == 192) sawTorch = true; }
    CHECK(sawGate);
    CHECK(sawTorch);

    SetSceneSyncHooks(nullptr);
}

TEST(CutsceneMisc5E2E, CitySceneLoadFailureAbortsFlow) {
    g_w = World{};
    g_w.loadResult = 0;   // scene file not found
    SceneSyncHooks h{};
    h.loadFromStream = E2ELoad;
    h.traverseTree   = E2ETraverse;
    SetSceneSyncHooks(&h);

    CHECK_EQ(static_cast<int>(SceneLoadStadtScene("MISSING")), 0);
    // No success traversal on a failed load.
    CHECK(g_w.traverseMasks.empty());
    SetSceneSyncHooks(nullptr);
}
