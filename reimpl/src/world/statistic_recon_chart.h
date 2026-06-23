#pragma once
// guild::world — OFFICIAL-COMPARISON statistics chart aggregation.
// Reconstructed 1:1 from gilde.exe.
//
// Cluster:
//   0x58beb8 VIBE_StatChart_BuildOfficialComparison -> StatChartBuildOfficialComparison
//   0x55fa88 VIBE_StatPanel_ShowCompareChart        -> StatPanelShowCompareChart (hook)
//
// VIBE_StatChart_BuildOfficialComparison scans the 768-entry person table
// (word_12CE910, 536-byte records / 268-word stride), selects every "official"
// (record type byte +2 in {5,6,7}, up to 8), and for each computes a normalized
// composite "standing" score from five weighted terms:
//
//   needs    = (sum of record bytes +128..+132)        * flt_6267DC   -> rec[6]
//   religion = (i16)(record byte +13)                  * flt_6267E0   -> rec[3]
//   office   = (officeDef(+358).b2 + officeDef(+361).b2) * flt_6267E4  -> rec[4]
//   favor    = (avg favorability over all type-{4..7} persons) * flt_6267E8 / n -> rec[5]
//   wealth   = max(ComputeTotalWealth(slot), 1)        (int)          -> rec[1]
//   ratio    = wealth / maxWealthAcrossOfficials                      -> rec[2]
//   composite= ratio*flt_6267D0 + religion*flt_6267D4 + office*flt_6267D4
//                                + favor*flt_6267D4 + needs*flt_6267D8 -> rec[7]
//   barHeight= (int) ConvertX( composite * (maxWealth*flt_6267CC) )
//
// The entity table, office definitions, per-pair favorability, total-wealth and the
// ConvertX FPU round are coupled leaves (the AI/wealth/office subsystems and the
// 768*536-byte person table). They are routed through StatChartEnv so the AGGREGATION
// MATH above is reproduced and tested byte-for-byte. NEVER faked: the selection loop,
// accumulators, weights, normalization and clamps are exactly the original's.
//
// VIBE_StatPanel_ShowCompareChart @0x55fa88 is the modal UI panel that calls
// BuildOfficialComparison then builds a form with one slider row per official; it is a
// UI leaf and is exposed as an inert command hook (StatPanelCompareSink) — the only
// pure logic it carries (the per-row color-index clamp) is reproduced as a helper.

#include "guild/common/types.h"

#include <array>
#include <cstddef>

namespace guild::world {

using guild::i32;
using guild::u8;
using guild::u16;

// Float constants recovered from gilde.exe (.rdata). Defined from their exact IEEE-754
// bit patterns (decimal literals would not round-trip to the same float).
namespace detail {
inline float FloatFromBits(std::uint32_t bits) {
    float f;
    static_assert(sizeof(float) == sizeof(std::uint32_t), "float must be 32-bit");
    __builtin_memcpy(&f, &bits, sizeof(f));
    return f;
}
}  // namespace detail
inline const float kStatMaxWealthScale = detail::FloatFromBits(0x38d1b717);  // flt_6267CC
inline const float kStatWeightRatio    = detail::FloatFromBits(0x3e99999a);  // flt_6267D0
inline const float kStatWeightMid      = detail::FloatFromBits(0x3e19999a);  // flt_6267D4
inline const float kStatWeightNeeds    = detail::FloatFromBits(0x3e800000);  // flt_6267D8
inline const float kStatNeedsScale     = detail::FloatFromBits(0x3a500d01);  // flt_6267DC
inline const float kStatReligionScale  = detail::FloatFromBits(0x3e2aaaab);  // flt_6267E0
inline const float kStatOfficeScale    = detail::FloatFromBits(0x3d9d89d9);  // flt_6267E4
inline const float kStatFavorScale     = detail::FloatFromBits(0x3c23d70a);  // flt_6267E8

inline constexpr int kStatPersonCount = 768;     // person-table capacity
inline constexpr int kStatMaxOfficials = 8;      // 72/9 — output array cap
inline constexpr int kStatRecordStride = 9;      // floats per official record

// One official's computed comparison record — the 9-float stride the original packs
// into its output buffer (read back by reinterpreting indices as float OR int).
struct OfficialCompareRecord {
    u16 slot = 0;       // rec[0] (u16) — person-table index of this official
    i32 wealth = 0;     // rec[1] (int) — ComputeTotalWealth, clamped >= 1
    float ratio = 0;    // rec[2] — wealth / maxWealth
    float religion = 0; // rec[3] — (i16)record[+13] * flt_6267E0
    float office = 0;   // rec[4] — (def358.b2 + def361.b2) * flt_6267E4
    float favor = 0;    // rec[5] — avgFavor * flt_6267E8 / n
    float needs = 0;    // rec[6] — sum(record[+128..+132]) * flt_6267DC
    float composite = 0;// rec[7] — weighted composite "standing"
    int barHeight = 0;  // (int)ConvertX(composite * maxWealth*flt_6267CC)
};

// ---------------------------------------------------------------------------
// Coupled-leaf environment. Provides the person-table reads, office defs, per-pair
// favorability, total wealth, and the ConvertX FPU round. Defaults are inert.
// ---------------------------------------------------------------------------
struct StatChartEnv {
    virtual ~StatChartEnv() = default;

