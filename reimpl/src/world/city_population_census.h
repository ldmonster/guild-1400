#pragma once
// City population census / per-round district population update (gilde.exe).
//
// This module recovers the per-round POPULATION DYNAMICS aggregation that the
// city simulation runs once per step to derive the city's live population
// figures from the actual resident and building populations on the map. It is a
// distinct pass from VIBE_Economy_ComputePopulationTrend (the AI-consumed score
// in world/economy_tick.cpp) and from the price-level EMA: this one walks the
// real Person/Object arrays, bins residents into the 8x8 district grid, scans
// the building-type census table for housing capacity / occupancy, folds the law
// state in, and writes out the 10-field broadcast stats record (current count,
// capacity, density, residential / industrial ratios, satisfaction).
//
// Translated functions:
//   VIBE_City_AggregateDistrictStats  0x578abc — the per-round population census
//   VIBE_City_ComputeWealthGrid       0x577e74 — per-district mean room-worth grid
//   VIBE_City_SnapshotStats           0x5783e4 — snapshot stats -> broadcast block
//
// REUSE (extern / no redefinition):
//   * world/city_satisfaction_grid.{h,cpp} — the 8x8 district grid backing store
//       (g_cityGrid + the GridSat* accessors) and VIBE_City_BuildSatisfactionGrid,
//       which AggregateDistrictStats calls first. The satisfaction-count field
//       (flt_12349B4) is the district resident bin both passes share.
//   * world/economy_quality.{h,cpp} — EconomyComputeWeightedLawScore (0x57a580)
//       and EconomyComputeInterpolatedLawScore (0x57a990).
//   * world/law.{h,cpp}             — GesetzGetRecord / g_lawTable (the law records
//       the interpolated-score read and the two GesetzGetRecord(0/1) calls use).
//   * util/coord.cpp               — ConvertX (VIBE_Coord_ConvertX truncation).
//   * world/city.cpp               — g_capDivisor (flt_641DA8) for the snapshot.
//
// CROSS-CLUSTER LIVE STATE (injected). The two scan loops read the live Person
// array (word_12CE910, stride 536, 768 slots) via two parallel resident-slot
// pointer columns (dword_12CEA7C / dword_12CEA80) and the live Object array
// (dword_13CE298, stride 169, 256 slots) joined to the building-TYPE table
// (dword_13CE294, stride 589) and the per-type census word table (word_641DB0..).
// All of these are runtime BSS (zero in the cold IDB) and owned by the sim
// entity/object agents. To exercise the exact census arithmetic in isolation we
// surface the two scans as injected input lists (mirroring the GridEnv pattern
// already used by city_satisfaction_grid). A production backend fills these from
// the live arrays; tests bind deterministic vectors.
#include <array>
#include <vector>

#include "guild/common/types.h"

