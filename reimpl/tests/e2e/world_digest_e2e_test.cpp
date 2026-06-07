// E2E: the broadened full-world digest as the self-consistency oracle. Seed the
// REAL CRT RNG with S and drive an RNG-derived sequence that mutates the WHOLE
// world (entities + buildings + economy/market + clock + treasury + other
// tables). Hashing the world twice after the same seeded sequence yields an
// identical full-world digest; a different seed (or a single post-hoc mutation)
// changes it. This stands in for a Wine/original-binary oracle.
#include "test.h"

#include "play/world_digest.h"
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

void ResetAll() {
    sim::ResetEntityArrays();
    sim::ResetBuildingPersons();
    sim::ResetBuildings();
    sim::ResetProductionTables();
    world::LawTableResetDefaults();
    world::EventTableReset();
    world::OfficeHolderTableReset();
    world::CrimeAndEvidenceReset();
    world::RelationReset();
    world::g_capDivisor = world::g_cityTotalMoney = world::g_cityTotalGoods = 0.0f;
    sim::g_sysGameTime = sim::GameTime{};
    sim::g_tickClock = sim::GameTime{};
    sim::g_tickSubCounter = sim::g_gameTick = 0;
    sim::g_currentPlayer = sim::g_sysActivePlayer = -1;
}

// A fixed, RNG-driven sequence that touches the full world. Pure function of the
// current RNG state, so two runs seeded identically mutate identically.
void DriveSeededFullWorld() {
    using namespace guild::sim;
    for (int i = 0; i < 24; ++i) {
        int slot = crt::RandNext() % kPersonCapacity;
        g_persons[slot].marker = static_cast<i16>(crt::RandNext() & 0x7);
        g_persons[slot].id     = crt::RandNext();
        g_persons[slot].cash   = static_cast<i16>(crt::RandNext() & 0x3FFF);
    }
    for (int i = 0; i < 12; ++i) {
        int slot = crt::RandNext() % kObjectCapacity;
        g_objects[slot].alive = 1;
        g_objects[slot].id    = crt::RandNext();
    }
    g_sceneNodeCount = crt::RandNext() % kSceneNodeCapacity;

    // Buildings.
    g_buildingNextId      = crt::RandNext();
    g_buildingTypesLoaded = (crt::RandNext() & 1) != 0;
    for (int i = 0; i < 16; ++i)
        g_prodStore[crt::RandNext() % kProdStoreDwords] = crt::RandNext();

    // Economy / market.
    world::g_capDivisor     = static_cast<float>(crt::RandNext());
    world::g_cityTotalMoney = static_cast<float>(crt::RandNext());
    world::g_cityTotalGoods = static_cast<float>(crt::RandNext());

    // Clock / treasury.
    g_sysGameTime.day  = crt::RandNext();
    g_sysGameTime.hour = static_cast<u16>(crt::RandNext() % 24);
    g_gameTick         = static_cast<u32>(crt::RandNext());
    g_currentPlayer    = static_cast<i16>(crt::RandNext() % 4);
    g_sysActivePlayer  = crt::RandNext() % 4;

    // Other tables.
    world::g_missionLcgState = static_cast<u32>(crt::RandNext());
    world::g_relationMatrix[crt::RandNext() % 8] = static_cast<u8>(crt::RandNext());
}

std::uint64_t RunFullSession(unsigned seed) {
    ResetAll();
    crt::Srand(seed);
    DriveSeededFullWorld();
    return play::HashFullWorld();
}

} // namespace

TEST(WorldDigestE2E, SameSeedIdenticalFullDigest) {
    std::uint64_t a = RunFullSession(31337u);
    std::uint64_t b = RunFullSession(31337u);
    CHECK_EQ(a, b);    // identical seeded full-world evolution -> identical digest
    CHECK(a != 0u);
}

TEST(WorldDigestE2E, DifferentSeedDifferentFullDigest) {
    std::uint64_t a = RunFullSession(31337u);
    std::uint64_t c = RunFullSession(80808u);
    CHECK(a != c);
}

TEST(WorldDigestE2E, PostHocMutationFlipsFullDigest) {
    // Seed a full world, hash it, then mutate ONE field of ONE additional table
    // and confirm the digest moves (the broadened fold actually covers it).
    std::uint64_t before = RunFullSession(31337u);
    // Mutate a building field that the entity-only HashWorldState would NOT see.
    sim::g_buildingNextId += 1;
    std::uint64_t after = play::HashFullWorld();
    CHECK(before != after);
}
