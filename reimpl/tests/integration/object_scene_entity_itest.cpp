#include "test.h"

// Integration tests for object_scene_entity (Wave 30 P6): compose the deferred
// Entity / Transform / GameObject leaves into a small object/scene pipeline —
// populate the entity table, classify a mixed population, drive a selection list,
// scale + animate node records, transform points through a two-frame chain, then
// tear the whole world down with FreeAllTables. Cross-module leaves are wired to
// REAL siblings (PersonQueryBegin/IterNext over g_objects, the real
// util::PointThroughBoneChainPivot inside the Transform leaf, ResetEntityArrays
// for the default teardown) — no mocks.
#include "sim/object_scene_entity.h"
#include "sim/entity.h"
#include "sim/person.h"
#include "sim/types.h"
#include "util/transform.h"

#include <cstring>
#include <vector>

using namespace guild;
using namespace guild::sim;

namespace {
void SeedPerson(int idx, i16 marker, u8 live, u8 kind, i32 id) {
    std::memset(&g_persons[idx], 0, sizeof(Person));
    g_persons[idx].marker = marker;
    g_persons[idx].id = id;
    PersonSetByte(&g_persons[idx], kPfIsPlayer, live);
    PersonSetByte(&g_persons[idx], kPfKind, kind);
}
}  // namespace

// ---------------------------------------------------------------------------
// Populate a mixed entity population and classify it; the counts must agree with
// the per-kind predicate semantics.
// ---------------------------------------------------------------------------
TEST(ObjSceneEntityItest, ClassifyMixedPopulation) {
    ResetEntityArrays();
    // kinds: 1 person, 2 person, 5 person+animal, 6 carried+animal, 7 carried+animal,
    //        9 person, 30 (object, none), plus a free slot and a non-live actor.
    SeedPerson(0, 4, 1, 1, 1000);
    SeedPerson(1, 4, 1, 2, 1001);
    SeedPerson(2, 4, 1, 5, 1002);
    SeedPerson(3, 4, 1, 6, 1003);
    SeedPerson(4, 4, 1, 7, 1004);
    SeedPerson(5, 4, 1, 9, 1005);
    SeedPerson(6, 4, 1, 30, 1006);
    SeedPerson(7, -1, 1, 1, 0);   // free slot -> all predicates false
    SeedPerson(8, 4, 0, 1, 1008); // not a live actor -> all predicates false

    int persons = 0, animals = 0, carried = 0;
    for (int i = 0; i < 9; ++i) {
        if (EntityIsPersonType((u16)i)) ++persons;
        if (EntityIsAnimalType((u16)i)) ++animals;
        if (EntityIsCarriedType((u16)i)) ++carried;
    }
    // persons: kinds {1,2,5,9} on slots 0,1,2,5 -> 4
    CHECK_EQ(persons, 4);
    // animals: kinds {5,6,7} on slots 2,3,4 -> 3
    CHECK_EQ(animals, 3);
    // carried: kinds {6,7} on slots 3,4 -> 2
    CHECK_EQ(carried, 2);
}

// ---------------------------------------------------------------------------
// Build a selection list from a classified set, then probe membership through
// the verbatim even-offset scan. Items placed on probed (even) offsets are
// "in"; items on odd offsets are reported "not in".
// ---------------------------------------------------------------------------
TEST(ObjSceneEntityItest, SelectionListRoundTrip) {
    EntityClearSelectionList();
    // probed offsets within the four 17-dword groups (heads 0,17,34,51):
    // {g, g+2, g+4, ..., g+14}. Pick a few across groups.
    struct E { int slot; i32 id; };
    const E entries[] = {
        {0, 2001},   // group0 head        -> probed
        {14, 2002},  // group0 +14 (even)  -> probed
        {3, 2003},   // group0 +3  (odd)   -> skipped
        {17, 2004},  // group1 head        -> probed
        {30, 2005},  // group1 +13 (odd)   -> skipped
        {36, 2006},  // group2 (34) +2     -> probed
        {51, 2007},  // group3 head        -> probed
    };
    for (const E& e : entries) EntitySetSelectionEntry(e.slot, e.id);

    // Verify the function's quirk precisely: it probes group-relative even offsets.
    auto probed = [](int slot) {
        for (int g : {0, 17, 34, 51}) {
            if (slot == g) return true;
            int rel = slot - g;
            if (rel > 0 && rel < 17 && (rel % 2) == 0) return true;
        }
        return false;
    };
    for (const E& e : entries) {
        bool isIn = (EntityIsNotInSelectionList(e.id) == 0);
        CHECK_EQ((int)isIn, (int)probed(e.slot));
    }
    // an id never inserted is always "not in"
    CHECK_EQ(EntityIsNotInSelectionList(999999), 1);
}

