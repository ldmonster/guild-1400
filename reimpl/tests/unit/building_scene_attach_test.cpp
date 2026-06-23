// building_scene_attach_test.cpp — golden-vector unit tests for the
// play/building_scene_attach module (0x50d01c family + the .cty -> scene
// placement chain), plus a guarded e2e over the real AUGSBURG city
// (GUILD_GAME_DIR) proving real objects resolve to real models at real,
// distinct world positions. Suite: BSA.
#include "test.h"

#include "app/real_boot.h"
#include "crt/rand.h"
#include "io/archive_mount.h"
#include "io/save_world_load.h"
#include "io/vfs.h"
#include "play/building_scene_attach.h"
#include "play/object_transform.h"
#include "shim_impl/disk_filesystem.h"
#include "sim/building.h"
#include "sim/building5.h"
#include "sim/entity.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <set>
#include <string>
#include <utility>
#include <vector>

using namespace guild;
using namespace guild::play;

// ===========================================================================
// Byte-stream builder for golden scene-record vectors.
// ===========================================================================
namespace {

struct Bytes {
    std::vector<u8> v;
    void b(u8 x) { v.push_back(x); }
    void d(u32 x) {
        v.push_back((u8)x); v.push_back((u8)(x >> 8));
        v.push_back((u8)(x >> 16)); v.push_back((u8)(x >> 24));
    }
    void f(float x) { u32 u; std::memcpy(&u, &x, 4); d(u); }
    void vec(float a, float bb, float c) { f(a); f(bb); f(c); }
    void s(const char* str) {
        while (*str) v.push_back((u8)*str++);
        v.push_back(0);
    }
};

constexpr u32 kVerBB = 0x3A6C00BBu;   // the shipped city-scene version

// One v-0xBB MESH object record (kind 4 -> type 4) with no child/sibling.
void EmitMeshRecord(Bytes& bs, const char* name, u32 ownerId, u32 classOvr,
                    const char* mesh, float px, float py, float pz,
                    float ex, float ey, float ez) {
    bs.b(1);                 // presence
    bs.s(name);              // name -> node+0
    bs.d(ownerId);           // -> node+512
    bs.d(classOvr);          // -> node+535
    bs.d(4);                 // kind (mesh object)
    bs.b(0);                 // suspend byte
    bs.b(0); bs.b(0);        // flag bytes (+528 bits 7/5)
    bs.b(0);                 // +529 bit2          (ver >= 0x3A6C000D)
    bs.b(0); bs.b(0);        // +530 bits 2..3 / 4 (ver >= 0x3A6C00A6)
    bs.b(0);                 // +530 bit5          (ver >= 0x3A6C00B4)
    bs.b(0);                 // +530 bit6          (ver >= 0x3A6C00B9)
    bs.d(0);                 // -> node+532
    bs.d(mesh ? 1u : 0u);    // mesh count
    if (mesh) bs.s(mesh);    // mesh name (single-string form, ver >= 0x3A6C00A4)
    bs.vec(px, py, pz);      // POSITION -> node+76
    bs.vec(ex, ey, ez);      // EULER    -> node+132
    bs.b(0);                 // aux flag (+529 bit0)
    for (int i = 0; i < 10; ++i) {            // anchors (ver >= 0x3A6C00AF)
        bs.vec(0, 0, 0); bs.vec(0, 0, 0);
    }
    bs.b(0);                 // no child
    bs.b(0);                 // no sibling
    bs.d(0);                 // event bindings: count 0 (ver >= 0x3A6C00A7)
}

// A minimal valid 0x3A6C00BB scene header (camera + fog + 7-light rig).
void EmitHeader(Bytes& bs, u32 objCount) {
    bs.d(kVerBB);
    bs.s("MegaCam");
    bs.f(1.0f);                    // ambient
    bs.vec(0, 0, 0);               // camPos
    bs.vec(0, 0, 1);               // camTarget        (>= 0x3A6C00B5)
    bs.d(0);                       // camFlag          (>= 0x3A6C00B7)
    bs.d(0); bs.f(0); bs.f(0);     // fog color/near/far (>= 0x3A6C00B3)
    for (int i = 0; i < 7; ++i) {  // 7-light rig      (>= 0x3A6C00BA)
        bs.vec(0, 0, 0);           // light pos
        bs.vec(0, 0, 0);           // light color      (>= 0x3A6C00B5)
        for (int k = 0; k < 6; ++k) { bs.d(0); bs.f(0); bs.f(0); }
    }
    bs.d(objCount);
}

// Hook recorder for the 0x50d01c loop tests.
struct RecorderHooks : BuildingSceneHooks {
    std::vector<std::string> resolves;     // VfsResolveAndBuildPath patterns
    std::set<std::string> members;         // patterns that resolve
    std::vector<std::string> loads;        // SceneLoadObjectGroup paths
    std::vector<sim::SceneNode3*> spawned; // owned group nodes
    std::vector<void*> released;
    std::vector<std::string> banners;
    std::vector<std::string> colors;       // "r,g,b" per SetVertexColors
    int frameIterations = 0;               // extra loop passes
    i32 dialogResult = 1;                  // price-dialog answer
    int dialogCalls = 0;
    bool failLoads = false;

