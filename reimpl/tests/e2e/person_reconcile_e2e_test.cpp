// E2E: build records in the canonical arrays (Person 536, Object 169,
// building-type 589), then run BOTH a world/economy demand pass and a sim id
// lookup over the SAME data and verify they stay consistent — the economy reads
// the object/type tables while the sim lookups read the person/object arrays,
// and the 536 vs 589 strides never alias.
#include <cstring>
#include <vector>

#include "sim/building.h"
#include "sim/entity.h"
#include "sim/person_record.h"
#include "test.h"
#include "world/city.h"
#include "world/economy.h"
#include "world/types.h"

using namespace guild::sim;

namespace {

// Put an Object/Building record (169-byte array) at slot i with a type byte and
// employment word; the type byte indexes the 589-stride type table.
void PutObj(int i, guild::i32 id, guild::u8 typeByte, guild::u16 employment) {
    g_objects[i].alive = typeByte;   // +0 alive/type byte (nonzero == alive)
    g_objects[i].id = id;
    std::memcpy(reinterpret_cast<guild::u8*>(&g_objects[i]) + 39, &employment,
                sizeof(employment));
}

}  // namespace

TEST(PersonReconcileE2E, EconomyDemandAndSimLookupOverSameData) {
    ResetEntityArrays();
    ResetBuildings();
    guild::world::CityInitParameterTable(1000.0f);

    // --- Seed the canonical building-type table (589 stride). ---
    // Good/profession index 5 is "default" class. Give type-byte 5 a need of 10.
    g_buildingTypes[5].security = 10;   // typeDef[5] + 583
    g_buildingTypes[5].kind = 5;
    g_buildingTypesLoaded = true;

    // --- Seed Object records (169 stride): three objects of type 5, all
    //     employed; plus a Person record (536 stride) sharing one id. ---
    PutObj(0, 1000, /*type*/5, /*employment*/100);
    PutObj(1, 1001, /*type*/5, /*employment*/100);
    PutObj(2, 1002, /*type*/5, /*employment*/100);
    g_personArrayLoaded = true;
    g_sceneArrayLoaded = true;

    // A Person with the SAME id 1000 lives in the separate 536-byte array.
    g_persons[7].marker = 0;
    g_persons[7].id = 1000;
    g_personIds[7] = 1000;

    // --- Build the economy views via the canonical adapter (the production join
    //     of object+39 and typeDef+583), exactly as the binary's iterator does. ---
    std::vector<guild::world::PersonEcoView> persons[28];
    for (int slot = 0; slot < 3; ++slot) {
        persons[5].push_back(guild::world::EcoViewFromObject(
            &g_objects[slot], g_buildingTypes, g_buildingTypesLoaded));
    }

    // Each view: need 10, employed.
    for (const auto& v : persons[5]) {
        CHECK_EQ((int)v.need, 10);
        CHECK(!v.unemployed);
    }

    // --- Run the economy demand pass. Good 5 (default class, employed):
    //     term = need*2 - 1 = 19 per person; 3 persons => 57. ---
    guild::world::EconomyComputeGoodsDemand(persons);
    CHECK_EQ(guild::world::g_goods[5].accum, 57.0f);

    // --- Sim lookups over the SAME data: the Object id resolver and the Person
    //     id resolver hit DIFFERENT storage for the same id 1000. ---
    ObjectRec* o = BuildingFindById(1000);
    Person* p = PersonFindRecordById(1000);
    CHECK(o == &g_objects[0]);
    CHECK(p == &g_persons[7]);
    CHECK((const void*)o != (const void*)p);

    // The object the economy summed is the same record the sim lookup returns.
    CHECK_EQ(o->id, 1000);
    CHECK_EQ((int)o->alive, 5);   // type byte preserved
}

TEST(PersonReconcileE2E, PersonQueryIteratorUsesTypeTableFilter) {
    ResetEntityArrays();
    ResetBuildings();

    // Type table: type byte 4 has kind 7, type byte 8 has kind 3.
    g_buildingTypes[4].kind = 7;
    g_buildingTypes[8].kind = 3;
    g_buildingTypesLoaded = true;

    // Three objects: two of type 4 (kind 7), one of type 8 (kind 3).
    g_objects[0].alive = 4; g_objects[0].id = 1;
    g_objects[1].alive = 8; g_objects[1].id = 2;
    g_objects[2].alive = 4; g_objects[2].id = 3;
    g_personArrayLoaded = true;
    g_sceneArrayLoaded = true;

    // Filter op 5 = AiPlayer/type-table byte (kind) == 7. The iterator must
    // index the 589-stride table by each object's +0 type byte and match kind.
    PersonFilter f[] = {{5, 7}};
    int found = 0;
    for (ObjectRec* r = PersonQueryBegin(f, 1); r; r = PersonIterNext()) {
        // Each matched record's type byte must map to kind 7 in the type table.
        CHECK_EQ((int)g_buildingTypes[r->alive].kind, 7);
        ++found;
    }
    CHECK_EQ(found, 2);
}
