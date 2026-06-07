#include "test.h"

#include "sim/person_lifecycle.h"
#include "sim/entity.h"
#include "sim/building.h"

#include <cstring>

using namespace guild::sim;

// ===========================================================================
// End-to-end person/building-type lifecycle flow across the translated leaves:
//   1. populate a small building-type catalog (g_buildingTypes, stride 589),
//   2. resolve a type by NAME (CountActiveSlots) and by KIND (CollectByType),
//   3. map the resolved type to a guild rank pair (GetGuildRankPair),
//   4. populate the object array and resolve an object by back-reference
//      (FindByObjectRef walks QueryBegin/IterNext),
//   5. spawn a "person" record and derive its birth dates (deterministic).
// This crosses the type-table, object-array, and person-record substrates.
// ===========================================================================

namespace {

void SetTypeRecord(int idx, guild::u8 kind, const char* name) {
    guild::u8* rec = reinterpret_cast<guild::u8*>(&g_buildingTypes[0])
                   + idx * kBuildingTypeStride;
    rec[0] = kind;
    std::memset(rec + 1, 0, 34);
    if (name) std::strncpy(reinterpret_cast<char*>(rec + 1), name, 33);
}

} // namespace

TEST(SimPersonLifecycleE2E, FullCatalogAndBirthFlow) {
    // ---- setup --------------------------------------------------------------
    ResetBuildings();
    ResetEntityArrays();
    ResetPersonLifecycle();
    g_buildingTypesLoaded = true;
    g_personArrayLoaded   = true;

    for (int i = 0; i < kBuildingTypeScanCount; ++i)
        SetTypeRecord(i, 0, "");
    // Three guild building types of kind 4 (-> rank pair {1,2}).
    SetTypeRecord(4,  4, "Weaver");
    SetTypeRecord(8,  4, "Tailor");
    SetTypeRecord(11, 4, "Dyer");
    // A production type (kind 11) at a known slot, plus a storage type (kind 10).
    SetTypeRecord(20, 11, "Mill");
    SetTypeRecord(30, 10, "Warehouse");

    // ---- 1. name -> index ---------------------------------------------------
    guild::u8 weaverIdx = PersonCountActiveSlots("weaver");   // case-insensitive
    CHECK_EQ(weaverIdx, guild::u8(4));
    guild::u8 millIdx = PersonCountActiveSlots("Mill");
    CHECK_EQ(millIdx, guild::u8(20));

    // ---- 2. kind -> index list ---------------------------------------------
    guild::u8 kind4[kBuildingTypeScanCount] = {0};
    int n4 = PersonCollectByType(4, kind4);
    CHECK_EQ(n4, 3);
    CHECK_EQ(kind4[0], guild::u8(4));
    CHECK_EQ(kind4[1], guild::u8(8));
    CHECK_EQ(kind4[2], guild::u8(11));

    // ---- 3. each kind-4 type -> guild rank pair ----------------------------
    for (int i = 0; i < n4; ++i) {
        guild::u8 ra = 0, rb = 0;
        int ok = BuildingTypeGetGuildRankPair(4, &ra, &rb);  // type code 4
        CHECK_EQ(ok, 1);
        CHECK_EQ(ra, guild::u8(1));
        CHECK_EQ(rb, guild::u8(2));
    }

    // ---- 4. object back-reference resolution -------------------------------
    // Two live objects (alive byte 71 == the FindByObjectRef filter), back-ref @+101.
    for (int i = 0; i < 2; ++i) {
        g_objects[i].alive = 71;
        guild::i32 id = 2000 + i;
        std::memcpy(reinterpret_cast<guild::u8*>(&g_objects[i]) + 1, &id, 4);
        guild::i32 ref = 7000 + i;
        std::memcpy(reinterpret_cast<guild::u8*>(&g_objects[i]) + 101, &ref, 4);
    }
    ObjectRec* obj = PersonFindByObjectRef(7001, /*token*/0);
    CHECK(obj == &g_objects[1]);
    CHECK(PersonFindByObjectRef(12345, 0) == nullptr);

    // ---- 5. person birth dates (deterministic) ------------------------------
    Person person;
    std::memset(&person, 0, sizeof(person));
    person.isPlayer = 0;                                  // +8 == 0 -> derivable
    guild::i32 spawn = 0x00050000;                        // >>16 == 5
    std::memcpy(reinterpret_cast<guild::u8*>(&person) + 38, &spawn, 4);
    guild::i32 seed = 0x12345678;
    std::memcpy(reinterpret_cast<guild::u8*>(&person) + 48, &seed, 4);

    PersonBirthDate bd;
    int gotDate = PersonComputeBirthDate(&person, &bd);
    CHECK_EQ(gotDate, 1);
    CHECK_EQ(bd.day,   guild::u8(13));
    CHECK_EQ(bd.month, guild::u8(12));
    CHECK_EQ(bd.year,  guild::u16(1405));

    // Dated variant from the live game-date snapshot. Use spawn field +38 ==
    // 0x00080000 (>>16 == 8) so this matches the documented FromRecord golden
    // (dateLow = 8 - age(3) = 5).
    guild::i32 spawn2 = 0x00080000;
    std::memcpy(reinterpret_cast<guild::u8*>(&person) + 38, &spawn2, 4);
    g_gameDateSnapshot = GameDateSnapshot{};
    g_gameDateSnapshot.dateLow = 100;
    g_gameDateSnapshot.byte10[0] = 0x34;
    g_gameDateSnapshot.byte10[1] = 0x12;
    guild::u16 age = 3;
    std::memcpy(reinterpret_cast<guild::u8*>(&person) + 10, &age, 2);
    guild::i32 seed2 = (guild::i32)0xCAFEBABE;
    std::memcpy(reinterpret_cast<guild::u8*>(&person) + 48, &seed2, 4);

    PersonBirthDate bd2;
    guild::u32 years = PersonComputeBirthDateFromRecord(&person, &bd2);
    CHECK_EQ(bd2.day,   guild::u8(15));
    CHECK_EQ(bd2.month, guild::u8(4));
    CHECK_EQ(bd2.year,  guild::u16(1405));
    CHECK_EQ(years,     guild::u32(3800996));
}
