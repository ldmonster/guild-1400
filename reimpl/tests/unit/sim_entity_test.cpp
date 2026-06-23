// Unit tests for the entity-record substrate + game time.
//   src/sim/entity.{h,cpp}, src/sim/gametime.{h,cpp}
#include "sim/entity.h"
#include "sim/gametime.h"
#include "test.h"

#include <cstring>

using namespace guild::sim;

namespace {

// Helper: insert a Person at slot `i` with id.
void PutPerson(int i, guild::i32 id, guild::i16 faction = 0,
               guild::i16 owner = 0) {
    g_persons[i].marker = 0;       // alive (not -1)
    g_persons[i].id = id;
    g_persons[i].factionA = faction;
    g_persons[i].ownerPlayer = owner;
    g_personIds[i] = id;
    g_personArrayLoaded = true;
    g_sceneArrayLoaded = true;
}

void PutObject(int i, guild::i32 id, guild::u8 alive = 1) {
    g_objects[i].alive = alive;
    g_objects[i].id = id;
    g_personArrayLoaded = true;
    g_sceneArrayLoaded = true;
}

void PutScene(int i, guild::i16 type, guild::i32 id, int child = -1,
              int entity = -1) {
    g_sceneNodes[i].type = type;
    g_sceneNodes[i].id = id;
    g_sceneNodes[i].childPtr = child;
    g_sceneNodes[i].entityPtr = entity;
    if (i + 1 > g_sceneNodeCount) g_sceneNodeCount = i + 1;
    g_sceneArrayLoaded = true;
}

} // namespace

// ----- Person lookup -----
TEST(SimEntity, PersonFindHitMissFreeSkip) {
    ResetEntityArrays();
    PutPerson(0, 100);
    PutPerson(2, 300);              // slot 1 left free (marker == -1)
    PutPerson(5, 600);

    Person* p = PersonFindRecordById(300);
    CHECK(p == &g_persons[2]);
    CHECK_EQ(p->id, 300);

    CHECK(PersonFindRecordById(600) == &g_persons[5]);
    // Miss: id not present.
    CHECK(PersonFindRecordById(999) == nullptr);
    // Free-slot id (slot 1 has id 0 but marker == -1) must be skipped.
    CHECK(PersonFindRecordById(0) == nullptr);
}

// ----- Building/Object lookup -----
TEST(SimEntity, BuildingFindHitMissDeadSkip) {
    ResetEntityArrays();
    PutObject(0, 10);
    PutObject(3, 40);
    PutObject(7, 80, 0);            // dead slot (alive == 0)

    CHECK(BuildingFindById(40) == &g_objects[3]);
    CHECK(BuildingFindById(10) == &g_objects[0]);
    CHECK(BuildingFindById(999) == nullptr);
    // id 80 lives in a dead slot -> miss.
    CHECK(BuildingFindById(80) == nullptr);
}

// ----- ResolveEntityById dispatch across the three arrays -----
TEST(SimEntity, ResolveDispatchPerson) {
    ResetEntityArrays();
    PutPerson(4, 555);
    Person* p = nullptr; ObjectRec* o = nullptr; SceneNode* s = nullptr;
    int kind = GameObjectResolveEntityById(&o, &s, 555, &p);
    CHECK_EQ(kind, 3);
    CHECK(p == &g_persons[4]);
    CHECK(o == nullptr);
    CHECK(s == nullptr);
}

TEST(SimEntity, ResolveDispatchObject) {
    ResetEntityArrays();
    PutObject(2, 777);
    Person* p = nullptr; ObjectRec* o = nullptr; SceneNode* s = nullptr;
    // No person with this id -> falls through to object search.
    int kind = GameObjectResolveEntityById(&o, &s, 777, &p);
    CHECK_EQ(kind, 1);
    CHECK(o == &g_objects[2]);
    CHECK(p == nullptr);
}

TEST(SimEntity, ResolveDispatchScene) {
    ResetEntityArrays();
    PutScene(0, 1, 11);
    PutScene(1, 2, 22);
    Person* p = nullptr; ObjectRec* o = nullptr; SceneNode* s = nullptr;
    int kind = GameObjectResolveEntityById(&o, &s, 22, &p);
    CHECK_EQ(kind, 2);
    CHECK(s == &g_sceneNodes[1]);
}

TEST(SimEntity, ResolveMiss) {
    ResetEntityArrays();
    PutPerson(0, 1);
    PutObject(0, 2);
    PutScene(0, 1, 3);
    Person* p = nullptr; ObjectRec* o = nullptr; SceneNode* s = nullptr;
    int kind = GameObjectResolveEntityById(&o, &s, 4242, &p);
    CHECK_EQ(kind, 0);
    CHECK(p == nullptr); CHECK(o == nullptr); CHECK(s == nullptr);
}

