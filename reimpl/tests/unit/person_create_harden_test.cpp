// WAVE-11 HARDENING — edge tests for the person-create leaf
//   src/sim/person_create.{h,cpp}  (VIBE_Person_CreateAndSpawn @0x58da70)
//
// Focus: the array-FULL boundary (no OOB write past g_persons[767]), the parallel
// id column staying in lockstep, and a parent BUILDING record whose type byte
// indexes the building-type table out of range / with the table unloaded.
// ASAN exercises the bounds; the in-bounds behavior is unchanged.
#include "tests/framework/test.h"

#include "sim/person_create.h"
#include "sim/entity.h"
#include "sim/building.h"

#include <cstring>

using namespace guild;
using namespace guild::sim;

namespace {
void ResetAll() {
    // Fully clear the person array: ResetEntityArrays() only stamps the free
    // marker (-1) + the parallel id column; it leaves each record's own +4 id
    // field intact. DefaultPersonCreate's free-slot probe is `marker==-1 && id==0`,
    // so a prior test that wrote g_persons[i].id directly would otherwise make the
    // array look full. memset gives every test a clean, free array.
    std::memset(g_persons, 0, sizeof(Person) * kPersonCapacity);
    ResetEntityArrays();   // restores marker==-1 on every slot
    ResetPersonCreate();
    ResetBuildings();
    g_personArrayLoaded = true;
}
}  // namespace

// All 768 slots fill, then the next create returns 0xFFFF without writing OOB.
TEST(PersonCreateHarden, ArrayFullReturnsSentinelNoOverrun) {
    ResetAll();
    int created = 0;
    for (int i = 0; i < kPersonCapacity; ++i) {
        PersonSpawnArgs a{};
        a.kind = 2;
        u16 idx = Person_CreateAndSpawn(a);
        CHECK(idx != 0xFFFF);
        CHECK(static_cast<int>(idx) < kPersonCapacity);   // never indexes past 767
        ++created;
    }
    CHECK_EQ(created, kPersonCapacity);

    // The array is full -> the scan reaches slot 767, finds no free slot, and the
    // marker-based reuse check fails -> 0xFFFF. No write past g_persons[767].
    PersonSpawnArgs a{};
    a.kind = 2;
    CHECK_EQ(Person_CreateAndSpawn(a), static_cast<u16>(0xFFFF));
}

// The last slot (index 767) is the only free one -> create must take it and stamp
// the parallel id column at the same index (lockstep), no OOB.
TEST(PersonCreateHarden, FillsLastFreeSlot) {
    ResetAll();
    // Occupy every slot except the last (marker != -1 + nonzero id == "used").
    for (int i = 0; i < kPersonCapacity - 1; ++i) {
        g_persons[i].marker = 0;
        g_persons[i].id = 1000 + i;
        g_personIds[i] = 1000 + i;
    }
    PersonSpawnArgs a{};
    a.kind = 5;
    u16 idx = Person_CreateAndSpawn(a);
    CHECK_EQ(static_cast<int>(idx), kPersonCapacity - 1);   // slot 767
    CHECK_EQ(g_persons[kPersonCapacity - 1].id, g_personIds[kPersonCapacity - 1]);
    CHECK(g_persons[kPersonCapacity - 1].id != 0);
}

// Parent building with the building-type table UNLOADED: BuildingTypeDefAt returns
// null, so the building-column populater treats the class as 0 (neither column).
// No OOB read of the type table.
TEST(PersonCreateHarden, BuildingColumnTypeTableUnloaded) {
    ResetAll();
    // g_buildingTypesLoaded is false after ResetBuildings().
    ObjectRec b{};
    b.alive = 200;        // arbitrary type byte (would index type table if loaded)
    b.id = 4242;
    PersonSpawnArgs a{};
    a.kind = 2;
    a.queryRec = &b;
    u16 idx = Person_CreateAndSpawn(a);
    CHECK(idx != 0xFFFF);
    // Neither building column written (class defaults to 0 with the table absent).
    i32 home, work;
    std::memcpy(&home, reinterpret_cast<u8*>(&g_persons[idx]) + 364, 4);
    std::memcpy(&work, reinterpret_cast<u8*>(&g_persons[idx]) + 368, 4);
    CHECK_EQ(home, 0);
    CHECK_EQ(work, 0);
}

// Parent building with type byte 255 and the table LOADED: index 255 is the last
// valid type-table slot (capacity 256). BuildingTypeDefAt(u8) stays in bounds; the
// class byte read must not run past g_buildingTypes[255].
TEST(PersonCreateHarden, BuildingColumnMaxTypeByteInBounds) {
    ResetAll();
    g_buildingTypesLoaded = true;
    g_buildingTypes[255].kind = 1;        // class 1 -> work column (+368)
    ObjectRec b{};
    b.alive = 255;                        // last valid type-table index
    b.id = 7777;
    PersonSpawnArgs a{};
    a.kind = 2;
    a.queryRec = &b;
    u16 idx = Person_CreateAndSpawn(a);
    CHECK(idx != 0xFFFF);
    i32 work;
    std::memcpy(&work, reinterpret_cast<u8*>(&g_persons[idx]) + 368, 4);
    CHECK_EQ(work, 7777);                  // class-1 building wrote the work column
    ResetBuildings();
}
