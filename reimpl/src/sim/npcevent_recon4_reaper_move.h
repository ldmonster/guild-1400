#pragma once
// ===========================================================================
// npcevent_recon4_reaper_move — Reaper ("Sensenmann") plague-NPC AI movement,
// pose caching, and 3D-sound positioning math, reconstructed 1:1 from gilde.exe.
//
// Cluster (provenance):
//   gilde.exe 0x4d8c34 — VIBE_NpcEvent_ReaperApproachTarget
//   gilde.exe 0x4d8f74 — VIBE_NpcEvent_ReaperMoveTowardTarget
//   gilde.exe 0x4d92a4 — VIBE_NpcEvent_ReaperCacheTargetPose
//   gilde.exe 0x4d9440 — VIBE_NpcEvent_ReaperUpdateSoundPos
//   gilde.exe 0x5cb148 — VIBE_Math_VectorNormalize  (directly used by move-toward)
//
// SCOPE: this unit reconstructs the EXACT float-domain movement/scoring math
// (delta vectors, squared distances, arrival threshold, time-scaled step toward
// the target, the height-clamp "hop" arc, the cached-pose write-back, and the
// 3D sound listener position deltas). The original op-order is preserved
// verbatim — distances are accumulated dx*dx + dy*dy + dz*dz (or with the y
// term forced to 0.0 in move-toward), and the step magnitude is computed as
// (gameSpeed * 0.25 + 0.5) * dtTicks before normalizing.
//
// COUPLED LEAVES (NOT reconstructed here; supplied through ReaperMoveHooks so
// the headless build links with no engine/Vulkan/SDL dependency):
//   - VIBE_Universe_SwitchActiveSlot      0x5b4a24  (render-slot context switch)
//   - VIBE_GameObject_ResolveEntityById   0x583b44  (entity handle resolve)
//   - VIBE_Transform_PointThroughBoneChain 0x5c8b38 (bone-chain world point)
//   - VIBE_Heightmap_WorldToTileWithHeight 0x5c6644 (terrain height query)
//   - VIBE_Object_AttachToUniverseNode    0x5b3e30  (spawn reaper avatar node)
//   - VIBE_Character_LoadObjectAnimation  0x426488  (*sensenmann.baf)
//   - VIBE_Object_SetPosition             0x5af38c  (write node world pos)
//   - VIBE_Math_VectorAngleWrapped        0x5ca504  (orientation; own math cluster)
//   - VIBE_Transform_RotateVectorByHierarchy 0x5c8990
//   - VIBE_Sound3d_SetListenerFromVectors 0x4262a0  (rule 5 — SDL audio backend)
//   - VIBE_Command_GetGameSpeed           0x493e80
// These hooks default to inert no-ops; the MATH below is exercised directly by
// the move/pose entry points using caller-provided world positions.
// ===========================================================================

#include <guild/common/types.h>

