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
    r.suggestedCost  = (int)((double)r.wealthCombined * (double)church::kDonationCostFrac);
    if (r.wealthCombined != 0) {
        // v48 = 255 * v15; v16 = (double)(255*v15) / (double)v44;  v47 = (int)v16
        r.reputDelta = (int)((double)(255 * paid) / (double)r.wealthCombined);
    } else {
        r.reputDelta = 0;
    }
    // v46 = v46 * flt_622410  (the per-peer spread the loop queues to each rival)
    r.reputSpread = (float)r.reputDelta * church::kDonationReputFactor;
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
IndulgenceResult ChurchComputeIndulgence(bool hasCrime, int playerWealth,
                                         bool npcIsPlayer, int favorability) {
    IndulgenceResult r{};
    r.offered = hasCrime;
    if (!hasCrime) {
        r.factor = 0.0f;
        r.rawCost = 0;
        r.cost = 0;
        return r;
    }
    double base = (double)playerWealth * (double)church::kIndulgenceCostFrac;
    float factor = church::kIndulgenceDefFactor;
    if (!npcIsPlayer) {
        factor = (church::kIndulgenceFavBase - (float)favorability) *
                 church::kIndulgenceFavScale;
    }
    r.factor = factor;
    int raw = (int)(base * (double)factor);
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
