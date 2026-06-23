#include "sim/aiaction_recon.h"

#include <cmath>

namespace guild::sim {

// ---------------------------------------------------------------------------
// VIBE_AiAction_EvalGuildhallTarget @0x475700
// ---------------------------------------------------------------------------
// 0x47578a:  if ( relTop >= (u16)rand(16) + 50 ) -> weighted (loyal) path
//            else -> own path, *a1 *= 3.0; *a2 = 3.0 * *a2;
GuildhallGate EvalGuildhallGate(i32 relationTop, u16 rand16) {
    GuildhallGate g;
    // threshold = (u16)rand16 + 50. rand16 is already in [0,15] (rand % 16).
    i32 threshold = static_cast<i32>(rand16) + 50;
    g.loyalBranch = (relationTop >= threshold);
    g.ownScale = g.loyalBranch ? 1.0f : kGuildhallOwnRelationScale;
    return g;
}

// ---------------------------------------------------------------------------
// VIBE_AiAction_PlanGuildhallUpgrade @0x4757e8
// ---------------------------------------------------------------------------
// 0x4758d4: if (level >= 33) { 0x4759ad: if (level >= 66) 10 else 30 } else 50.
// IMPORTANT: the level byte is read as a SIGNED char (`mov ah,[esi+5Ch]`) and the
// two comparisons are signed (`cmp ah,21h; jge` @0x4758d4 / `cmp ah,42h; jge`
// @0x4759ad). So a high-bit-set byte (128..255 == -128..-1) takes the `< 33`
// path and returns 50, NOT 10. Take/compare as i8 to be 1:1.
u8 GuildhallUpgradeTierWeight(i8 upgradeLevel) {
    if (upgradeLevel >= 33) {
        if (upgradeLevel >= 66)
            return 10;
        return 30;
    }
    return 50;
}

// 0x4758b4: if ( relTop < (u16)rand(16)+50 && (flag90 & 4) == 0 ) -> enabled.
bool GuildhallUpgradeEnabled(i32 relationTop, u16 rand16, u8 flagByte90) {
    i32 threshold = static_cast<i32>(rand16) + 50;
    return (relationTop < threshold) && ((flagByte90 & 4) == 0);
}

// 0x475b92: (double)buildWorth * 0.4 -> truncated to int.
i32 UpgradeCostFromBuildWorth(i32 buildWorth) {
    return static_cast<i32>(static_cast<double>(buildWorth) *
                            static_cast<double>(kUpgradeCostBuildScale));
}
// 0x475c26 / 0x475980 use (cash-80000) scaled. Caller guarantees cash>=80000.
i32 UpgradeCostCashScaleA(i32 currency) {
    return static_cast<i32>(static_cast<double>(currency - 80000) *
                            static_cast<double>(kUpgradeCostCashScaleA));
}
i32 UpgradeCostCashScaleB(i32 currency) {
    return static_cast<i32>(static_cast<double>(currency - 80000) *
                            static_cast<double>(kUpgradeCostCashScaleB));
}

// ---------------------------------------------------------------------------
// VIBE_AiAction_EvalNeedFulfillTarget @0x4761a0
// ---------------------------------------------------------------------------
// 0x47624b: capf = (float)(i16)cap; if (capf <= 1.0f) capf = 1.0f;
// 0x476275: ratio = (double)(i16)stat / capf;
double NeedRatio(i16 stat, i16 cap) {
    float capf = static_cast<float>(cap);
    // The original compares the *bit pattern* against 0x3f800000 (== 1.0f);
    // for the non-negative caps here this is exactly capf <= 1.0f.
    if (!(capf > 1.0f))
        capf = 1.0f;
    return static_cast<double>(stat) / static_cast<double>(capf);
}

// 0x4762e2: if (rand(2)) min-mode else max-mode. Reproduced *literally* from
// the decompile (the index is the boolean value of the comparison, not a
// "pick the smaller/larger" — the original code is exactly):
//   min (0x476300): i = (s0 <= s1);  if (s[i] <= s2) i = 2
//   max (0x4763dd): i = (s0 >= s1);  if (s[i] >= s2) i = 2
// where the comparison yields 0 or 1 used directly as the slot index.
int NeedPickSlot(float s0, float s1, float s2, u16 rand2) {
    float s[3] = {s0, s1, s2};
    int i;
    if (rand2) {
        i = (static_cast<double>(s0) <= static_cast<double>(s1)) ? 1 : 0;
        if (static_cast<double>(s[i]) <= static_cast<double>(s2))
            i = 2;
    } else {
        i = (static_cast<double>(s0) >= static_cast<double>(s1)) ? 1 : 0;
        if (static_cast<double>(s[i]) >= static_cast<double>(s2))
            i = 2;
    }
    return i;
}

// 0x47632e: v18 = 10 * amount.
i32 NeedFulfillCost(i32 slotAmount) { return 10 * slotAmount; }

// ---------------------------------------------------------------------------
// VIBE_AiAction_PlanPersonInteraction @0x475f48
// ---------------------------------------------------------------------------
// 0x47604a: (double)wealth * 0.015 -> (int).  0x476077: currency < 6 * cost.
i32 PersonInteractionUnitCost(i32 targetWealth) {
    return static_cast<i32>(static_cast<double>(targetWealth) *
                            static_cast<double>(kInteractionWealthScale));
}
i32 PersonInteractionTotalCost(i32 unitCost) { return 6 * unitCost; }

// 0x4760a5..0x476119: idx = rand(13); while (!idx || occupied[idx]) {
//     idx = (idx+1) % 13; if (!--guard) -> fail }  (guard starts at 13)
int PersonInteractionFamilyNeed(int startIdx, const u8 occupied[13]) {
    int idx = startIdx;
    int guard = 13;
    while (idx == 0 || occupied[idx]) {
        idx = (idx + 1) % 13;
        if (!--guard)
            return -1;  // LABEL_29 fall-through: no free family need.
    }
    return idx;
}

// ---------------------------------------------------------------------------
// VIBE_AiAction_EvalConversationTarget @0x47b468
// ---------------------------------------------------------------------------
// 0x47b650: favor < 33.0 (flt_61AC28) && slotState == 1.
bool ConversationPrimaryAccept(float favorability, u8 slotState) {
    return (favorability < kConversationFavorGateA) && (slotState == 1);
}
// 0x47b7b8: (rand(32) + 40.0) > favor (flt_61AC2C) && slotState == 1.
bool ConversationSecondaryAccept(float favorability, u16 rand32, u8 slotState) {
    float r = static_cast<float>(static_cast<i32>(rand32));
    return ((r + kConversationFavorGateB) > favorability) && (slotState == 1);
}

// ---------------------------------------------------------------------------
// VIBE_AiAction_EvalSleepSpot @0x47b7d4
// ---------------------------------------------------------------------------
// 0x47b81f: if ( !pred() || value == field27 || value < low || value > high )
//               return 0; else emit.
bool SleepSpotValid(bool predicate, i32 value, i32 fieldCurrent,
                    i32 lowBound, i32 highBound) {
    if (!predicate || value == fieldCurrent || value < lowBound ||
        value > highBound)
        return false;
    return true;
}

// ---------------------------------------------------------------------------
// VIBE_AiAction_DispatchTargetSearch @0x47c430 / _DispatchSecondarySearch
// ---------------------------------------------------------------------------
// 0x47c501: if (countA && countB) { if (!rand(2)) use A else use B }
// 0x47c510: else if (countA) use A
// 0x47c603: else if (countB) use B   else none.
int DispatchPickSet(int countA, int countB, u16 rand2) {
    if (countA && countB)
        return rand2 ? 1 : 0;   // 0x47c658: rand2==0 -> set A (LABEL_15)
    if (countA)
        return 0;
    if (countB)
        return 1;
    return -1;
}

// 0x47c52b: rand(count) — uniform index in [0,count).
int DispatchPickIndex(int count, u16 randN) {
    if (count <= 0)
        return -1;
    return static_cast<int>(randN);
}

// ---------------------------------------------------------------------------
// VIBE_AiNeeds_EvaluateActions @0x47852c (final selection tail)
// ---------------------------------------------------------------------------
const i32 kSplitThresholds[4] = {-1, 1, 3, 5};   // dword_47848C
const i32 kSplitDivisors[4]   = { 1, 3, 5, 7};   // dword_478490

// The threshold walk (0x478c42..0x478c57): v45 descends while
//   dword_62EB9C(=n) > dword_47848C[v45].  The original seeds v45/v44 from
//   dword_4784BC; within the recovered 4-entry table the surviving count is the
//   number of thresholds strictly below n, clamped to [1,4].
int ActionSplitSurvivors(int n, int tableLen) {
    int len = tableLen;
    if (len > 4) len = 4;       // clamp to the recovered table extent.
    if (len < 1) len = 1;
    int v45 = len - 1;
    int survivors = len;
    while (v45 > 0) {
        if (n > kSplitThresholds[v45])
            break;
        --v45;
        --survivors;
    }
    if (survivors < 1) survivors = 1;
    return survivors;
}

// 0x478c2c: if (n <= 3) single split (divisor implicitly 1, no jitter).
// else: groupIndex = 3 * rand(n); divisor = divisors[rand(survivors)];
//       if (divisor > 1 && n % divisor == 0) jitter = rand(2) ? +1 : -1.
ActionSplit SelectActionSplit(int n, u16 randGroup, u16 randDiv,
                              u16 randJitter) {
    ActionSplit out{};
    if (n <= 3) {
        out.divisor = 1;
        out.groupIndex = 0;
        out.jitter = 0;
        return out;
    }
    int survivors = ActionSplitSurvivors(n, /*tableLen=*/4);
    int di = static_cast<int>(randDiv);
    if (di < 0) di = 0;
    if (di >= survivors) di = survivors - 1;
    out.divisor = kSplitDivisors[di];
    out.groupIndex = 3 * static_cast<int>(randGroup);
    out.jitter = 0;
    if (out.divisor > 1 && (n % out.divisor) == 0)
        out.jitter = randJitter ? 1 : -1;   // 0x47931a / 0x479343
    return out;
}

// ---------------------------------------------------------------------------
// VIBE_AiTarget_FindNearestPerson @0x479dd8
// ---------------------------------------------------------------------------
// 0x479f0e: noise = RandomFloatScaled()*0.5 + 0.75;
// 0x479f2d: dist  = sqrt(dx*dx + dy*dy + dz*dz) * noise.
double NoisyDistance(float dx, float dy, float dz, float randFloatScaled) {
    float noise = randFloatScaled * kNearestNoiseScale + kNearestNoiseBase;
    double mag = std::sqrt(static_cast<double>(dx) * dx +
                           static_cast<double>(dy) * dy +
                           static_cast<double>(dz) * dz);
    return mag * static_cast<double>(noise);
}

// ---------------------------------------------------------------------------
// VIBE_AiPlayer_TryRangedAttack @0x47d364
// ---------------------------------------------------------------------------
bool RangedAttackDepthOk(i32 actionDepth) { return actionDepth >= 3; }

// 0x47d3de: v13 = (double)rand(16) + 33.0 (dbl_61AE48).
double RangedAttackAimRadius(u16 rand16) {
    return static_cast<double>(static_cast<i32>(rand16)) +
           kRangedAttackAccuracyBase;
}

// 0x47d49a: if (curve + rand >= 1.0) hit.
bool RangedAttackHitAccept(double curveRating, double randFloatScaled) {
    return (curveRating + randFloatScaled) >= 1.0;
}

} // namespace guild::sim
