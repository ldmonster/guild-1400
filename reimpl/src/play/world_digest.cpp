#include "play/world_digest.h"

#include "play/determinism.h"          // Fnv1a64, WorldSnapshot, SnapshotWorld

// Live world tables, read by extern from their home headers (never redefined).
#include "sim/building.h"              // g_buildingTypes / g_buildingTypesLoaded
#include "sim/building_create.h"       // g_buildingNextId
#include "sim/building_production.h"   // g_sceneTypes/_Loaded, g_sceneTypeRemap,
                                       //   g_prodStore, g_prodSchedules
#include "sim/actionqueue.h"           // g_gameTick (the wall-clock tick)
#include "sim/command_apply5.h"        // g_sysGameTime (qword_13CE852 clock),
                                       //   g_sysActivePlayer, g_currentPlayer
#include "sim/command_apply6.h"        // g_tickClock, g_tickSubCounter
#include "world/city.h"                // g_cities, g_goods, g_capDivisor,
                                       //   g_cityTotalMoney/Goods
#include "world/law.h"                 // g_lawTable
#include "world/event.h"               // g_eventTable, g_eventTableCount,
                                       //   g_missionLcgState
#include "world/office.h"              // g_officeHolders
#include "world/crime.h"               // g_crimeTable
#include "world/relation.h"            // g_relationMatrix