    // record type byte (+2) of person `slot`; the selection key. 0xFF == empty.
    virtual u8 PersonType(int slot) const { (void)slot; return 0; }
    // true when person `slot` is a free slot (word_12CE910[268*slot] == -1).
    virtual bool PersonFreeSlot(int slot) const { (void)slot; return true; }
    // record byte at the given offset for person `slot` (needs +128.., religion +13).
    virtual u8 PersonByte(int slot, std::size_t off) const {
        (void)slot; (void)off; return 0;
    }
    // record trait byte (+358 / +361), used as an office-def rank.
    virtual u8 PersonOfficeRank(int slot, std::size_t off) const {
        (void)slot; (void)off; return 0;
    }
    // VIBE_Office_GetDefinition(rank).b2 — byte +2 of the office definition.
    virtual u8 OfficeDefByte2(u8 rank) const { (void)rank; return 0; }
    // VIBE_Ai_ComputePersonFavorability(self, other, applyLaw=1) — [0,100].
    virtual double Favorability(int self, int other) const {
        (void)self; (void)other; return 0.0;
    }
    // VIBE_Person_ComputeTotalWealth(slot) — total wealth, or -1 for free slot.
    virtual i32 ComputeTotalWealth(int slot) const { (void)slot; return 0; }
    // VIBE_Coord_ConvertX — FPU frndint/round applied before the (int) bar-height cast.
    virtual double ConvertX(double x) const { return x; }
};

// gilde.exe 0x58beb8 — VIBE_StatChart_BuildOfficialComparison.
// Fills `out` (at most kStatMaxOfficials records) and returns the official count.
int StatChartBuildOfficialComparison(
    std::array<OfficialCompareRecord, kStatMaxOfficials>& out, StatChartEnv& env);

// ---------------------------------------------------------------------------
// VIBE_StatPanel_ShowCompareChart @0x55fa88 — modal UI panel (coupled leaf).
// The slider color index is clamped: `idx = clamp(growthCode - 1342, 0..7)` indexing
// the palette table dword_552704[8] (recovered: {0x7E,0x84,0x8A,0x90,0x96,0x9C,0xA2,0xA8}).
// Reproduced as a pure helper; the form/loop is exposed as an inert sink.
// ---------------------------------------------------------------------------
inline constexpr std::array<i32, 8> kCompareSliderPalette = {
    0x7E, 0x84, 0x8A, 0x90, 0x96, 0x9C, 0xA2, 0xA8};

// Color-index clamp + palette lookup (dword_12CE964[134*slot] is `growthCode`).
i32 CompareSliderColor(i32 growthCode);

// Inert command sink for the panel's form/widget operations.
struct StatPanelCompareSink {
    virtual ~StatPanelCompareSink() = default;
    // Whether the panel may open (VIBE_Interaction_TestHandlerFlagDword(4)).
    virtual bool CanOpen() { return false; }
    virtual void EmitRows(const std::array<OfficialCompareRecord, kStatMaxOfficials>&,
                          int /*count*/) {}
};
void StatPanel_SetCompareSink(StatPanelCompareSink* sink);

// gilde.exe 0x55fa88 — build + push comparison through the sink; returns the official
// count when the panel opened, else 0.
int StatPanelShowCompareChart(StatChartEnv& env);

}  // namespace guild::world