    ~RecorderHooks() override {
        for (auto* n : spawned) delete n;
    }
    i32 VfsResolveAndBuildPath(const char* pattern, char* out256) override {
        resolves.push_back(pattern);
        if (members.count(pattern)) {
            std::strncpy(out256, pattern, 255); out256[255] = 0;
            return 1;
        }
        return 0;
    }
    void* SceneLoadObjectGroup(const char* path) override {
        loads.push_back(path);
        if (failLoads) return nullptr;
        auto* n = new sim::SceneNode3();
        spawned.push_back(n);
        return n;
    }
    void ObjectDetachAndRelease(void* node) override { released.push_back(node); }
    void HudSetStatusBannerText(const char* t) override { banners.push_back(t); }
    void MeshSetVertexColors(void* /*n*/, u8 a, u8 b, u8 c) override {
        colors.push_back(std::to_string(a) + "," + std::to_string(b) + "," +
                         std::to_string(c));
    }
    i32 GameLogicRunFrameLoop(i32 loopId) override {
        CHECK_EQ(loopId, kFrameLoopId);
        return frameIterations-- > 0 ? 1 : 0;
    }
    i32 DialogShowMessageBox(const char* /*text*/, i32 /*mode*/) override {
        ++dialogCalls;
        return dialogResult;
    }
};

// Building5 scene-walk source feeding plot nodes into FilterBlockedBauplatze.
struct PlotWalkHooks : sim::Building5Hooks {
    std::vector<const u8*> nodes;
    const std::uint8_t* SceneNode(int index) override {
        return index < (int)nodes.size() ? nodes[index] : nullptr;
    }
};

// A fake plot node: SceneNode3 named "bk_*" with +76 local pos and no parent
// (+504 == 0) so PointThroughBoneChain yields +76 directly.
sim::SceneNode3* MakePlot(const char* name, float x, float y, float z) {
    auto* n = new sim::SceneNode3();
    std::strncpy(reinterpret_cast<char*>(n->raw), name, 63);
    n->f(76) = x; n->f(80) = y; n->f(84) = z;
    return n;
}

void SeedTypeTable(u8 type, const char* name, u8 kind) {
    sim::g_buildingTypesLoaded = true;
    u8* rec = reinterpret_cast<u8*>(&sim::g_buildingTypes[type]);
    std::memset(rec, 0, sim::kBuildingTypeStride);
    rec[0] = kind;
    std::strncpy(reinterpret_cast<char*>(rec) + 1, name, 32);
}

} // namespace

// ===========================================================================
// ReadCityObjectRecord — golden vectors from the 0x5e67c8 grammar.
// ===========================================================================
TEST(BSA, ReadRecord_MeshBody_PosEulerOwner) {
    Bytes bs;
    EmitMeshRecord(bs, "gb_KIRCHE_A", 4711, 3, "gb_KIRCHE", 123.5f, -8.25f,
                   904.0f, 0.0f, 1.5707964f, 0.0f);
    render::SceneReader r(bs.v.data(), bs.v.size());
    std::vector<CityWorldNode> out;
    int idx = ReadCityObjectRecord(r, kVerBB, out, -1);
    CHECK_EQ(idx, 0);
    CHECK_EQ((int)out.size(), 1);
    const CityWorldNode& n = out[0];
    CHECK(n.name == "gb_KIRCHE_A");
    CHECK_EQ((int)n.ownerId, 4711);          // file dword -> node+512
    CHECK_EQ((int)n.classOvr, 3);            // file dword -> node+535
    CHECK_EQ((int)n.spawnType, 4);           // kind 4 -> type 4 (mesh)
    CHECK(n.hasMesh);
    CHECK(n.meshName == "gb_KIRCHE");
    CHECK(n.pos[0] == 123.5f && n.pos[1] == -8.25f && n.pos[2] == 904.0f);
    CHECK(std::fabs(n.euler[1] - 1.5707964f) < 1e-6f);
    CHECK(r.pos() == bs.v.size());           // every byte consumed exactly
}