namespace guild::world {

// ===========================================================================
// Recovered FP constants (get_bytes; raw bit patterns asserted in the .cpp).
// ===========================================================================
namespace popcensus {
constexpr double kHalf        = 0.5;                  // dbl_625694
constexpr float  kTwo         = 2.0f;                 // flt_62569C
constexpr float  kQuarterF    = 0.25f;               // flt_6256A0
constexpr double kThreeQuarter= 0.75;                // dbl_6256A4
constexpr double kQuarter     = 0.25;                // dbl_6256AC
constexpr float  kSevenTenths = 0.699999988079071f;  // flt_6256B4 (0.7f)
constexpr double kThreeTenths = 0.3;                 // dbl_6256BC
constexpr float  kThirdF      = 0.3333333432674408f; // flt_6256C4 (1/3f)
}  // namespace popcensus

// ===========================================================================
// Loop-1 input: one resident placement read from the two resident-slot columns.
// ===========================================================================
// The original walks the 768 person slots; for each LIVE slot (marker != -1 and
// kind byte <= 1) it dereferences TWO parallel pointer columns
// (dword_12CEA7C[slot] and dword_12CEA80[slot]). Each non-null pointer is an
// object record; if it has a scene back-ptr (+97 != 0), an eligible type byte
// (NOT in 0xB..0xD and != 0x10), and tiles onto the map, it adds 1.0 to that
// district's resident-count bin (flt_12349B4). Both columns also bump raw
// counters used for the residential / industrial ratios.
//
// We surface each non-null column entry as a CensusResident. The per-slot live
// gate (marker/kind) is the caller's concern; only entries that passed it appear
// here. `column` selects which counter the entry feeds (0 == dword_12CEA7C path,
// the +0x4 "homePop" counter v46; 1 == dword_12CEA80 path, the v49 counter).
struct CensusResident {
    bool  hasObject  = false;  // column pointer non-null (object record present)
    bool  hasScene   = false;  // object +97 != 0 (placed in the world)
    int   typeByte   = 0;      // object type byte (gates 0xB..0xD and 0x10)
    bool  onMap      = false;  // WorldToCityTile succeeded
    int   tileX      = 0;      // 0..7 district x (only if onMap)
    int   tileY      = 0;      // 0..7 district y (only if onMap)
    int   column     = 0;      // 0 == column A (v46), 1 == column B (v49)
};

// ===========================================================================
// Loop-2 input: one building census entry (object joined to its type record).
// ===========================================================================
// The original walks the 256 object slots; for each whose type byte is
// 0 < t < 68 and whose employment word (+39) != 0xFFFF it reads the per-type
// census words word_641DB0[4*t] (population), word_641DB4[4*t] (v47 accumulator),
// word_641DB2[4*t] (v48 occupancy), word_641DB6[4*t] (a gate flag) and the type
// record's +583 (security / current level) and +584 (max upgrade level).
struct CensusBuilding {
    u16 pop641DB0   = 0;  // word_641DB0[4*t] -> v3 population sum
    u16 word641DB2  = 0;  // word_641DB2[4*t] -> v48 occupancy sum + gates +583/+584
    u16 word641DB4  = 0;  // word_641DB4[4*t] -> v47 sum
    u16 word641DB6  = 0;  // word_641DB6[4*t] -> gate for the v50/v53 sums
    u8  security583 = 0;  // type record +583 (current level)
    u8  maxLevel584 = 0;  // type record +584 (max upgrade level)
};

// The 10-field broadcast stats record the aggregate writes (a1 / v45, floats &
// ints interleaved exactly as the original stores them). Field names map to the
// store offsets the original uses (out[i] is a 4-byte slot).
struct CityStats {
    float density0;     // out[0]  (== v45[0], written by the law-fold tail)
    float ratioB1;      // out[1]  1 - v46/count  (column-A presence ratio)
    float satScore2;    // out[2]  the satisfaction*law fold (v55 chain)
    float ratioD3;      // out[3]  1 - v49/count  (column-B presence ratio)
    i32   residents4;   // out[4]  v1  (live resident-slot count)
    float popDensity5;  // out[5]  the population/occupancy density product
    i32   v47sum7;      // out[7]  v47 (word_641DB4 sum)
    i32   popClamped8;  // out[8]  v3  (population sum, clamped >= 2*word_641DB8)
    float lawFold9;     // out[9]  the final law/satisfaction composite
};

// ===========================================================================
// Census inputs the two scans + the law fold consume (all live BSS in the orig).
// ===========================================================================
struct CityCensusInputs {
    // Loop-1: the resident placements that passed the per-slot live gate.
    std::vector<CensusResident> residents;
    // v1 == number of LIVE person slots scanned (marker!=-1 && kind<=1). It is
    // NOT residents.size() (a live slot may have both/neither column populated),
    // so it is supplied explicitly; the original divides by it.
    int liveSlotCount = 0;

    // Loop-2: the per-object building census entries that passed the type gate.
    std::vector<CensusBuilding> buildings;
    // word_641DB8 — the population floor scalar: v3 = max(v3, 2*word_641DB8).
    u16 popFloor641DB8 = 0;

