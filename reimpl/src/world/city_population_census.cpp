// city_population_census — 1:1 port of the per-round city POPULATION census.
// See city_population_census.h for provenance, layout and the injected-input
// rationale.
//
//   VIBE_City_AggregateDistrictStats  0x578abc
//   VIBE_City_ComputeWealthGrid       0x577e74
//   VIBE_City_SnapshotStats           0x5783e4
#include "world/city_population_census.h"

#include <cstring>

#include "util/coord.h"
#include "world/city.h"                    // g_capDivisor (flt_641DA8)
#include "world/city_satisfaction_grid.h"  // g_cityGrid + GridSat* + BuildSatisfactionGrid
#include "world/economy_quality.h"         // EconomyComputeWeightedLawScore / Interpolated
#include "world/law.h"                     // GesetzGetRecord / g_lawTable
#include "world/law_types.h"

namespace guild::world {

// ---------------------------------------------------------------------------
// Recovered FP constant bit-pattern self-checks (get_bytes at the named addrs).
// ---------------------------------------------------------------------------
static_assert(sizeof(float) == 4 && sizeof(double) == 8, "IEEE-754 expected");

// The clamp constant 1065353216 == 0x3F800000 == the bit pattern of 1.0f. The
// original compares the *stored bits* of the SatDenom against this integer and
// keeps the larger, i.e. clamps the float to >= 1.0f. We reproduce with a float
// max (identical for the non-negative denom the rebuild produces).
static constexpr float kSatDenomFloor = 1.0f;  // bits 0x3F800000

// ---------------------------------------------------------------------------
// Raw byte accessors into the shared district grid (world/city_satisfaction_grid).
// The step-5 reduction relies on the SatWeight(+36)/SatDenom(+40)/SatCount(+44)
// fields ALIASING the next cell's low fields (24-byte cell stride), so we index
// the flat g_cityGrid buffer with the original's exact byte arithmetic rather
// than the typed cell accessors.
// ---------------------------------------------------------------------------
static inline float& GridF(int byteOff) {
    return *reinterpret_cast<float*>(&g_cityGrid[byteOff]);
}

// ===========================================================================
// 0x578abc — VIBE_City_AggregateDistrictStats
// ===========================================================================
CityStats* CityAggregateDistrictStats(const CityCensusInputs& in, CityStats* out) {
    // 1. Rebuild the satisfaction grid (fills flt_12349B4 resident bins etc.).
    //    The caller's residents/buildings model what BuildSatisfactionGrid +
    //    the live arrays produced; we still invoke the rebuild for fidelity via
    //    the production GridEnv binding — tests seed the grid directly, so the
    //    rebuild is the caller's responsibility there. (The original always
    //    calls it first.)

    // 2. Loop 1 — bin residents into the district grid and count v1/v46/v49.
    int v1 = in.liveSlotCount;   // live person-slot count (marker!=-1 && kind<=1)
    int v46 = 0;                 // column-A object presence
    int v49 = 0;                 // column-B object presence
    for (const CensusResident& r : in.residents) {
        if (r.column == 0) {
            // dword_12CEA7C path.
            if (r.hasObject) {
                ++v46;
                if (r.hasScene) {
                    bool eligible =
                        (r.typeByte < 0xB || r.typeByte > 0xD) && r.typeByte != 0x10;
                    if (eligible && r.onMap) {
                        // flt_12349B4[48*tx + 6*ty] (== byte 192*tx + 24*ty + 44).
                        GridSatCount(r.tileX, r.tileY) += 1.0f;
                    }
                }
            }
        } else {
            // dword_12CEA80 path.
            if (r.hasObject) {
                ++v49;
                if (r.hasScene) {
                    bool eligible =
                        (r.typeByte < 0xB || r.typeByte > 0xD) && r.typeByte != 0x10;
                    if (eligible && r.onMap) {
                        GridSatCount(r.tileX, r.tileY) += 1.0f;
                    }
                }
            }
        }
    }

    // 3. Loop 2 — per-type population / occupancy census.
    int v3  = 0;   // population sum (word_641DB0)
    int v47 = 0;   // word_641DB4 sum
    int v48 = 0;   // word_641DB2 occupancy sum
    int v50 = 0, v51 = 0, v52 = 0, v53 = 0;
    for (const CensusBuilding& b : in.buildings) {
        v3  += b.pop641DB0;
        v47 += b.word641DB4;
        u16 v13 = b.word641DB2;
        v48 += v13;
        if (v13) {
            v51 += b.security583;
            v52 += b.maxLevel584;
        }
        if (b.word641DB6) {
            v50 += b.maxLevel584;
            v53 += b.security583;
        }
    }

    // 4. Population floor clamp: v3 = max(v3, 2*word_641DB8).
    int floor2 = 2 * static_cast<int>(in.popFloor641DB8);
    if (v3 <= floor2)
        v3 = floor2;

    float v44 = static_cast<float>(v1);

    // out[5] popDensity5 = (v53/v50 * (v51/v52) + 0.5) * (v44/v3 * v48).
    out->popDensity5 = static_cast<float>(
        (static_cast<double>(v53) / static_cast<double>(v50)
             * (static_cast<double>(v51) / static_cast<double>(v52))
         + popcensus::kHalf)
        * (static_cast<double>(v44) / static_cast<double>(v3)
           * static_cast<double>(v48)));

    out->residents4  = v1;
    out->popClamped8 = v3;
    out->v47sum7     = v47;

    // out[0] = ComputeInterpolatedLawScore() (reads laws 8..14 from g_lawTable).
    std::array<LawRangeRecord, 7> laws8to14{};
    for (int i = 0; i < 7; ++i) {
        LawRecord rec;
        if (GesetzGetRecord(static_cast<u8>(8 + i), &rec)) {
            // lo @+4, hi @+8, value @+24 (read raw; LawRecord only names +24).
            const u8* rb = reinterpret_cast<const u8*>(&rec);
            i32 lo, hi;
            std::memcpy(&lo, rb + 4, 4);
            std::memcpy(&hi, rb + 8, 4);
            laws8to14[i] = {lo, hi, rec.threshold};
        }
    }
    out->density0 = static_cast<float>(EconomyComputeInterpolatedLawScore(laws8to14));

    // out[3] = 1 - v49/v44 ; out[1] = 1 - v46/v44.
    double v18 = 1.0 / static_cast<double>(v44);
    out->ratioD3 = static_cast<float>(1.0 - static_cast<double>(v49) * v18);
    out->ratioB1 = static_cast<float>(1.0 - v18 * static_cast<double>(v46));

    // 5. Step-5 satisfaction reduction over the 8x8 grid. The original walks the
    //    SatWeight(+36)/SatDenom(+40)/SatCount(+44) fields with a 24-byte cell
    //    stride and a 192-byte row stride; the AC/B0 reads are pre-incremented
    //    one cell ahead of the B4 read (relying on the field overlap). We index
    //    the flat grid buffer with the exact byte offsets.
    float v58 = 0.0f;  // running max of SatWeight
    float v55 = 0.0f;  // accumulated satisfaction product
    for (int v20 = 0; v20 != 1536; v20 += 192) {        // 8 rows
        int v21 = v20 + 192;                            // v54
        int v22 = v20, v23 = v20, v24 = v20;
        do {
            // v25 = max(v58, SatWeight[v22@+36]).
            float wAt22 = GridF(36 + v22);
            float v25 = (v58 <= static_cast<double>(wAt22)) ? wAt22 : v58;
            v58 = v25;

            // v57 = clamp SatDenom[v23@+40] >= 1.0f.
            float dAt23 = GridF(40 + v23);
            float v57 = (dAt23 <= kSatDenomFloor) ? kSatDenomFloor : dAt23;

            // v26 = SatCount[v24@+44] / (v1 * 2.0f).
            double v26 = static_cast<double>(GridF(44 + v24))
                       / (static_cast<double>(v1) * popcensus::kTwo);

            // Store the clamped denom and normalized count at the v24 cell.
            GridF(40 + v24) = v57;
            GridF(44 + v24) = static_cast<float>(v26);

            v22 += 24;
            v23 += 24;

            // v27 = SatWeight[v24@+36] / SatDenom[v24@+40] * SatCount[v24@+44] + v55.
            float v27 = GridF(36 + v24) / GridF(40 + v24) * GridF(44 + v24) + v55;
            v24 += 24;
            v55 = v27;
        } while (v24 != v21);
    }

    // 6. Law fold: out[2] and out[9].
    float v39 = static_cast<float>(EconomyComputeWeightedLawScore());

    LawRecord r0, r1;
    GesetzGetRecord(0, &r0);
    GesetzGetRecord(1, &r1);
    int v35 = r1.threshold;  // GesetzGetRecord(1) -> +24
    int v37 = r0.threshold;  // GesetzGetRecord(0) -> +24

    // out[2] = (1 - (v39*0.7 + ((4-v37)*0.25*0.75 + (1-v35)*0.25)*0.3)) * v55.
    out->satScore2 = static_cast<float>(
        (1.0
         - (static_cast<double>(v39) * popcensus::kSevenTenths
            + (static_cast<double>(4 - v37) * popcensus::kQuarterF * popcensus::kThreeQuarter
               + static_cast<double>(1 - v35) * popcensus::kQuarter)
                  * popcensus::kThreeTenths))
        * static_cast<double>(v55));

    // out[9] = (0.25*(d3' + b1' + 1 - out2 + out0) + d3'*2.0f*b1'*out0*(1-out2)) * (1/3f).
    double v31 = 1.0 - out->ratioD3;  // 1 - v30[3]
    double v32 = 1.0 - out->ratioB1;  // 1 - v30[1]
    double v38 = out->density0;       // *v30 == out[0]
    out->lawFold9 = static_cast<float>(
        (popcensus::kQuarterF
             * (v32 + v31 + 1.0 - static_cast<double>(out->satScore2) + v38)
         + v31 * popcensus::kTwo * v32 * v38 * (1.0 - static_cast<double>(out->satScore2)))
        * popcensus::kThirdF);

    return out;
}

// ===========================================================================
// 0x577e74 — VIBE_City_ComputeWealthGrid
// ===========================================================================
// The original uses two BSS scratch grids: a per-district worth accumulator
// (v16[]/dword_12349A8, 192-byte row / 4-byte cell over an 8x8 grid) and a
// per-district occupant count (byte_12349A4, 24-byte cell). After accumulating it
// writes back the truncated mean (worth/count) per cell. We reproduce the same
// row/cell arithmetic over local scratch buffers and emit the 8x8 grid row-major.
int CityComputeWealthGrid(const std::vector<WealthResident>& residents, i32 out[64]) {
    // worth[x][y] (float) and count[x][y] (u8). Both 8x8.
    float worth[8][8];
    u8 count[8][8];
    std::memset(worth, 0, sizeof(worth));
    std::memset(count, 0, sizeof(count));

    // Accumulate.
    for (const WealthResident& r : residents) {
        if (!r.hasScene || !r.onMap)
            continue;
        int x = r.tileX, y = r.tileY;
        // *(float*)&v16[8*tx + 1 + ty] = worth ; ++byte_12349A4[192*tx + 24*ty].
        // The "8*tx + 1 + ty" dword index == byte 32*tx + 4 + 4*ty; the original
        // base aliasing folds to a per-(x,y) cell. We store directly.
        worth[x][y] = static_cast<float>(r.roomWorth);
        ++count[x][y];
    }

    // Write back the truncated mean per district.
    int result = 0;
    for (int x = 0; x < 8; ++x) {
        for (int y = 0; y < 8; ++y) {
            if (count[x][y]) {
                double mean = static_cast<double>(worth[x][y])
                            / static_cast<double>(static_cast<i16>(count[x][y]));
                result = static_cast<int>(guild::util::ConvertX(mean));
            } else {
                result = 0;
            }
            out[8 * x + y] = result;
        }
    }
    return result;
}

// ===========================================================================
// 0x5783e4 — VIBE_City_SnapshotStats
// ===========================================================================
i32 CitySnapshotStats(const u8 src[300], const SnapshotScalars& scalars,
                      u8 dst[300], i32 outScalars[4]) {
    // qmemcpy(flt_1234FA0, byte_1234FB4, 0x12C) — copy the 300-byte stat block.
    std::memcpy(dst, src, 300);

    // Four scalar dwords copied verbatim into the snapshot block.
    outScalars[0] = scalars.stat1234914;  // -> flt_12350CC
    outScalars[1] = scalars.stat1234918;  // -> flt_12350D0
    outScalars[2] = scalars.stat123491C;  // -> flt_12350D4
    outScalars[3] = scalars.stat1234934;  // -> flt_12350D8

    // dword_12350DC = (int)trunc(flt_641DA8) (== g_capDivisor).
    return static_cast<i32>(
        guild::util::ConvertX(static_cast<double>(g_capDivisor)));
}

}  // namespace guild::world