TEST(BSA, ReadRecord_DummyAndChildSiblingFraming) {
    Bytes bs;
    bs.b(1);
    bs.s("dummy_PARENT");
    bs.d(0); bs.d(0); bs.d(3); bs.b(0);      // kind 3 -> dummy type 3
    bs.vec(10, 20, 30); bs.vec(0, 0, 0);
    bs.b(0);
    for (int i = 0; i < 10; ++i) { bs.vec(0,0,0); bs.vec(0,0,0); }
    bs.b(1);                                   // HAS child
    {   // child dummy (kind 2 -> type 2)
        bs.b(1); bs.s("dummy_CHILD"); bs.d(0); bs.d(0); bs.d(2); bs.b(0);
        bs.vec(1, 2, 3); bs.vec(0, 0, 0); bs.b(0);
        for (int i = 0; i < 10; ++i) { bs.vec(0,0,0); bs.vec(0,0,0); }
        bs.b(0); bs.b(0); bs.d(0);             // no child/sibling, 0 events
    }
    bs.b(1);                                   // HAS sibling
    {   // sibling dummy
        bs.b(1); bs.s("dummy_SIB"); bs.d(0); bs.d(0); bs.d(3); bs.b(0);
        bs.vec(7, 8, 9); bs.vec(0, 0, 0); bs.b(0);
        for (int i = 0; i < 10; ++i) { bs.vec(0,0,0); bs.vec(0,0,0); }
        bs.b(0); bs.b(0); bs.d(0);
    }
    bs.d(0);                                   // parent's event bindings
    render::SceneReader r(bs.v.data(), bs.v.size());
    std::vector<CityWorldNode> out;
    ReadCityObjectRecord(r, kVerBB, out, -1);
    CHECK_EQ((int)out.size(), 3);
    CHECK(out[0].name == "dummy_PARENT");
    CHECK(out[1].name == "dummy_CHILD");
    CHECK_EQ(out[1].parent, 0);                // SetParent(parent, child)
    CHECK(out[2].name == "dummy_SIB");
    CHECK_EQ(out[2].parent, -1);               // sibling shares the parent
    CHECK(out[0].spawnType == 3 && out[1].spawnType == 2);
    CHECK(r.pos() == bs.v.size());
}

TEST(BSA, ReadRecord_BangPrefixStrip) {
    // '!'-prefixed name + suspend==0 strips the '!' (0x5e6c96).
    Bytes bs;
    bs.b(1); bs.s("!sLICHT"); bs.d(0); bs.d(0); bs.d(2); bs.b(0);
    bs.vec(0,0,0); bs.vec(0,0,0); bs.b(0);
    for (int i = 0; i < 10; ++i) { bs.vec(0,0,0); bs.vec(0,0,0); }
    bs.b(0); bs.b(0); bs.d(0);
    render::SceneReader r(bs.v.data(), bs.v.size());
    std::vector<CityWorldNode> out;
    ReadCityObjectRecord(r, kVerBB, out, -1);
    CHECK_EQ((int)out.size(), 1);
    CHECK(out[0].name == "sLICHT");
}

TEST(BSA, ParseCityWorld_HeaderPlusObjects) {
    Bytes bs;
    EmitHeader(bs, 2);
    EmitMeshRecord(bs, "gb_A", 1, 3, "mA", 100, 0, 200, 0, 0, 0);
    EmitMeshRecord(bs, "gb_B", 2, 3, "mB", -50, 5, 75, 0, 0.5f, 0);
    CityWorld w = ParseCityWorld(bs.v.data(), bs.v.size());
    CHECK(w.ok);
    CHECK_EQ(w.header.tag, kVerBB);
    CHECK_EQ((int)w.nodes.size(), 2);
    CHECK(w.nodes[0].pos[0] == 100.0f && w.nodes[1].pos[0] == -50.0f);
    CHECK_EQ((int)w.nodes[1].ownerId, 2);
}

