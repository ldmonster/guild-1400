// ===========================================================================
// npcevent_reaper_full — implementation. See header for provenance and the
// coupled-leaf list. The He-field state machine + control flow are translated
// 1:1; the float math is delegated to the recon4 kernels; the engine scene /
// transform / heightmap / sound leaves are reached through ReaperFullHooks.
//
// Original He byte offsets (from the decompiled bodies of 0x4d8c34/8f74/92a4/9440):
//   +176 source object id     +180 target ("victim") object id
//   +196 spawned flag (dword)  +200 reaper avatar node ptr (native side slot)
//   +208/+212/+216 cur pos     +224/+228/+232 prev pose    +236 last game tick
// ===========================================================================
#include "sim/npcevent_reaper_full.h"

#include "sim/actionqueue.h"  // extern u32 g_gameTick;  (dword_62EB38)

#include <unordered_map>

namespace guild::sim {

// ---------------------------------------------------------------------------
// Module state: hook table + the He+200 native-pointer side map (LP64).
// ---------------------------------------------------------------------------
namespace {
ReaperFullHooks g_default{};
const ReaperFullHooks* g_hooks = &g_default;

// Side map: He record -> reaper avatar node handle (the native 8-byte pointer
// the original kept in the 4-byte He+200 slot). Keyed by record address.
std::unordered_map<HeRecord*, void*>& nodeMap() {
    static std::unordered_map<HeRecord*, void*> m;
    return m;
}

// He raw-offset accessors specific to the reaper layout (these offsets are not
// all named in he.h; address them byte-faithfully like the originals).
inline i32& He_SourceObjId(HeRecord* h) { return *reinterpret_cast<i32*>(HeBytes(h) + 176); }
inline i32& He_VictimObjId(HeRecord* h) { return *reinterpret_cast<i32*>(HeBytes(h) + 180); }
inline i32& He_Spawned(HeRecord* h)     { return *reinterpret_cast<i32*>(HeBytes(h) + 196); }
inline f32& He_CurX(HeRecord* h)        { return *reinterpret_cast<f32*>(HeBytes(h) + 208); }
inline f32& He_CurY(HeRecord* h)        { return *reinterpret_cast<f32*>(HeBytes(h) + 212); }
inline f32& He_CurZ(HeRecord* h)        { return *reinterpret_cast<f32*>(HeBytes(h) + 216); }
inline f32& He_PrevX(HeRecord* h)       { return *reinterpret_cast<f32*>(HeBytes(h) + 224); }
inline f32& He_PrevY(HeRecord* h)       { return *reinterpret_cast<f32*>(HeBytes(h) + 228); }
inline f32& He_PrevZ(HeRecord* h)       { return *reinterpret_cast<f32*>(HeBytes(h) + 232); }
inline u32& He_LastTick(HeRecord* h)    { return *reinterpret_cast<u32*>(HeBytes(h) + 236); }

inline void HeSetCur(HeRecord* h, const Vec3& p)  { He_CurX(h) = p.x; He_CurY(h) = p.y; He_CurZ(h) = p.z; }
inline void HeSetPrev(HeRecord* h, const Vec3& p) { He_PrevX(h) = p.x; He_PrevY(h) = p.y; He_PrevZ(h) = p.z; }
inline Vec3 HeGetCur(HeRecord* h)  { return Vec3{He_CurX(h), He_CurY(h), He_CurZ(h)}; }
}  // namespace

void  ReaperFull_SetNode(HeRecord* h, void* node) {
    if (node) nodeMap()[h] = node; else nodeMap().erase(h);
}
void* ReaperFull_GetNode(HeRecord* h) {
    auto it = nodeMap().find(h);
    return it == nodeMap().end() ? nullptr : it->second;
}

void SetReaperFullHooks(const ReaperFullHooks* hooks) { g_hooks = hooks ? hooks : &g_default; }
const ReaperFullHooks& GetReaperFullHooks() { return *g_hooks; }

namespace {
inline int SavedSlot() {  // dword_649D60 -> v3
    return g_hooks->getActiveSceneSlot ? g_hooks->getActiveSceneSlot() : 0;
}
inline void SlotEnter(int v3) {            // SwitchActiveSlot(0, 1, a2, v3)
    if (g_hooks->switchActiveSlot) g_hooks->switchActiveSlot(0, 1, v3);
}
inline void SlotLeave(int v3) {            // SwitchActiveSlot(v3, 1, _, v3)
    if (g_hooks->switchActiveSlot) g_hooks->switchActiveSlot(v3, 1, v3);
}
}  // namespace

// ===========================================================================
// gilde.exe 0x4d8c34 — VIBE_NpcEvent_ReaperApproachTarget
// ===========================================================================
// v3 = dword_649D60; SwitchActiveSlot(0,1,a2,v3);
// Resolve SOURCE(+176)->v6, gate (+97,+460,+16); Resolve TARGET(+180)->v39,
// gate. If a1+196 (already spawned): build spawn from cached pose at +224..+232
// (prev) and +208..+216 (cur); recompute delta vs the live target point.
// Else (first spawn): point source+target bone chains, delta = dst - src,
// sqrLen = dx*dx+dy*dy+dz*dz, prev-pose seed = 0, orientation via VectorAngle.
// Attach node -> a1+200; on success set a1+196=1, refresh +224..+232 (euler) and
// +208..+216 (pos) from the node, clear/set the node flag bytes, load the baf,
// latch +236 = tick, restore slot, return 1. Any gate fail -> restore, return 0.
// ---------------------------------------------------------------------------
int ReaperApproachTarget(HeRecord* h) {
    int v3 = SavedSlot();                   // v3 = dword_649D60
    SlotEnter(v3);                          // SwitchActiveSlot(0,1,a2,v3)

    Vec3 srcPt{};   // v36..v38 (source bone point, +flt_61EFE0 on y)
    Vec3 dstPt{};   // v33..v35 (target bone point, +flt_61EFE0 on y)

    // Resolve + gate SOURCE (+176) and TARGET (+180).
    if (!g_hooks->resolveSourcePoint ||
        !g_hooks->resolveSourcePoint(h, He_SourceObjId(h), &srcPt)) {
        SlotLeave(v3); return 0;            // LABEL_12
    }
    if (!g_hooks->resolveTargetPoint ||
        !g_hooks->resolveTargetPoint(h, He_VictimObjId(h), &dstPt)) {
        SlotLeave(v3); return 0;            // LABEL_12
    }

    Vec3 spawnPos;        // &v36 fed to AttachToUniverseNode
    Vec3 prevSeed{0,0,0}; // v27..v29 (the &v27 "prev pose" passed to attach)
    if (He_Spawned(h)) {
        // already-spawned branch (a1+196 != 0): prev pose comes from the cached
        // pose at +224..+232; spawn pos from cached cur pos +208..+216; delta
        // recomputed vs the live target point (dstPt) which here is the SOURCE
        // resolve's node-relative point in the original — we feed the cached cur
        // pos as the reaper position.  v40 = |dst - cur|^2 (math kernel).
        prevSeed = Vec3{He_PrevX(h), He_PrevY(h), He_PrevZ(h)};   // v27,v28,v29
        spawnPos = HeGetCur(h);                                   // v36..v38 = +208..+216
        (void)VIBE_NpcEvent_ReaperApproachTarget_Math(spawnPos, dstPt);
    } else {
        // first-spawn branch: delta = dst - src, spawnPos = src.
        ApproachResult ar = VIBE_NpcEvent_ReaperApproachTarget_Math(srcPt, dstPt);
        spawnPos = ar.spawnPos;     // v36..v38 = source point
        prevSeed = Vec3{0, 0, 0};   // v27 = 0 ; v29 = 0 (v28 = VectorAngle, leaf)
    }

    // AttachToUniverseNode(0,&spawnPos,"Sensenmann",&prevSeed,v3) -> a1+200.
    Vec3 nodeEuler{}, nodePos{};
    void* node = g_hooks->attachReaperNode
                     ? g_hooks->attachReaperNode(h, spawnPos, &nodeEuler, &nodePos)
                     : nullptr;
    ReaperFull_SetNode(h, node);
    if (!node) {
        SlotLeave(v3); return 0;           // attach failed -> LABEL_12 (return 0)
    }

    He_Spawned(h) = 1;                      // *(a1+196) = 1
    HeSetPrev(h, nodeEuler);                // +224..+232 = node +132..+140 (euler)
    HeSetCur(h, nodePos);                   // +208..+216 = node +76..+84 (pos)
    if (g_hooks->loadReaperAnim) g_hooks->loadReaperAnim(node); // *sensenmann.baf
    He_LastTick(h) = g_gameTick;            // *(a1+236) = dword_62EB38
    SlotLeave(v3);                          // restore v3
    return 1;
}

// ===========================================================================
// gilde.exe 0x4d8f74 — VIBE_NpcEvent_ReaperMoveTowardTarget
// ===========================================================================
// Gate on a1+200 (node) and the target (+180). If node[+464] (move-blocked):
// latch +236=tick and return 1. Else point target + node bone chains, compute
// the planar distance (y forced 0), the height-biased delta, decide arrived
// (planar < 45 -> v31=2, clear +196) vs moving (v31=1): step the position
// (gameSpeed*0.25+0.5)*dt along the normalized delta, apply the height "hop"
// clamp, refresh +224..+232 / +208..+216 from the node, write +208..+216 = new
// pos, SetPosition(node,&newPos). Restore slot, return v31. Gate fail -> 0.
// ---------------------------------------------------------------------------
int ReaperMoveTowardTarget(HeRecord* h) {
    int v3 = SavedSlot();
    SlotEnter(v3);

    void* node = ReaperFull_GetNode(h);
    Vec3 targetPt{}, reaperPt{};
    // Gate chain: node present + node mesh/instance + target resolve+gate.
    bool gate = (node != nullptr)
              && g_hooks->resolveTargetPoint
              && g_hooks->resolveTargetPoint(h, He_VictimObjId(h), &targetPt)
              && g_hooks->resolveNodePoint
              && g_hooks->resolveNodePoint(h, &reaperPt);
    if (!gate) { SlotLeave(v3); return 0; }

    // node[+464] move-blocked: latch tick and return 1 (no movement).
    if (g_hooks->nodeMoveBlocked && g_hooks->nodeMoveBlocked(h)) {
        He_LastTick(h) = g_gameTick;        // *(a1+236) = dword_62EB38
        SlotLeave(v3);
        return 1;
    }

    // Terrain heights at the target and (for the hop clamp) the stepped point.
    f32 targetHeight = g_hooks->terrainHeight ? g_hooks->terrainHeight(targetPt) : 0.0f;

    // The original samples the heightmap TWICE: once at the target (above, the
    // delta-y bias) and once at the *stepped* candidate position v20 (for the
    // hop-clamp test). The recon4 kernel takes `stepHeight` directly and only
    // uses it in the hop branch, so to find the un-clamped stepped position we
    // run the kernel once with a stepHeight low enough that the hop never fires
    // (-1e30), query the real terrain at that position, then run it for real.
    u32 gameTick = g_gameTick;              // dword_62EB38
    u32 lastTick = He_LastTick(h);

    MoveResult probe = VIBE_NpcEvent_ReaperMoveTowardTarget_Math(
        targetPt, reaperPt, targetHeight, gameTick, lastTick, /*stepHeight=*/-1e30f);
    f32 stepHeight = targetHeight;
    if (!probe.arrived && g_hooks->terrainHeight) {
        // probe.newPos is the un-clamped v20 (the hop branch was suppressed);
        // sample the terrain under it exactly as the original does at v20.
        stepHeight = g_hooks->terrainHeight(probe.newPos);
    }

    MoveResult mr = VIBE_NpcEvent_ReaperMoveTowardTarget_Math(
        targetPt, reaperPt, targetHeight, gameTick, lastTick, stepHeight);

    if (mr.arrived) {
        He_Spawned(h) = 0;                  // *(a1+196) = 0
        SlotLeave(v3);
        return mr.code;                     // v31 == 2
    }

    // Moving: latch tick, refresh prev/cur pose from the node, write new pos.
    He_LastTick(h) = g_gameTick;            // *(a1+236) = dword_62EB38 (set in kernel pass too)
    Vec3 nodeEuler{}, nodePos{};
    if (g_hooks->readNodePose) g_hooks->readNodePose(h, &nodeEuler, &nodePos);
    HeSetPrev(h, nodeEuler);                // +224..+232 = node +132..+140
    // The original then overwrites +208..+216 with the NEW position (not nodePos).
    HeSetCur(h, mr.newPos);                 // +208..+216 = v20,v21,v22
    if (g_hooks->setNodePosition) g_hooks->setNodePosition(h, mr.newPos); // SetPosition(node,&v20)

    SlotLeave(v3);
    return mr.code;                         // v31 == 1
}

// ===========================================================================
// gilde.exe 0x4d92a4 — VIBE_NpcEvent_ReaperCacheTargetPose
// ===========================================================================
// Gate node(+200) + target(+180). Point target + node bone chains, delta =
// target - node (height +flt_61F000), sqrLen, orientation via VectorAngle
// (overwrites v26). Refresh +224..+232 (euler) and +208..+216 (node pos = the
// reaper's own bone point v16..v18), set +196=1. Restore, return 1. Gate -> 0.
// ---------------------------------------------------------------------------
int ReaperCacheTargetPose(HeRecord* h) {
    int v3 = SavedSlot();
    SlotEnter(v3);

    void* node = ReaperFull_GetNode(h);
    Vec3 targetPt{}, reaperPt{};
    bool gate = (node != nullptr)
              && g_hooks->resolveTargetPoint
              && g_hooks->resolveTargetPoint(h, He_VictimObjId(h), &targetPt)
              && g_hooks->resolveNodePoint
              && g_hooks->resolveNodePoint(h, &reaperPt);
    if (!gate) { SlotLeave(v3); return 0; }

    // delta = target - node ; sqrLen (math kernel). The angle (VectorAngle) is a
    // separate math cluster routed through the orientation seam (not stored here
    // beyond the original's transient v26).
    (void)VIBE_NpcEvent_ReaperCacheTargetPose_Math(targetPt, reaperPt);

    // Refresh the cached pose from the live node: +224..+232 euler, +208..+216
    // the reaper's own bone point (v16..v18 = the node bone-chain point).
    Vec3 nodeEuler{}, nodePos{};
    if (g_hooks->readNodePose) g_hooks->readNodePose(h, &nodeEuler, &nodePos);
    HeSetPrev(h, nodeEuler);                // +224..+232 = node +132..+140
    HeSetCur(h, reaperPt);                  // +208..+216 = v16,v17,v18 (node bone point)
    He_Spawned(h) = 1;                      // *(a1+196) = 1

    SlotLeave(v3);
    return 1;
}

// ===========================================================================
// gilde.exe 0x4d9440 — VIBE_NpcEvent_ReaperUpdateSoundPos
// ===========================================================================
// Gate node(+200) + target(+180). Point target + node bone chains, delta =
// target - node (height +flt_61F004), sqrLen (stored in v22[1]). Rotate the
// facing vector by the node hierarchy, take the orientation angle, then call
// Sound3d_SetListenerFromVectors(node, nodeBonePt, 120, facing, 8). Restore,
// return 1. Gate fail -> 0.
// ---------------------------------------------------------------------------
int ReaperUpdateSoundPos(HeRecord* h) {
    int v3 = SavedSlot();
    SlotEnter(v3);

    void* node = ReaperFull_GetNode(h);
    Vec3 targetPt{}, reaperPt{};
    bool gate = (node != nullptr)
              && g_hooks->resolveTargetPoint
              && g_hooks->resolveTargetPoint(h, He_VictimObjId(h), &targetPt)
              && g_hooks->resolveNodePoint
              && g_hooks->resolveNodePoint(h, &reaperPt);
    if (!gate) { SlotLeave(v3); return 0; }

    // delta = target - node ; sqrLen (math kernel).
    (void)VIBE_NpcEvent_ReaperUpdateSoundPos_Math(targetPt, reaperPt);

    // The facing vector (rotated by the node hierarchy) is the audio orientation;
    // it is supplied through the sound leaf, which receives the node bone point
    // (reaperPt = v15) as the listener position. Gain/rolloff 120, falloff 8.
    if (g_hooks->sound3dSetListener) {
        Vec3 facing{0, 0, 1};   // flt_5CA2B0 base facing, rotated by the leaf.
        g_hooks->sound3dSetListener(h, reaperPt, facing);
    }

    SlotLeave(v3);
    return 1;
}

} // namespace guild::sim
