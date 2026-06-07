// e2e flow for pathfind_map: drive a city-load + marker-spawn + road-layout +
// palette-search sequence through installed hooks, asserting the cross-function
// behaviour end-to-end.
#include "sim/pathfind_map.h"
#include "test.h"

#include <cstring>
#include <string>
#include <vector>

using namespace guild;
using namespace guild::sim;

namespace {

// A recording world: tracks the orchestration call order and serves canned objects.
struct E2EWorld {
    std::vector<std::string> calls;
    float objA[24] = {0};   // dummy LINKS_OBEN
    float objB[24] = {0};   // dummy RECHTS_UNTEN
    float node[24] = {0};   // attached node
    int   sceneResult = 42;
    int   eligibleId = 7;
    double favorability = 60.0;
};
E2EWorld* g_w = nullptr;

void  WSwitch(int, int, int, int)        { g_w->calls.push_back("switch"); }
void  WReset(int, int)                   { g_w->calls.push_back("resetSlot"); }
void  WEnter(const char*, int)           { g_w->calls.push_back("enterCity"); }
void  WSave(const char* p, const char*, int, int) { g_w->calls.push_back(std::string("save:") + p); }
void  WResetB()                          { g_w->calls.push_back("resetBuild"); }
void  WResetP()                          { g_w->calls.push_back("resetPersons"); }
void  WRelink()                          { g_w->calls.push_back("relink"); }
void  WDestroy(int, int)                 { g_w->calls.push_back("destroy"); }
int   WLoadScene(const char*, int, i16, int) { g_w->calls.push_back("loadScene"); return g_w->sceneResult; }

void* WFind(int, int, const char* name, int, void*) {
    std::string n = name;
    if (n == "dummy_Kartentisch_LINKS_OBEN") return g_w->objA;
    if (n == "dummy_Kartentisch_RECHTS_UNTEN") return g_w->objB;
    if (n.rfind("dummy_", 0) == 0) return g_w->objA;  // city-point dummy
    return nullptr;
}
void* WAttach(int, const float*, const char*, int) { g_w->calls.push_back("attach"); return g_w->node; }
void  WLight(void*)                      { g_w->calls.push_back("light"); }
void  WAnim(int, const char*, int)       { g_w->calls.push_back("anim"); }

int   WEligible(u16, u16 cand, void*)    { return cand == static_cast<u16>(g_w->eligibleId) ? 1 : 0; }
double WFav(int, u16, int)               { return g_w->favorability; }

PathfindMapHooks MakeWorldHooks(E2EWorld& w) {
    g_w = &w;
    PathfindMapHooks h = PathfindMapGetHooks();  // inert base
    h.universeSwitchActiveSlot = WSwitch;
    h.universeResetCurrentSlot = WReset;
    h.sceneEnterCity           = WEnter;
    h.saveWriteGameFile        = WSave;
    h.buildingResetAll         = WResetB;
    h.worldResetPersonTable    = WResetP;
    h.worldRelinkObjectOwners  = WRelink;
    h.objectDestroySpawned     = WDestroy;
    h.sceneLoadFromStream      = WLoadScene;
    h.findObjectByHandle       = WFind;
    h.attachToUniverseNode     = WAttach;
    h.lightBuildObjectCache    = WLight;
    h.loadObjectAnimation      = WAnim;
    h.evaluateEligibility      = WEligible;
    h.computeFavorability      = WFav;
    return h;
}

bool HasCall(const E2EWorld& w, const std::string& c) {
    for (const auto& s : w.calls) if (s == c) return true;
    return false;
}

}  // namespace

TEST(PathfindMapE2E, CityLoadOrchestrationOrder) {
    E2EWorld w;
    PathfindMapHooks h = MakeWorldHooks(w);
    PathfindMapSetHooks(&h);

    int rc = MapLoadCityFile(/*loadNet=*/1, "altdorf");
    CHECK_EQ(rc, 42);  // scene load result threaded through
    // path must be the .NET form under gamedata/cities
    CHECK(HasCall(w, "save:gamedata/cities/altdorf.NET"));
    // world reset sequence present
    CHECK(HasCall(w, "resetBuild"));
    CHECK(HasCall(w, "resetPersons"));
    CHECK(HasCall(w, "relink"));
    CHECK(HasCall(w, "loadScene"));
    // .CTY form when loadNet=0
    w.calls.clear();
    MapLoadCityFile(0, "altdorf");
    CHECK(HasCall(w, "save:gamedata/cities/altdorf.CTY"));

    PathfindMapSetHooks(nullptr);
}