    // The 7 law records 8..14 (lo@+4, hi@+8, value@+24) the interpolated-law
    // score reads. If left default the live g_lawTable is read via GesetzGetRecord.
    bool useLiveLawTable = true;
};

// ===========================================================================
// 0x578abc — VIBE_City_AggregateDistrictStats  (__usercall, eax=out@a1)
// ===========================================================================
// Runs the per-round population census:
//   1. BuildSatisfactionGrid() (rebuilds the district satisfaction weights).
//   2. Loop 1: bin residents into flt_12349B4 per district; count v1 / v46 / v49.
//   3. Loop 2: sum the per-type population/occupancy census words + level bytes.
//   4. Clamp population v3 >= 2*word_641DB8; write the resident/ratio fields.
//   5. Fold the law scores (interpolated + weighted) and the per-district
//      satisfaction product into out[2] / out[9] / out[5].
// Returns `out`. The district grid (flt_12349B4 etc.) must have been seeded by a
// prior BuildSatisfactionGrid run (which this calls) — the resident bins are read
// back in step 5. `out` must point at a CityStats.
CityStats* CityAggregateDistrictStats(const CityCensusInputs& in, CityStats* out);

// ===========================================================================
// 0x577e74 — VIBE_City_ComputeWealthGrid  (void)
// ===========================================================================
// Builds the per-district MEAN room-worth grid: zeroes the per-district worth /
// count cells, walks the residents (here the CensusResident list with a worth and
// occupancy count per entry), accumulates the worth into each district cell and
// bumps that cell's occupant count, then writes back the truncated mean
// (worth / count) per district. Returns the last cell value written (the original
// returns the final `result`). The grid cells are surfaced as an out array of
// 64 ints (8x8, row-major: district [x][y] at out[8*x + y]).
//
// Each input carries its district tile (tileX/tileY), the room worth
// (VIBE_BuildingValue_ComputeRoomWorth result) and contributes 1 to that cell's
// occupant count. Entries with onMap==false or hasScene==false are skipped
// (matching the original's *(obj+97) and WorldToCityTile gates).
struct WealthResident {
    bool hasScene = false;  // object +97 != 0
    bool onMap    = false;  // WorldToCityTile succeeded
    int  tileX    = 0;      // 0..7
    int  tileY    = 0;      // 0..7
    i32  roomWorth = 0;     // VIBE_BuildingValue_ComputeRoomWorth result
};
int CityComputeWealthGrid(const std::vector<WealthResident>& residents,
                          i32 out[64]);

// ===========================================================================
// 0x5783e4 — VIBE_City_SnapshotStats  (__thiscall)
// ===========================================================================
// Copies the 300-byte district stat block (flt_1234FA0..) into the broadcast
// snapshot (byte_1234FB4), copies four scalar stat dwords, then truncates the
// cap divisor (flt_641DA8 == g_capDivisor) into the snapshot's last dword. The
// source block + the four scalars are surfaced as inputs; the destination is the
// 304-byte snapshot returned via `outSnapshot` (300 bytes copied + 4 scalars
// laid out as the original stores them, followed by the truncated divisor).
struct SnapshotScalars {
    i32 stat1234914 = 0;  // dword_1234914 -> flt_12350CC
    i32 stat1234918 = 0;  // dword_1234918 -> flt_12350D0
    i32 stat123491C = 0;  // dword_123491C -> flt_12350D4
    i32 stat1234934 = 0;  // dword_1234934 -> flt_12350D8
};
// `src` is the 300-byte (75-dword) source block; `dst` receives the 300 bytes.
// Returns the truncated divisor (int)trunc(g_capDivisor) the original writes to
// dword_12350DC.
i32 CitySnapshotStats(const u8 src[300], const SnapshotScalars& scalars,
                      u8 dst[300], i32 outScalars[4]);

}  // namespace guild::world
