#pragma once
#include "guild/common/types.h"

// =============================================================================
// guild::render — VEGETATION PER-FRAME ANIMATION
//
//   gilde.exe 0x5c8560  VIBE_Light_BuildVegetationCache   (the per-frame veg update)
//   gilde.exe 0x5c9d04  VIBE_Mesh_TransformPackedVertices  (i8 unpack + bone xform)
//
// FINDING (rule 8 — documented dead-end, NOT a faked sway):
//   The original engine does NOT apply a wind/sine vertex "sway" to vegetation.
//   The vegetation path that VIBE_Anim_UpdateSkeletonPose @0x5cd1d8 references via
//   its "vegetation light-cache gate" (0x5cebed..0x5ce466, type-4 objects) is
//   VIBE_Light_BuildVegetationCache @0x5c8560. That function is a per-frame
//   DYNAMIC-LIGHT relight + a STATIC bone-matrix vertex transform — there is no
//   time term, no phase accumulator, no fsin/fcos anywhere in the vegetation
//   chain (verified across 0x5c8560 BuildVegetationCache, 0x5c9d04
//   TransformPackedVertices, 0x5c8c40 PointToBoneLocalSpace, 0x5c8b38
//   PointThroughBoneChain, 0x5c8fac ComputeBoneWorldMatrix, 0x5c6f90
//   ApplyToCachedVertices). Contrast the water grid 0x5be428 which DOES carry
//   four sine phase accumulators (render/water_vertices.h) — vegetation has none.
//
//   The only things that make vegetation change frame-to-frame in Die Gilde are:
//     (1) the SEASONAL TEXTURE-SET swap on "pfl_"/"vg_"/"!vg_" mesh members
//         (reconstructed in wave-4, render/texture_set_table.* +
//         sim/object_lifecycle2.h ObjectHideFoliageDecor @0x506388), and
//     (2) the per-frame DYNAMIC-LIGHT relight reconstructed here — vegetation is
//         the one decor class relit EVERY frame (type-4), so moving light sources
//         (e.g. fire) re-shade the leaves. This is the genuine per-frame
//         "vegetation animation" the pose driver gates.
//
// This module reconstructs (1:1) the pure, host-independent halves of the veg
// cache update — the packed-vertex unpack scale/bias and the per-vertex
// luminance->intensity-byte computation (both lit and unlit branches) — and
// assembles the gate + driver shape. The genuinely runtime-coupled leaves (the
// bone-world-matrix build, the scene-graph light walk, the LUT-attenuated light
// accumulation) are routed through callbacks, identical in spirit to how
// render/water_vertices.h injects FindGroupMember.
// =============================================================================
namespace guild::render {

// -----------------------------------------------------------------------------
// Recovered constants (get_bytes @0x628C..0x64A0). Float bit patterns decoded.
// -----------------------------------------------------------------------------
// VIBE_Mesh_TransformPackedVertices i8 unpack:  v = ((i16)byte + bias) * scale
//   flt_628CDC = -128.0   (bias),  flt_628CD8 = 1/127.5 = 0.007843137...  (scale)
constexpr float kPackedVertexBias  = -128.0f;             // flt_628CDC @0x628CDC
constexpr float kPackedVertexScale = 0.007843137718737125f; // flt_628CD8 @0x628CD8

// VIBE_Light_BuildVegetationCache unlit luminance weights (the byte_649D70==0 arm):
//   intensity = G*0.59 + R*0.30 + B*0.11  (note: G weight applied to .y/+52,
//   R weight to .x/+48, B weight to .z/+56 — see ComputeVertexIntensityUnlit)
constexpr float kLumWeightR = 0.30000001192092896f; // flt_628C9C @0x628C9C (vert+48 .x/R)
constexpr float kLumWeightG = 0.5899999737739563f;  // flt_628C98 @0x628C98 (vert+52 .y/G)
constexpr float kLumWeightB = 0.10999999940395355f; // flt_628CA0 @0x628CA0 (vert+56 .z/B)

// VIBE_Light_BuildVegetationCache lit-clamp (the byte_649D70!=0 arm):
//   if max(r,g,b) > 255.0 (dbl_628CAC), rescale by 255.0 (flt_628CA4) / max.
constexpr float  kLitClampScale = 255.0f; // flt_628CA4 @0x628CA4
constexpr double kLitClampMax   = 255.0;  // dbl_628CAC @0x628CAC

// Init colour written to the per-vertex RGB scratch before the light walk
// (flt_64A074/78/7C = 200.0 each), and the lighting reach bias (flt_64A06C, the
// light-source +56 radius added to the mesh draw-range +468 before squaring).
constexpr float kVegBaseColour = 200.0f;  // flt_64A074/78/7C @0x64A074..

// -----------------------------------------------------------------------------
// A packed vegetation vertex as the cache walks it. The engine's mesh-member
// record is two parallel arrays the cache reads/writes:
//   - the SOURCE i8-packed positions: base+184 indexed [3*i+0..2] (u8 each)
//   - the OUTPUT 20-float (80-byte) vertex stride. The cache touches:
//       float[12..14] (+48..+56)  RGB colour scratch (seeded, accumulated, read)
//       byte  +68 (=float[17] b0)  B intensity byte
//       byte  +69                  G intensity byte
//       byte  +70                  R intensity byte (unlit arm writes only +70)
// Modelled by the fields the reconstructed math actually reads/writes.
// -----------------------------------------------------------------------------
struct VegVertex {
    u8    packed[3];   // source i8-packed position (base+184, [3*i..3*i+2])
    float pos[3];      // float[0..2]  bone-transformed position (TransformPackedVertices)
    float rgb[3];      // float[12..14] (+48/52/56) colour scratch (R,G,B order in mem)
    u8    iB;          // +68 B intensity byte
    u8    iG;          // +69 G intensity byte
    u8    iR;          // +70 R intensity byte
};

// gilde.exe 0x5c9d04 — the i8 packed-vertex unpack (the scale/bias half, BEFORE
// the bone-matrix multiply). Returns the model-space float component for a packed
// byte:  ((i16)b + (-128)) * (1/127.5).  Reproduces the exact FILD/FADD/FMUL.
//   v13/v14/v15 = ((double)(__int16)byte + flt_628CDC) * flt_628CD8.
float UnpackVertexComponent(u8 packedByte);

// Unpack all three components of one vertex's packed position (model space, pre
// bone transform). Convenience over UnpackVertexComponent for the three axes.
void UnpackVertexPosition(const u8 packed[3], float outModel[3]);

// gilde.exe 0x5c8560 (loc_5C87FE, the byte_649D70==0 arm) — UNLIT per-vertex
// intensity. The engine computes a single grayscale byte from the accumulated
// colour scratch and stores it at +70:
//   n = (int)(rgb_G*0.59 + rgb_R*0.30 + rgb_B*0.11);  if (n >= 255) n = -1 (0xFF).
// The `(int)` is a BARE fistp (@0x5c8844) => round-to-NEAREST-EVEN, NOT truncate
// (no VIBE_Coord_ConvertX). Returns the saturated byte (0xFF when the rounded
// sum reaches/exceeds 255). `rgb` is the in-memory order: rgb[0]=.x/+48 (R
// weight), rgb[1]=.y/+52 (G weight), rgb[2]=.z/+56 (B weight).
u8 ComputeVertexIntensityUnlit(const float rgb[3]);

// gilde.exe 0x5c8560 (loc_5C86D7, the byte_649D70!=0 arm) — LIT per-vertex
// intensity. Three separate bytes (+68/+69/+70 = B/G/R). If the brightest channel
// exceeds 255.0, all three are rescaled by 255/max BEFORE the byte store. Each
// store is a BARE fistp (@0x5c8764/8775/8786) => round-to-NEAREST-EVEN, NOT
// truncate:
//   m = max(r, g, b);  if (m > 255.0) { s = 255.0/m; r*=s; g*=s; b*=s; }
//   outB = (u8)r_scratch_x(+48); outG = (u8)g(+52); outR = (u8)b(+56)
// Matches the decompile's per-channel store: +70 <- (int)rgb[0], +69 <- (int)rgb[1],
// +68 <- (int)rgb[2]. `rgb` in-memory order; writes outBGR[0]=B(+68) etc.
void ComputeVertexIntensityLit(const float rgb[3], u8 outBGR[3]);

// -----------------------------------------------------------------------------
// Driver hooks — the runtime-coupled leaves (rule 8: NAMED, not faked).
//   transformVertices : VIBE_Mesh_TransformPackedVertices @0x5c9d04 bone-matrix
//                       multiply (needs the live bone palette). Fills v.pos[].
//   accumulateLights  : VIBE_SceneGraph_WalkAndInvoke @0x5ac738 +
//                       VIBE_Light_ApplyToCachedVertices @0x5c6f90 — walks the
//                       scene-graph light list and adds LUT-attenuated dynamic
//                       light into each vertex's rgb[] scratch (needs the live
//                       scene graph + the flt_1405110 attenuation LUT). Called
//                       AFTER the seed, BEFORE the intensity quantize.
// -----------------------------------------------------------------------------
struct VegCacheHooks {
    void (*transformVertices)(VegVertex* verts, u32 count, void* ctx) = nullptr;
    void (*accumulateLights)(VegVertex* verts, u32 count, void* ctx)  = nullptr;
    void* ctx = nullptr;
};

// gilde.exe 0x5c8560 — VIBE_Light_BuildVegetationCache (the host-independent
// shape). Reproduces:
//   1. seed every vertex rgb[] = (200,200,200)   (flt_64A074/78/7C)
//   2. transformVertices(...)                    (hook -> bone-matrix unpack)
//   3. accumulateLights(...)                     (hook -> dynamic light walk)
//   4. quantize each vertex: lit ? ComputeVertexIntensityLit
//                                : ComputeVertexIntensityUnlit  (byte_649D70 gate)
// `lit` is the runtime byte_649D70 (nonzero -> three-channel lit arm).
void BuildVegetationCache(VegVertex* verts, u32 count, bool lit,
                          const VegCacheHooks& hooks);

} // namespace guild::render