namespace guild {
namespace sim {

using f32 = float;
using f64 = double;

// ----- original immediate float constants (verified via get_bytes) ----------
// flt_61EFE0 / flt_61EFE4 / flt_61F000 / flt_61F004 = 80.0  (avatar height bias)
// flt_61EFE8 = 45.0   (arrival distance threshold)
// flt_61EFEC = 0.25   (game-speed step coefficient)
// flt_61EFF0 = 0.5    (step base coefficient)
// dbl_61EFF4 = 0.978  (hop arc forward-shrink toward landing)
// flt_61EFFC = 0.1    (hop arc forward coefficient)
constexpr f32 kReaperHeightBias    = 80.0f;   // flt_61EFE0 etc.
constexpr f32 kReaperArriveDist    = 45.0f;   // flt_61EFE8
constexpr f32 kReaperStepSpeedCoef = 0.25f;   // flt_61EFEC
constexpr f32 kReaperStepBaseCoef  = 0.5f;    // flt_61EFF0
constexpr f64 kReaperHopArcUp      = 0.978;   // dbl_61EFF4
constexpr f32 kReaperHopArcFwd     = 0.1f;    // flt_61EFFC

struct Vec3 { f32 x, y, z; };

// gilde.exe 0x5cb148 — VIBE_Math_VectorNormalize. In-place; zero vector -> zero.
void VIBE_Math_VectorNormalize(Vec3& v);

// Squared-distance accumulation in the original op-order dx*dx + dy*dy + dz*dz.
f32 VIBE_Reaper_SqrDist(const Vec3& target, const Vec3& self);

// ----- inert-default coupled-leaf hooks -------------------------------------
struct ReaperMoveHooks {
    // VIBE_Command_GetGameSpeed 0x493e80 — speed multiplier (default 1).
    int (*getGameSpeed)() = nullptr;
};
void SetReaperMoveHooks(const ReaperMoveHooks* hooks);
const ReaperMoveHooks& GetReaperMoveHooks();

// ===========================================================================
// Pure math entry points. The full original entry points also do entity
// resolution / node attach / sound updates (coupled leaves); these functions
// expose the exact float math the originals run once the world positions are in
// hand, so it can be golden-tested headless.
// ===========================================================================

// gilde.exe 0x4d8c34 — VIBE_NpcEvent_ReaperApproachTarget (math portion).
// Given source ("from", v6) and destination ("to", v8) bone-chain world points
// (each already height-biased by +flt_61EFE0 on y), compute the delta v30..v32,
// the squared length v40, and report them. Mirrors the else-branch (first spawn)
// op-order: delta = dst - src; v40 = dx*dx + dy*dy + dz*dz.
struct ApproachResult {
    Vec3 delta;      // v30,v31,v32 = dst - src
    f32  sqrLen;     // v40
    Vec3 spawnPos;   // v36..v38 = the source point (where the avatar spawns)
};
ApproachResult VIBE_NpcEvent_ReaperApproachTarget_Math(const Vec3& srcBiased,
                                                       const Vec3& dstBiased);

// gilde.exe 0x4d8f74 — VIBE_NpcEvent_ReaperMoveTowardTarget (math portion).
//   targetFlat : target bone point (y will be ignored for the planar distance)
//   reaperPos  : current reaper node bone point
//   targetHeight : terrain height at the target tile (+flt_61EFE4 applied here)
//   gameTick   : current dword_62EB38 tick value
//   lastTick   : the avatar's stored +236 tick value (a1+236)
//   stepHeight : terrain height at the *stepped* position (+flt_61EFE4 applied)
// Returns whether arrived plus the new position to write back.
struct MoveResult {
    int  code;        // v31: 2 = arrived (planarDist < 45), 1 = still moving
    bool arrived;     // code == 2
    Vec3 newPos;      // v20,v21,v22 to feed VIBE_Object_SetPosition
    f32  planarDist;  // v25 (sqrt of dx*dx + 0*0 + dz*dz)
    f32  stepScale;   // v32
};
MoveResult VIBE_NpcEvent_ReaperMoveTowardTarget_Math(const Vec3& targetFlat,
                                                     const Vec3& reaperPos,
                                                     f32 targetHeight,
                                                     u32 gameTick,
                                                     u32 lastTick,
                                                     f32 stepHeight);

// gilde.exe 0x4d92a4 — VIBE_NpcEvent_ReaperCacheTargetPose (math portion).
// delta = targetBiased - reaperPos; sqrLen = dx*dx+dy*dy+dz*dz. (The original
// then also caches the node's orientation via VectorAngleWrapped — a separate
// math cluster — and writes the prev/cur pose fields.)
struct CachePoseResult {
    Vec3 delta;   // v19,v20,v21
    f32  sqrLen;  // v26 (before being overwritten by the angle)
};
CachePoseResult VIBE_NpcEvent_ReaperCacheTargetPose_Math(const Vec3& targetBiased,
                                                         const Vec3& reaperPos);

// gilde.exe 0x4d9440 — VIBE_NpcEvent_ReaperUpdateSoundPos (math portion).
// delta = targetBiased - reaperPos; sqrLen = dx*dx+dy*dy+dz*dz. The original
// feeds reaperPos + the rotated facing vector to VIBE_Sound3d_SetListenerFromVectors
// (rule 5 SDL audio leaf) with the gain/rolloff 120 and falloff 8.
struct SoundPosResult {
    Vec3 delta;   // v19,v20,v21
    f32  sqrLen;  // *(float*)&v22[1]
};
SoundPosResult VIBE_NpcEvent_ReaperUpdateSoundPos_Math(const Vec3& targetBiased,
                                                       const Vec3& reaperPos);

} // namespace sim
} // namespace guild
