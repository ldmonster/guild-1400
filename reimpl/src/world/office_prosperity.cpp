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

// gilde.exe 0x57b718 — the prosperity score. DISASM-EXACT precision (0x57b85d-0x57b8d1):
//   57b865 fstp [var_40](float) = (float)v25            -> v19 rounded to f32
//   57b86d fstp [var_30](float) = (float)v15            -> v22 rounded to f32
//   57b871 fld v19 ; 57b875 fdiv v22 ; 57b879 fstp [var_64](DOUBLE) = v19/v22  -> v12 is a DOUBLE
//          (operands are f32, but the quotient is STORED at double precision, not f32).
//   57b87e fcomp v12(double) ; 57b884 jbe -> if (1.0 <= v12) v13=1.0 else v13=v12 (double copy).
//   57b89d fld v13(double) ; 57b8a9 fstp [var_48](FLOAT) = v13  -> ratio re-ROUNDED to f32 here,
//          and that float feeds the blend (57b8c5 fld [var_48]).
//   blend: round_f32( ratio_f32 * 0.5 + 0.5 * (1.0 - (double)v15 / (double)v16) ).
float ProsperityCompute(const ProsperityInput& in) {
    i32 avg = ProsperityAverageWealth(in);

    float fAvg    = static_cast<float>(avg);            // v19 (fild/fstp -> f32)
    float fWealth = static_cast<float>(in.ownerWealth); // v22 (fild/fstp -> f32)
    // The quotient of the two f32 operands is stored to a DOUBLE slot (var_64) — keep it
    // double, do NOT round to f32 here (that is what the source previously got wrong).
    // NOTE: binary divides the two f32 operands in the 80-bit x87 register then rounds the
    // quotient to double (fdiv mem32 ; fstp mem64). Portable double divide here may differ
    // only by a double-rounding ULP in pathological inputs (no 80-bit float in C++17).
    double ratioD = static_cast<double>(fAvg) / static_cast<double>(fWealth); // v12 (double)

    double v13 = (ratioD >= 1.0) ? 1.0 : ratioD;       // clamp on the double (fcomp/jbe)
    // var_48 (float) = v13 -> the clamped ratio is rounded to f32 before the blend.
    float ratioF32 = static_cast<float>(v13);          // *(float *)&v17 = v13

    double cityTerm = 1.0 - static_cast<double>(in.ownerWealth)
                          / static_cast<double>(in.cityMax);
    double score = static_cast<double>(ratioF32) * kProsperityBlend
                 + kProsperityBlend * cityTerm;
    return static_cast<float>(score);                  // fstp [var_48](float)
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
        // (2) write the field at +476. DISASM 0x57b840-0x57b84c: AppendDeltaField(4,1,&v15,
        // v23+476-base) passes &v15 = the ORIGINAL owner wealth (v15 is NOT reassigned to the
        // average; the average lives in v25/var_24 and is used only for the score). So +476
        // receives the integer ownerWealth, NOT averageWealth (prior source bug).
        g_commitHook(ProsperityCommit::WealthField, objectId,
                     static_cast<float>(in.ownerWealth), g_commitCtx);
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
