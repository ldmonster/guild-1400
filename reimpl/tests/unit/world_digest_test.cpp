// Unit: the FULL-world digest (play::HashFullWorld / SnapshotFullWorld) is
// order-stable, reproducible, and sensitive to a single-field change in ANY of
// the additional folded world tables (buildings, economy/market, clock, treasury,
// law/event/office/crime/relation) — not just the base entity arrays. Operates on
// the REAL world globals via extern.
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

using namespace guild;

namespace {
// Bring the whole simulated world to a known baseline.
void ResetFullWorld() {
    sim::ResetEntityArrays();
    sim::ResetBuildingPersons();
    sim::ResetBuildings();
    sim::ResetProductionTables();
    world::LawTableResetDefaults();
    world::EventTableReset();
    world::OfficeHolderTableReset();
    world::CrimeAndEvidenceReset();
    world::RelationReset();
    world::g_capDivisor      = 0.0f;
    world::g_cityTotalMoney  = 0.0f;
    world::g_cityTotalGoods  = 0.0f;
    sim::g_sysGameTime  = sim::GameTime{};
    sim::g_tickClock    = sim::GameTime{};
    sim::g_tickSubCounter = 0;
    sim::g_gameTick     = 0;
    sim::g_currentPlayer  = -1;
    sim::g_sysActivePlayer = -1;
    crt::Srand(1);
}
} // namespace

TEST(WorldDigest, OrderStableAndReproducible) {
    ResetFullWorld();
    std::uint64_t h1 = play::HashFullWorld();
    std::uint64_t h2 = play::HashFullWorld();
    CHECK_EQ(h1, h2);              // pure: hashing twice is identical

    ResetFullWorld();
    std::uint64_t h3 = play::HashFullWorld();
    CHECK_EQ(h1, h3);             // reproducible from the same baseline
    CHECK(h1 != 0u);             // non-empty fold never collapses to 0
}

TEST(WorldDigest, StrictlyDominatesBaseDigest) {
    // A change confined to the BASE entity arrays must still move the full hash.
    ResetFullWorld();
    std::uint64_t base = play::HashFullWorld();
    sim::g_persons[5].marker = 3;
    CHECK(play::HashFullWorld() != base);
    sim::g_persons[5].marker = -1;
    CHECK_EQ(play::HashFullWorld(), base);
}

TEST(WorldDigest, SensitiveToBuildingFieldChange) {
    ResetFullWorld();
    std::uint64_t base = play::HashFullWorld();

    // Save/restore the EXACT prior value (the reset baselines are not all zero).
    const i32  prevId      = sim::g_buildingNextId;
    sim::g_buildingNextId  = prevId + 4242;
    CHECK(play::HashFullWorld() != base);
    sim::g_buildingNextId  = prevId;
    CHECK_EQ(play::HashFullWorld(), base);

    const bool prevLoaded     = sim::g_buildingTypesLoaded;
    sim::g_buildingTypesLoaded = !prevLoaded;
    CHECK(play::HashFullWorld() != base);
    sim::g_buildingTypesLoaded = prevLoaded;
    CHECK_EQ(play::HashFullWorld(), base);

    const i32 prevProd  = sim::g_prodStore[100];
    sim::g_prodStore[100] = prevProd ^ 0x55;
    CHECK(play::HashFullWorld() != base);
    sim::g_prodStore[100] = prevProd;
    CHECK_EQ(play::HashFullWorld(), base);
}

TEST(WorldDigest, SensitiveToEconomyAndMarketChange) {
    ResetFullWorld();
    std::uint64_t base = play::HashFullWorld();

    world::g_capDivisor = 12.5f;
    CHECK(play::HashFullWorld() != base);
    world::g_capDivisor = 0.0f;
    CHECK_EQ(play::HashFullWorld(), base);

    world::g_cityTotalMoney = 99999.0f;
    CHECK(play::HashFullWorld() != base);
    world::g_cityTotalMoney = 0.0f;
    CHECK_EQ(play::HashFullWorld(), base);
}

TEST(WorldDigest, SensitiveToClockAndTreasuryChange) {
    ResetFullWorld();
    std::uint64_t base = play::HashFullWorld();

    sim::g_sysGameTime.day = 17;
    CHECK(play::HashFullWorld() != base);
    sim::g_sysGameTime.day = 0;
    CHECK_EQ(play::HashFullWorld(), base);

    sim::g_gameTick = 555;
    CHECK(play::HashFullWorld() != base);
    sim::g_gameTick = 0;
    CHECK_EQ(play::HashFullWorld(), base);

    sim::g_currentPlayer = 2;
    CHECK(play::HashFullWorld() != base);
    sim::g_currentPlayer = -1;
    CHECK_EQ(play::HashFullWorld(), base);
}

TEST(WorldDigest, SensitiveToOtherWorldTables) {
    ResetFullWorld();
    std::uint64_t base = play::HashFullWorld();

    world::g_missionLcgState ^= 0xABCDu;
    CHECK(play::HashFullWorld() != base);
    world::g_missionLcgState ^= 0xABCDu;
    CHECK_EQ(play::HashFullWorld(), base);

    world::g_relationMatrix[0] ^= 0x40;
    CHECK(play::HashFullWorld() != base);
    world::g_relationMatrix[0] ^= 0x40;
    CHECK_EQ(play::HashFullWorld(), base);
}

TEST(WorldDigest, RegionListExtendsBaseSnapshot) {
    ResetFullWorld();
    play::WorldSnapshot base = play::SnapshotWorld();
    play::WorldSnapshot full = play::SnapshotFullWorld();

    // The full region list begins with every base region (same order) and then
    // appends the extra full-world regions.
    CHECK(full.regions.size() > base.regions.size());
    bool prefixMatches = full.regions.size() >= base.regions.size();
    for (std::size_t i = 0; i < base.regions.size() && prefixMatches; ++i) {
        if (full.regions[i].label != base.regions[i].label ||
            full.regions[i].hash  != base.regions[i].hash)
            prefixMatches = false;
    }
    CHECK(prefixMatches);
    // First base region is g_persons; the appended tail ends at the relation matrix.
    CHECK(full.regions.front().label == std::string("g_persons"));
    CHECK(full.regions.back().label == std::string("g_relationMatrix"));
}
