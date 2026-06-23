#pragma once
// =============================================================================
// guild::render — the per-frame skeletal/object-anim POSE DRIVER, reconstructed
// 1:1 from gilde.exe.
//
//   0x5cd1d8  VIBE_Anim_UpdateSkeletonPose   (__usercall al = fn(obj@eax, time@edx))
//             — the 6.6 KB (0x1A25) state machine that, each tick:
//                * early-outs if the object's +64 lastUpdateTime already equals the
//                  incoming time (time is masked with 0x7FFFFFFF; the sign bit is a
//                  "do not rebuild light caches" flag carried in `forced`),
//                * for the SKELETAL object (+492 drawData != 0): optionally advances
//                  texture anim frames, then walks every LOD layer (+624 gate,
//                  384-byte stride) and within each its 3 per-bone tracks (116-byte
//                  stride at drawData+244+28) — for each active track (+104 header,
//                  +110 & 2) it advances the frame cursor / sub-frame phase honoring
//                  loop(+109 &1) / reverse-ping-pong(+109 &2) / clamp(+109 &0x10) and
//                  the repeat counter (+108), samples the bone translation, runs the
//                  per-frame attachment-window blend (Object_SetPosition), and on a
//                  boundary prunes expired attachments + records the layer that drives
//                  the vegetation light cache,
//                * calls ComputeBoneMatrices to (re)build the bone palette,
//                * for the OBJECT morph anim (+464 != 0, +46 & 2): the Catmull-Rom
//                  keyframed position (+20 base + interp) and world translation
//                  (Object_SetWorldTranslation), same loop/reverse/clamp advance over
//                  88-byte keyframes (+56 frame array, +4 fromFrame, +8 toFrame,
//                  +12 phase), with the v192 "anim finished" termination that frees
//                  the morph block,
//                * finally, if a track crossed a boundary AND the object is a type-4
//                  (vegetation) and lighting is enabled, rebuilds the vegetation light
//                  cache for the highest-priority layer (Light_BuildVegetationCache).
//
// ALTITUDE — this is an ORCHESTRATOR. The leaf MATH is reused from its existing
// reconstructions; only the genuine OBJECT/SCENE-GRAPH/ASSET side effects and the
// few runtime-context reads (the global frame counter, the env-map 3x3, etc.) are
// routed through an installable SkeletonPoseHooks dispatch struct with INERT
// DEFAULTS so the driver links + tests headless. The VALUE delivered here is the
// faithful DRIVER control flow: the frame/phase advance ladder, the bone iteration
// order, and the exact conditions under which positions are pushed, attachments
// pruned, and normal/light caches recomputed.
//
// LEAVES REUSED DIRECTLY (the math; not re-translated here):
//   AdvanceFrameIndex            0x5ccf18  render/skeleton.h
//   (InterpolateBoneFrame        0x5cbc10  render/skeleton.h — via hook, needs bone rec)
//   (SampleBoneTranslation       0x5cc920  — via hook, needs anim header/bone rec)
//   ComputeFrameTangents         0x5ccf70  render/object_anim.h  (used on the morph path)
//   CatmullRom (object morph)    via SampleObjectAnim / the Hermite the tangents define
//
// LEAVES ROUTED THROUGH HOOKS (genuine engine side effects / runtime context):
//   Object_SetPosition           0x5af38c   — push bone/morph world position
//   Object_SetWorldTranslation   0x5af50c   — push morph world translation
//   Object_PropagateDirtyFlag    0x5af2c0   — mark the object's transform dirty
//   Texture_AdvanceAnimFrames    0x5daf78   — advance texture-anim cursors
//   Anim_ComputeBoneDelta        0x5cba40   — per-bone delta when not delta-encoded
//   Anim_SampleBoneTranslation   0x5cc920   — sample bone translation (reads anim hdr)
//   Anim_InterpolateBoneFrame    0x5cbc10   — interpolate when header is delta-encoded
//   Anim_PruneExpiredAttachments 0x5d0d38   — drop expired bone attachments at a boundary
//   Anim_ComputeBoneMatrices     0x5cc0d0   — rebuild the bone-matrix palette
//   Anim_FindHighestPriorityLayer 0x5d0e84  — pick the layer that drives veg lighting
//   Light_BuildVegetationCache   0x5c8560   — recompute the per-frame veg light cache
//   Memory_FreeDebug             0x43923c   — free the finished morph anim block
//   (global frame counter dword_649D58, env-map context) — runtime context
//
// GAPS (rule 8) — sub-behaviours intentionally left as named hooks because they
// require the full object/universe runtime that is not modelled here; see the
// per-hook comments and the module report. None are faked.
// =============================================================================
#include "guild/common/types.h"

