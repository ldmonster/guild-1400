#include "world/statistics_report.h"

// Faithful port of the decision logic of VIBE_Statistics_BuildEconomyReport
// (gilde.exe 0x579ad0). The float reduction lives in world/statistics.cpp; here we
// recover the cadence gate, the per-category trend-id selection, the running argmin
// that finds the weakest sector, the weak-line broadcast gate + severity tiers, and
// the luxury summary branch. All comparisons are reproduced exactly (note the
// originals promote the float totals to double for the threshold tests, matching
// the FPU comparisons in the binary).

namespace guild::world {

EconomyReportInput EconomyReportFromAccum(const float* accum, float lawScore) {
    EconomyReportInput in;
    in.total0 = StatisticsCategoryTotal(accum, 0);   // v33 (goods)
    in.total1 = StatisticsCategoryTotal(accum, 1);   // v34 (services)
    in.total2 = StatisticsCategoryTotal(accum, 2);   // v35 (trade/production)
    in.luxury = StatisticsCategoryTotal(accum, 3);   // v26 (luxury)
    in.lawScore = lawScore;                          // v32
    return in;
}

// if ( (int)qword_13CE852 >= 8 && !((int)qword_13CE852 % 4) )
bool EconomyReportShouldRun(i32 round) {
    return round >= 8 && (round % 4) == 0;
}

EconomyReportTrends EconomyReportTrendIds(const EconomyReportInput& in) {
    EconomyReportTrends t;
    // v0 = (v35 > 0.15) ? 6178 : 6174;   (trade)
    t.trade    = (static_cast<double>(in.total2) > kReportTrendThreshold) ? 6178 : 6174;
    // v2 = (v33 > 0.15) ? 6179 : 6175;   (goods)
    t.goods    = (static_cast<double>(in.total0) > kReportTrendThreshold) ? 6179 : 6175;
    // v3 = (v32 < 0.0) ? 6180 : 6176;    (law, signed)
    t.law      = (in.lawScore < 0.0f) ? 6180 : 6176;
    // v5 = (v34 > 0.15) ? 6181 : 6177;   (services)
    t.services = (static_cast<double>(in.total1) > kReportTrendThreshold) ? 6181 : 6177;
    return t;
}

// gilde.exe 0x579c1a.. — running argmin over (v35, v33, v32, v34).
EconomyReportWeak EconomyReportWeakest(const EconomyReportInput& in) {
    float v35 = in.total2;   // trade
    float v33 = in.total0;   // goods
    float v32 = in.lawScore; // law
    float v34 = in.total1;   // services

    // v7 = (v35 >= v33) ? v33 : v35;   v30 = v29 = v7;
    float v7 = (v35 >= v33) ? v33 : v35;
    float v29 = v7;
    // v8 = (v32 <= v7) ? (v33 > v35) : 2;   v9 = v8;
    int v9 = (static_cast<double>(v32) <= static_cast<double>(v7))
                 ? (v33 > v35 ? 1 : 0)
                 : 2;
    // v10 = (v29 >= v32) ? v32 : v29;   v31 = v27 = v10;
    float v10 = (static_cast<double>(v29) >= static_cast<double>(v32)) ? v32 : v29;
    float v27 = v10;
    // v11 = (v34 <= v10) ? v9 : 3;   v12 = v11;
    int v12 = (static_cast<double>(v34) <= static_cast<double>(v10)) ? v9 : 3;
    // v13 = (v27 >= v34) ? v34 : v27;   v28 = v24 = v13;
    float v13 = (static_cast<double>(v27) >= static_cast<double>(v34)) ? v34 : v27;

    EconomyReportWeak w;
    w.index = v12;
    w.value = v13;
    return w;
}

// if ( v24 > dbl_62571C )   (0.20)
bool EconomyReportEmitsWeakLine(const EconomyReportWeak& weak) {
    return static_cast<double>(weak.value) > kReportWeakGate;
}

// if ( v24 >= 0.30 ) { if ( v24 >= 0.50 ) tier2 else tier1 } else tier0
EconomyReportSeverity EconomyReportWeakSeverity(float weakValue) {
    double v = static_cast<double>(weakValue);
    if (v >= kReportTier2Gate) {
        if (v >= kReportTier3Gate)
            return EconomyReportSeverity::kSevere;
        return EconomyReportSeverity::kModerate;
    }
    return EconomyReportSeverity::kMild;
}

// if ( v26 > 0.55 ) booming; else if ( v25 < 0.45 ) slump; else neutral.
EconomyReportLuxury EconomyReportLuxuryBranch(float luxury) {
    double v = static_cast<double>(luxury);
    if (v > kReportLuxuryHigh)
        return EconomyReportLuxury::kBooming;
    if (v < kReportLuxuryLow)
        return EconomyReportLuxury::kSlump;
    return EconomyReportLuxury::kNeutral;
}

} // namespace guild::world
