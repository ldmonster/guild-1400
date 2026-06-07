#include "world/office_prosperity.h"

// Faithful 1:1 port of the prosperity math core of VIBE_Amt_UpdateOfficeProsperity
// (gilde.exe 0x57b718). The building/person walk and the network commit are
// routed through a hook; the arithmetic below preserves the original's float /
// double mixing exactly (so the truncated/blended result is bit-faithful).

namespace guild::world {

namespace {
ProsperityCommitHook g_commitHook = nullptr;
void*                g_commitCtx  = nullptr;
} // namespace

void ProsperitySetCommitHook(ProsperityCommitHook hook, void* ctx) {
    g_commitHook = hook;
    g_commitCtx  = ctx;
}

// gilde.exe 0x57b718 — the v25 average:
//   v25 = 0; v20 = 0;
//   for i in 0..2: v21 = room[i]; v25 += v21; v20 += (v21 != 0);
//   v25 = (v15 + v25) / (v20 + 1);
i32 ProsperityAverageWealth(const ProsperityInput& in) {
    i32 roomSum = 0;
    int nonzero = 0;
    for (int i = 0; i < 3; ++i) {
        i32 v = in.room[i];
        roomSum += v;
        nonzero += (v != 0) ? 1 : 0;
    }
    return (in.ownerWealth + roomSum) / (nonzero + 1);
}

// gilde.exe 0x57b718 — the prosperity score:
//   v19 = (float)v25; v22 = (float)v15; v12 = v19 / v22;
//   v13 = (v12 >= 1.0) ? 1.0 : v12;
//   score = v13 * 0.5 + 0.5 * (1.0 - (double)v15 / (double)v16);
// The ratio is computed in float (v19/v22) then widened to double for the blend.
float ProsperityCompute(const ProsperityInput& in) {
    i32 avg = ProsperityAverageWealth(in);

    float fAvg    = static_cast<float>(avg);   // v19
    float fWealth = static_cast<float>(in.ownerWealth); // v22
    float ratioF  = fAvg / fWealth;            // v12 (single-precision divide)

    double ratio;
    if (static_cast<double>(ratioF) >= 1.0) {
        ratio = 1.0;
    } else {
        // The original keeps the float ratio (the COERCE_DOUBLE of the float
        // bits); widening the float value reproduces it exactly.
        ratio = static_cast<double>(ratioF);
    }

    double cityTerm = 1.0 - static_cast<double>(in.ownerWealth)
                          / static_cast<double>(in.cityMax);
    double score = ratio * kProsperityBlend + kProsperityBlend * cityTerm;
    return static_cast<float>(score);
}

ProsperityResult ProsperityUpdateBuilding(const ProsperityInput& in,
                                          i32 objectId,
                                          float currentField480,
                                          float aiField180) {
    ProsperityResult r;
    r.averageWealth = ProsperityAverageWealth(in);
    r.score = ProsperityCompute(in);

    // (3) prosperity delta: *(float*)&v11 = -(*(float*)(v23+480) - *(float*)&v17)
    r.prosperityDelta = -(currentField480 - r.score);

    // (4) AI decay delta: tmp = field180 * 0.95; delta = -(field180 - tmp)
    float aiTmp = static_cast<float>(static_cast<double>(aiField180)
                                     * kProsperityAiDecay);
    r.aiDecayDelta = -(aiField180 - aiTmp);

    if (g_commitHook) {
        // (2) write the averaged wealth at +476.
        g_commitHook(ProsperityCommit::WealthField, objectId,
                     static_cast<float>(r.averageWealth), g_commitCtx);
        // (3) prosperity delta (QueueRequestArgs26 obj,480,delta).
        g_commitHook(ProsperityCommit::ProsperityDelta, objectId,
                     r.prosperityDelta, g_commitCtx);
        // (4) AI method 3 decay delta.
        g_commitHook(ProsperityCommit::AiDecayDelta, objectId,
                     r.aiDecayDelta, g_commitCtx);
    }
    return r;
}

} // namespace guild::world
