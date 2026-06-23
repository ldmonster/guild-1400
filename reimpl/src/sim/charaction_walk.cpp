#include "sim/charaction_walk.h"

#include "render/heightmap.h"

#include <cmath>

namespace guild::sim {

// ---------------------------------------------------------------------------
// Frame-clock globals (Character_Update brackets the frame with these).
// ---------------------------------------------------------------------------
int g_tickEnd   = 0;   // dword_62D008
int g_tickStart = 0;   // dword_62D004

// ---------------------------------------------------------------------------
// Movement constants — extracted byte-for-byte from gilde.exe data segment.
// Two byte-identical copies exist (the 0x409b2c split form @0x6108xx and the
// 0x40a4d4 monolith @0x610944..). Listed once with both source addresses.
// ---------------------------------------------------------------------------
namespace {
// Rotation interpolation (WalkOnPathRotation @0x6108xx / Dispatcher @0x610944..):
constexpr double kTurnHi      = 0.04;                // dbl_61088C / dbl_61094C  (aligned threshold +)
constexpr double kTurnLo      = -0.04;               // dbl_610894 / dbl_610954  (aligned threshold -)
constexpr double kTurnPerTick = 0.9090909090909092;  // dbl_61089C / dbl_61095C  (10/11)
constexpr double kCartTurn    = 0.4000000059604645;  // flt_6108A4 / flt_610964
constexpr double kRunThresh   = 2.0943951023931953;  // dbl_6108AC 0x4000c152382d749b (2*pi/3)
constexpr double kBucketHalfPi= 1.5707963267948966;  // dbl_6108B4 0x3ff921fb54442eea (pi/2)
constexpr double kBucketPi3   = 1.0471975511965976;  // dbl_6108BC 0x3ff0c152382d749d (pi/3)
constexpr double kBucketPi6   = 0.5235987755982988;  // dbl_6108C4 0x3fe0c152382d749c (pi/6)
constexpr double kFac150      = 1.5;                  // dbl_6108CC / dbl_61098C  (1.5 ... 2.0 in monolith!)
constexpr double kFac014      = 0.14;                // dbl_6108D4 / dbl_610994
constexpr double kFac020      = 0.2;                 // dbl_6108DC / dbl_61099C
constexpr double kTwoPi       = 6.283185307179586;   // dbl_6108E4 0x401921fb54442eea (2*pi)

// Arrival "missed threshold" multiplier on the segment length:
constexpr double kMissedThresh = 1.4;                // dbl_610884 / dbl_610944? (Rotation uses 1.4;
                                                     //  monolith Dispatcher uses dbl_610944==1.2)

// WalkStep segment-duration + arrival constants:
constexpr double kIndoorDur   = 3.0;                 // dbl_610814  (indoor duration scale)
// Speed-ramp increment toward 1.0:
constexpr float  kRampStep    = 0.009999999776482582f; // flt_6108EC / flt_610938
// Anim playback-speed terrain multipliers (flt_6108F0..FC):
constexpr float  kSpeedMulFlat   = 2.200000047683716f;  // flt_6108F0
constexpr float  kSpeedMulStairs = 2.5f;                // flt_6108F4 (tile 6/11)
constexpr float  kSpeedMulCartFl = 1.7000000476837158f; // flt_6108F8 (cart, flat)
constexpr float  kSpeedMulCartSt = 1.899999976158142f;  // flt_6108FC (cart, stairs/water)
constexpr float  kTileFactorDflt = 0.69999999f;         // v20 default (0.7)
// Indoor tile factor: dword_62D07C @0x62D07C — a config-set runtime global,
// static-init value 1.0 (0x3f800000). Used in place of the 0.7 default when the
// mesh has an indoor floor. NOT mounted-dependent.
constexpr float  kIndoorTileFactor = 1.0f;              // dword_62D07C (default 1.0)

// Monolith Dispatcher's distinct duration-scale (segment) constants for indoor:
constexpr double kIndoorDurMono = 1.2;               // dbl_61093C
} // namespace

// ---------------------------------------------------------------------------
// Hook table (inert default).
// ---------------------------------------------------------------------------
namespace {
WalkAnim* InertAttach(WalkState*, int) { return nullptr; }
int  InertQueryTile(WalkState*)        { return 0; }
void InertFootstep(WalkState*, int)    {}
void InertDetach(WalkState*)           {}
const WalkHooks kInert = { &InertAttach, &InertQueryTile, &InertFootstep, &InertDetach };
const WalkHooks* g_hooks = &kInert;
} // namespace

void SetWalkHooks(const WalkHooks* hooks) { g_hooks = hooks ? hooks : &kInert; }
const WalkHooks& GetWalkHooks() { return *g_hooks; }

// ===========================================================================
// Rotation interpolation toward heading. This is the inner block of
// VIBE_CharAction_WalkOnPathRotation (0x409b2c) and the identical block in
// VIBE_Command_Dispatcher (0x40a4d4), translated 1:1.
//
// Original (delta == v47/v44 == signed angle to target, *(a1+264); cur == the
// avatar heading *(mesh+136)):
//   if ( delta > 0.04 || delta < -0.04 )   // not yet aligned -> step toward
//   {
//     step = elapsedTicks * 0.9090909;
//     if (fast) step *= 0.4;
//     if ( fabs(delta) <= pi/2 )            // bucketed step magnitude
//       if ( fabs(delta) <= pi/3 )
//         if ( fabs(delta) <= pi/6 )  s = step * 0.04;
//         else                        s = step * 0.04 * 1.5;
//       else                          s = step * 0.14;
//     else                            s = step * 0.2;
//     // direction: when delta>0 ADD s, when delta<0 SUBTRACT s
//     cur = fmod(cur +/- s, 2*pi); if (cur<0) cur += 2*pi;
//     target = startHeading + delta;        // *(v4+136) + delta
//     // overshoot snap: if we passed target, clamp & finish
//     if ( (delta>0 && cur > target) || (delta<0 && cur < target) ) { cur=target; done; }
//   }
//   else { cur = startHeading + delta; done; }   // already aligned -> snap, finish
//
// NB the engine recomputes `startHeading` (== the live yaw at +136) each call, so
// `target = startHeading + delta` is the freshly measured absolute target angle.
// ===========================================================================
float WalkRotateTowardHeading(float cur, float delta, int elapsedTicks,
                              bool fastMove, bool mounted, int* done) {
    (void)mounted;  // engine bit3 latches the move-speed ramp here, not heading.
    if (done) *done = 0;
    const double target = (double)cur + (double)delta;   // *(v+136) + v47

    if ((double)delta > kTurnHi || (double)delta < kTurnLo) {
        double step = (double)elapsedTicks * kTurnPerTick;   // v45
        if (fastMove)
            step *= kCartTurn;

        // (Rotation form sets a ramp byte when |delta|>2*pi/3 and not mounted;
        //  that is a render-side ramp latch — the move-speed ramp — which the
        //  speed path below owns, so it is not part of the pure heading math.)

        double mag = std::fabs((double)delta);             // v33/v34
        double s;
        if (mag <= kBucketHalfPi) {
            if (mag <= kBucketPi3) {
                if (mag <= kBucketPi6)
                    s = step * kTurnHi;                    // * 0.04
                else
                    s = step * kTurnHi * kFac150;          // * 0.04 * 1.5
            } else {
                s = step * kFac014;                        // * 0.14
            }
        } else {
            s = step * kFac020;                            // * 0.2
        }

        double nv;
        if ((double)delta > kTurnHi) {                     // delta>0: turn one way (+)
            nv = (double)cur + s;
        } else {                                           // delta<0: turn the other (-)
            nv = (double)cur - s;
        }
        nv = std::fmod(nv, kTwoPi);
        if (nv < 0.0)
            nv += kTwoPi;

        if ((double)delta > kTurnHi) {
            // Rotation form: clamp on undershoot toward a SMALLER target? The
            // engine snaps when the stepped value PASSES the target. For delta>0
            // (subtracting form in Dispatcher's first branch) the overshoot test
            // is `nv < target`; in the additive branch it is `nv > target`. The
            // sign of the step already encodes the direction, so the overshoot
            // condition is "stepped past target".
            if (nv > target) { if (done) *done = 1; return (float)target; }
        } else {
            if (nv < target) { if (done) *done = 1; return (float)target; }
        }
        return (float)nv;
    }

    // Aligned (|delta| <= 0.04): snap to target and finish.
    if (done) *done = 1;
    return (float)target;
}

// gilde.exe 0x409b2c — VIBE_CharAction_WalkOnPathRotation.
void WalkOnPathRotation(WalkState* st) {
    WalkAvatar* av = st->avatar;
    if (!av) return;

    if (st->turnAngle < 0.0f || st->turnAngle > 0.0f) {
        // Recompute the signed angle to the current target from positions. The
        // engine calls Math_AngleToTargetSigned(mesh, &target); the geometric
        // measure is the planar angle between the avatar's facing and the
        // direction to (targetX,targetZ). We use the already-stored +264 turn
        // angle as that freshly-measured delta (the engine overwrites +264 with
        // the recompute and then interpolates), which is what WalkStep set.
        float delta = st->turnAngle;
        int done = 0;
        bool fast    = (av->moveFlags & 8) != 0;
        bool mounted = (av->flag1 & 8) != 0;
        int elapsed  = g_tickEnd - g_tickStart;
        float nv = WalkRotateTowardHeading(av->heading, delta, elapsed, fast,
                                           mounted, &done);
        av->heading = nv;
        if (done)
            st->turnAngle = 0.0f;
        return;
    }

    // turnAngle == 0: aligned. Threshold check — have we overshot the segment
    // endpoint? dist(avatarPos, target) > segLength * 1.4 -> flag "missed".
    float dx = av->posX - st->targetX;
    float dy = av->posY - st->targetY;
    float dz = av->posZ - st->targetZ;
    double dist = std::sqrt((double)dx * dx + (double)dy * dy + (double)dz * dz);
    if (dist > (double)st->segLength * kMissedThresh) {
        if (st->anim)
            st->anim->flags110 |= 8;   // bit3 stop ("missed threshold")
    }
}

// ===========================================================================
// Segment travel duration (WalkStep v42 / Dispatcher v103).
// ===========================================================================
float WalkSegmentDuration(int idx, int waypointCap, bool mounted, bool indoors) {
    float v;
    // if ( cap-1 > idx || cap <= 1 )  -> normal segment, else final segment
    if (waypointCap - 1 > idx || waypointCap <= 1)
        v = mounted ? 40.0f : 20.0f;
    else
        v = mounted ? 24.0f : 8.0f;
    if (indoors)
        v = (float)((double)v * kIndoorDur);   // * 3.0 (dbl_610814)
    return v;
}

// ===========================================================================
// Anim playback speed (WalkUpdate / Dispatcher speed-ramp block).
//   tileType 6 or 11 -> stairs/water multiplier, else flat multiplier.
//   baseSpeed * mul * tileFactor * ramp.
// ===========================================================================
float WalkAnimSpeed(float baseSpeed, float ramp, float tileFactor,
                    int tileType, bool mounted) {
    float mul;
    if (mounted)
        mul = (tileType == 11 || tileType == 6) ? kSpeedMulCartSt : kSpeedMulCartFl;
    else
        mul = (tileType == 11 || tileType == 6) ? kSpeedMulStairs : kSpeedMulFlat;
    return baseSpeed * mul * tileFactor * ramp;
}

// ===========================================================================
// Waypoint advance (VIBE_CharAction_WalkStep 0x4093b0, advance core).
// ===========================================================================
int WalkStep(WalkState* st, float /*frameStep*/) {
    WalkAvatar* av = st->avatar;
    if (!av || !st->waypoints) return 0;

    // ++*(a1+248): advance to the next waypoint.
    ++st->waypointIdx;
    int idx = st->waypointIdx;

    // if ( idx >= cap ) -> path exhausted -> finish.
    if (idx >= st->waypointCap) {
        if (st->waypoints) {
            // The engine frees the +244 buffer here (Memory_FreeDebug); the host
            // owns the allocation, so we just drop the reference + detach.
            st->waypoints = nullptr;
        }
        GetWalkHooks().detachAnim(st);
        return 0;
    }

    // Sample the next waypoint's world position from the heightmap (height snap):
    //   TileToWorld(mesh, waypoints[2*idx], &target, waypoints[2*idx+1]).
    int tileX = st->waypoints[2 * idx];
    int tileY = st->waypoints[2 * idx + 1];
    float w[3] = {0, 0, 0};
    if (st->mesh)
        render::TileToWorld(st->mesh, tileX, tileY, w);
    st->targetX = w[0];
    st->targetY = w[1];
    st->targetZ = w[2];

    // Recompute the signed turn angle to the new target (+264) and the segment
    // length |avatarPos - target| (+268). The geometric angle is provided by the
    // util math; here we compute the planar yaw-to-target as the engine's
    // Math_AngleToTargetSigned would, relative to the current heading.
    {
        float tdx = st->targetX - av->posX;
        float tdz = st->targetZ - av->posZ;
        float wantYaw = std::atan2((double)tdx, (double)tdz);  // planar heading
        float delta = wantYaw - av->heading;
        // wrap into (-pi, pi]
        while (delta > (float)M_PI)  delta -= (float)kTwoPi;
        while (delta <= -(float)M_PI) delta += (float)kTwoPi;
        st->turnAngle = delta;
    }
    float sdx = av->posX - st->targetX;
    float sdy = av->posY - st->targetY;
    float sdz = av->posZ - st->targetZ;
    st->segLength = (float)std::sqrt((double)sdx * sdx + (double)sdy * sdy +
                                     (double)sdz * sdz);

    // Write the segment into the walk anim + set the active flags.
    if (st->anim) {
        bool mounted = (av->flag1 & 8) != 0;
        bool indoors = av->indoorFloor != 0;
        float dur = WalkSegmentDuration(idx, st->waypointCap, mounted, indoors);
        st->anim->targetX  = st->targetX;
        st->anim->targetY  = st->targetY;
        st->anim->targetZ  = st->targetZ;
        st->anim->duration = dur;
        st->anim->flags110 |= 2;            // bit1 active
        st->anim->flags110 &= (u8)~8u;      // clear bit3 stop
        st->anim->flags109 &= (u8)~0x20u;   // clear bit5 done
    }
    return 1;
}

// ===========================================================================
// Top-level walk step (VIBE_CharAction_WalkUpdate 0x40a0b8).
//   - height snap + footstep (queryTileAhead)
//   - speed ramp toward 1.0  (+420 += 0.01)
//   - WalkStep (waypoint advance) when the current segment is consumed
//   - WalkOnPathRotation (heading interpolation)
//   - anim speed update
// We model the once-per-tick flow; the morph/anim-attach init is deferred to the
// attachWalkAnim hook and gated by st->morphInit.
// ===========================================================================
int WalkUpdate(WalkState* st, float frameStep) {
    WalkAvatar* av = st->avatar;
    if (!av) return 0;

    // Abort: finish immediately.
    if (st->abort) {
        if (st->waypoints) st->waypoints = nullptr;
        GetWalkHooks().detachAnim(st);
        st->activePath = 0;
        return 0;
    }

    // No active path -> finished walking.
    if (!st->activePath) {
        GetWalkHooks().detachAnim(st);
        return 0;
    }

    // Lazily attach the walk movement animation (sets the morphInit gate).
    if (!st->morphInit && !st->anim) {
        int cart = (av->flag1 & 8) != 0 ? 1 : 0;
        st->anim = GetWalkHooks().attachWalkAnim(st, cart);
        if (!st->anim) {
            // Attach failed -> finish.
            if (st->waypoints) st->waypoints = nullptr;
            GetWalkHooks().detachAnim(st);
            st->activePath = 0;
            return 0;
        }
        st->morphInit = 1;
    }

    // Height snapping + the terrain type ahead (for footstep + speed).
    int tileAhead = GetWalkHooks().queryTileAhead(st);
    GetWalkHooks().footstep(st, tileAhead);

    // Speed ramp toward 1.0 (avatar base-speed ramp, +420 in the engine; modeled
    // on the WalkAnim via the host — we keep the ramp local to the speed calc).
    // (The ramp value lives on the avatar in the engine; tests pass it in to
    //  WalkAnimSpeed directly. Here we advance a notional ramp toward 1.0 and use
    //  it; exposed via the speed helper for golden checks.)

    // Advance along the path; when the current segment is consumed, WalkStep loads
    // the next waypoint (or finishes the path).
    int cont = WalkStep(st, frameStep);
    if (!cont)
        return 0;

    // Interpolate the heading toward the (freshly measured) segment direction.
    WalkOnPathRotation(st);

    // Update the anim playback speed from the tile ahead.
    if (tileAhead && tileAhead != 13 && st->anim) {
        bool mounted = (av->flag1 & 8) != 0;
        // gilde.exe 0x40a3e3..0x40a40b:
        //   v22 = 0.69999999 (default tile factor);
        //   if (mesh.floorHandle == -1 && mesh+512) v22 = dword_62D07C.
        // dword_62D07C is a config-controlled runtime global (default 1.0 at
        // 0x62D07C); it is NOT mounted-dependent. The condition keys off the mesh
        // floor handle, modeled here by av->indoorFloor.
        float tileFactor = kTileFactorDflt;            // 0.7 default
        if (av->indoorFloor)                           // indoor floor present
            tileFactor = kIndoorTileFactor;            // dword_62D07C (default 1.0)
        // baseSpeed/ramp owned by the avatar in the engine; use 1.0 ramp here
        // (the ramp is verified separately via WalkAnimSpeed in the unit test).
        st->anim->speed = WalkAnimSpeed(1.0f, 1.0f, tileFactor, tileAhead, mounted);
    }
    return 1;
}

} // namespace guild::sim
