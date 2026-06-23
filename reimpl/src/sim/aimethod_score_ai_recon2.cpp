// ===========================================================================
// AiMethod scoring / selection kernels — implementation.
// 1:1 translations of the pure decision math embedded in the gilde.exe method
// scorers. See header for addresses, prototypes, and coupled-leaf list.
// Constants verified byte-exact via IDA MCP get_bytes.
// ===========================================================================
#include "aimethod_score_ai_recon2.h"

#include <cstring>

namespace guild::sim {

bool AttackFavorabilityAccept(int roll0to29, float favorability) {
    // 0x4671c8: v25 = (float)RandomModulo(30); accept iff v25 + 40.0 > fav.
    float roll = static_cast<float>(roll0to29);
    return (static_cast<double>(roll) + static_cast<double>(kAttackFavBias)) >
           static_cast<double>(favorability);
}

int WealthScoreFloored(int totalWealth) {
    // 0x4680e0: v16 = (double)wealth * 0.015 ; trunc ; if (v22 <= 800) v22 = 800.
    int score = CoordTrunc2(static_cast<double>(totalWealth) *
                            static_cast<double>(kWealthScoreScale));
    if (score <= kWealthScoreFloor)
        score = kWealthScoreFloor;
    return score;
}

bool MethodEmitWeight(int slotByte, float rawWeight, float* out) {
    // 0x469a18: if (byte_B57240[..] >= 0) { v = -raw * 0.5; AppendAiMethodEntry(.,v); }
    if (slotByte < 0)
        return false;
    float v = static_cast<float>(-static_cast<double>(rawWeight) *
                                 static_cast<double>(kMethodEmitScale));
    if (out)
        *out = v;
    return true;
}

float MoveScoreContribution(int slotByte, float entryWeight, float personFloat36,
                            bool sameFaction) {
    // 0x46ac24: if (slot>=0 && entryWeight > 0.0) {
    //   scale = sameFaction ? 0.02 : 0.01 ;
    //   contrib = entryWeight * personFloat36 * scale ; *score += contrib; }
    if (slotByte < 0 || !(entryWeight > 0.0f))
        return 0.0f;
    float scale = sameFaction ? kMoveSameFaction : kMoveOtherFaction;
    return static_cast<float>(static_cast<double>(entryWeight) *
                              static_cast<double>(personFloat36) *
                              static_cast<double>(scale));
}

int PickMostDislikedRelation(const std::array<float, 3>& fav) {
    // 0x46ac24: v42 = 100.0 ; v27 = -1 ; do { if (fav[i] < v42) { v27=i; v42=fav[i]; } } while(i<3);
    float best = kMoveRelationCap;
    int idx = -1;
    for (int i = 0; i < 3; ++i) {
        if (static_cast<double>(fav[static_cast<size_t>(i)]) < static_cast<double>(best)) {
            idx = i;
            best = fav[static_cast<size_t>(i)];
        }
    }
    return idx;
}

MethodChoice SelectBestAiMethod(const std::vector<MethodCandidate>& candidates) {
    // 0x469248: v24 = v23 = -1.0e30 ; A-winner / B-winner tracked separately.
    const float kNeg = -1.0e30f;
    float bestA = kNeg, bestB = kNeg;
    int winA = -1, winB = -1;
    float winAa = 0.0f, winAb = 0.0f, winBa = 0.0f, winBb = 0.0f;
    for (const auto& c : candidates) {
        // gate: candidate only considered if scoreA >= bestA OR scoreB >= bestB.
        if (!(c.scoreA >= bestA || c.scoreB >= bestB))
            continue;
        if (c.scoreA >= bestA) {
            bestA = c.scoreA;
            winA  = c.id;
            winAa = c.scoreA;
            winAb = c.scoreB;
        }
        if (c.scoreB >= bestB) {
            bestB = c.scoreB;
            winB  = c.id;
            winBa = c.scoreA;
            winBb = c.scoreB;
        }
    }
    MethodChoice choice;
    // (bits(bestA) & 0x7FFFFFFF) != 0  &&  bestA >= bestB  → choose A winner.
    std::uint32_t bits;
    std::memcpy(&bits, &bestA, sizeof(bits));
    if ((bits & 0x7FFFFFFFu) != 0 && bestA >= bestB) {
        choice.id     = winA;
        choice.choseA = true;
        choice.scoreA = winAa;
        choice.scoreB = winAb;
    } else {
        choice.id     = winB;
        choice.choseA = false;
        choice.scoreA = winBa;
        choice.scoreB = winBb;
    }
    return choice;
}

} // namespace guild::sim