TEST(SimEntity, ResolveNotLoaded) {
    ResetEntityArrays();
    // g_sceneArrayLoaded false -> immediate 0.
    Person* p = nullptr; ObjectRec* o = nullptr; SceneNode* s = nullptr;
    CHECK_EQ(GameObjectResolveEntityById(&o, &s, 1, &p), 0);
}

// ----- Person query iterator (over objects) -----
TEST(SimEntity, PersonQueryByOwner) {
    ResetEntityArrays();
    // Object record: owner word lives at byte +39 (unaligned -> memcpy).
    auto setOwner = [](int i, guild::i16 owner) {
        std::memcpy(reinterpret_cast<guild::u8*>(&g_objects[i]) + 39, &owner,
                    sizeof(owner));
    };
    auto getOwner = [](ObjectRec* r) {
        guild::i16 v;
        std::memcpy(&v, reinterpret_cast<guild::u8*>(r) + 39, sizeof(v));
        return v;
    };
    PutObject(0, 1); setOwner(0, 7);
    PutObject(1, 2); setOwner(1, 3);
    PutObject(2, 3); setOwner(2, 7);
    PutObject(4, 4); setOwner(4, 7);

    PersonFilter f[] = {{4, 7}}; // op 4 = owner == 7
    int found = 0;
    for (ObjectRec* r = PersonQueryBegin(f, 1); r; r = PersonIterNext()) {
        CHECK_EQ(getOwner(r), 7);
        ++found;
    }
    CHECK_EQ(found, 3);
}

TEST(SimEntity, PersonQueryById) {
    ResetEntityArrays();
    PutObject(0, 100);
    PutObject(1, 200);
    PutObject(2, 300);
    PersonFilter f[] = {{1, 200}};
    ObjectRec* r = PersonQueryBegin(f, 1);
    CHECK(r == &g_objects[1]);
    CHECK(PersonIterNext() == nullptr);
}

// ----- GameObject DFS iterator: flat scan -----
TEST(SimEntity, SceneFlatScanByType) {
    ResetEntityArrays();
    PutScene(0, 1, 10);
    PutScene(1, 2, 20);
    PutScene(2, 1, 30);
    PutScene(3, 2, 40);
    SceneFilter f[] = {{0, 1}, {7, 0}}; // type==1, flat scan
    int found = 0;
    for (SceneNode* n = GameObjectQueryFind(-1, f, 2); n; n = GameObjectIterNext()) {
        CHECK_EQ(n->type, 1);
        ++found;
    }
    CHECK_EQ(found, 2);
}

// ----- GameObject DFS iterator: child walk -----
TEST(SimEntity, SceneTreeChildWalk) {
    ResetEntityArrays();
    // Build a small chain: node0 -child-> node1 -child-> node2 (end).
    PutScene(0, 5, 100, /*child*/1);
    PutScene(1, 5, 200, /*child*/2);
    PutScene(2, 5, 300, /*child*/-1);
    // Find id 300 via a tree walk starting at node 0 (no flat flag).
    SceneFilter f[] = {{1, 300}};
    SceneNode* n = GameObjectQueryFind(0, f, 1);
    CHECK(n == &g_sceneNodes[2]);
    CHECK_EQ(n->id, 300);
}

// ===========================================================================
// WAVE-11 HARDENING — array-boundary indexing + bad-record / sparse-scan edges.
// ASAN exercises the bounds. The valid-input behavior is unchanged.
// ===========================================================================

// Person lookup at the LAST slot (index 767) — the 411648-byte cursor bound.
// The scan must read slot 767 and miss cleanly past it (no OOB at 768).
TEST(SimEntityHarden, PersonFindAtLastSlot) {
    ResetEntityArrays();
    PutPerson(kPersonCapacity - 1, 4242);     // slot 767
    CHECK(PersonFindRecordById(4242) == &g_persons[kPersonCapacity - 1]);
    // A miss must scan all 768 slots and return null without overrunning.
    CHECK(PersonFindRecordById(0x7FFFFFFF) == nullptr);
}

// Object lookup at the LAST slot (index 255) — the 43264-byte cursor bound.
TEST(SimEntityHarden, BuildingFindAtLastSlot) {
    ResetEntityArrays();
    PutObject(kObjectCapacity - 1, 909);      // slot 255
    CHECK(BuildingFindById(909) == &g_objects[kObjectCapacity - 1]);
    CHECK(BuildingFindById(0x7FFFFFFF) == nullptr);
}

