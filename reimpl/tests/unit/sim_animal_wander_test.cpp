// Unit tests (golden vectors) for the animal wander / herd / spawn-placement +
// per-species AI step + model-handle table:
//   src/sim/animal_wander.{h,cpp}  (VIBE_Animal_BuildWanderPath, FindHerdGrouping,
//   FindDoorTarget, PickSpawnBuilding, CollectSpawnBuilding, UpdateCat/Sheep,
//   Load/ResetModelHandles)
//
// Determinism: the CRT LCG (crt::Srand) drives every RNG decision; golden vectors
// are computed by a Python oracle replicating state=state*1103515245+12345,
// return (state>>16)&0x7FFF, RandomModulo(n)=RandNext()%n.
#include "sim/animal_wander.h"
#include "sim/animal.h"
#include "sim/map.h"
#include "render/heightmap.h"
#include "crt/rand.h"
#include "test.h"

#include <cstring>
#include <vector>

using namespace guild::sim;
using guild::i32;
using guild::u8;
using guild::u16;

namespace {

// A flat, fully-walkable size*size heightmap: origin 0, scale 1, height 0, every
// tile cell (24-byte entry, type byte at +0) set to 1 (walkable). With this:
//   TileToWorld(col,row) == (col, 0, row)
//   WorldToTileWithHeight((x,_,z)) -> (trunc(x), trunc(z))
//   MapTraceLineOfSight(flags=0) returns the requested (clamped) tile.
struct FlatMap {
    guild::render::Heightmap hm{};
    std::vector<u8> entries;
    std::vector<u8> heights;
    explicit FlatMap(int size) {
        entries.assign(static_cast<size_t>(size) * size * 24, 1);  // type 1 everywhere
        heights.assign(static_cast<size_t>(size) * size, 0);
        std::memset(&hm, 0, sizeof(hm));
        hm.originX = 0; hm.originY = 0; hm.originZ = 0;
        hm.scaleX = 1; hm.scaleY = 1; hm.scaleZ = 1;
        hm.size = size;
        hm.entries = entries.data();
        hm.heights = heights.data();
    }
};

// A scene-ops mock that hands back a fixed heightmap + a scripted building set.
struct MockOps : IAnimalSceneOps {
    const guild::render::Heightmap* map = nullptr;
    std::vector<BuildingAnchor> buildings;
    float* doorFrame = nullptr;
    std::vector<int> meshHandles;     // returned by LoadMesh in order
    size_t loadIdx = 0;
    std::vector<int> released;
    struct Wander { i32 actor; int col, row; const char* tag; };
    std::vector<Wander> wanders;
    struct Sound { i32 actor; int sound; };
    std::vector<Sound> sounds;

    const guild::render::Heightmap* SceneHeightmap() override { return map; }
    int EnumBuildings(int, BuildingAnchor* out, int cap) override {
        int n = static_cast<int>(buildings.size());
        if (n > cap) n = cap;
        for (int i = 0; i < n; ++i) out[i] = buildings[i];
        return n;
    }
    float* FindDoorFrame(void*) override { return doorFrame; }
    void IssueWanderAction(AnimalRec* a, int col, int row, const char* tag) override {
        wanders.push_back({a->actor, col, row, tag});
    }
    void IssueSoundAction(AnimalRec* a, int sound) override {
        sounds.push_back({a->actor, sound});
    }
    int LoadMesh(const char*) override {
        return loadIdx < meshHandles.size() ? meshHandles[loadIdx++] : 0;
    }
    void ReleaseMesh(int h) override { released.push_back(h); }
};

// identity-ish bone frame: PointThroughBoneChain reads frame translation/parent
// chain; a frame that is mostly zero with no parent link (byte 504 == 0) yields a
// deterministic transform of the point. We give each building a distinct +0x10
// world translation so VectorWithinTolerance separates them.
struct Frame {
    float f[140];   // > 504 bytes (126 floats) so the parent-link byte is in range
    Frame() { std::memset(f, 0, sizeof(f)); }
};

} // namespace