// ---------------------------------------------------------------------------
// Treat a small node record array as a scene: scale each node's +148 field and
// stash a per-node animation rect, then verify the composed mutations.
// ---------------------------------------------------------------------------
TEST(ObjSceneEntityItest, NodeFieldPipeline) {
    constexpr int kNodes = 4;
    std::vector<std::vector<char>> nodes(kNodes, std::vector<char>(256, 0));
    std::vector<std::vector<i32>> recs(kNodes, std::vector<i32>(16, 0));
    for (int n = 0; n < kNodes; ++n) {
        *reinterpret_cast<float*>(nodes[n].data() + 148) = (float)(n + 1);  // 1..4
        float scale = 2.0f;
        EntityScaleField148(nodes[n].data(), &scale);
        // animation rect: (x=n*10, y=n*10+1, w=4, h=5) using the (a1,a2,a3,a4) map.
        EntityAnimationUpdate(n * 10, n * 10 + 1, /*a3*/5 + (n * 10 + 1), /*a4*/4,
                              recs[n].data());
    }
    for (int n = 0; n < kNodes; ++n) {
        CHECK_EQ(*reinterpret_cast<float*>(nodes[n].data() + 148), (float)((n + 1) * 2));
        CHECK_EQ(recs[n][9], n * 10);
        CHECK_EQ(recs[n][10], n * 10 + 1);
        CHECK_EQ(recs[n][11], n * 10 + 4);          // a1 + a4
        CHECK_EQ(recs[n][12], (5 + n * 10 + 1) + (n * 10 + 1));  // a3 + a2
    }
}

// ---------------------------------------------------------------------------
// Transform a point through a two-frame chain (parent -> child) then into a pivot
// frame's space, exercising the REAL util::PointThroughBoneChainPivot the leaf
// reuses. With a parent translation the bone-chain output shifts; the pivot then
// subtracts its two translations.
// ---------------------------------------------------------------------------
TEST(ObjSceneEntityItest, PivotThroughFrameChain) {
    // child frame with a parent link (byte 504 == float index 126).
    float parent[140] = {0};
    parent[99] = 1.0f; parent[104] = 1.0f; parent[109] = 1.0f;   // identity 3x3
    parent[19] = 0.0f;
    parent[27] = 0.0f; parent[28] = 0.0f; parent[29] = 0.0f;
    parent[30] = 100.0f; parent[31] = 200.0f; parent[32] = 300.0f; // parent translate

    float child[140] = {0};
    child[99] = 1.0f; child[104] = 1.0f; child[109] = 1.0f;
    child[27] = 1.0f; child[28] = 1.0f; child[29] = 1.0f;
    child[30] = 1.0f; child[31] = 2.0f; child[32] = 3.0f;
    reinterpret_cast<signed char*>(child)[528] = 0;  // pivot branch enabled
    // link child.parent = &parent (byte 504).
    *reinterpret_cast<float**>(reinterpret_cast<char*>(child) + 504) = parent;

    // Independently compute the expected bone-chain-pivot world point.
    float in[3] = {0.0f, 0.0f, 0.0f};
    float world[3] = {0};
    util::PointThroughBoneChainPivot(child, in, world);

    float pivot[140] = {0};
    pivot[99] = 1.0f; pivot[104] = 1.0f; pivot[109] = 1.0f;
    pivot[19] = 10.0f; pivot[20] = 20.0f; pivot[21] = 30.0f;
    pivot[30] = 1.0f;  pivot[31] = 1.0f;  pivot[32] = 1.0f;

    float out[3] = {0};
    TransformPointPivotToFrameSpace(child, pivot, out, in);
    // identity pivot 3x3 -> out == world - pivot[19..21] - pivot[30..32].
    CHECK_EQ(out[0], world[0] - 10.0f - 1.0f);
    CHECK_EQ(out[1], world[1] - 20.0f - 1.0f);
    CHECK_EQ(out[2], world[2] - 30.0f - 1.0f);
    // sanity: the parent translation made it to the world point.
    CHECK(world[0] != 0.0f);
}

// ---------------------------------------------------------------------------
// Full teardown: populate buildings + persons, FreeAllTables (default release ==
// real ResetEntityArrays) returns the swept building count and empties the world.
// ---------------------------------------------------------------------------
TEST(ObjSceneEntityItest, FreeAllTablesTearsDownWorld) {
    ResetEntityArrays();
    g_personArrayLoaded = true;
    const int kBuildings = 5;
    for (int i = 0; i < kBuildings; ++i) {
        std::memset(&g_objects[i], 0, sizeof(ObjectRec));
        g_objects[i].alive = 1;
        g_objects[i].id = 200 + i;
    }
    SeedPerson(0, 4, 1, 1, 5000);
    SetObjectSceneEntityHooks(nullptr);  // default release -> ResetEntityArrays

    int swept = GameObjectFreeAllTables();
    CHECK_EQ(swept, kBuildings);
    // world is empty + unloaded afterwards
    for (int i = 0; i < kBuildings; ++i) CHECK_EQ((int)g_objects[i].alive, 0);
    CHECK(g_persons[0].marker == -1);
    CHECK(!g_personArrayLoaded);
    // a fresh QueryBegin now finds nothing (array unloaded).
    static const PersonFilter any[1] = {{6, 0}};
    CHECK(PersonQueryBegin(any, 1) == nullptr);
}
