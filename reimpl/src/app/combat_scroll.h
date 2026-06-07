#pragma once
// gilde.exe — combat-mode edge-scroll decision core (guild::app).
//
// Faithful 1:1 reconstruction of the portable DECISION half of:
//   0x487b2c  VIBE_Camera_UpdateCombatScroll
//
// In combat mode the camera scrolls when the active unit's motion vector pushes it
// toward a screen edge. The function reads two scroll-DIRECTION inputs
// (dword_6316CC = horizontal, dword_6316D0 = vertical; each -1/0/+1) and four
// EDGE-MOTION vectors out of the camera scroll-state block (dword_13FCD1C + 180,
// +204, +228, +252 — three floats each). For each edge it tests whether the
// motion vector is still within 0.2 of the zero vector (flt_5CA2E0 = {0,0,0}, the
// "settled / no scroll" state) via VIBE_Math_VectorWithinTolerance. The result is
// a per-edge SCROLL CODE in {0,1,2}:
//   0 = no scroll arrow for this edge
//   1 = scroll this edge, motion settled  (the "vector within tolerance" arrow)
//   2 = scroll this edge, motion active   (the "vector outside tolerance" arrow)
// The codes then select which scroll-arrow VIBE_Animation_Basic blits + how the
// camera pans (the blit/pan are render leaves, out of scope here — see the .cpp).
//
// This module reconstructs ONLY the code-resolution arithmetic: the part that
// turns (direction input, edge-vector) into the four scroll codes. It depends on
// the already-reconstructed util::VectorWithinTolerance (0x5caa4c) and nothing
// else, so it is exercisable in isolation.
#include "guild/common/types.h"

namespace guild::app {

// The settle tolerance the original passes (gilde.exe immediate 0.2).
constexpr float kCombatScrollSettleTol = 0.2f;

// Per-edge scroll codes (the v1/v2/v17/v18 locals of the original).
enum class CombatScrollCode : int {
    kNone     = 0, // no scroll arrow for this edge
    kSettled  = 1, // scroll, motion within tolerance of {0,0,0}
    kActive   = 2, // scroll, motion outside tolerance
};

// The four resolved per-edge scroll codes.
struct CombatScrollDecision {
    CombatScrollCode right;  // v1   (edge vector +252 / dword_6316CC)
    CombatScrollCode left;   // v17  (edge vector +228 / dword_6316CC)
    CombatScrollCode top;    // v18  (edge vector +180 / dword_6316D0)
    CombatScrollCode bottom; // v2   (edge vector +204 / dword_6316D0)
};

// gilde.exe 0x487b2c — VIBE_Camera_UpdateCombatScroll (decision portion).
//   The camera scroll-state block holds the four edge MOTION vectors (3 floats
//   each): `topVec` (+180), `bottomVec` (+204), `leftVec` (+228), `rightVec`
//   (+252). `scrollX` is dword_6316CC, `scrollY` is dword_6316D0 (each -1/0/+1).
//
//   horizontal (dword_6316CC):
//     case -1:  right = settled(rightVec) ? 1 : 2
//     case  1:  left  = settled(leftVec)  ? 1 : 2
//     case  0:  left  = settled(leftVec)  ? 0 : 1   (note: !within -> 1, not 2)
//               right = settled(rightVec) ? 0 : 1
//   vertical (dword_6316D0): symmetric over top (+180) / bottom (+204):
//     case -1:  top    = settled(topVec)    ? 1 : 2
//     case  1:  bottom = settled(bottomVec) ? 1 : 2
//     case  0:  top    = settled(topVec)    ? 0 : 1
//               bottom = settled(bottomVec) ? 0 : 1
//   (Any direction value other than -1/0/1 leaves both that-axis codes 0, matching
//   the original's switch with no default.)
//
// Vectors are `const float*` to a 3-float [x,y,z] motion vector. Passing nullptr
// for an unused edge is allowed; that edge's "settled" test counts as settled (the
// original always reads live vectors, so callers pass real ones).
CombatScrollDecision ResolveCombatScroll(int scrollX, int scrollY,
                                         const float* topVec, const float* bottomVec,
                                         const float* leftVec, const float* rightVec);

} // namespace guild::app