// ===========================================================================
// SpawnKindForVersion / SpawnTypeForKind golden vectors.
// ===========================================================================
TEST(BSA, SpawnKindLadders) {
    CHECK_EQ((int)SpawnKindForVersion(4, kVerBB), 4);
    CHECK_EQ((int)SpawnKindForVersion(7, kVerBB), 7);
    // old ladder: 2 -> 3, 3/4 -> 4, 9 -> 2 (default)
    CHECK_EQ((int)SpawnKindForVersion(2, 0x3A6C00A0u), 3);
    CHECK_EQ((int)SpawnKindForVersion(3, 0x3A6C00A0u), 4);
    CHECK_EQ((int)SpawnKindForVersion(9, 0x3A6C00A0u), 2);
    // spawn type from name for kind >= 5: 'r'->6 's'->8 'p'->7 else 5
    CHECK_EQ((int)SpawnTypeForKind(5, "room"), 6);
    CHECK_EQ((int)SpawnTypeForKind(5, "sfx"), 8);
    CHECK_EQ((int)SpawnTypeForKind(5, "part"), 7);
    CHECK_EQ((int)SpawnTypeForKind(5, "zzz"), 5);
    CHECK_EQ((int)SpawnTypeForKind(1, "anything"), 1);
}

// ===========================================================================
// BuildingComputePlacementHeight @0x50cec0 — plot yaw -> (0, yaw, 0).
// ===========================================================================
TEST(BSA, ComputePlacementHeight_PlotYaw) {
    BuildingSceneHooks h;
    sim::SceneNode3 plot;
    float e[3] = {0.0f, 1.5707964f, 0.0f};
    h.ObjectSetWorldTranslation(&plot, e);     // real MatrixFromEuler -> +396
    float out[3] = {99, 99, 99};
    BuildingComputePlacementHeight(out, plot.raw, h);
    CHECK(out[0] == 0.0f && out[2] == 0.0f);
    // VectorAngleBetween @0x5ca334's +acos branch wraps by -2pi (dbl_628CE8):
    // for a +pi/2 plot the engine yields -3pi/2 — the SAME orientation mod 2pi.
    CHECK(std::fabs(out[1] - (-4.7123890f)) < 1e-3f);
    CHECK(std::fabs(std::sin(out[1]) - 1.0f) < 1e-3f);  // == sin(pi/2)
    sim::SceneNode3 plot0;
    float z[3] = {0, 0, 0};
    h.ObjectSetWorldTranslation(&plot0, z);
    BuildingComputePlacementHeight(out, plot0.raw, h);
    CHECK(std::fabs(out[1]) < 1e-6f);
}

// ===========================================================================
// BuildingAlignMeshToTerrain @0x50cfd0 — terrain clamp restores pos.
// ===========================================================================
namespace {
struct SinkTerrainHooks : BuildingSceneHooks {
    float sinkY = 0.0f;
    void CollisionResolveMeshAgainstTerrain(void* node, i32, i32) override {
        static_cast<sim::SceneNode3*>(node)->f(80) = sinkY;  // push node.y
    }
};
} // namespace

TEST(BSA, AlignMeshToTerrain_RestoresWhenBelow) {
    SinkTerrainHooks h;
    sim::SceneNode3 node;
    float pos[3] = {10, 50, 20};
    h.sinkY = 40.0f;                            // terrain drops it BELOW pos.y
    BuildingAlignMeshToTerrain(&node, pos, h);
    CHECK(node.f(80) == 50.0f);                 // second SetPosition restored
    h.sinkY = 60.0f;                            // terrain ABOVE pos.y — keep
    BuildingAlignMeshToTerrain(&node, pos, h);
    CHECK(node.f(80) == 60.0f);
}

// ===========================================================================
// ObjectBuildModelName @0x4ffe0c / ObjectRebuildModelByOwner @0x5a8140.
// ===========================================================================
TEST(BSA, BuildModelName_Gb) {
    std::memset(sim::g_objects, 0, sizeof(sim::g_objects));
    SeedTypeTable(7, "KIRCHE", 2);
    BuildingSceneHooks hooks;
    BuildingSceneEnv env; env.hooks = &hooks;

    u8 rec[169] = {};
    rec[0] = 7;                                 // building type
    sim::SceneNode3 node;
    node.b(533) = 1;                            // spawn type 1
    ObjectBuildModelName(&node, rec, 2, 42, 5, env);
    CHECK(std::strcmp(reinterpret_cast<char*>(node.raw), "gb_KIRCHE") == 0);
    CHECK_EQ((int)node.b(535), 3);              // class byte = 3 (building)
    CHECK_EQ(node.d(512), 42);                  // node+512 = record slot link
    i32 tok; std::memcpy(&tok, rec + 97, 4);
    CHECK_EQ(tok, 5);                           // rec+97 = node token link
    // type 10 -> class byte 0 / +536 = 0
    SeedTypeTable(10, "MARKT", 1);
    u8 rec10[169] = {}; rec10[0] = 10;
    sim::SceneNode3 n10;
    ObjectBuildModelName(&n10, rec10, 2, 1, 1, env);
    CHECK_EQ((int)n10.b(535), 0);
    CHECK_EQ(n10.d(536), 0);
    sim::g_buildingTypesLoaded = false;
}