#include <functional>

namespace guild::render {

using namespace ::guild;  // u8/i32/u32/...

// ---------------------------------------------------------------------------
// Per-track playback record — the subset of the engine's 116-byte per-bone track
// record (drawData+244+28 + 116*k) that the ADVANCE state machine reads/writes.
// Byte offsets are the ORIGINAL offsets within the track record (the `v5` base in
// the decompile); they are cited so the driver's reads/writes stay verifiable.
// ---------------------------------------------------------------------------
struct PoseTrack {
    i32  fromFrame = 0;     // +0    current keyframe index            (*(v5))
    i32  toFrame   = 0;     // +4    target keyframe (AdvanceFrameIndex result) (*(v5+4))
    i32  phase     = 0;     // +8    accumulated sub-frame phase, signed (*(v5+8))
    i32  deltaScratch = 0;  // +12   delta scratch (cleared on clamp re-arm) (*(v5+12))
    // +20/+24/+28 attachment window [start,end,active]; +48 attachment record ptr (id)
    i32  attachStart  = -1; // +20   (*(v5+20))
    i32  attachEnd    = -1; // +24   (*(v5+24))
    i32  attachCur    = -1; // +28   (*(v5+28))
    i32  attachRec    = 0;  // +48   (*(v5+48)) attachment-record id (0 = none)
    float attachBase[3] = {0,0,0}; // +32/+36/+40 captured attach base (v22[19..21])
    u32  blendFrom = 0;     // +52   blend window start time            (*(v5+52))
    u32  blendTo   = 0;     // +56   blend window end time              (*(v5+56))
    u32  expiry    = 0;     // +60   activity-expiry threshold          (*(v5+60))
    float blendWeight = 0;  // +64   current blend weight               (*(v5+64))
    float blendBeg = 0;     // +68   blend weight at window start       (*(v5+68))
    float blendEnd = 0;     // +72   blend weight at window end         (*(v5+72))
    float tolPos[3] = {0,0,0}; // +76/+84 position-settle reference (x@+76,z@+84)
    float settleTol = 0;    // +92   settle tolerance                   (*(v5+92))
    float phaseFrac = 0;    // +100  fractional phase carry             (*(v5+100))
    i32   animHeaderId = 0; // +104  AnimHeader id/handle (0 = inactive) (*(v5+104))
    u8    repeatCount = 0;  // +108  remaining repeats                  (*(v5+108))
    u8    mode = 0;         // +109  mode flags: 0x1 loop,0x2 reverse,0x10 clamp,0x20 reset-pending
    u8    flags = 0;        // +110  flags: 0x2 active,0x4 settle,0x8 finished,0x10 hold,0x40 attach-held
    // Per-tick scratch (the v161/v188/v189 locals in the decompile).
    bool  finished = false; // v188: this track reached a clamp/one-shot endpoint
    bool  boundary = false; // v189: a boundary crossing that triggers attach-prune
    i32   boundaryFrame = 0;// v161: the frame index recorded at the clamp endpoint
};

// One AnimHeader's playback constants the advance reads (the v168/v59+56 record).
// In the engine these come from the loaded AGF/BAF header (render/skeleton.h
// AnimHeader); here the driver reads them through this resolved view so it never
// dereferences the raw 32-bit header.
struct PoseAnimHeader {
    i32        frameCount = 0;  // +328  number of frames
    i32        startFrame = 0;  // +336  first playable frame (clamp lower)
    i32        endFrame   = 0;  // +340  last playable frame   (clamp upper)
    bool       deltaEncoded = false; // +361 byte: delta-encode mode (drives Interpolate vs Delta)
    const i32* durations  = nullptr; // *(+348)+192*k+4 : per-frame segment durations
};

// ---------------------------------------------------------------------------
// Object morph-anim record — the engine's `v59` (object+464) record; the
// Catmull-Rom keyframed position/world-translation track. Offsets cited.
// ---------------------------------------------------------------------------
struct PoseMorphAnim {
    i32   frameCount = 0;    // +0    keyframe count                    (*(v59))
    i32   fromFrame  = 0;    // +4    current keyframe                  (*(v59+4))
    i32   toFrame    = 0;    // +8    target keyframe                   (*(v59+8))
    i32   phase      = 0;    // +12   accumulated sub-frame phase       (*(v59+12))
    u32   expiry     = 0;    // +16   activity-expiry threshold         (*(v59+16))
    float basePos[3] = {0,0,0}; // +20/+24/+28 base position offset
    float baseRot[3] = {0,0,0}; // +32/+36/+40 base "rotation"/2nd-channel offset
    float phaseFrac  = 0;    // +52   fractional phase carry            (*(v59+52))
    u8    repeatCount = 0;   // +44   remaining repeats                 (*(v59+44))
    u8    mode  = 0;         // +45   mode: 0x1 loop,0x2 reverse,0x4 interp,0x10 clamp,0x20 reset-pending
    u8    flags = 0;         // +46   flags: 0x2 active,0x10 boundary,0x20 finished-latch
    // The keyframe array (+56 in the engine -> *(v59+56)): 88-byte frames whose +0
    // is the segment duration, +4 position, +20 rotation, +32/+48 pos tangents,
    // +64/+76 rot tangents (render/object_anim.h ObjAnimFrame).
    // The driver reads it through `frames` (resolved by the host).
    const void* frames = nullptr; // *(v59+56)
    // Per-tick scratch.
    bool finished = false;   // v192: morph anim reached its terminal frame this tick
};

// ---------------------------------------------------------------------------
// Top-level driver state — the per-OBJECT record fields UpdateSkeletonPose reads.
// ---------------------------------------------------------------------------
struct SkeletonPoseState {
    // Object record fields (the `a1`/`v165` base; idxN = byte 4*N).
    u32  lastUpdateTime = 0;  // +64  (idx16) last serviced time (== time => early out)
    u32  animBaseTime   = 0;  // +68  (idx17) anim base time (elapsed = time - this)
    bool hasSkin   = false;   // +460 (idx115) skinned-vertex source present (nonzero)
    bool hasMorph  = false;   // +464 (idx116) object morph-anim present (the v59 block)
    bool hasDrawData = false; // +492 (idx123) drawData present (the v3/skeletal gate)
    u8   drawFlags  = 0;      // drawData+2317 byte: 0x40 = lighting disabled
    u8   objectType = 0;      // +533 byte: == 4 selects the vegetation/relight path
    bool lightingEnabled = true; // convenience mirror of !(drawFlags & 0x40) && hasSkin

