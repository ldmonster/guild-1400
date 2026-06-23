#pragma once
#include "guild/common/types.h"
#include "render/geometry_types.h"  // Vertex
#include "render/anim_load.h"   // Animation, AnimFrame
#include "render/skeleton.h"    // AdvanceFrameIndex, Accumulate/ComputeBoneWorldMatrix

// =============================================================================
// guild::render — the object-level skeletal-pose driver + the morph/light vertex
// object-walks that VIBE_Anim_UpdateSkeletonPose chains through. Faithful 1:1
// reconstruction of the gilde.exe (d3_engine.c) anim object-walk cluster:
//
//   0x5cd1d8  VIBE_Anim_UpdateSkeletonPose        (per-track keyframe state machine)
//   0x5c953c  VIBE_Mesh_InterpolateMorphVertices  (object morph-vertex walk)
//   0x5c9054  VIBE_Mesh_ComputeVertexLighting     (object env-map lighting walk)
//
// SCOPE / DEFERRED
// ---------------------------------------------------------------------------
// NOTE: the FULL driver control flow of UpdateSkeletonPose @0x5cd1d8 is now
// reconstructed 1:1 in render/skeleton_pose_driver.{h,cpp} (a hooks-based
// orchestrator). This module remains the home of the self-contained advance/walk
// MATH that driver reuses (AdvanceTrackPhase, the morph/light vertex object-walks).
//
// The PRODUCTION UpdateSkeletonPose (1777 instructions, 185 blocks) is a sprawl
// of object-record + scene-graph state mutation with ~20 leaf calls
// (VIBE_Object_SetPosition/SetWorldTranslation push into the live scene graph,
// VIBE_Texture_AdvanceAnimFrames, VIBE_Anim_ComputeBoneDelta/SampleBoneTranslation/
// ComputeFrameTangents, VIBE_Math_CatmullRomInterp morph-track blend,
// VIBE_Anim_PruneExpiredAttachments, VIBE_Light_BuildVegetationCache,
// VIBE_Memory_FreeDebug). Those scene-graph application leaves are NOT reproduced
// here (listed deferred in the module report). What IS reconstructed byte-for-byte
// is the per-track keyframe ADVANCE state machine (the loop/ping-pong/clamp frame
// stepping that feeds VIBE_Anim_AdvanceFrameIndex + VIBE_Anim_InterpolateBoneFrame),
// and the two vertex object-walks the function ends by chaining.
//
// RECOVERED OBJECT / MESH RECORD LAYOUTS (byte-for-byte)
// ---------------------------------------------------------------------------
// Object record (the `v163`/`result` base):
//   +64  (idx16) i32  lastUpdateTime   (compared to the a2 time; == -> early out)
//   +68  (idx17) i32  animBaseTime     (v183 - this == elapsed)
//   +460 (idx115) ptr meshSkin block   (the skinned-vertex source)
//   +464 (idx116) ptr morphAnim block  (the v59 Catmull-Rom morph track)
//   +492 (idx123) ptr drawData block   (+2316 LOD count, +2317 flags, +624 gate)
//   +528 flags (0x40 cull), +529 flags (0x20 morph-attach), +530 render,
//   +531 LOD bits, +533 byte object-type (== 4 drives the vegetation path)
// Mesh block (a2 in the morph/light walks):
//   +0   ptr   vertex array (80-byte Vertex stride)
//   +4   ptr   light/skin sub-block (a2[4]; +480 light count)
//   +8   i32   vertex count
//   +20  ptr   light array base (a2[5])
//   +28..+376  morph layers (116-byte stride; +380 byte = morph-active gate)
//   per-vertex +72 -> packed normal/uv source block; +77 byte = lit flag
// Per-bone track record (116 bytes; the v5 records in UpdateSkeletonPose):
//   +0  fromFrame  +4 toFrame  +8 phaseAccum(int)  +12 deltaScratch
//   +52/+56 blendFrom/blendTo time  +60 expiry  +64 blendWeight  +68/+72 blend ends
//   +100 f32 phaseFrac  +104 AnimHeader* (+348 frame array, +328 frameCount,
//   +336 startFrame, +340 endFrame, +361 deltaMode byte)
//   +108 u8 repeatCount  +109 mode flags (bit1 reverse, bit0 loop, bit4 clamp)
//   +110 flags (bit1 active, bit2 attach, bit4 boundary)
// =============================================================================
namespace guild::render {

// ---------------------------------------------------------------------------
// Per-track playback state — the subset of the 116-byte track record the keyframe
// ADVANCE state machine reads/writes. This is the deterministic core extracted
// from UpdateSkeletonPose's inner loop (the non-reverse branch + the reverse/
// ping-pong branch), driving AdvanceFrameIndex + the per-frame phase carry.
// ---------------------------------------------------------------------------
struct TrackState {
    i32 fromFrame = 0;   // +0   current keyframe index
    i32 toFrame = 0;     // +4   next keyframe (AdvanceFrameIndex output)
    i32 phase = 0;       // +8   accumulated sub-frame phase (signed)
    u8  mode = 0;        // +109 mode flags: 0x1 loop, 0x2 reverse, 0x10 clamp
    u8  repeatCount = 0; // +108 remaining repeats (decremented at boundaries)
    bool boundary = false; // v186: a track boundary was hit this step
};

// gilde.exe 0x5cd1d8 — VIBE_Anim_UpdateSkeletonPose (per-track advance core).
//   Advances `st` by one phase tick using `firstFrame`/`lastFrame`/`frameCount` and
//   the per-frame durations `durations[k]` (the *(frame+4) segment lengths). This is
//   the forward (non-reverse) playback path of the original's inner `while(1)` loop:
//   while the accumulated phase exceeds the current segment's duration, subtract it
//   and step the frame (loop/clamp/hold per the mode flags via AdvanceFrameIndex).
//   Sets st.boundary when a one-shot/clamp endpoint is reached. Returns the number
//   of frame steps taken. `frameCount` is the anim header's +328 count.
i32 AdvanceTrackPhase(TrackState& st, const i32* durations, i32 firstFrame,
                      i32 lastFrame, i32 frameCount);

// ---------------------------------------------------------------------------
// Object morph-vertex walk (VIBE_Mesh_InterpolateMorphVertices object loop).
// ---------------------------------------------------------------------------
// A per-vertex source block (the engine's *(vertex+72) target): a model-space
// position at +0 and a normal at +12. The morph/light walks read it through the
// vertex's +72 pointer; the reconstruction surfaces the +72 pointers as an explicit
// parallel array (the 80-byte Vertex view cannot hold both an 8-byte +72 pointer
// and the +77 lit flag, so we keep the source blocks separate, which is exactly the
// original's indirection: *(vertex+72) -> a distinct packed-vertex record).
struct VertexSource {
    float pos[3];      // +0   model-space position (non-morph transform input)
    float normal[3];   // +12  vertex normal (lighting input)
};

// Mesh block for the morph/light walks — explicit fields replacing the raw
// a2 + offset access. `sources[i]` is vertex i's +72 source block; `litFlags[i]`
// the vertex's +77 lit byte. `attachClamp` mirrors the object +529 & 0x20 path
// (which additionally clamps the running near/far depth bounds; here those bounds
// are returned via outNear/outFar for testability).
struct MorphMeshBlock {
    Vertex*             vertices = nullptr;  // a2[0]  (80-byte stride; out positions/UV)
    const VertexSource* sources = nullptr;   // *(vertex+72) per vertex (in positions/normals)
    const u8*           litFlags = nullptr;  // *(vertex+77) per vertex (lighting gate)
    i32                 vertexCount = 0;     // a2[8]
};

// gilde.exe 0x5c953c — non-morph vertex transform walk. For each vertex, transform
// its packed source position (vertex+72 -> 3 floats) by the 16-float `world` matrix
// into the vertex's +0/+4/+8 screen-space slot. `attachClamp` (object+529 & 0x20)
// selects the depth-clamping variant; outNear/outFar (optional) receive the running
// min/max transformed z (the flt_13FD168[0]/flt_13FCF3C globals). This is the path
// taken when the mesh has no active morph layer (*(a2+380) == 0).
void TransformMeshVertices(MorphMeshBlock& mesh, const float* world,
                           bool attachClamp, float* outNear, float* outFar);

// ---------------------------------------------------------------------------
// Object env-map lighting walk (VIBE_Mesh_ComputeVertexLighting object loop).
// ---------------------------------------------------------------------------
// gilde.exe 0x5c9054 — non-skinned vertex env-map UV walk. For each lit vertex
// (+77 flag set) computes the reflection of its position about the matrix-rotated
// normal (read from vertex+72 -> +12/+16/+20) and writes the spherical env-map UV
// to the vertex's +32/+36 slot. `m3x3` is the bone/world rotation's 3x3 in the flat
// {m0,m1,m2, m4,m5,m6, m8,m9,m10} order ComputeBoneWorldMatrix produces. This
// mirrors the v6==0 (no skin palette) branch of the original.
void ComputeMeshVertexLightingNonSkinned(MorphMeshBlock& mesh, const float* m3x3);

} // namespace guild::render
