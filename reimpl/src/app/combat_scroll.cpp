// gilde.exe — combat-mode edge-scroll decision core (guild::app).
// See combat_scroll.h. 1:1 reconstruction of the decision half of
// VIBE_Camera_UpdateCombatScroll @0x487b2c.
#include "app/combat_scroll.h"

#include "util/math.h"

namespace guild::app {

namespace {

// The original's `flt_5CA2E0` is the zero reference vector {0,0,0} — the
// "settled / no motion" state every edge vector is compared against.
const float kZeroVec[3] = {0.0f, 0.0f, 0.0f};

// !VIBE_Math_VectorWithinTolerance(vec, &flt_5CA2E0, 0.2) — true when the edge
// motion vector is OUTSIDE the settle tolerance (i.e. the unit is actively
// pushing this edge). A null vector counts as settled (within tolerance).
inline bool MotionActive(const float* vec) {
    if (!vec)
        return false; // settled
    // util::VectorWithinTolerance takes non-const float* (matches the __userpurge
    // float* args); we only read, so the const-cast is safe.
    return !util::VectorWithinTolerance(const_cast<float*>(vec),
                                        const_cast<float*>(kZeroVec),
                                        kCombatScrollSettleTol);
}

} // namespace

// gilde.exe 0x487b2c — VIBE_Camera_UpdateCombatScroll (decision portion).
CombatScrollDecision ResolveCombatScroll(int scrollX, int scrollY,
                                         const float* topVec, const float* bottomVec,
                                         const float* leftVec, const float* rightVec) {
    // v1 = right, v17 = left, v18 = top, v2 = bottom — all start 0.
    int v1 = 0, v17 = 0, v18 = 0, v2 = 0;

    // switch ( dword_6316CC )  — horizontal scroll direction.
    switch (scrollX) {
    case -1:
        // v1 = 1; if ( !within(+252) ) v1 = 2;
        v1 = 1;
        if (MotionActive(rightVec))
            v1 = 2;
        break;
    case 1:
        // v17 = 1; if ( !within(+228) ) v17 = 2;
        v17 = 1;
        if (MotionActive(leftVec))
            v17 = 2;
        break;
    case 0:
        // v17 = !within(+228); v1 = !within(+252);
        v17 = MotionActive(leftVec) ? 1 : 0;
        v1 = MotionActive(rightVec) ? 1 : 0;
        break;
    default:
        break; // original switch has no default -> both stay 0
    }

    // switch ( dword_6316D0 )  — vertical scroll direction.
    switch (scrollY) {
    case -1:
        // v18 = 1; if ( !within(+180) ) v18 = <count>+1;  (the count base is 1 here)
        v18 = 1;
        if (MotionActive(topVec))
            v18 = 2;
        break;
    case 1:
        // v2 = 1; if ( !within(+204) ) v2 = 2;
        v2 = 1;
        if (MotionActive(bottomVec))
            v2 = 2;
        break;
    case 0:
        // if ( !within(+180) ) v18 = 1; if ( !within(+204) ) v2 = 1;
        if (MotionActive(topVec))
            v18 = 1;
        if (MotionActive(bottomVec))
            v2 = 1;
        break;
    default:
        break;
    }

    CombatScrollDecision out;
    out.right  = static_cast<CombatScrollCode>(v1);
    out.left   = static_cast<CombatScrollCode>(v17);
    out.top    = static_cast<CombatScrollCode>(v18);
    out.bottom = static_cast<CombatScrollCode>(v2);
    return out;
}

} // namespace guild::app