    // LOD layers: each carries a +624 active gate and 3 per-bone tracks.
    // Modelled as a flat structure the host fills before calling.
    struct Layer {
        bool        active = false;          // drawData[i+624] gate byte
        PoseTrack       tracks[3];
        PoseAnimHeader  headers[3];          // resolved header per active track
        i32         priority = 0;            // FindHighestPriorityLayer key
    };
    Layer*  layers = nullptr;
    i32     layerCount = 0;

    // Object morph anim (resolved view of object+464).
    PoseMorphAnim* morph = nullptr;

    // Cross-tick outputs the original carries in v191/v193/v180 (the light-cache
    // arming + the layer that drives the vegetation cache rebuild).
    bool boundaryThisTick = false; // v193: a skeletal track crossed a boundary
    bool morphActiveOut   = false; // v191: morph anim active past its expiry
    i32  vegCacheLayer    = 0;     // v180: the frame-array base that feeds the veg cache
};

// ---------------------------------------------------------------------------
// Hooks — genuine engine side effects / runtime context. Inert defaults in the .cpp.
// ---------------------------------------------------------------------------
struct SkeletonPoseHooks {
    // 0x5daf78 — advance every texture-anim cursor for the object (drawData driven).
    std::function<void(SkeletonPoseState&, u32 elapsed)> textureAdvanceAnimFrames;