TEST(BSA, RebuildModelByOwner_BindsMatchingRecord) {
    std::memset(sim::g_objects, 0, sizeof(sim::g_objects));
    SeedTypeTable(3, "SCHMIEDE", 1);
    BuildingSceneHooks hooks;
    BuildingSceneEnv env; env.hooks = &hooks;

    u8* rec = reinterpret_cast<u8*>(sim::g_objects) + 5 * 169;  // slot 5
    rec[0] = 3;
    i32 id = 9001; std::memcpy(rec + 1, &id, 4);

    sim::SceneNode3 node;
    node.d(512) = 9001;                         // the .ed3 saved owner id
    node.b(533) = 1;
    CHECK_EQ(ObjectRebuildModelByOwner(&node, 77, env), 1);
    CHECK(std::strcmp(reinterpret_cast<char*>(node.raw), "gb_SCHMIEDE") == 0);
    CHECK_EQ(node.d(512), 5);                   // overwritten with the slot link
    i32 tok; std::memcpy(&tok, rec + 97, 4);
    CHECK_EQ(tok, 77);
    std::memset(sim::g_objects, 0, sizeof(sim::g_objects));
    sim::g_buildingTypesLoaded = false;
}

// ===========================================================================
// ResolveGebaeudeOgrVariants — the LABEL_9/LABEL_15 probe.
// ===========================================================================
TEST(BSA, VariantProbe_BaseAndSuffixes) {
    RecorderHooks hooks;
    BuildingSceneEnv env; env.hooks = &hooks;
    env.pathPrefix = "";
    hooks.members.insert("gebaeude/*gb_KIRCHE.ogr");
    hooks.members.insert("gebaeude/*gb_KIRCHE_A.ogr");
    hooks.members.insert("gebaeude/*gb_KIRCHE_B.ogr");
    char paths[kMaxOgrVariants][kOgrPathSlot];
    i32 n = ResolveGebaeudeOgrVariants("gb_KIRCHE", env, paths, kMaxOgrVariants);
    CHECK_EQ(n, 3);                                  // base + _A + _B
    CHECK(std::strcmp(paths[0], "gebaeude/*gb_KIRCHE.ogr") == 0);
    CHECK(std::strcmp(paths[1], "gebaeude/*gb_KIRCHE_A.ogr") == 0);
    CHECK(std::strcmp(paths[2], "gebaeude/*gb_KIRCHE_B.ogr") == 0);
    CHECK(hooks.resolves.back() == "gebaeude/*gb_KIRCHE_C.ogr");  // stop probe
}

TEST(BSA, VariantProbe_MissingBaseStillTriesA) {
    // gb_KIRCHE.ogr absent but _A/_B/_C present (the real Groups.BIN KIRCHE
    // family): the first miss with counter==0 advances to the variants.
    RecorderHooks hooks;
    BuildingSceneEnv env; env.hooks = &hooks;
    hooks.members.insert("gebaeude/*gb_KIRCHE_A.ogr");
    hooks.members.insert("gebaeude/*gb_KIRCHE_B.ogr");
    hooks.members.insert("gebaeude/*gb_KIRCHE_C.ogr");
    char paths[kMaxOgrVariants][kOgrPathSlot];
    i32 n = ResolveGebaeudeOgrVariants("gb_KIRCHE", env, paths, kMaxOgrVariants);
    CHECK_EQ(n, 3);
    CHECK(std::strcmp(paths[0], "gebaeude/*gb_KIRCHE_A.ogr") == 0);
    CHECK(std::strcmp(paths[2], "gebaeude/*gb_KIRCHE_C.ogr") == 0);
}

TEST(BSA, VariantProbe_NoneFound) {
    RecorderHooks hooks;
    BuildingSceneEnv env; env.hooks = &hooks;
    char paths[kMaxOgrVariants][kOgrPathSlot];
    CHECK_EQ(ResolveGebaeudeOgrVariants("gb_NIX", env, paths, kMaxOgrVariants), 0);
    CHECK_EQ((int)hooks.resolves.size(), 2);    // base + 'A', then stop
    CHECK(hooks.resolves[1] == "gebaeude/*gb_NIX_A.ogr");
}

