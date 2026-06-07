// End-to-end: build a small mixed world, resolve a batch of ids across the
// three record arrays, then advance time across a day boundary and verify
// consistency. Exercises src/sim/entity.{h,cpp} + src/sim/gametime.{h,cpp}.
#include "sim/entity.h"
#include "sim/gametime.h"
#include "test.h"

#include <cstring>

using namespace guild::sim;

TEST(SimEntityE2E, MixedWorldResolveAndTime) {
    ResetEntityArrays();

    // ---- Build a small world of mixed entities ----
    // Persons (ids 1000..1002) in non-contiguous slots, with a free gap.
    g_persons[0].marker = 0; g_persons[0].id = 1000; g_personIds[0] = 1000;
    // slot 1 stays free (marker == -1)
    g_persons[2].marker = 0; g_persons[2].id = 1001; g_personIds[2] = 1001;
    g_persons[3].marker = 0; g_persons[3].id = 1002; g_personIds[3] = 1002;

    // Objects/buildings (ids 2000..2001), with a dead slot in between.
    g_objects[0].alive = 1; g_objects[0].id = 2000;
    g_objects[1].alive = 0; g_objects[1].id = 9999; // dead, must be ignored
    g_objects[2].alive = 1; g_objects[2].id = 2001;

    // Scene nodes (ids 3000..3002) as a flat set.
    g_sceneNodes[0].type = 1; g_sceneNodes[0].id = 3000; g_sceneNodes[0].childPtr = -1;
    g_sceneNodes[1].type = 1; g_sceneNodes[1].id = 3001; g_sceneNodes[1].childPtr = -1;
    g_sceneNodes[2].type = 2; g_sceneNodes[2].id = 3002; g_sceneNodes[2].childPtr = -1;
    g_sceneNodeCount = 3;

    g_personArrayLoaded = true;
    g_sceneArrayLoaded = true;

    // ---- Resolve a batch of ids ----
    struct Probe { guild::i32 id; int wantKind; };
    const Probe probes[] = {
        {1000, 3}, {1001, 3}, {1002, 3},  // persons
        {2000, 1}, {2001, 1},             // objects
        {3000, 2}, {3001, 2}, {3002, 2},  // scene nodes
        {9999, 0}, {4242, 0},             // dead-slot id + absent id
    };
    for (const auto& pr : probes) {
        Person* p = nullptr; ObjectRec* o = nullptr; SceneNode* s = nullptr;
        int kind = GameObjectResolveEntityById(&o, &s, pr.id, &p);
        CHECK_EQ(kind, pr.wantKind);
        if (pr.wantKind == 3) { CHECK(p != nullptr); CHECK_EQ(p->id, pr.id); }
        if (pr.wantKind == 1) { CHECK(o != nullptr); CHECK_EQ(o->id, pr.id); }
        if (pr.wantKind == 2) { CHECK(s != nullptr); CHECK_EQ(s->id, pr.id); }
    }

    // Direct lookups agree with the resolver.
    CHECK(PersonFindRecordById(1001) == &g_persons[2]);
    CHECK(BuildingFindById(2001) == &g_objects[2]);
    CHECK(PersonFindRecordById(9999) == nullptr);

    // Iterate scene nodes of type 1 via the flat-scan query.
    SceneFilter sf[] = {{0, 1}, {7, 0}};
    int typed = 0;
    for (SceneNode* n = GameObjectQueryFind(-1, sf, 2); n; n = GameObjectIterNext())
        ++typed;
    CHECK_EQ(typed, 2);

    // ---- Advance time across a day boundary ----
    GameTime t; std::memset(&t, 0, sizeof(t));
    t.day = 41; t.hour = 23; t.minute = 30; t.second = 0;

    // Add 45 minutes: 23:30 + 0:45 = 24:15 -> rolls to day 42, 00:15.
    int ret = GameTimeAdvance(&t, 0, 0, 45);
    CHECK_EQ(t.day, 42);
    CHECK_EQ(static_cast<int>(t.hour), 0);
    CHECK_EQ(t.minute, 15);
    CHECK_EQ(t.second, 0);
    CHECK_EQ(ret, 0);

    // The world is unchanged by time advance; re-resolve one id for consistency.
    Person* p = nullptr; ObjectRec* o = nullptr; SceneNode* s = nullptr;
    CHECK_EQ(GameObjectResolveEntityById(&o, &s, 2000, &p), 1);
    CHECK(o == &g_objects[0]);
}