    // 0x5af2c0 — mark the object's transform dirty (propagates to children).
    std::function<void(SkeletonPoseState&, u32 flag)> objectPropagateDirty;

    // Global frame/version counter dword_649D58 written into AnimHeader+344 each step.
    std::function<u32()> globalFrameCounter;

    // 0x5cc920 — sample a bone's accumulated translation for (layer,track,frame,phase)
    // into out[3]. Needs the anim header's frame array + the bone record; supplied here.
    std::function<void(SkeletonPoseState&, int layer, int track,
                       i32 frame, i32 phase, float out[3])> sampleBoneTranslation;

    // 0x5cbc10 — interpolate the bone frame (delta-encoded header path) and push the
    // result into the bone palette. Side-effecting on the bone record.
    std::function<void(SkeletonPoseState&, int layer, int track,
                       i32 fromFrame, i32 phase, int boneSel)> interpolateBoneFrame;

    // 0x5cba40 — compute the per-bone delta (non-delta-encoded header path).
    std::function<void(SkeletonPoseState&, int layer, int track,
                       i32 frame, i32 deltaScratch, int boneSel)> computeBoneDelta;

    // 0x5af38c — push a world position for a bone-attached child object.
    std::function<void(SkeletonPoseState&, const float pos[3])> objectSetPosition;

    // 0x5af50c — push the object's world translation (morph path).
    std::function<void(SkeletonPoseState&, const float pos[3])> objectSetWorldTranslation;

    // 0x5d0d38 — drop attachments whose window has expired (called at a track boundary).
    std::function<void(SkeletonPoseState&, int layer)> pruneExpiredAttachments;

    // 0x5cc0d0 — rebuild the object's bone-matrix palette after all tracks advanced.
    std::function<void(SkeletonPoseState&)> computeBoneMatrices;

    // 0x5d0e84 — pick the highest-priority layer for the vegetation light cache.
    // Returns the layer index, or -1 if none. (Reconstructed-equivalent default below.)
    std::function<int(SkeletonPoseState&)> findHighestPriorityLayer;

    // 0x5c8560 — recompute the per-frame vegetation light cache for `frameArrayBase`.
    std::function<void(SkeletonPoseState&, i32 frameArrayBase)> buildVegetationCache;

    // 0x43923c — free the finished morph-anim block (the v192 terminal path).
    std::function<void(SkeletonPoseState&)> freeMorphAnim;
};

// gilde.exe 0x5cd1d8 — VIBE_Anim_UpdateSkeletonPose.
//   `time` is the raw 32-bit time the caller passes in edx: its top bit (0x80000000)
//   is the "forced / suppress light-cache rebuild" flag (`v190`), the low 31 bits are
//   the actual tick. Returns true (the original always returns 1; the bool mirrors the
//   `al` result for callers that branch on it). `h` supplies the genuine side effects;
//   omit/leave members null for a headless run.
bool UpdateSkeletonPose(SkeletonPoseState& st, SkeletonPoseHooks& h, u32 time);

} // namespace guild::render