// ===========================================================================
// BuildingLoadAndAlignGebaeudeModel @0x50d01c — scripted-loop goldens.
// ===========================================================================
namespace {
struct MainFnFixture {
    RecorderHooks hooks;
    PlotWalkHooks walk;
    BuildingSceneEnv env;
    std::vector<sim::SceneNode3*> plots;
    char outPath[260];

    MainFnFixture() {
        env.hooks = &hooks;
        env.pathPrefix = "";
        env.textNoPlot = "NO_PLOT";
        env.textBlocked = "BLOCKED";
        std::memset(outPath, 0xCD, sizeof(outPath));
        sim::SetBuilding5Hooks(&walk);
        SeedTypeTable(7, "KIRCHE", 1);          // kind 1: no duplicate gate
        hooks.members.insert("gebaeude/*gb_KIRCHE.ogr");
        crt::Srand(1234);                       // deterministic RandomModulo
    }
    ~MainFnFixture() {
        sim::SetBuilding5Hooks(nullptr);
        for (auto* p : plots) delete p;
        sim::g_buildingTypesLoaded = false;
    }
    void AddPlot(const char* name, float x, float y, float z) {
        plots.push_back(MakePlot(name, x, y, z));
        walk.nodes.push_back(plots.back()->raw);
    }
};
} // namespace

TEST(BSA, MainFn_NoOgr_ReturnsNullAndLogs) {
    MainFnFixture fx;
    fx.hooks.members.clear();                   // nothing resolves
    const u8* plot = BuildingLoadAndAlignGebaeudeModel(7, fx.outPath, fx.env);
    CHECK(plot == nullptr);
    CHECK(fx.hooks.lastError.find("Could not find any 3D-Object-Group") !=
          std::string::npos);
}

TEST(BSA, MainFn_NoFreePlot_ShowsNoPlotDialog) {
    MainFnFixture fx;                           // no plots installed
    const u8* plot = BuildingLoadAndAlignGebaeudeModel(7, fx.outPath, fx.env);
    CHECK(plot == nullptr);
    CHECK_EQ(fx.hooks.dialogCalls, 1);          // ShowMessageBox(textNoPlot, 0)
}

TEST(BSA, MainFn_ConfirmPlacesOnNearestPlot) {
    MainFnFixture fx;
    fx.AddPlot("bk_FAR", 500, 0, 500);
    fx.AddPlot("bk_NEAR", 10, 0, 10);
    fx.hooks.frameIterations = 2;
    fx.env.leftClick = 1;                       // click-confirm immediately
    fx.hooks.dialogResult = 1;                  // accept the price dialog
    const u8* plot = BuildingLoadAndAlignGebaeudeModel(7, fx.outPath, fx.env);
    CHECK(plot != nullptr);
    CHECK(plot == fx.plots[1]->raw);            // bk_NEAR wins the distance scan
    CHECK(std::strcmp(fx.outPath, "gebaeude/*gb_KIRCHE.ogr") == 0);
    CHECK_EQ((int)fx.hooks.loads.size(), 3);    // 2 wimpels + 1 preview
    CHECK((int)fx.hooks.released.size() >= 2);  // wimpels released
    bool sawBlue = false;
    for (auto& c : fx.hooks.colors) sawBlue |= (c == "64,64,192");
    CHECK(sawBlue);                             // valid-plot tint applied
}

TEST(BSA, MainFn_CancelReturnsNullAndEmptyPath) {
    MainFnFixture fx;
    fx.AddPlot("bk_ONLY", 5, 0, 5);
    fx.hooks.frameIterations = 1;
    fx.env.leftClick = 1;
    fx.hooks.dialogResult = 0;                  // decline the price dialog
    const u8* plot = BuildingLoadAndAlignGebaeudeModel(7, fx.outPath, fx.env);
    CHECK(plot == nullptr);
    CHECK(fx.outPath[0] == 0);                  // byte_621554 "" copied out
    CHECK((int)fx.hooks.released.size() >= 2);  // preview + wimpel released
}

TEST(BSA, MainFn_DuplicateChurchGateDeclined) {
    MainFnFixture fx;
    SeedTypeTable(9, "KATHEDRALE", 2);          // kind 2 -> duplicate gate
    fx.hooks.members.insert("gebaeude/*gb_KATHEDRALE.ogr");
    struct GateHooks : RecorderHooks {
        u8 rec[4] = {9, 0, 0, 0};
        const u8* PersonQueryBegin(i32, i32, u16, i32, i32) override { return rec; }
    } gate;
    gate.dialogResult = 0;                      // decline "build another?"
    fx.env.hooks = &gate;
    const u8* plot = BuildingLoadAndAlignGebaeudeModel(9, fx.outPath, fx.env);
    CHECK(plot == nullptr);
    CHECK_EQ(gate.dialogCalls, 1);
}