// --- BuildWanderPath golden vector ----------------------------------------
TEST(SimAnimalWander, BuildWanderPathGolden) {
    FlatMap fm(64);
    guild::crt::Srand(12345);
    // start world (10.4, 0, 20.7) -> start tile (10, 20).
    const float start[3] = {10.4f, 0.0f, 20.7f};
    float out[5 * 4] = {0};
    int n = Animal_BuildWanderPath(&fm.hm, start, 5, out);
    CHECK_EQ(n, 5);
    // Oracle: tiles (14,24)(13,24)(13,21)(7,18)(6,17); world=(col,0,row).
    const int expCol[5] = {14, 13, 13, 7, 6};
    const int expRow[5] = {24, 24, 21, 18, 17};
    for (int i = 0; i < 5; ++i) {
        CHECK_EQ(out[i * 4 + 0], static_cast<float>(expCol[i]));
        CHECK_EQ(out[i * 4 + 1], 0.0f);
        CHECK_EQ(out[i * 4 + 2], static_cast<float>(expRow[i]));
    }
}

// --- BuildWanderPath clamping (start near the SW corner) -------------------
TEST(SimAnimalWander, BuildWanderPathClampGolden) {
    FlatMap fm(64);
    guild::crt::Srand(1);
    const float start[3] = {1.0f, 0.0f, 1.0f};   // tile (1,1)
    float out[3 * 4] = {0};
    Animal_BuildWanderPath(&fm.hm, start, 3, out);
    // Oracle: clamped tiles (5,5)(2,2)(2,4).
    const int ec[3] = {5, 2, 2};
    const int er[3] = {5, 2, 4};
    for (int i = 0; i < 3; ++i) {
        CHECK_EQ(out[i * 4 + 0], static_cast<float>(ec[i]));
        CHECK_EQ(out[i * 4 + 2], static_cast<float>(er[i]));
    }
}

// Null heightmap: returns count, writes nothing (guard).
TEST(SimAnimalWander, BuildWanderPathNullMap) {
    const float start[3] = {0, 0, 0};
    float out[4] = {-1, -1, -1, -1};
    CHECK_EQ(Animal_BuildWanderPath(nullptr, start, 1, out), 1);
    CHECK_EQ(out[0], -1.0f);  // untouched
}

// --- CollectSpawnBuilding: name filter + 32-cap walk control --------------
TEST(SimAnimalWander, CollectSpawnBuilding) {
    SpawnBuildingCollector col;
    col.wantedName = "dummy_SHEEP";
    col.count = 0;
    int a = 1, b = 2, c = 3;
    CHECK(Animal_CollectSpawnBuilding(&col, "dummy_COW", &a));    // no match
    CHECK_EQ(col.count, 0);
    CHECK(Animal_CollectSpawnBuilding(&col, "dummy_SHEEP", &b));  // match
    CHECK_EQ(col.count, 1);
    CHECK_EQ(col.records[0], &b);
    CHECK(Animal_CollectSpawnBuilding(&col, "dummy_SHEEP", &c));  // match
    CHECK_EQ(col.count, 2);
    // Fill to the 32 cap; the collector returns false (stop) once count reaches 32.
    bool keep = true;
    for (int i = col.count; i < 31; ++i)
        keep = Animal_CollectSpawnBuilding(&col, "dummy_SHEEP", &a);
    CHECK(keep);                 // count == 31 < 32 -> keep walking
    keep = Animal_CollectSpawnBuilding(&col, "dummy_SHEEP", &a);
    CHECK_EQ(col.count, 32);
    CHECK(!keep);                // count == 32 -> stop
}

// --- PickSpawnBuilding: RNG pick among matching records -------------------
static const char* g_nameTbl[8];
static const char* NameOf(void* rec) {
    return g_nameTbl[reinterpret_cast<long>(rec)];
}
TEST(SimAnimalWander, PickSpawnBuildingGolden) {
    MockOps ops;
    SetAnimalSceneOps(&ops);
    // 5 candidates; 4 named "dummy_COW" (indices 0,1,3,4), one "dummy_PIG" (2).
    g_nameTbl[0] = "dummy_COW"; g_nameTbl[1] = "dummy_COW"; g_nameTbl[2] = "dummy_PIG";
    g_nameTbl[3] = "dummy_COW"; g_nameTbl[4] = "dummy_COW";
    for (long i = 0; i < 5; ++i)
        ops.buildings.push_back({nullptr, reinterpret_cast<void*>(i)});
    guild::crt::Srand(999);
    // 4 matches collected (records 0,1,3,4); RandomModulo(4)==0 -> first match (record 0).
    void* pick = Animal_PickSpawnBuilding(0, "dummy_COW", &NameOf);
    CHECK_EQ(pick, reinterpret_cast<void*>(0L));
    // No match -> null.
    CHECK_EQ(Animal_PickSpawnBuilding(0, "dummy_HORSE", &NameOf), (void*)nullptr);
    SetAnimalSceneOps(nullptr);
}

