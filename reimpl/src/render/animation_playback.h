#pragma once
#include "guild/common/types.h"
#include "render/skeleton.h"   // AnimFrame/AnimHeader layout + AdvanceFrameIndex

// =============================================================================
// guild::render — skeletal-animation PLAYBACK: the per-track keyframe select /
// blend cluster that runs every frame after the tracks have been advanced. This
// is a faithful 1:1 reconstruction of the gilde.exe (d3_engine.c) anim playback
// leaves that sit BETWEEN the frame-advance state primitive (AdvanceFrameIndex,
// owned by render/skeleton) and the scene-graph push (Object_Set*, owned by the
// object module). Distinct from animation_decode / bone_sample / mesh_transform /
// skeleton_pose / bone_palette (the loaders, the raw samplers, the vertex walks,
// and the name-keyed matrix accumulator respectively).
//
//   0x5cc920  VIBE_Anim_SampleBoneTranslation   keyframe select + blend + rotate
//   0x5cba40  VIBE_Anim_ComputeBoneDelta        from->to delta apply (pos/world)
//   0x5d0edc  VIBE_Anim_UpdateTrackBlendWeight  time-driven cross-fade weight
//   0x5d0e84  VIBE_Anim_FindHighestPriorityLayer pick the heaviest active layer
//   0x5d0dcc  VIBE_Anim_ClearLoopFlags          clear loop bit on named tracks
//   0x5d0e10  VIBE_Anim_SetLoopFlags            set/clear loop bit on named tracks
//   0x5cd184  VIBE_Anim_FindFirstActiveBone     bone-name -> index lookup
//   0x5d0f7c  VIBE_Anim_SeekToFrame             clamp/seek a track to a keyframe
//
// 64-BIT POINTER/FLAG ALIASING (why the track record is a TYPED view)
// ---------------------------------------------------------------------------
// In the original 32-bit record the per-track AnimHeader pointer is a 4-byte slot
// at +104 (104..107) and the mode-flag byte sits at +108 — they do NOT overlap.
// With a native 64-bit pointer (104..111) the pointer would ALIAS the +108/+109
// mode bytes and the +132 name pointer would alias the +138 loop-flag byte. The
// established codebase fix (see skeleton_pose.h VertexSource / TrackState) is to
// surface such colliding fields as separate TYPED struct members carrying their
// ORIGINAL byte offsets. So the TRACK record is modelled by AnimTrack below; the
// frame/bone float records (no pointer/flag collisions) stay raw-byte-addressed.
//
// RECORD LAYOUTS (byte-for-byte from the decompilation; offsets are ORIGINAL)
// ---------------------------------------------------------------------------
// Bone "frame" float-record (the `result`/a1 base, same as InterpolateBoneFrame):
//   [19..21]  base translation   (+76/+80/+84)   [30..32] local trans (+120/4/8)
//   [99..109] local 3x3 rotation rows {99,103,107 | 100,104,108 | 101,105,109}
//   +396..+436 the same 3x3 viewed as bytes (ComputeBoneDelta reads cols)
//   +132/+136/+140 world-translation column (ComputeBoneDelta +44 keyframe path)
// AnimFrame keyframe (192-byte stride): +4 dur, +32/+36/+40 tx/ty/tz,
//   +44/+48/+52 rotation/aux triple.
// AnimHeaderView (track+104): +340 frameCount, +348 frame array, +361 deltaMode,
//   idx82 count / idx84 first / idx85 last (AdvanceFrameIndex inputs).
// =============================================================================
namespace guild::render {

// ---------------------------------------------------------------------------
// Typed AnimHeader view (the track+104 target). Fields carry their ORIGINAL byte
// offsets within the 364-byte AnimHeader; `frames` is the +348 frame array.
// ---------------------------------------------------------------------------
struct AnimHeaderView {
    const AnimFrame* frames = nullptr; // +348  frame array (192-byte stride)
    i32 frameCount = 0;                // +340  total frames (SeekToFrame clamp)
    i32 firstFrame = 0;                // idx84 (+336) clip start
    i32 lastFrame = 0;                 // idx85 (+340 is frameCount; last is idx85)
    i32 advanceCount = 0;              // idx82 (AdvanceFrameIndex `count`)
    u8  deltaMode = 0;                 // +361  delta-encode gate (SampleBoneTranslation)
};

// ---------------------------------------------------------------------------
// Typed per-track view (the 116-byte v5 record). The AnimHeader pointer (+104),
// mode flags (+108/+109) and name pointer (+132) / loop-flag (+138) are surfaced
// as separate members to avoid the 64-bit aliasing described above.
// ---------------------------------------------------------------------------
struct AnimTrack {
    i32 fromFrame = 0;      // +0   *a2
    i32 toFrame = 0;        // +4   a2[1]
    i32 phaseAccum = 0;     // +8   a2[2]
    i32 segFrom = 0;        // +12  a2[3] (v51)
    i32 segPhase = 0;       // +16  a2[4] (v49)
    i32 blendFromTime = 0;  // +52  a2[13]
    i32 blendToTime = 0;    // +56  a2[14]
    float blendCur = 0.0f;  // +64  result[16]  (cross-fade weight output)
    float blendFrom = 0.0f; // +68  result[17]
    float blendTo = 0.0f;   // +72  result[18]
    const AnimHeaderView* hdr = nullptr; // +104 a2[26]
    u8 modeFlags = 0;       // +109 BYTE1(a2[27]) — fed to AdvanceFrameIndex
    const char* boneName = nullptr; // +132 NUL-keyed StrCmp target (0 => skip)
    u8 loopFlags = 0;       // +138 bit1 (0x2) = loop
};

// One animation layer (FindHighestPriorityLayer scans 3 of these at obj+28,
// stride 116). The +132 active flag and +92 weight are the only fields read.
struct AnimLayer {
    i32 active = 0;    // +132 (offset within the 116-byte layer record)
    float weight = 0;  // +92
};

// ---------------------------------------------------------------------------
// Unowned scene-graph / util leaves (NOT owned by render). Declared here,
// defined ONCE in animation_playback_unowned_stubs.cpp until the owning agent
// translates them. The Object_* pushes record into test-observable globals.
// ---------------------------------------------------------------------------
int AnimStrCmp(const char* a, const char* b);            // 0x5d3f10 VIBE_Util_StrCmp
int ObjectSetPosition(void* obj, const float* pos);      // 0x5af38c
int ObjectSetWorldTranslation(void* obj, const float* pos); // 0x5af50c
int ObjectPropagateDirtyFlag(void* obj, u32 flag);       // 0x5af2c0

// ===========================================================================
// 0x5cc920 — VIBE_Anim_SampleBoneTranslation
// ===========================================================================
// Selects + blends the bone's keyframe translation for the current track state
// and rotates the result into bone-local space, writing the final posed
// translation to `out` (3 floats). `bone` is the bone float-record base; `track`
// the per-track state; `phaseEnd` (a3) and `toFrame` (a4) the current end-phase +
// target keyframe. Two paths gated by track.hdr->deltaMode:
//   delta SET  : accumulate per-segment deltas across [segFrom..toFrame] (leading/
//                inner/trailing prorated by phase), rotate by the bone 3x3
//                (idx 99..109), add base (idx 19..21) + local (idx 30..32). The
//                static-pose fast path (toFrame==segFrom && phaseEnd==segPhase)
//                returns base+local directly.
//   delta CLEAR: a plain two-keyframe lerp (track.fromFrame .. track.toFrame by
//                track.phaseAccum/dur(from)) then rotate + add.
// Returns `bone` (the original returns its eax = result).
const float* SampleBoneTranslation(const float* bone, const AnimTrack& track,
                                   i32 phaseEnd, i32 toFrame, float* out);

// ===========================================================================
// 0x5cba40 — VIBE_Anim_ComputeBoneDelta
// ===========================================================================
// Applies the from->to keyframe delta for a single bone and pushes it to the
// scene graph. `mode`: bit0 rotates the keyframe (+32/+36/+40) delta by the bone
// 3x3 (+396..) + base (+76/+80/+84) -> ObjectSetPosition; bit1 adds the keyframe
// (+44/+48/+52) delta to the world column (+132/+136/+140) -> ObjectSetWorldTranslation.
// `obj` is the bone object base; `track` supplies the AnimHeader frame array.
// Returns the low byte of the last push (the original's al).
u8 ComputeBoneDelta(void* obj, const AnimTrack& track, i32 toFrame, i32 fromFrame, u8 mode);

// ===========================================================================
// 0x5d0edc — VIBE_Anim_UpdateTrackBlendWeight
// ===========================================================================
// Sets a track's cross-fade window [startTime..endTime] with endpoint weights
// [wFrom..wTo] (float bit patterns, as the original stored the i32 args then read
// them as float) and evaluates track.blendCur from the global clock g_gameTick:
// before the window the phase term is 0 so blendCur == wFrom; inside it lerps;
// at/after the window it snaps to wTo. No-op unless track.hdr && start<=end.
void UpdateTrackBlendWeight(AnimTrack& track, i32 wFromBits, i32 wToBits,
                            u32 startTime, u32 endTime);

// ===========================================================================
// 0x5d0e84 — VIBE_Anim_FindHighestPriorityLayer
// ===========================================================================
// Scans 3 layer records (k = 2..0) and returns the index of the active
// (layer.active != 0) layer with the greatest weight, or -1 if none / the gate is
// clear. (The original returned the layer address obj+28+116*k; here we return the
// index k, which the caller multiplies back identically.)
i32 FindHighestPriorityLayer(bool layersActive, const AnimLayer* layers, i32 count);

// ===========================================================================
// 0x5d0dcc / 0x5d0e10 — Clear/Set loop flags on the named track(s)
// ===========================================================================
// Walk a track array, matching each track.boneName against `name` via AnimStrCmp.
// ClearLoopFlags clears bit1 (0x2) of the matched track's loopFlags. SetLoopFlags
// additionally stamps the object's anim sub-block time (`outStampTime`) with
// g_gameTick, then sets (loop) or clears bit1.
void AnimClearLoopFlags(AnimTrack* tracks, i32 count, const char* name);
void AnimSetLoopFlags(AnimTrack* tracks, i32 count, bool loop, const char* name,
                      i32* outStampTime);

// ===========================================================================
// 0x5cd184 — VIBE_Anim_FindFirstActiveBone
// ===========================================================================
// Looks up `name` in the AnimHeader's bone-name table: header[87] (frames ptr)
// must be set, header[81] (bone count) > 0; names start at header+16 (byte 64)
// with a 64-byte stride. Returns the matching index, or -1 if absent.
i32 FindFirstActiveBone(const void* header, const char* name);

// ===========================================================================
// 0x5d0f7c — VIBE_Anim_SeekToFrame
// ===========================================================================
// Clamps `frame` into [0, frameCount-1] (negative wraps to the last frame), seeds
// track.phaseAccum to 0 mid-clip or dur(frame)-1 at the last frame, stores the new
// fromFrame, recomputes toFrame via AdvanceFrameIndex, and marks `obj` dirty. No-op
// unless track.hdr. Returns the low byte of ObjectPropagateDirtyFlag (the al).
u8 SeekToFrame(void* obj, AnimTrack& track, i32 frame);

} // namespace guild::render