namespace guild::play {

namespace {

// Fold one named region of raw bytes into BOTH the running whole-world digest
// `whole` and a fresh per-region digest recorded in `snap.regions`. Mirrors the
// FoldRegion helper in determinism.cpp so the appended regions behave identically
// to the base ones for diffing.
void FoldRegion(WorldSnapshot& snap, Fnv1a64& whole, const char* label,
                const void* data, std::size_t n) {
    whole.bytes(data, n);
    Fnv1a64 region;
    region.bytes(data, n);
    snap.regions.push_back({label, region.value(), n});
}

// ===========================================================================
// FIXED FULL-WORLD FOLD ORDER (do not reorder — the digest depends on it):
//
//   [0]  base entity digest  : SnapshotWorld().hash folded as a scalar seed.
//        This carries the ENTIRE existing HashWorldState fold (g_persons,
//        g_personIds, g_objects, g_sceneNodes, g_sceneNodeCount,
//        g_buildingPersons, the loaded flags, crt_rng_state). The base regions
//        are also copied verbatim into snap.regions[0..] so a base divergence
//        still localizes.
//
//   --- buildings ---
//   [1]  g_buildingTypes        building-type catalog (dword_13CE294)
//   [2]  g_buildingTypesLoaded  catalog loaded guard
//   [3]  g_buildingNextId       building id allocator (dword_649890)
//   [4]  g_sceneTypes           scene-type catalog
//   [5]  g_sceneTypesLoaded     scene-type loaded guard
//   [6]  g_sceneTypeRemap       prot -> type-code remap (byte_13CE862)
//   [7]  g_prodStore            production accumulator store
//   [8]  g_prodSchedules        per-building production schedules
//
//   --- economy / market ---
//   [9]  g_cities               multi-city records (byte_13CD6A0)
//   [10] g_goods                28-good economy/profession table (0x1234750)
//   [11] g_capDivisor           cap/equilibrium divisor (flt_641DA8)
//   [12] g_cityTotalMoney       city money total (flt_641FD4)
//   [13] g_cityTotalGoods       city goods total (flt_641FD8)
//
//   --- game clock / time ---
//   [14] g_sysGameTime          the game clock (qword_13CE852)
//   [15] g_tickClock            tick-handler clock mirror
//   [16] g_tickSubCounter       per-advance sub-counter (dword_62EB98)
//   [17] g_gameTick             wall-clock tick (dword_62EB38)
//
//   --- treasury / player bookkeeping ---
//   [18] g_currentPlayer        current player index (word_63CC5C)
//   [19] g_sysActivePlayer      active player slot (dword_63CC24)
//
//   --- other reconstructed world tables ---
//   [20] g_lawTable             law/Gesetz table
//   [21] g_eventTable           event descriptor table
//   [22] g_eventTableCount      live event count
//   [23] g_missionLcgState      mission/event RNG state
//   [24] g_officeHolders        office-holder table
//   [25] g_crimeTable           crime/Straftat table
//   [26] g_relationMatrix       inter-faction relation matrix
// ===========================================================================
WorldSnapshot FoldFullWorld() {
    using namespace guild::sim;
    using namespace guild::world;

    // [0] Start from the base entity snapshot (HashWorldState equivalent).
    WorldSnapshot base = SnapshotWorld();
    WorldSnapshot snap;
    snap.regions = base.regions;   // carry the base regions for localized diffs

    Fnv1a64 whole;
    // Fold the base whole-world digest as a scalar so the full digest strictly
    // dominates the base digest (a base-only change still moves the full hash)
    // without re-walking the base arrays here.
    whole.scalar(base.hash);

    // --- buildings ---
    FoldRegion(snap, whole, "g_buildingTypes",
               g_buildingTypes, sizeof(g_buildingTypes));
    FoldRegion(snap, whole, "g_buildingTypesLoaded",
               &g_buildingTypesLoaded, sizeof(g_buildingTypesLoaded));
    FoldRegion(snap, whole, "g_buildingNextId",
               &g_buildingNextId, sizeof(g_buildingNextId));
    FoldRegion(snap, whole, "g_sceneTypes",
               g_sceneTypes, sizeof(g_sceneTypes));
    FoldRegion(snap, whole, "g_sceneTypesLoaded",
               &g_sceneTypesLoaded, sizeof(g_sceneTypesLoaded));
    FoldRegion(snap, whole, "g_sceneTypeRemap",
               g_sceneTypeRemap, sizeof(g_sceneTypeRemap));
    FoldRegion(snap, whole, "g_prodStore",
               g_prodStore, sizeof(g_prodStore));
    FoldRegion(snap, whole, "g_prodSchedules",
               g_prodSchedules, sizeof(g_prodSchedules));

    // --- economy / market ---
    FoldRegion(snap, whole, "g_cities",
               g_cities, sizeof(g_cities));
    FoldRegion(snap, whole, "g_goods",
               g_goods, sizeof(g_goods));
    FoldRegion(snap, whole, "g_capDivisor",
               &g_capDivisor, sizeof(g_capDivisor));
    FoldRegion(snap, whole, "g_cityTotalMoney",
               &g_cityTotalMoney, sizeof(g_cityTotalMoney));
    FoldRegion(snap, whole, "g_cityTotalGoods",
               &g_cityTotalGoods, sizeof(g_cityTotalGoods));

    // --- game clock / time ---
    FoldRegion(snap, whole, "g_sysGameTime",
               &g_sysGameTime, sizeof(g_sysGameTime));
    FoldRegion(snap, whole, "g_tickClock",
               &g_tickClock, sizeof(g_tickClock));
    FoldRegion(snap, whole, "g_tickSubCounter",
               &g_tickSubCounter, sizeof(g_tickSubCounter));
    FoldRegion(snap, whole, "g_gameTick",
               &g_gameTick, sizeof(g_gameTick));

    // --- treasury / player bookkeeping ---
    FoldRegion(snap, whole, "g_currentPlayer",
               &g_currentPlayer, sizeof(g_currentPlayer));
    FoldRegion(snap, whole, "g_sysActivePlayer",
               &g_sysActivePlayer, sizeof(g_sysActivePlayer));

    // --- other reconstructed world tables ---
    FoldRegion(snap, whole, "g_lawTable",
               g_lawTable, sizeof(g_lawTable));
    FoldRegion(snap, whole, "g_eventTable",
               g_eventTable, sizeof(g_eventTable));
    FoldRegion(snap, whole, "g_eventTableCount",
               &g_eventTableCount, sizeof(g_eventTableCount));
    FoldRegion(snap, whole, "g_missionLcgState",
               &g_missionLcgState, sizeof(g_missionLcgState));
    FoldRegion(snap, whole, "g_officeHolders",
               g_officeHolders, sizeof(g_officeHolders));
    FoldRegion(snap, whole, "g_crimeTable",
               g_crimeTable, sizeof(g_crimeTable));
    FoldRegion(snap, whole, "g_relationMatrix",
               g_relationMatrix, sizeof(g_relationMatrix));

    snap.hash = whole.value();
    return snap;
}

} // namespace

std::uint64_t HashFullWorld() {
    return FoldFullWorld().hash;
}

WorldSnapshot SnapshotFullWorld() {
    return FoldFullWorld();
}

} // namespace guild::play
