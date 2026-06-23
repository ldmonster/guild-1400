#include "world/location_church.h"

namespace guild::world {

// gilde.exe 0x521674 — VIBE_Location_ChurchDonationDialog cost+reputation core.
//   if (ComputeTotalWealth(player) <= 1) wA = 1; else wA = ComputeTotalWealth(player);
//   if (ComputeTotalWealth(empl)   <= 1) wB = 1; else wB = ComputeTotalWealth(empl);
//   combined = wA + wB;
//   suggested = (int)((double)combined * flt_62240C);
//   ...on confirm with `paid`:
//   reputDelta = (int)((double)(255 * paid) / (double)combined);
//   spread     = reputDelta * flt_622410;
DonationResult ChurchComputeDonation(int playerWealth, int employerWealth, int paid) {
    DonationResult r{};
    r.wealthPlayer   = (playerWealth   <= 1) ? 1 : playerWealth;
    r.wealthEmployer = (employerWealth <= 1) ? 1 : employerWealth;
    r.wealthCombined = r.wealthPlayer + r.wealthEmployer;
    // 0x521741: v6 = (double)combined * flt_62240C; ConvertX truncates -> (int)v6.
    r.suggestedCost  = (int)((double)r.wealthCombined * (double)church::kDonationCostFrac);
    // 0x5218fe..0x521945: v15 = (double)(255*paid) / (double)combined (x87 fdivrp).
    //   fst var_18 (FLOAT slot) at 0x521939 stores the quotient BEFORE the truncating
    //   fistp at 0x521945. reputSpread (v45) is built from that FLOAT, not the (int).
    float quotientF = 0.0f;
    if (r.wealthCombined != 0) {
        const double q = (double)(255 * paid) / (double)r.wealthCombined;
        quotientF    = (float)q;          // fst var_18 (32-bit float)
        r.reputDelta = (int)q;            // fistp var_14 (ConvertX truncates toward 0)
    } else {
        r.reputDelta = 0;
    }
    // 0x521958/0x52195f: v45 = (float)quotient; v45 = v45 * flt_622410.  The per-peer
    // spread uses the UN-truncated float quotient (var_18), NOT the (int) reputDelta.
    r.reputSpread = quotientF * church::kDonationReputFactor;
    return r;
}

// gilde.exe 0x521bac — VIBE_Location_ChurchIndulgenceDialog cost core.
//   if (!hasCrime) cost stays 0 (no indulgence offered).
//   v37 = (double)wealth * flt_622430;            // base
//   v34 = 1.5;
//   if (npc != player)
//       v34 = (flt_622434 - Favorability(player,npc,1)) * flt_622438;
//   v32 = (int)(v37 * v34);                        // rawCost
//   v28 = (double)v32 * dbl_622440;                // /10
//   if (v28 > 3200.0 && v28 >= 320000.0) cost = 320000;
//   else { v29 = v32 * 0.1; cost = (v29 <= 3200.0) ? 3200 : v29; }
IndulgenceResult ChurchComputeIndulgence(int crimeHandlerSum, int playerWealth,
                                         bool npcIsPlayer, int favorability) {
    IndulgenceResult r{};
    r.offered = (crimeHandlerSum != 0);
    if (!r.offered) {
        r.factor = 0.0f;
        r.rawCost = 0;
        r.cost = 0;
        return r;
    }
    // 0x521d35: base = (float)((double)wealth * flt_622430), stored to FLOAT var_20.
    double base = (double)playerWealth * (double)church::kIndulgenceCostFrac;
    base = (double)(float)base;            // fstp var_20 narrows to 32-bit float
    float factor = church::kIndulgenceDefFactor;   // mov var_2C, 3FC00000h (1.5f)
    if (!npcIsPlayer) {
        factor = (church::kIndulgenceFavBase - (float)favorability) *
                 church::kIndulgenceFavScale;       // fstp var_2C (float)
    }
    // 0x521d8b..0x521d99: factor *= (double)crimeHandlerSum (ecx == SumPlayerHandlerValues,
    // preserved across ComputeTotalWealth/Favorability), re-stored to FLOAT var_2C.
    float factorEff = (float)((double)crimeHandlerSum * (double)factor);
    r.factor = factor;
    // 0x521da0..0x521db3: raw = (int)trunc(base * factorEff)  (ConvertX truncates).
    int raw = (int)(base * (double)factorEff);
    r.rawCost = raw;

    double t = (double)raw * church::kIndulgenceTenthDiv;
    int cost;
    if (t > church::kIndulgenceMinClamp && t >= church::kIndulgenceMaxClamp) {
        cost = (int)church::kIndulgenceMaxClamp;
    } else {
        double v29 = (double)raw * church::kIndulgenceTenthDiv;
        double clamped = (v29 <= church::kIndulgenceMinClamp)
                             ? church::kIndulgenceMinClamp
                             : v29;
        cost = (int)clamped;
    }
    r.cost = cost;
    return r;
}

// gilde.exe 0x522ae8 — VIBE_Location_ChurchConfessionDialog eligibility core.
//   if (person+39 == 0xFFFF) return;                 // unemployed NPC -> ineligible
//   DataPtr = Straftat_CountActiveByTarget(player);  // active crimes shown
//   confess button offered only when DataPtr != 0.
//   on confess: He_RequestRivalEntityPairs(..., paid/2, ...)  -> priest standing +
ConfessionResult ChurchComputeConfession(u16 npcEmploymentWord, int activeCrimes,
                                         int paid) {
    ConfessionResult r{};
    r.eligible     = (npcEmploymentWord != 0xFFFF);
    r.activeCrimes = activeCrimes;
    r.canConfess   = r.eligible && activeCrimes > 0;
    r.reputGain    = r.canConfess ? (paid / 2) : 0;
    return r;
}

} // namespace guild::world
