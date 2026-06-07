// Integration: seed a small world across several real tables, take a full-world
// snapshot, mutate exactly ONE field in one subsystem (a building counter, a
// market/economy scalar, the game clock), and assert the region-level diff of the
// full-world snapshot localizes the change to that one region — leaving every
// other region untouched. This exercises SnapshotFullWorld + CompareSnapshots as
// the divergence-localizer over the broadened world state.
#include "test.h"

#include "play/world_digest.h"
#include "play/determinism.h"
#include "sim/entity.h"
#include "sim/building_lifecycle.h"
#include "sim/building.h"
#include "sim/building_create.h"
#include "sim/building_production.h"
#include "sim/command_apply5.h"
#include "sim/command_apply6.h"
#include "sim/actionqueue.h"
#include "world/city.h"
#include "world/law.h"
#include "world/event.h"
#include "world/office.h"
#include "world/crime.h"
#include "world/relation.h"
#include "crt/rand.h"

#include <string>

using namespace guild;

namespace {

// Seed a known, non-empty world touching the entity arrays + several world
// tables, so the snapshot has real content in many regions at once.
void SeedWorld() {
    sim::ResetEntityArrays();
    sim::ResetBuildingPersons();
    sim::ResetBuildings();
    sim::ResetProductionTables();
    world::LawTableResetDefaults();
    world::EventTableReset();
    world::OfficeHolderTableReset();
    world::CrimeAndEvidenceReset();
    world::RelationReset();
    crt::Srand(20250101u);

    // Entities.
    for (int i = 0; i < 6; ++i) {
        sim::g_persons[i].marker = static_cast<i16>(i);
        sim::g_persons[i].id     = 1000 + i;
        sim::g_objects[i].alive  = 1;
        sim::g_objects[i].id     = 2000 + i;
    }
    sim::g_sceneNodeCount = 4;

    // Buildings.
    sim::g_buildingNextId      = 50;
    sim::g_buildingTypesLoaded = true;
    sim::g_prodStore[0]        = 11;
    sim::g_prodStore[64]       = 22;

    // Economy / market.
    world::g_capDivisor     = 8.0f;
    world::g_cityTotalMoney = 100000.0f;
    world::g_cityTotalGoods = 4200.0f;

    // Clock / treasury.
    sim::g_sysGameTime.day  = 3;
    sim::g_sysGameTime.hour = 9;
    sim::g_gameTick         = 360;
    sim::g_currentPlayer    = 1;
    sim::g_sysActivePlayer  = 1;
}

} // namespace

TEST(WorldDigestIT, BuildingMutationLocalizes) {
    SeedWorld();
    play::WorldSnapshot before = play::SnapshotFullWorld();

    // Identical re-snapshot must compare equal with no diff text.
    play::WorldSnapshot same = play::SnapshotFullWorld();
    std::string diff;
    CHECK(play::CompareSnapshots(before, same, &diff));
    CHECK(diff.empty());

    // Mutate ONE building field.
    sim::g_buildingNextId = 51;
    play::WorldSnapshot after = play::SnapshotFullWorld();
    diff.clear();
    CHECK(!play::CompareSnapshots(before, after, &diff));
    CHECK(diff.find("g_buildingNextId") != std::string::npos);
    // No unrelated region appears in the diff.
    CHECK(diff.find("g_cities") == std::string::npos);
    CHECK(diff.find("g_sysGameTime") == std::string::npos);
    CHECK(diff.find("g_persons'") == std::string::npos);

    sim::g_buildingNextId = 50;  // restore
    CHECK(play::CompareSnapshots(before, play::SnapshotFullWorld()));
}

TEST(WorldDigestIT, PriceMutationLocalizes) {
    SeedWorld();
    play::WorldSnapshot before = play::SnapshotFullWorld();

    // Mutate ONE economy/market scalar (a "price"-side field).
    world::g_cityTotalMoney = 100001.0f;
    play::WorldSnapshot after = play::SnapshotFullWorld();
    std::string diff;
    CHECK(!play::CompareSnapshots(before, after, &diff));
    CHECK(diff.find("g_cityTotalMoney") != std::string::npos);
    CHECK(diff.find("g_buildingNextId") == std::string::npos);
    CHECK(diff.find("g_relationMatrix") == std::string::npos);

    world::g_cityTotalMoney = 100000.0f;  // restore
    CHECK(play::CompareSnapshots(before, play::SnapshotFullWorld()));
}

TEST(WorldDigestIT, ClockMutationLocalizes) {
    SeedWorld();
    play::WorldSnapshot before = play::SnapshotFullWorld();

    // Mutate ONE game-clock field.
    sim::g_sysGameTime.hour = 10;
    play::WorldSnapshot after = play::SnapshotFullWorld();
    std::string diff;
    CHECK(!play::CompareSnapshots(before, after, &diff));
    CHECK(diff.find("g_sysGameTime") != std::string::npos);
    // The wall-clock tick region is a SEPARATE region and must be untouched.
    CHECK(diff.find("g_gameTick") == std::string::npos);
    CHECK(diff.find("g_cityTotalGoods") == std::string::npos);

    sim::g_sysGameTime.hour = 9;  // restore
    CHECK(play::CompareSnapshots(before, play::SnapshotFullWorld()));
}
