#include "world/statistic_recon_chart.h"

namespace guild::world {

namespace {
StatPanelCompareSink* g_compareSink = nullptr;
}

// gilde.exe 0x58beb8 — VIBE_StatChart_BuildOfficialComparison.
int StatChartBuildOfficialComparison(
    std::array<OfficialCompareRecord, kStatMaxOfficials>& out, StatChartEnv& env) {
    // ---- Pass 1: select officials (type byte +2 in {5,6,7}; cap 8). -----------
    int count = 0;       // v3
    int packed = 0;      // v2 — output cursor in float units (stride 9)
    int maxWealth = 0;   // v37
    int slot = 0;        // v45
    while (slot < kStatPersonCount && packed < kStatMaxOfficials * kStatRecordStride) {
        u8 type = env.PersonType(slot);  // byte_12CE912[536*slot]
        if (type == 6 || type == 7 || type == 5) {
            out[count].slot = static_cast<u16>(slot);  // *(WORD*)v6 = v45
            ++count;
            packed += kStatRecordStride;
        }
        ++slot;
    }

    if (count == 0)
        return 0;

    // ---- Pass 2: per-official term accumulation. ------------------------------
    for (int i = 0; i < count; ++i) {
        OfficialCompareRecord& rec = out[i];
        const int s = rec.slot;

        // needs = sum(record[+128..+132]) * flt_6267DC
        int needsSum = 0;  // v10
        for (int j = 0; j < 5; ++j)
            needsSum += static_cast<int>(env.PersonByte(s, 128 + j));
        rec.needs = static_cast<float>(static_cast<double>(needsSum) * kStatNeedsScale);

        // religion = (i16)record[+13] * flt_6267E0
        int religionByte = static_cast<int>(env.PersonByte(s, 13));  // byte_12CE91D
        rec.religion = static_cast<float>(
            static_cast<double>(static_cast<guild::i16>(religionByte)) * kStatReligionScale);

        // office = (def(+358).b2 + def(+361).b2) * flt_6267E4
        u8 rank358 = env.PersonOfficeRank(s, 358);  // byte_12CEA76
        float b358 = static_cast<float>(env.OfficeDefByte2(rank358));
        u8 rank361 = env.PersonOfficeRank(s, 361);  // byte_12CEA79
        double officeSum = static_cast<double>(env.OfficeDefByte2(rank361)) + b358;
        rec.office = static_cast<float>(officeSum * kStatOfficeScale);

        // favor = (sum favorability over type-{4,5,6,7} persons, excl self/free) / n
        double favorAccum = 0.0;  // v40
        int favorN = 0;           // v19
        for (int other = 0; other < kStatPersonCount; ++other) {
            if (other != s && !env.PersonFreeSlot(other)) {
                u8 otype = env.PersonType(other);  // byte_12CE912[other*536]
                if (otype == 6 || otype == 7 || otype == 5 || otype == 4) {
                    favorAccum += env.Favorability(s, other);
                    ++favorN;
                }
            }
        }
        // v44[5] = v40 * flt_6267E8 / (double)v19  (division by zero if favorN==0,
        // exactly as the original — left intact).
        rec.favor = static_cast<float>(favorAccum * kStatFavorScale /
                                       static_cast<double>(favorN));

        // wealth = max(ComputeTotalWealth(slot), 1)
        i32 wealth = env.ComputeTotalWealth(s);
        if (wealth <= 1)
            wealth = 1;
        rec.wealth = wealth;

        // running max wealth (v37)
        if (wealth > maxWealth)
            maxWealth = wealth;
    }

    // ---- Pass 3: normalize, composite, bar height. ----------------------------
    // 0x58c19b..0x58c1ba: fild maxWealth; fmul flt_6267CC; fstp DWORD — v41 is
    // stored as a 4-byte float, and every bar-height multiply reloads that float.
    const float maxScale = static_cast<float>(
        static_cast<double>(maxWealth) * static_cast<double>(kStatMaxWealthScale));  // v41
    for (int i = 0; i < count; ++i) {
        OfficialCompareRecord& rec = out[i];
        // ratio = wealth / maxWealth (double divide; maxWealth >= 1 here).
        double ratio = static_cast<double>(rec.wealth) / static_cast<double>(maxWealth);
        rec.ratio = static_cast<float>(ratio);

        // composite = ratio*0.3 + religion*0.15 + office*0.15 + favor*0.15 + needs*0.25
        double composite = ratio * static_cast<double>(kStatWeightRatio) +
                           static_cast<double>(rec.religion) * static_cast<double>(kStatWeightMid) +
                           static_cast<double>(rec.office) * static_cast<double>(kStatWeightMid) +
                           static_cast<double>(rec.favor) * static_cast<double>(kStatWeightMid) +
                           static_cast<double>(rec.needs) * static_cast<double>(kStatWeightNeeds);
        rec.composite = static_cast<float>(composite);

        // barHeight = (int) ConvertX(composite * maxScale)
        // 0x58c21f: `fst dword ptr [edx+1Ch]` (not fstp) — the UNROUNDED
        // composite stays on the x87 stack and feeds the multiply at 0x58c222;
        // only the record field is rounded to float.
        double h = composite * static_cast<double>(maxScale);
        rec.barHeight = static_cast<int>(env.ConvertX(h));
    }

    return count;
}

// gilde.exe 0x55fa88 — slider color index clamp + palette lookup.
i32 CompareSliderColor(i32 growthCode) {
    int idx = growthCode - 1342;
    if (idx > 6)
        idx = 7;
    else if (idx <= 0)
        idx = 0;
    // (1 <= idx <= 6 falls through unchanged)
    return kCompareSliderPalette[static_cast<std::size_t>(idx)];
}

void StatPanel_SetCompareSink(StatPanelCompareSink* sink) { g_compareSink = sink; }

// gilde.exe 0x55fa88 — VIBE_StatPanel_ShowCompareChart (UI panel; inert hook).
int StatPanelShowCompareChart(StatChartEnv& env) {
    std::array<OfficialCompareRecord, kStatMaxOfficials> rows{};
    int count = StatChartBuildOfficialComparison(rows, env);
    if (count == 0)
        return 0;
    if (!g_compareSink || !g_compareSink->CanOpen())
        return 0;
    g_compareSink->EmitRows(rows, count);
    return count;
}

}  // namespace guild::world