// ===========================================================================
// AttachCityBuildingScene — synthetic golden world.
// ===========================================================================
TEST(BSA, Attach_SyntheticCity_RealPositionsAndLink) {
    std::memset(sim::g_objects, 0, sizeof(sim::g_objects));
    SeedTypeTable(3, "SCHMIEDE", 1);

    Bytes bs;
    EmitHeader(bs, 3);
    EmitMeshRecord(bs, "gb_SCHMIEDE", 9001, 3, "gb_SCHMIEDE", 150, 2, -300,
                   0, 0.7853982f, 0);          // owner-bound building
    EmitMeshRecord(bs, "ub_HAUS_07", 0, 0, "ub_HAUS", -75, 0, 40, 0, 0, 0);
    EmitMeshRecord(bs, "bk_HANDWERK", 0, 0, nullptr, 33, 0, 44, 0, 0, 0);

    u8* rec = reinterpret_cast<u8*>(sim::g_objects) + 2 * 169;  // slot 2
    rec[0] = 3;
    i32 id = 9001; std::memcpy(rec + 1, &id, 4);

    RecorderHooks hooks;
    hooks.members.insert("gebaeude/*gb_SCHMIEDE.ogr");
    BuildingSceneEnv env; env.hooks = &hooks; env.pathPrefix = "";

    CityAttachResult R = AttachCityBuildingScene(bs.v.data(), bs.v.size(), env);
    CHECK(R.ok);
    CHECK_EQ(R.parsedNodes, 3);
    CHECK_EQ(R.meshNodes, 2);
    CHECK_EQ(R.liveNodeCount, 3);
    CHECK_EQ((int)R.attached.size(), 1);
    const AttachedCityObject& a = R.attached[0];
    CHECK_EQ(a.objSlot, 2);
    CHECK_EQ((int)a.typeId, 3);
    CHECK_EQ((int)a.ownerId, 9001);
    CHECK(a.gbName == "gb_SCHMIEDE");
    CHECK(a.ogrPattern == "gebaeude/*gb_SCHMIEDE.ogr");
    CHECK(a.pos[0] == 150.0f && a.pos[1] == 2.0f && a.pos[2] == -300.0f);
    CHECK(std::fabs(a.euler[1] - 0.7853982f) < 1e-6f);
    // the LIVE node carries the real engine placement (+76 / +396 matrix).
    // (spawn type 4 is treated as non-drawable by object_transform's TestNodeFlag
    // model, so read the raw fields + WorldMatrixYaw on the +396 frame.)
    sim::SceneNode3* node = R.liveNodes[a.nodeIndex].get();
    CHECK(node->f(76) == 150.0f && node->f(80) == 2.0f && node->f(84) == -300.0f);
    CHECK(std::fabs(WorldMatrixYaw(&node->f(396)) - 0.7853982f) < 1e-4f);
    // the static house node also landed at its real position:
    sim::SceneNode3* house = R.liveNodes[1].get();
    CHECK(house->f(76) == -75.0f && house->f(84) == 40.0f);
    // rec+97 received the node-token backlink:
    i32 tok; std::memcpy(&tok, rec + 97, 4);
    CHECK_EQ(tok, a.nodeIndex);
    std::memset(sim::g_objects, 0, sizeof(sim::g_objects));
    sim::g_buildingTypesLoaded = false;
}

// ===========================================================================
// E2E (guarded): the REAL AUGSBURG — .cty world + stadt_AUGSBURG.ed3 +
// Groups.BIN. Proves real city objects resolve to real models at real,
// DISTINCT world positions through the reconstructed chain.
// ===========================================================================
namespace {
std::string GameDirOrEmpty() {
    if (const char* env = std::getenv("GUILD_GAME_DIR"))
        return env;
    return "";
}
} // namespace