// Sparse scene array: sceneCount counts OCCUPIED nodes, but the occupied node
// sits far past sceneCount empty slots. Pre-fix, the scene scan advanced its raw
// index past kSceneNodeCapacity (empty slots bump idx but not the examined-count)
// -> OOB read. The bound now fails safe (miss) at capacity.
TEST(SimEntityHarden, ResolveSceneSparseNoOverrun) {
    ResetEntityArrays();
    g_sceneArrayLoaded = true;
    // One occupied node near the end; everything before it is empty (type==0).
    g_sceneNodes[kSceneNodeCapacity - 1].type = 9;
    g_sceneNodes[kSceneNodeCapacity - 1].id   = 77;
    // sceneCount = 1 occupied node, but it's 511 empty slots away.
    g_sceneNodeCount = 1;
    Person* p = nullptr; ObjectRec* o = nullptr; SceneNode* s = nullptr;
    // id not present -> the scan walks empty slots; must stop at capacity, not OOB.
    int kind = GameObjectResolveEntityById(nullptr, &s, 1234, &p);
    CHECK_EQ(kind, 0);
    CHECK(s == nullptr);
}

// sceneCount larger than capacity must not drive the scan past the flat array.
TEST(SimEntityHarden, ResolveSceneCountOverCapacityNoOverrun) {
    ResetEntityArrays();
    g_sceneArrayLoaded = true;
    g_sceneNodeCount = kSceneNodeCapacity + 1000;   // declared-count too large
    Person* p = nullptr; SceneNode* s = nullptr;
    int kind = GameObjectResolveEntityById(nullptr, &s, 5, &p);  // all empty -> miss
    CHECK_EQ(kind, 0);
    CHECK(s == nullptr);
}

// Person query with an out-of-range owner value: no object matches; the iterator
// must scan all 256 object slots and terminate cleanly (no OOB at 256).
TEST(SimEntityHarden, PersonQueryOwnerNoMatchFullScan) {
    ResetEntityArrays();
    PutObject(0, 1);
    PutObject(kObjectCapacity - 1, 2);        // alive object in the LAST slot
    PersonFilter f[] = {{4, 0x4242}};         // owner == 0x4242 (none match)
    CHECK(PersonQueryBegin(f, 1) == nullptr);
    CHECK(PersonIterNext() == nullptr);       // exhausted, no overrun
}

// Person query "count too large" guard: q_count > kObjectCapacity short-circuits
// (the original's `if (loaded && q_count <= 256)` bound). Drive IterNext directly
// after a begin so the count-guard path is exercised without OOB.
TEST(SimEntityHarden, PersonQueryAtLastObjectSlot) {
    ResetEntityArrays();
    PutObject(kObjectCapacity - 1, 555);      // only the last slot is alive
    PersonFilter f[] = {{1, 555}};            // id == 555
    ObjectRec* r = PersonQueryBegin(f, 1);
    CHECK(r == &g_objects[kObjectCapacity - 1]);
    CHECK(PersonIterNext() == nullptr);
}

// ============================ Game time ============================

// Golden vectors computed by /tmp/gt.py (the original arithmetic model).
struct GtCase { int d, h, m, s, addD, addS, addM; int rd, rh, rm, rs, ret; };

TEST(SimGameTime, AdvanceGoldenVectors) {
    const GtCase cases[] = {
        // start{d,h,m,s}  add{D,S,M}     expected{d,h,m,s}  ret
        {0,0,0,0,    0,30,0,      0,0,0,30,     0},
        {0,0,0,30,   0,45,0,      0,0,1,15,     0},
        {0,23,59,30, 0,30,0,      1,0,0,0,      0},
        {0,0,0,0,    0,3600,0,    0,1,0,0,      1},
        {0,0,0,0,    0,0,1440,    1,0,0,0,      0},
        {5,12,30,30, 2,7200,90,   5,18,0,30,    18},
        {0,0,0,0,    0,-30,0,     0,0,0,-30,    0},
        {3,1,0,0,    0,0,-120,    2733,15,0,0,  15},
        {10,5,15,45, 0,86400,0,   11,5,15,45,   5},
    };
    for (const auto& c : cases) {
        GameTime t;
        t.day = c.d; t.hour = static_cast<guild::u16>(c.h);
        t.minute = c.m; t.second = c.s;
        int ret = GameTimeAdvance(&t, c.addD, c.addS, c.addM);
        CHECK_EQ(ret, c.ret);
        CHECK_EQ(t.day, c.rd);
        CHECK_EQ(static_cast<int>(t.hour), c.rh & 0xFFFF);
        CHECK_EQ(t.minute, c.rm);
        CHECK_EQ(t.second, c.rs);
    }
}

// Day rollover via repeated 1-hour advances over a full day.
TEST(SimGameTime, DayRollover) {
    GameTime t; std::memset(&t, 0, sizeof(t));
    for (int i = 0; i < 24; ++i)
        GameTimeAdvance(&t, 0, 3600, 0);
    CHECK_EQ(t.day, 1);
    CHECK_EQ(static_cast<int>(t.hour), 0);
}
