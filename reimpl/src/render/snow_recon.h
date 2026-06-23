#pragma once
// Snow scene-update reconstruction for Die Gilde (gilde.exe).
//
//   gilde.exe 0x42a2cc VIBE_Snow_UpdateScene
//   gilde.exe 0x42b1fc VIBE_Snow_ApplyToVisibleObjects
//   gilde.exe 0x42b3fc VIBE_Snow_AccumulateOnObjects
//
// These three functions are dominated by global-object-array iteration coupled
// to the texture cache, memory allocator and VIBE_Snow_AccumulateOnTexture
// (handled elsewhere / in snow.cpp).  The genuinely pure, faithfully
// reconstructable kernels they contain are the per-frame snow "coverage budget"
// arithmetic and the batched-processing pacing.  Those are reconstructed here
// 1:1; the surrounding iteration is documented but routed through inert hooks so
// no fake object-array logic is invented (rule 8).

#include "guild/common/types.h"

namespace guild {
namespace render {

// gilde.exe 0x42b3fc tail / 0x42b1fc tail — snow coverage threshold.
// Given the (already float-clamped) coverage value `coverage` (a float in
// [0,255]), the engine truncates it toward zero, then computes the level at
// which an object is considered "covered" this pass:
//     v = trunc(coverage)
//     budget = 4*v / 5          (unsigned integer division; divisor is 5)
//     if (v - 16) < budget:  budget = v - 16     (unsigned compare)
//     return budget
// `out_trunc` (optional) receives the truncated coverage `v`.
u32 SnowCoverageThreshold(float coverage, u32* out_trunc);

// gilde.exe 0x42b3fc — accumulator step (VIBE_Snow_AccumulateOnObjects head):
//     acc' = rate * dt + acc        (rate = *(int*)a1 read as float? no: (double)*(int*)a1)
// The original reads *(int*)a1 as an integer rate and converts to double:
//     v3 = (double)*(int*)a1 * dt + *(float*)(a1+84)
// Returns the new accumulator value (also the basis for the *0.0004 coverage).
float SnowAccumulatorStep(i32 rate, float dt, float acc);

// gilde.exe 0x42b3fc — coverage from accumulator (AccumulateOnObjects):
//     coverage = clamp(acc * 0.00039999998989515007, 255.0)   (flt_6118A4, float 255)
float SnowCoverageFromAccumulator(float acc);

// gilde.exe 0x42b1fc — coverage from object timer (ApplyToVisibleObjects):
//     coverage = min(timer * 0.002, 255.0)    (dbl_611894 = 0.002, dbl_61189C = 255.0)
float SnowCoverageFromTimer(float timer);

// gilde.exe 0x42b1fc / 0x42b3fc — batched-processing pacing.
// `total` = candidate count ([a1+20]/[a1+44]), `per` = per-frame quota
// ([a1+16]), `done_cap` = running done counter ([a1+24]/[a1+48]):
//     if (total - per) >= done_cap:  limit = total/per + done_cap
//     else                        :  limit = total
// All values are signed ints in the original (signed division).
i32 SnowBatchLimit(i32 total, i32 per, i32 done_cap);

} // namespace render
} // namespace guild
