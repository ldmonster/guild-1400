#pragma once
#include "render/anim_load.h"     // render::Animation, LoadBinaryAnimation (reused, not duplicated)
#include "render/bgf_loader.h"    // render::BgfModel / BgfVertex (the static rest mesh)
#include "guild/common/types.h"

#include <cstddef>
#include <vector>

// =============================================================================
// guild::render — REAL character-animation (Resources/animations.BIN) decode +
// "posed mesh at time t" sampler.
//
// FORMAT (recovered from the shipped bytes + the binary):
//   Resources/animations.BIN is a PKZIP/DEFLATE archive (2007 members: 953 `.baf`
//   binary animations, 954 sibling `.ini` clip settings, a few `.bak`/`.oam`).
//   Each `.baf` member, once inflated, is the SAME "BGF\0" (42 47 46 00) token /
//   script stream the static AGF mesh format uses — it is read by the very loader
//   the engine calls for binary animations:
//       0x5e450c  VIBE_ModelIo_LoadBinaryAnimation
//   which is already reconstructed 1:1 as render::LoadBinaryAnimation (anim_load.*).
//   A `.baf` carries a header (token 35 frameCount, 41/42 loop start/end, 25/52
//   vertexCount) and, per frame, a morph POINT array (token 33: vertexCount * vec3)
//   plus a root frame transform (token 49). So a `.baf` is a per-frame VERTEX-MORPH
//   stream: frame f holds the full posed position of every animated point.
//
//   This module does NOT re-parse that stream — it CALLS render::LoadBinaryAnimation
//   (a public helper) so the token grammar lives in exactly one place — and adds the
//   genuinely-new pieces Wave 28 asks for: a clip wrapper + a time -> posed-mesh
//   SAMPLER, grounded in:
//       0x5c9394  VIBE_Anim_ComputeMorphWeights  (the from/to blend-weight pair)
//       0x5ca2fc  VIBE_Math_VectorLerp           (per-component a + (b-a)*t)
//   ComputeMorphWeights yields *a2 = 1-w (weight of the "from" frame) and *a3 = w
//   (weight of the "to" frame), w = phase / segDuration; an optional cosine ease
//   (flag bit 4, constants pi/2=0x628CCC, pi=0x628CD0, 0.5=0x628CD4) shapes w at the
//   clip ends. The posed point is then lerp(from, to, w) over each morph vec3.
//
// WIRED vs INERT (see report):
//   WIRED:  LoadBinaryAnimation (anim_load) for the real `.baf` parse;
//           VectorLerp / ComputeMorphWeights math reconstructed here for the sample.
//   INERT/DEFERRED: the full engine pose driver VIBE_Anim_ComputeBoneMatrices
//           @0x5cc0d0 + VIBE_Anim_UpdateSkeletonPose @0x5cd1d8 are object/scene-graph
//           state machines (name-match dummies, VIBE_Object_SetPosition /
//           SetWorldTranslation, MatrixFromEuler bone palette). They are NOT driven
//           here; this module reconstructs the self-contained morph-blend leaf those
//           drivers are built on. The skeletal bone-matrix leaves remain in
//           render/skeleton.* (already reconstructed).
// =============================================================================
namespace guild::render {

// A loaded real animation clip: the parsed Animation (header + per-frame morph
// points) plus convenience accessors. `Load` is a thin wrapper over the shared
// LoadBinaryAnimation token parser.
struct AnimClip {
    Animation anim;                 // header (frameCount, vertexCount, loop range) + points
    bool      valid = false;

    int   FrameCount()  const { return anim.header.frameCount; }
    int   VertexCount() const { return anim.header.vertexCount; }  // morph points per frame
    int   StartFrame()  const { return anim.header.startFrame; }
    int   EndFrame()    const { return anim.header.endFrame; }

    // Frame f's flat morph point array (vertexCount * 3 floats). Empty / oversize
    // requests return an empty span via the size==0 vector path.
    const std::vector<float>& FramePoints(int f) const;
};

// gilde.exe 0x5e450c (via render::LoadBinaryAnimation) — parse one inflated `.baf`
// member of animations.BIN into `out`. `name` is copied into the header; `loadFlag`
// is the engine `a3` delta-encode gate (1 = the per-frame delta-from-root post-pass,
// which is how the engine stores them). Returns false on a bad/truncated stream.
bool LoadAnimation(const u8* data, size_t size, const char* name, AnimClip& out,
                   u8 loadFlag = 1);

// gilde.exe 0x5c9394 — VIBE_Anim_ComputeMorphWeights (blend-weight pair).
//   Given the current segment [fromFrame .. toFrame] of `clip` and an integer
//   sub-frame `phase` in [0, segDuration], returns the "to" weight w = phase/seg in
//   [0,1] (and, via wFrom, the "from" weight 1-w). `ease` selects the cosine-ease
//   variant (engine flag bit 4): at clip extremes w is shaped by the recovered
//   constants. Pure; no side effects.
struct MorphWeights { float wFrom = 1.0f; float wTo = 0.0f; };
MorphWeights ComputeMorphWeights(const AnimClip& clip, int fromFrame, int toFrame,
                                 int phase, bool ease = false);

// A posed mesh sample at time t: the morph points interpolated between two frames.
struct PosedMesh {
    std::vector<float> points;   // vertexCount * 3, the blended positions
    int   vertexCount = 0;
    float bbMin[3] = {0, 0, 0};  // axis-aligned bounds of the posed points
    float bbMax[3] = {0, 0, 0};
    bool  valid = false;
};

// gilde.exe 0x5c9394 + 0x5ca2fc — sample `clip` at continuous time `t` (in frame
// units, 0 .. frameCount-1) into a posed morph mesh. t is split into the integer
// "from" frame and a fractional weight; the posed point of vertex i is
//   lerp(frame[from].point[i], frame[to].point[i], w)
// with w derived from the fractional part scaled by that segment's duration (so the
// blend honours the engine's per-segment timing exactly as ComputeMorphWeights does).
// `clamp` holds at the clip ends (default); when false t wraps modulo the loop range.
// Fills the posed bbox. Returns out.valid=false for an invalid/empty clip.
PosedMesh SamplePosedMesh(const AnimClip& clip, float t, bool clamp = true);

// Sample at an explicit segment+weight (the form ComputeMorphWeights feeds): blend
// frame `fromFrame` -> `toFrame` by the "to" weight `wTo` in [0,1]. Used by the
// time-based SamplePosedMesh and exposed for tests / the playback driver.
PosedMesh SamplePosedMeshSeg(const AnimClip& clip, int fromFrame, int toFrame, float wTo);

// gilde.exe 0x5ca2fc — VIBE_Math_VectorLerp: out = a + (b-a)*t, per component.
// Exposed so callers (and the bone leaves) share one definition.
inline void VectorLerp(const float a[3], const float b[3], float t, float out[3]) {
    out[0] = (b[0] - a[0]) * t + a[0];
    out[1] = (b[1] - a[1]) * t + a[1];
    out[2] = a[2] + t * (b[2] - a[2]);
}

} // namespace guild::render