TEST(BSA, E2E_Augsburg_RealCityAttach) {
    const std::string dir = GameDirOrEmpty();
    if (dir.empty()) {
        std::printf("[ SKIP ] GUILD_GAME_DIR not set\n");
        return;
    }
    shim::DiskFileSystem fs(dir);
    if (!fs.exists("Resources/scenes.BIN") || !fs.exists("Resources/Groups.BIN") ||
        !fs.exists("Resources/gamedata/Cities/AUGSBURG.cty")) {
        std::printf("[ SKIP ] real assets not present under %s\n", dir.c_str());
        return;
    }

    io::ArchiveMount scenesM, groupsM;
    CHECK(scenesM.Mount(&fs, "Resources/scenes.BIN", true));
    CHECK(groupsM.Mount(&fs, "Resources/Groups.BIN", true));

    // 0. the stadt_AUGSBURG.ed3 TEMPLATE: same scene grammar, ALL owner ids 0
    //    (the placement-fields finding: owner links live in the .cty's embedded
    //    scene, not the scenes.BIN template).
    {
        std::vector<u8> ed3;
        CHECK(scenesM.OpenMember("Staedte/stadt_AUGSBURG.ed3", ed3));
        CHECK(ed3.size() > 1000);
        CityWorld tpl = ParseCityWorld(ed3.data(), ed3.size());
        CHECK(tpl.ok);
        CHECK((int)tpl.nodes.size() > 100);
        int tplOwners = 0;
        for (const auto& n : tpl.nodes)
            if (n.ownerId) ++tplOwners;
        CHECK_EQ(tplOwners, 0);
    }

    // 1. the live world (sim::g_objects) + the EMBEDDED scene blob from the
    //    real .cty seed — the stream VIBE_Save_PostLoadInitScene @0x5a7ef8
    //    hands to VIBE_Scene_LoadFromStream @0x5e7e38.
    app::RealGameAssets assets = app::MountRealGameAssets(&fs, dir, "Gilde.INI");
    CHECK(assets.vfsBound);
    sim::ResetEntityArrays();
    io::WorldState world{};
    std::vector<u8> blob;
    const bool loaded =
        io::LoadWorldEx("Resources/gamedata/Cities/AUGSBURG.cty", world, &blob);
    CHECK(loaded);
    CHECK(blob.size() > 1000);
    u32 tag = 0;
    std::memcpy(&tag, blob.data(), 4);
    CHECK_EQ(tag, 0x3A6C00BBu);                  // the embedded scene tag
    int aliveRecs = 0;
    for (int i = 0; i < sim::kObjectCapacity; ++i)
        if (reinterpret_cast<u8*>(sim::g_objects)[i * 169]) ++aliveRecs;
    std::printf("[bsa-e2e] alive object records: %d\n", aliveRecs);
    CHECK(aliveRecs > 0);

    // 2. parse + attach through the reconstructed chain.
    GroupsArchiveResolver hooks(&groupsM);
    BuildingSceneEnv env; env.hooks = &hooks; env.pathPrefix = "";
    CityAttachResult R = AttachCityBuildingScene(blob.data(), blob.size(), env);
    CHECK(R.ok);
    std::printf("[bsa-e2e] parsed=%d meshNodes=%d attached=%zu\n", R.parsedNodes,
                R.meshNodes, R.attached.size());
    CHECK(R.parsedNodes > 100);                  // a real city is dense
    CHECK(R.meshNodes > 50);
    CHECK((int)R.attached.size() > 0);           // .cty records bound by owner id

    int resolvedModels = 0, withMeshes = 0;
    std::set<std::pair<int, int>> positions;
    for (const auto& a : R.attached) {
        if (!a.ogrMember.empty()) {
            ++resolvedModels;
            CHECK(groupsM.Find(a.ogrMember.c_str()) != nullptr);  // REAL member
        }
        if (!a.meshNames.empty()) ++withMeshes;
        positions.insert(std::make_pair((int)a.pos[0], (int)a.pos[2]));
    }
    std::printf("[bsa-e2e] resolvedModels=%d withMeshes=%d distinctPos=%zu\n",
                resolvedModels, withMeshes, positions.size());
    CHECK(resolvedModels > 0);                   // real shipped .ogr groups
    CHECK(withMeshes > 0);                       // naming real .bgf meshes
    CHECK((int)positions.size() >= 3);           // DISTINCT real positions
    bool nonZeroPos = false, nonZeroYaw = false;
    for (const auto& a : R.attached) {
        if (a.pos[0] != 0.0f || a.pos[2] != 0.0f) nonZeroPos = true;
        if (a.euler[1] != 0.0f) nonZeroYaw = true;
    }
    CHECK(nonZeroPos);
    CHECK(nonZeroYaw);

    // 3. the live nodes expose the engine placement the renderer walks:
    int visible = 0;
    for (const auto& n : R.liveNodes) {
        WorldPlacement wp = SceneNodeWorldPlacement(n.get());
        if (wp.visible) ++visible;
    }
    std::printf("[bsa-e2e] visible nodes: %d / %d\n", visible, R.liveNodeCount);
    CHECK(visible > 50);

    sim::ResetEntityArrays();
    io::VfsShutdown();
}