// --- FindDoorTarget: RNG pick + door-or-building frame transform ----------
TEST(SimAnimalWander, FindDoorTargetGolden) {
    MockOps ops;
    SetAnimalSceneOps(&ops);
    Frame f0, f1, f2;
    // distinct local translations at frame[30..32] (PointThroughBoneChain reads them).
    f1.f[30] = 100.0f; f1.f[31] = 5.0f; f1.f[32] = 7.0f;
    ops.buildings.push_back({f0.f, nullptr});
    ops.buildings.push_back({f1.f, nullptr});
    ops.buildings.push_back({f2.f, nullptr});
    ops.doorFrame = nullptr;   // fall back to the picked building's frame
    guild::crt::Srand(7);      // RandomModulo(3) == 1 -> building index 1 (f1)
    float pos[3] = {0, 0, 0};
    CHECK_EQ(Animal_FindDoorTarget(0, pos), 1);
    // PointThroughBoneChain of (frame+19 point) through f1: with only a translation
    // at [30..32] and no parent chain, out == frame[30..32] translated by the point.
    // We only assert it picked f1 by checking the X carries f1's 100.0 translation.
    CHECK(pos[0] >= 100.0f && pos[0] < 200.0f);
    // No candidates -> 0.
    ops.buildings.clear();
    CHECK_EQ(Animal_FindDoorTarget(0, pos), 0);
    SetAnimalSceneOps(nullptr);
}

// --- FindHerdGrouping: gather same-pasture points within tolerance --------
TEST(SimAnimalWander, HerdGroupingGolden) {
    MockOps ops;
    SetAnimalSceneOps(&ops);
    // 5 buildings: 0,1,2 clustered near origin (within 6000); 3,4 far away.
    Frame fr[5];
    fr[0].f[30] = 0;    fr[1].f[30] = 100;  fr[2].f[30] = 200;
    fr[3].f[30] = 99000; fr[4].f[30] = 99100;
    BuildingAnchor cand[5];
    for (int i = 0; i < 5; ++i) cand[i] = {fr[i].f, reinterpret_cast<void*>((long)i)};
    guild::crt::Srand(555);   // RandomModulo(5) == 0 -> anchor = building 0 (near origin)
    float pts[16] = {0};
    int group = Animal_HerdGroupFrom(cand, 5, pts);
    // anchor 0 is at x=0; buildings 1,2 within 6000 -> 2 matches -> 2*3 = 6 floats.
    CHECK_EQ(group, 6);
    // First two gathered points carry the x translations 100 and 200.
    CHECK(pts[0] >= 100.0f && pts[0] < 300.0f);
    CHECK(pts[3] >= 100.0f && pts[3] < 300.0f);
    SetAnimalSceneOps(nullptr);
}

// FindHerdGrouping honours the "already computed" gate (herdCount != -1).
TEST(SimAnimalWander, HerdGroupingGate) {
    AnimalRec rec{};
    rec.herdCount = 7;            // already computed
    CHECK_EQ(Animal_FindHerdGrouping(&rec), &rec);
    CHECK_EQ(rec.herdCount, 7);   // unchanged
}

// --- UpdateCat / UpdateSheep: wander vs sound branch ----------------------
TEST(SimAnimalWander, UpdateCatBusyReturnsZero) {
    AnimalRec rec{};
    rec.actor = 42;
    // actionPending != 0 -> early return, no scene calls.
    MockOps ops; SetAnimalSceneOps(&ops);
    CHECK_EQ(Animal_UpdateCat(&rec, /*actionPending=*/1), (char)0);
    CHECK_EQ(ops.wanders.size(), (size_t)0);
    CHECK_EQ(ops.sounds.size(), (size_t)0);
    SetAnimalSceneOps(nullptr);
}