TEST(PathfindMapE2E, TowerMarkerPlacedBetweenDummies) {
    E2EWorld w;
    // objA at world x=100 (+76 => float idx 19), objB at x=300; y idx 21: 50 / 250
    w.objA[19] = 100.0f; w.objA[20] = 5.0f; w.objA[21] = 50.0f;
    w.objB[19] = 300.0f; w.objB[21] = 250.0f;
    PathfindMapHooks h = MakeWorldHooks(w);
    PathfindMapSetHooks(&h);

    float xform[8] = {0};
    // offX=700 => sx = 700 * (1/700) = 1.0 ; spanX=200 => x = 100 + 200*1 = 300
    int rc = MapSpawnCityTowerMarker(700.0f, 0.0f, xform);
    CHECK_EQ(rc, 1);
    CHECK(xform[0] > 299.0f && xform[0] < 301.0f);  // ~300
    CHECK(xform[2] > 49.5f && xform[2] < 50.5f);    // offY=0 => stays at objA.y=50
    CHECK(HasCall(w, "attach"));
    CHECK(HasCall(w, "light"));
    CHECK(HasCall(w, "anim"));

    PathfindMapSetHooks(nullptr);
}

TEST(PathfindMapE2E, TowerMarkerMissingDummyFails) {
    E2EWorld w;
    PathfindMapHooks h = MakeWorldHooks(w);
    // make find return null for everything
    h.findObjectByHandle = [](int, int, const char*, int, void*) -> void* { return nullptr; };
    PathfindMapSetHooks(&h);
    float xform[8] = {0};
    CHECK_EQ(MapSpawnCityTowerMarker(1.0f, 1.0f, xform), 0);
    PathfindMapSetHooks(nullptr);
}

TEST(PathfindMapE2E, PointMarkerUppercasesAndAttaches) {
    E2EWorld w;
    PathfindMapHooks h = MakeWorldHooks(w);
    PathfindMapSetHooks(&h);
    int rc = MapSpawnCityPointMarker("rathaus", 1.5f);
    CHECK_EQ(rc, 1);
    CHECK(HasCall(w, "attach"));
    PathfindMapSetHooks(nullptr);
}

TEST(PathfindMapE2E, RoadLayoutTwoLevelChain) {
    // Build a 3-node graph: node0 root (depth0), node1 child of node0, node2 child
    // of node1 -> a 3-level chain. Links are by hi-word id matching.
    RoadNetwork net{};
    net.nodeCount = 3;
    // node0: id=10, root (no parent links)
    net.nodes[0].nodeId = 10; net.nodes[0].parentFromId = 0; net.nodes[0].parentToId = 0;
    net.nodes[0].depth = static_cast<i16>(0xFFFF); net.nodes[0].cost = 0;
    // node1: id=20, parent 10
    net.nodes[1].nodeId = 20; net.nodes[1].parentFromId = 10; net.nodes[1].parentToId = 0;
    net.nodes[1].depth = static_cast<i16>(0xFFFF); net.nodes[1].cost = 0;
    // node2: id=30, parent 20
    net.nodes[2].nodeId = 30; net.nodes[2].parentFromId = 20; net.nodes[2].parentToId = 0;
    net.nodes[2].depth = static_cast<i16>(0xFFFF); net.nodes[2].cost = 0;

    int rc = MapComputeRoadNetworkLayout(net, 800, 600);
    CHECK_EQ(rc, 0);
    // depths should be 0,1,2 -> depthCount 3. Nodes are sorted by depth ascending.
    CHECK_EQ(net.depthCount, 3);
    CHECK_EQ(static_cast<int>(static_cast<unsigned short>(net.nodes[0].depth)), 0);
    CHECK_EQ(static_cast<int>(static_cast<unsigned short>(net.nodes[2].depth)), 2);
    // y rows: depth d -> min(96*3,584)*d/3 + 16 = 288*d/3 + 16
    CHECK_EQ(net.nodes[0].coordY, 16);   // d0
    CHECK_EQ(net.nodes[2].coordY, 208);  // d2: 288*2/3+16

    PathfindMapSetHooks(nullptr);
}

TEST(PathfindMapE2E, PaletteSearchWithFavorabilityFlow) {
    E2EWorld w;
    w.eligibleId = 7;
    w.favorability = 60.0;
    PathfindMapHooks h = MakeWorldHooks(w);
    PathfindMapSetHooks(&h);
    // start=0 stride index 0 (stride 1) -> visits 0,1,2,... 7 is eligible.
    u16 ref = 1, out = 0;
    int rc = ObjectSearchFindOneByPaletteRange(&ref, nullptr, 50.0f, 90.0f, &out, 0, 0);
    CHECK_EQ(rc, 1);
    CHECK_EQ(out, static_cast<u16>(7));
    // tighten the range so favorability 60 falls out -> miss
    rc = ObjectSearchFindOneByPaletteRange(&ref, nullptr, 70.0f, 90.0f, &out, 0, 0);
    CHECK_EQ(rc, 0);
    PathfindMapSetHooks(nullptr);
}
