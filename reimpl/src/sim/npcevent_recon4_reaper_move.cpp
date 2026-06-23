// ===========================================================================
// npcevent_recon4_reaper_move — implementation. See header for provenance and
// the coupled-leaf list. Every float expression below preserves the original's
// operand order (Hex-Rays + disasm of gilde.exe).
// ===========================================================================
#include "npcevent_recon4_reaper_move.h"

#include <cmath>
#include <cstring>

namespace guild {
namespace sim {

namespace {
ReaperMoveHooks g_default{};
const ReaperMoveHooks* g_hooks = &g_default;
} // namespace

void SetReaperMoveHooks(const ReaperMoveHooks* hooks) {
    g_hooks = hooks ? hooks : &g_default;
}
const ReaperMoveHooks& GetReaperMoveHooks() { return *g_hooks; }

// gilde.exe 0x5cb148 — VIBE_Math_VectorNormalize.
//   v3 = sqrt(x*x + y*y + z*z); if (bits(v3) & 0x7fffffff) v *= 1/v3 else v = 0.
void VIBE_Math_VectorNormalize(Vec3& v) {
    f32 len = (f32)std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z); /*0x5cb169*/
    u32 bits;
    std::memcpy(&bits, &len, 4);
    if ((bits & 0x7FFFFFFFu) != 0) { /*0x5cb173*/
        f32 inv = 1.0f / len;       /*0x5cb17e*/
        f32 ny = v.y * inv;         /*0x5cb187*/  // v1
        f32 nz = v.z * inv;         /*0x5cb190*/  // v2
        v.x = v.x * inv;            /*0x5cb192*/
        v.y = ny;                   /*0x5cb194*/
        v.z = nz;                   /*0x5cb197*/
    } else {
        v.y = 0.0f; /*0x5cb1a0*/
        v.z = 0.0f; /*0x5cb1a7*/
        v.x = 0.0f; /*0x5cb1ae*/
    }
}

f32 VIBE_Reaper_SqrDist(const Vec3& target, const Vec3& self) {
    f32 dx = target.x - self.x;
    f32 dy = target.y - self.y;
    f32 dz = target.z - self.z;
    return dx * dx + dy * dy + dz * dz;
}

// gilde.exe 0x4d8c34 — first-spawn (else) branch of ReaperApproachTarget.
ApproachResult VIBE_NpcEvent_ReaperApproachTarget_Math(const Vec3& srcBiased,
                                                       const Vec3& dstBiased) {
    ApproachResult r;
    // v36..v38 = src ; v33..v35 = dst (both already +flt_61EFE0 on y)
    r.spawnPos = srcBiased;
    r.delta.x = dstBiased.x - srcBiased.x;                       /*0x4d8d57*/
    r.delta.y = dstBiased.y - srcBiased.y;                       /*0x4d8d65*/
    r.delta.z = dstBiased.z - srcBiased.z;                       /*0x4d8d75*/
    r.sqrLen = r.delta.x * r.delta.x + r.delta.y * r.delta.y     /*0x4d8da3*/
             + r.delta.z * r.delta.z;
    return r;
}

// gilde.exe 0x4d8f74 — ReaperMoveTowardTarget move math.
MoveResult VIBE_NpcEvent_ReaperMoveTowardTarget_Math(const Vec3& targetFlat,
                                                     const Vec3& reaperPos,
                                                     f32 targetHeight,
                                                     u32 gameTick,
                                                     u32 lastTick,
                                                     f32 stepHeight) {
    MoveResult r;

    // First pass: planar delta with the y term forced to 0.0 (v18 = 0.0).
    Vec3 d;
    d.y = 0.0f;                          /*0x4d903f  v18 = 0.0 */
    d.x = targetFlat.x - reaperPos.x;    /*0x4d9043  v17 = v20 - v14 */
    d.z = targetFlat.z - reaperPos.z;    /*0x4d9053  v19 = v22 - v16 */
    r.planarDist = (f32)std::sqrt(d.x * d.x + 0.0f * 0.0f + d.z * d.z); /*0x4d9082*/

    // Recompute the delta using the target's terrain height (+flt_61EFE4).
    f32 ty = targetHeight + kReaperHeightBias;  /*0x4d9095  v21 = v27 + flt_61EFE4 */
    d.x = targetFlat.x - reaperPos.x;           /*0x4d90a0  v17 = v20 - v14 */
    d.y = ty - reaperPos.y;                     /*0x4d90ac  v18 = v21 - v15 */
    d.z = targetFlat.z - reaperPos.z;           /*0x4d90b8  v19 = v22 - v16 */

    // v31 = (v25 < 45.0) + 1  ->  arrived = 2, moving = 1.
    bool within = r.planarDist < (f64)kReaperArriveDist; /*0x4d90cc*/
    r.code = (within ? 1 : 0) + 1;                       /*0x4d90d2*/
    r.arrived = within;

    if (within) {
        // Arrived: clear the moving flag; position unchanged this tick.
        r.stepScale = 0.0f;
        r.newPos = reaperPos;
        return r;
    }

    // dt in ticks (v32 = gameTick - lastTick), as float.            /*0x4d915e*/
    f32 dt = (f32)((f64)(u32)gameTick - (f64)(u64)lastTick);
    int gameSpeed = GetReaperMoveHooks().getGameSpeed
                        ? GetReaperMoveHooks().getGameSpeed()
                        : 1;                                         /*0x4d9167*/
    // v32 = (gameSpeed * 0.25 + 0.5) * dt
    r.stepScale = (f32)(((f64)(u32)gameSpeed * (f64)kReaperStepSpeedCoef
                         + (f64)kReaperStepBaseCoef) * (f64)dt);     /*0x4d917f*/

    VIBE_Math_VectorNormalize(d);                                    /*0x4d9192*/
    d.x = d.x * r.stepScale;                                         /*0x4d919f*/
    d.y = d.y * r.stepScale;                                         /*0x4d91ab*/
    d.z = d.z * r.stepScale;                                         /*0x4d91c3*/

    r.newPos.x = reaperPos.x + d.x;                                  /*0x4d91d2*/
    r.newPos.y = reaperPos.y + d.y;                                  /*0x4d91df*/
    r.newPos.z = reaperPos.z + d.z;                                  /*0x4d91ed*/

    // Height clamp / hop arc: if the terrain at the stepped position is above
    // the candidate y, lift y by stepScale*0.978 and pull x/z back by
    // stepScale*0.1 along the (already step-scaled) delta, re-based at reaperPos.
    //
    // The original keeps the entire arc chain on the x87 stack at 80-bit
    // precision: dbl_61EFF4 (0.978) is a *double* loaded directly into the fmul
    // (NOT truncated to float), and `fwd` (stepScale * 0.1) is never spilled to a
    // 32-bit slot — it stays extended-precision through the x/z multiplies. We
    // model that extended accumulation as `double` (per the x87-as-double rule):
    // 0.978 stays a double, and `fwd` is a double so the x/z products are formed
    // before the final f32 store. Casting 0.978 to f32 here (0.97799998...) or
    // truncating `fwd` to f32 would diverge from the binary.
    if (stepHeight + kReaperHeightBias > r.newPos.y) {               /*0x4d9207*/
        r.newPos.y =                                                  /*0x4d9219*/
            (f32)((f64)r.stepScale * kReaperHopArcUp + (f64)r.newPos.y);
        f64 fwd = (f64)r.stepScale * (f64)kReaperHopArcFwd;          /*0x4d921d  v10 */
        r.newPos.x = (f32)(fwd * (f64)d.x + (f64)reaperPos.x);       /*0x4d922c*/
        r.newPos.z = (f32)(fwd * (f64)d.z + (f64)reaperPos.z);       /*0x4d9238*/
    }
    return r;
}

// gilde.exe 0x4d92a4 — ReaperCacheTargetPose delta math.
CachePoseResult VIBE_NpcEvent_ReaperCacheTargetPose_Math(const Vec3& targetBiased,
                                                         const Vec3& reaperPos) {
    CachePoseResult r;
    r.delta.x = targetBiased.x - reaperPos.x;                    /*0x4d9370*/
    r.delta.y = targetBiased.y - reaperPos.y;                    /*0x4d9380*/
    r.delta.z = targetBiased.z - reaperPos.z;                    /*0x4d9390*/
    r.sqrLen = r.delta.x * r.delta.x + r.delta.y * r.delta.y     /*0x4d93ba*/
             + r.delta.z * r.delta.z;
    return r;
}

// gilde.exe 0x4d9440 — ReaperUpdateSoundPos delta math.
SoundPosResult VIBE_NpcEvent_ReaperUpdateSoundPos_Math(const Vec3& targetBiased,
                                                       const Vec3& reaperPos) {
    SoundPosResult r;
    r.delta.x = targetBiased.x - reaperPos.x;                    /*0x4d9517*/
    r.delta.y = targetBiased.y - reaperPos.y;                    /*0x4d9527*/
    r.delta.z = targetBiased.z - reaperPos.z;                    /*0x4d9538*/
    r.sqrLen = r.delta.x * r.delta.x + r.delta.y * r.delta.y     /*0x4d955a*/
             + r.delta.z * r.delta.z;
    return r;
}

} // namespace sim
} // namespace guild
