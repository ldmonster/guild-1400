#pragma once
#include "guild/common/types.h"

// =============================================================================
// guild::render — per-vertex OBJECT-LIGHT SHADE kernels (the self-contained math
// of VIBE_Light_BuildObjectCache @0x5c8218 + VIBE_Light_ApplyVertexShading
// @0x5c7f04). 1:1 reconstruction of the shade-FINALIZATION math, with the exact
// constants recovered via get_bytes.
//
// THE CACHE (BuildObjectCache). Each frame the engine seeds every vertex's RGB
// light accumulator (the float triple at vertex +48/+52/+56) to the ambient term
//   flt_64A074 = flt_64A078 = flt_64A07C = 200.0
// then walks the scene's affected lights (VIBE_Light_ApplyToCachedVertices
// @0x5c6f90) accumulating each light's diffuse contribution into that triple, then
// FINALIZES the triple to the per-vertex shade bytes:
//   * software branch (byte_649D70): normalise so max(R,G,B) <= 255 (scale by
//     255/max when it overflows) and store the B/G/R bytes at vertex +68/+69/+70;
//   * hardware branch: reduce to a single luma byte
//     luma = G*0.59 + R*0.30 + B*0.11, clamped to 255, at vertex +70.
// VIBE_Light_ApplyVertexShading then modulates per material:
//   shade = (R*0.30 + G*0.59 + B*0.11 + texHi) * (1/256) * texLo, clamped to 255.
//
// SCOPE (rule 8). The per-light ACCUMULATION (VIBE_Light_ApplyToCachedVertices)
// indexes a runtime-built shade-ramp LUT (flt_1405110, all-zero in the static
// image — assembled at engine init) and walks the live scene's light objects
// (point / sun / per-poly), so it needs that LUT builder + the live light list and
// is the NAMED boundary, not reconstructed here. These finalize kernels are the
// self-contained, exact-constant math the cache produces once the accumulator is
// filled — golden-testable in isolation (the same pattern as render/vertex_lighting).
//
// Recovered constants (get_bytes):
//   flt_64A074/78/7C = 0x43480000 = 200.0   (ambient seed, per channel)
//   flt_628C88 = 0x3f170a3d = 0.59  flt_628C8C = 0x3e99999a = 0.30
//   flt_628C90 = 0x3de147ae = 0.11  flt_628C94 = 0x437f0000 = 255.0  (BuildObjectCache luma+cap)
//   flt_628C64 = 0.30  flt_628C68 = 0.59  flt_628C6C = 0.11
//   flt_628C70 = 0x3b800000 = 1/256        flt_628C74 = 255.0          (ApplyVertexShading)
// =============================================================================
namespace guild::render {

inline constexpr float kVertexLightAmbient = 200.0f;  // flt_64A074/78/7C (seed per channel)
inline constexpr float kLightIntensityScale = 10.0f;  // flt_628C4C (IlluminateObject intensity)
inline constexpr float kRampIndexScale = -1023.0f;    // flt_628C50 / flt_628C28 (LUT index = NdotL * this)
inline constexpr float kLumaR = 0.30f;                // flt_628C8C / flt_628C64
inline constexpr float kLumaG = 0.59f;                // flt_628C88 / flt_628C68
inline constexpr float kLumaB = 0.11f;                // flt_628C90 / flt_628C6C
inline constexpr float kLightNormCap = 255.0f;        // flt_628C94 / flt_628C74
inline constexpr float kMaterialShadeScale = 1.0f / 256.0f;  // flt_628C70

// The finalized per-vertex B/G/R shade bytes (vertex +68/+69/+70 in the engine).
struct ShadeBytes { u8 b, g, r; };

// gilde.exe 0x5c8218 (software finalize loop) — normalise the accumulated RGB so
// max(R,G,B) <= 255 (scale by 255/max on overflow), then truncate to B/G/R bytes.
ShadeBytes FinalizeVertexShadeSoftware(float r, float g, float b);

// gilde.exe 0x5c8218 (hardware finalize loop) — luma reduce + clamp to a byte:
//   luma = G*0.59 + R*0.30 + B*0.11 ; if >= 255 -> 255.
u8 FinalizeVertexShadeLuma(float r, float g, float b);

// gilde.exe 0x5c7f04 — VIBE_Light_ApplyVertexShading per-vertex modulation kernel:
//   shade = (R*0.30 + G*0.59 + B*0.11 + texHi) * (1/256) * texLo ; clamp to 255.
// `texHi`/`texLo` are the texture record +108 word's high/low bytes (the engine's
// HIBYTE(v18)/LOBYTE(v18)); for an opaque untextured surface pass texHi=0, texLo=255.
u8 ApplyMaterialVertexShade(float r, float g, float b, u8 texHi, u8 texLo);

// One light's diffuse RGB contribution for one vertex.
struct LightRgb { float r, g, b; };

// gilde.exe 0x5c7804 (VIBE_Light_IlluminateObject per-vertex inner loop) — the
// point-light diffuse term for one vertex, reconstructed 1:1:
//   d      = vpos - lpos ;  dist2 = d.d ;  if (dist2 >= range2) -> {0,0,0}
//   nd     = normalize(d) ;  NdotL = vnormal . nd ;  if (NdotL >= 0) -> {0,0,0}
//   idx    = (int)(NdotL * -1023.0)            (flt_628C50; idx in [0,1023])
//   atten  = 1/(falloff*dist2) * intensityScaled
//   factor = atten * ramp[idx]
//   rgb    = lcolor * factor
// `ramp`/`rampLen` is the engine's shade-ramp LUT (flt_1405110). NOTE (rule 8): that
// LUT is all-zero in the static image (a runtime-built .bss table with no writer among
// its xref sites), so with the live engine ramp this term evaluates to ZERO and object
// shading is AMBIENT-DOMINANT — which is what the universe driver renders. The formula
// is reconstructed exactly and parameterised on the ramp so it is golden-testable and
// ready for the day the ramp builder is recovered. `intensityScaled` =
// lightIntensity * objScale * 10.0 (flt_628C4C, folded by the caller or via the helper).
LightRgb PointLightDiffuse(const float vpos[3], const float vnormal[3],
                           const float lpos[3], const float lcolor[3],
                           float range2, float falloff, float intensityScaled,
                           const float* ramp, int rampLen);

// =============================================================================
// THE CONSOLIDATED PER-MESH VERTEX-LIGHTING ENTRY (the brief's clean entry).
//
// gilde.exe 0x5c8218 VIBE_Light_BuildObjectCache + 0x5c6f90
// VIBE_Light_ApplyToCachedVertices, reconstructed as ONE faithful flow so the
// universe frame can light an object's vertices from the sun + scene point
// lights before the object draw. It is the in-engine sequence:
//
//   1. seed every vertex's RGB accumulator (the engine's vertex +48/+52/+56 in
//      IlluminateObject, or the cache +12/+13/+14 in ApplyToCachedVertices)
//      from the GLOBAL AMBIENT (flt_64A074/78/7C = 200 each by default — here the
//      caller supplies `ambient`, which play::ComputeSceneAmbient fills from the
//      day/night band rig);
//   2. walk the scene LIGHT LIST (VIBE_Light_ApplyToCachedVertices):
//        * POINT lights (object type != 7) -> AccumulatePointLight;
//        * the DIRECTIONAL "sun" (object type == 7) -> AccumulateDirectionalLight
//          (the per-vertex sun branch @0x5c7129 uses flt_628C30 = 0.01 as its
//          intensity scale, NOT the per-poly 0.001 branch);
//      both index the angular falloff LUT (VIBE_Light_InitFalloffTable @0x5c88f8,
//      render::BuildFalloffLUT) by (int)(NdotL * -1023);
//   3. FINALIZE the accumulator to the 0..255 vertex shade (the software colour
//      branch: hue-preserving max-channel normalise to 255 — the exact
//      FinalizeVertexShadeSoftware math @0x5c8218).
//
// Inputs:
//   verts/count : per-vertex world position (vpos) + smoothed world normal
//                 (vnormal); the engine's per-vertex source after the world xform.
//   ambient     : the seed RGB (the global ambient store; 200,200,200 default).
//   sunDir/sunColor/sunIntensity : the directional sun (W6-SUN's sun vector); the
//                 sun is OMITTED when sunIntensity <= 0 (the engine's
//                 `0.0 == light+148` early-out in CollectAffectedObject).
//   pointLights : the scene's point lights (each: world pos, RGB colour, range,
//                 intensity, rangeParam == the stored +38 falloff denom field).
//   objScale    : the per-object marker scale folded into every light's
//                 attenuation (the engine's *(obj+492)+2296 object intensity).
//   lut         : the 1024-entry falloff LUT (BuildFalloffLUT). Required.
// Writes each vertex's finalized B/G/R shade bytes to `outShade[i]`.
// =============================================================================
struct MeshLightVertex {
    float vpos[3];     // world position (engine vertex +0/+4/+8 after xform)
    float vnormal[3];  // smoothed world normal (engine vertex +32/+36/.. source)
};
struct MeshPointLight {
    float pos[3];      // light world position (light +118/+119/+120)
    float color[3];    // light RGB (light +23/+24/+25 == bytes +92/+96/+100)
    float range;       // cull radius (range^2 is the v105 gate)
    float intensity;   // light +37 (== +148 word) intensity
    float rangeParam;  // light +38 (the stored falloff denom, v104)
};

// gilde.exe 0x5c8218/0x5c6f90 — light a mesh's vertices (sun + point lights),
// finalize to per-vertex shade bytes. `sunIntensity <= 0` disables the sun.
//
// NOTE on the normal: this overload takes the vertex normal already in the same
// space as `sunDir` (the bind site supplies the smoothed WORLD normal + world-space
// sun). The engine's per-vertex sun arm @0x5c7129 instead fetches the OBJECT-space
// normal and transforms it by the per-object bone-world matrix before the NdotL —
// see LightMeshVertices (the bone-matrix overload) below for the faithful transform.
void LightMeshVertices(const MeshLightVertex* verts, int count,
                       const float ambient[3],
                       const float sunDir[3], const float sunColor[3],
                       float sunIntensity,
                       const MeshPointLight* pointLights, int pointCount,
                       float objScale, const float lut[1024],
                       ShadeBytes* outShade);

// The per-vertex sun intensity scale of the type-7 vertex branch @0x5c7129.
inline constexpr float kSunVertexIntensityScale = 0.009999999776482582f; // flt_628C30

// flt_628C24 = 0x41A00000 = 20.0 — the per-vertex sun-normal Y pre-bias scale of the
// type-7 vertex branch (the `v99 = flt_628C24 / bone[+0x1D4]` divide @0x5c70a7).
inline constexpr float kSunNormalYScale = 20.0f; // flt_628C24

// =============================================================================
// GAP-1 (wave-7) — the REAL per-vertex normal source/transform of the type-7 sun
// arm @0x5c7129..0x5c7232. Reconstructed 1:1 from the disasm:
//
//   bone     = *(object+16)            the object's bone/transform record
//   M[16]    = VIBE_Transform_ComputeBoneWorldMatrix(object, ..., 1)   (out @ var_E0)
//   boneY    = bone[+0x6C]   (= *(float*)(bone+108))   matrix-record Y term  (v98)
//   sy       = flt_628C24(20.0) / bone[+0x1D4]         (= +468)              (v99)
//
//   for each cache vertex:
//     n  = *(float(*)[3])(vertex+0x48)     a POINTER to the object-space normal
//     ny = (n.y - boneY) * sy                                   <-- the Y pre-bias
//     t.x = n.x*M[0] + ny*M[4] + n.z*M[8]                       (4-stride 3x3 rotate,
//     t.y = n.x*M[1] + ny*M[5] + n.z*M[9]                        cols 0..2 of rows 0..2,
//     t.z = n.x*M[2] + ny*M[6] + n.z*M[10]                       same layout as `world`)
//     VIBE_Math_VectorNormalize(&t)
//     NdotL = t . sunDir            (sunDir = *(*(light+488)+392/396/400))
//
// This is the named gap ("the directional NdotL is the named gap"): the normal is
// NOT the world normal — it is the object-space normal, Y-rebiased by the bone
// translation/scale, rotated by the bone-world matrix, then renormalized.
//
// Transforms one object-space vertex normal into the lit (bone-world) space the sun
// NdotL is taken in, writing the renormalized result to `outN[3]`. `m16` is the
// bone-world matrix in the engine's flat 16-float / 4-stride layout (same as
// TransformPointByWorldMatrix's `world`); only the 3x3 rotate (m[0,4,8 / 1,5,9 /
// 2,6,10]) is used — a normal carries no translation.
void TransformVertexLightingNormal(const float n[3], const float m16[16],
                                   float boneY, float boneScale, float outN[3]);
// =============================================================================

// gilde.exe 0x5c6f90 (type-7 vertex arm @0x5c7129) — light a mesh's vertices from
// the sun (and point lights) using the FAITHFUL per-vertex normal transform above:
// each vertex's OBJECT-space normal is bone-world-transformed + renormalized before
// the sun NdotL. Identical to LightMeshVertices except for the sun's normal source.
//
//   verts[i].vnormal : the OBJECT-space normal (the engine's *(vertex+0x48) source).
//   boneMatrix       : the bone-world matrix (ComputeBoneWorldMatrix output, var_E0).
//   boneY/boneScale  : bone[+0x6C] / bone[+0x1D4] (the Y pre-bias terms v98/v99).
// The point-light arm and the ambient seed/finalize are identical to LightMeshVertices
// (the engine's point arm dots the cache normal directly; only the sun arm rebuilds
// the normal). `sunIntensity <= 0` disables the sun (ambient-only path unchanged).
void LightMeshVertices(const MeshLightVertex* verts, int count,
                       const float ambient[3],
                       const float sunDir[3], const float sunColor[3],
                       float sunIntensity,
                       const MeshPointLight* pointLights, int pointCount,
                       float objScale, const float lut[1024],
                       const float boneMatrix[16], float boneY, float boneScale,
                       ShadeBytes* outShade);

} // namespace guild::render