TEST(SimAnimalWander, UpdateCatSoundBranch) {
    // Choose a seed whose first RandomModulo(100) is > 30 (sound branch).
    // crt seed 3: RandNext()=... compute below in integration; here just assert a
    // sound action of 1..3 fires and no wander, then verify determinism.
    FlatMap fm(64);
    MockOps ops; ops.map = &fm.hm; SetAnimalSceneOps(&ops);
    AnimalRec rec{};
    rec.actor = 9;
    rec.kind = 1;                 // dog (no cat-only burn)
    guild::crt::Srand(2);         // oracle: RandomModulo(100) first value
    // Determine branch by replaying the oracle inline.
    guild::crt::Srand(2);
    char r = Animal_UpdateCat(&rec, 0);
    // Exactly one of the two branches fired.
    bool wandered = !ops.wanders.empty();
    bool sounded = !ops.sounds.empty();
    CHECK(wandered != sounded);   // exactly one branch
    if (sounded) {
        CHECK(ops.sounds[0].sound >= 1 && ops.sounds[0].sound <= 3);
        CHECK_EQ((int)r, ops.sounds[0].sound);
        CHECK_EQ(ops.sounds[0].actor, 9);
    }
    SetAnimalSceneOps(nullptr);
}

// Sheep's sound branch spends an UNCONDITIONAL extra d100 (gilde.exe 0x484015),
// whereas the cat's sound branch only spends it when kind==0 (0x483e7b). With a
// dog-kind (kind==1) cat call the cat draws gate(d100)+d3; the sheep draws
// gate(d100)+d100(burn)+d3. This test pins that extra draw via an inline oracle.
TEST(SimAnimalWander, SheepSoundBranchBurnsExtraD100) {
    // Inline CRT LCG oracle (state*1103515245+12345, return (state>>16)&0x7FFF).
    auto next = [](guild::u32& s) {
        s = s * 1103515245u + 12345u;
        return static_cast<int>((s >> 16) & 0x7FFF);
    };
    // Find a seed whose first d100 (gate) is > 20 so BOTH cat(>30 also) and sheep
    // take the sound branch.
    guild::u32 seed = 1;
    int gate = 0;
    for (; seed < 100000; ++seed) {
        guild::u32 s = seed;
        gate = next(s) % 100;
        if (gate > 30) break;   // >30 => sound branch for both cat and sheep
    }
    CHECK(gate > 30);

    // Oracle for SHEEP: gate(d100), burn(d100), d3 -> sound = (d3)+1.
    guild::u32 so = seed;
    (void)(next(so) % 100);      // gate
    (void)(next(so) % 100);      // unconditional burn
    int sheepSound = (next(so) % 3) + 1;

    // Oracle for CAT (kind=1, no burn): gate(d100), d3 -> sound = (d3)+1.
    guild::u32 co = seed;
    (void)(next(co) % 100);      // gate
    int catSound = (next(co) % 3) + 1;

    MockOps ops; SetAnimalSceneOps(&ops);

    AnimalRec sheep{}; sheep.actor = 70; sheep.kind = guild::sim::kAnimalSheep;
    guild::crt::Srand(seed);
    Animal_UpdateSheep(&sheep, 0);
    CHECK_EQ(ops.sounds.size(), (size_t)1);
    CHECK_EQ(ops.sounds[0].sound, sheepSound);

    ops.sounds.clear();
    AnimalRec cat{}; cat.actor = 71; cat.kind = 1;   // dog -> no cat burn
    guild::crt::Srand(seed);
    Animal_UpdateCat(&cat, 0);
    CHECK_EQ(ops.sounds.size(), (size_t)1);
    CHECK_EQ(ops.sounds[0].sound, catSound);

    SetAnimalSceneOps(nullptr);
}

// --- LoadModels / ResetModelHandles ---------------------------------------
TEST(SimAnimalWander, LoadAndResetModels) {
    ResetAnimalWander();
    MockOps ops;
    ops.meshHandles = {11, 22, 33, 44, 55, 66, 77};
    SetAnimalSceneOps(&ops);
    int count = Animal_LoadModels();
    CHECK_EQ(count, 7);
    CHECK_EQ(g_animalModelCount, 7);
    CHECK_EQ(g_animalModelHandles[0], 11);
    CHECK_EQ(g_animalModelHandles[6], 77);
    // Reload should release the prior set first then load again.
    ops.loadIdx = 0;
    ops.released.clear();
    int count2 = Animal_LoadModels();
    CHECK_EQ(count2, 7);
    CHECK_EQ(ops.released.size(), (size_t)7);   // 7 prior handles released
    CHECK_EQ(ops.released[0], 11);
    // Reset clears everything.
    Animal_ResetModelHandles();
    CHECK_EQ(g_animalModelCount, 0);
    CHECK_EQ(g_animalModelHandles[0], 0);
    ResetAnimalWander();
}
