#pragma once
#include "guild/common/types.h"

// =============================================================================
// guild::render — render-math leaves, batch 7 (Shadow / Light / sky-flare).
//
// A seventh slice of self-contained, deterministic render leaves translated 1:1
// from the Hex-Rays reference. The targets are the heavier projection /
// rasterisation / vertex-lighting math that survives after batches 1..6:
//
//   0x5f3048  VIBE_Shadow_BuildGroundShadow   (clip-rect derivation + dispatch)
//   0x5f2a58  VIBE_Shadow_RasterizeHeightField(ground-shadow grid -> vertex pool)
//   0x5f216c  VIBE_Shadow_ProjectGroundQuad   (terrain-tile shadow projection)
//   0x5c6f90  VIBE_Light_ApplyToCachedVertices(point/spot/area vertex lighting)
//   0x5ef19c  VIBE_Render_ComputeSkyFlarePositions (lens-flare grid + indices)
//
// These are pure arithmetic over the live render records and three global
// scratch vertex/index pools. NONE of them issue DDraw/D3D/GDI calls directly.
// The cross-module callees that are NOT reconstructed (the bone-chain transforms
// used to seat the caster, the texture acquire/upload for projected shadows, the
// world-matrix concat) are routed through an installable hooks struct whose
// default implementations are INERT and defined in render_leaves7.cpp — so the
// pure math is golden-testable in isolation and the unified build never sees an
// undefined reference.
//
// Reused reconstructed siblings (NO hook — extern-declared via util/math.h):
//   util::VectorNormalize       (0x5cb148)  — normalise a 3-vector in place
//   util::VectorLerp            (0x5ca2fc)  — component lerp, sky-flare grid
//   util::VectorAngleBetween    (0x5ca334)  — flare sun-angle
//   util::TriangleNormal        (0x5cb824)  — area-light face normal
//   util::VectorWithinTolerance (0x5caa4c)  — caster move epsilon (documented)
//   render::InitFalloffTable    (0x5c88f8 falloff_lut.cpp) — the 1024-entry
//        light-falloff LUT the vertex-lighting kernel indexes.  The original
//        reads flt_1405110[idx], which is flt_140510C[idx+1] (table base +1
//        float); the integration test seeds the REAL table and asserts the
//        cross-module lookup.
//
// Recovered constants (get_bytes; bit-exact):
//   flt_62C26C = 0.5    shadow rasterizer Z bias (added to projected ground Y)
//   flt_62C278 = 0.5    BuildGroundShadow caster-height scale
//   dbl_62C270 = 25.0   height-field edge-discontinuity tolerance
//   dbl_62C134 = 6.28318530718 (2*pi)  sky-flare sun-angle fmod modulus
//   flt_62C13C = 0.2    sky-flare ring step  (1/5; outer loop 6 rings)
//   flt_62C140 = 0.142857149 (1/7) sky-flare segment step (inner loop 8 segs)
//   flt_62C144 = 255.0  sky-flare alpha clamp ceiling
//   flt_628C20 = 10.0   point-light range^2 multiplier (radius -> cull dist^2)
//   flt_628C24 = 20.0   spotlight cone-projection scale
//   flt_628C28 = -1023.0 falloff-LUT index scale: idx = (int)(NdotL * -1023)
//   flt_628C2C = 0.001  area-light intensity scale
//   flt_628C30 = 0.01   spotlight intensity scale
//   flt_5F1D80 = {60.0, 200.0, 255.0} shadow-cache LOD distance thresholds
// =============================================================================
namespace guild::render {

// Recovered float constants (bit-exact; see header note for provenance).
constexpr float  kShadowZBias        = 0.5f;     // flt_62C26C
constexpr float  kCasterHeightScale  = 0.5f;     // flt_62C278
constexpr double kEdgeTolerance      = 25.0;     // dbl_62C270
constexpr double kTwoPi              = 6.283185307179586; // dbl_62C134
constexpr float  kFlareRingStep      = 0.20000000298023224f; // flt_62C13C (1/5)
constexpr float  kFlareSegStep       = 0.1428571492433548f;  // flt_62C140 (1/7)
constexpr float  kFlareAlphaCeil      = 255.0f;  // flt_62C144
constexpr float  kPointRangeMul      = 10.0f;    // flt_628C20
constexpr float  kSpotProjScale      = 20.0f;    // flt_628C24
constexpr float  kFalloffIdxScale    = -1023.0f; // flt_628C28
constexpr float  kAreaIntensity      = 0.0010000000474974513f; // flt_628C2C
constexpr float  kSpotIntensity      = 0.009999999776482582f;  // flt_628C30

// ---------------------------------------------------------------------------
// Cross-module hooks (inert defaults in render_leaves7.cpp). These are the
// non-math callees the heavier shadow/flare paths reach; the deterministic math
// each leaf below performs does NOT depend on them.
// ---------------------------------------------------------------------------
struct RenderLeaves7Hooks {
    // VIBE_Coord_ConvertX (0x5c6b08) round-toward-zero chop used by the caster
    // colour key in BuildGroundShadow. Default: C truncation toward zero.
    i32 (*truncToward)(double v) = nullptr;

    // VIBE_Texture_LoadByName("Schatten", ...) — projected-shadow texture. The
    // ground-shadow path only uses the *cache slot*; texture acquire is inert
    // (returns 0 => caller takes the rasterize path).  Documented for fidelity.
    void* (*loadShadowTexture)(const char* name) = nullptr;
};

void InstallRenderLeaves7Hooks(const RenderLeaves7Hooks& hooks);
const RenderLeaves7Hooks& CurrentRenderLeaves7Hooks();

// ===========================================================================
// 0x5f3048 — VIBE_Shadow_BuildGroundShadow (clip-rect derivation).
//
// Before dispatching to ProjectGroundQuad / RasterizeHeightField the original
// derives, from the shadow-map record (+92 minX, +96 maxX, +100 minY, +104 maxY,
// +16 dimension), a clamped integer sub-rectangle plus four interpolation
// parameters used by the rasterizer to map cell -> [0,1] texture coords.  This
// is the pure arithmetic; we expose it as `ComputeShadowClipRect`.
//
// Returns 1 when a valid (non-empty) rect was produced (the original proceeds to
// rasterize), 0 on any of the early-out rejections (degenerate / out-of-bounds).
//
// Inputs (named per original field offsets, all i32 unless float):
//   dim      *(+16)  shadow-map dimension (pixel extent, exclusive bound)
//   minX     *(+92), maxX *(+96), minY *(+100), maxY *(+104)
// ---------------------------------------------------------------------------
struct ShadowClipRect {
    i32   x0 = 0, x1 = 0, y0 = 0, y1 = 0;   // v51, v49, v14, v50 (clamped)
    float uOff = 0.0f, uStep = 0.0f;        // v45, v42 (X interp start / step)
    float vOff = 0.0f, vStep = 0.0f;        // v43, v39 (Y interp start / step)
    bool  valid = false;
};
i32 ComputeShadowClipRect(i32 dim, i32 minX, i32 maxX, i32 minY, i32 maxY,
                          ShadowClipRect* out);

// ===========================================================================
// 0x5f216c — VIBE_Shadow_ProjectGroundQuad (per-cell world-space projection).
//
// For one terrain tile-cell the original maps the height-map sample at (col,row)
// to a world-space vertex:  x = col*xStep + xBase, z = row*zStep + zBase,
// y = heightSample * yScale + (groundY + 0.5).  We expose the single-vertex
// kernel `ProjectGroundVertex` (the body's innermost arithmetic, verbatim) and
// the per-row X advance so the projection grid is golden-testable without the
// global vertex pool.  `heightSample` is the raw u8 height byte.
// ---------------------------------------------------------------------------
struct GroundVertex { float x, y, z; };
GroundVertex ProjectGroundVertex(i32 col, i32 row, u8 heightSample,
                                 float xBase, float xStep,
                                 float zBase, float zStep,
                                 float yScale, float groundY);

// ===========================================================================
// 0x5f2a58 — VIBE_Shadow_RasterizeHeightField (vertex emit + edge flag).
//
//   vy = (heightSample + bias) * yScale[1] + (a3[1] + bias)
//   vx = col*xStep + xBaseSample,  vz = constant per row.
// The `bias` is 1.5 when byte_64A351 (high-detail flag) else 1.0.  The triangle
// quad emitted also flags an edge-discontinuity bit when neighbouring heights
// differ by > 25.0 (dbl_62C270).  We expose:
//   `HeightFieldBias`     -> the 1.5 / 1.0 selector (highDetail flag).
//   `RasterizeHeightVertexY` -> the Y of one emitted vertex (the verbatim
//        `((double)h + bias) * yStep + (baseY + bias)` arithmetic).
//   `HeightEdgeDiscontinuous` -> |hA-hB| > 25 || |hA-hC| > 25.
// ---------------------------------------------------------------------------
float HeightFieldBias(bool highDetail);
float RasterizeHeightVertexY(u8 heightSample, float bias, float yStep,
                             float baseY);
bool  HeightEdgeDiscontinuous(float hA, float hB, float hC);

// ===========================================================================
// 0x5c6f90 — VIBE_Light_ApplyToCachedVertices (per-vertex lighting kernel).
//
// The function walks the object's cached vertices and accumulates, per light, a
// colour contribution.  Three light flavours share one attenuation kernel:
//
//   point/omni (the default branch):
//     d2  = |vertexPos - lightPos|^2
//     if (d2 < range2):
//        atten = (1/(falloffDenom * d2)) * intensity
//        the omni path applies `atten` straight to the light RGB.
//   spot (class 535==1) and area (triangle-normal) branches add a directional
//     term:   NdotL = dot(normal, lightDir);  if (NdotL < 0):
//        idx = (int)(NdotL * -1023.0);  scale = intensity * falloffLUT[idx];
//        contribution = lightRGB * scale.
//
// We expose the two deterministic kernels exactly as the original computes them:
//   `PointLightAttenuation` — d2 < range2 ? (1/(denom*d2))*intensity : 0.
//   `DirectionalFalloffScale` — NdotL<0 ? intensity*LUT[(int)(NdotL*-1023)] : 0,
//        taking the resolved LUT pointer (so the test seeds the real table).
// `range2` is the squared cull radius (= lightRange^2, the original's v105),
// `falloffDenom` = lightRange^3-ish term (v104, the stored +38 field).
// ---------------------------------------------------------------------------
float PointLightAttenuation(const float vertexPos[3], const float lightPos[3],
                            float range2, float falloffDenom, float intensity);

// NdotL<0 -> intensity * lut[(int)(NdotL * -1023.0)]; else 0.  `lut` must point
// at flt_1405110 (== falloff table base + 1 float); see header note.
float DirectionalFalloffScale(float NdotL, float intensity, const float* lut);

// ===========================================================================
// 0x5ef19c — VIBE_Render_ComputeSkyFlarePositions (lens-flare grid + indices).
//
// Builds a 6x8 grid of flare vertices by bilinearly lerping the four projected
// sky-quad corners (top/bottom/left/right edges), normalising each, computing a
// per-vertex alpha from the depth term, then emitting two-triangle index runs
// for grid cells whose 4 corners are all visible.  The matrix transforms route
// through util::Vector* siblings.  We expose the deterministic pieces:
//
//   `FlareRingT` / `FlareSegT` — the ring/segment interpolation parameters
//        (i*0.2 for ring 0..5; i*(1/7) for segment 0..7), verbatim.
//   `FlareSunAngle` — VectorAngleBetween(up, viewDir) then fmod(.,2*pi).
//   `FlareVertexAlpha` — the per-vertex alpha: if outside the near band
//        (depth<0 || depth>=255) the vertex is fully transparent path; else
//        alpha = clamp((flareDepth - bias) * scale, 0, 255) (the v57/v17 term).
//   `ComputeFlareGrid` — the full 6x8 lerp+normalise into a caller buffer
//        (48 vertices), driving the real util siblings.
// ---------------------------------------------------------------------------
float  FlareRingT(i32 ring);
float  FlareSegT(i32 seg);
double FlareSunAngle(const float up[3], const float viewDir[3]);

// Per-vertex alpha (verbatim 0x5ef4df..0x5ef545).  `band` is v48 = (depth -
// nearBias)*nearScale.  The original: if (band < 0 || 255 >= band) alpha =
// max(0, rawAlpha); else alpha = 255 (the band > 255 "behind near plane" path).
// `rawAlpha` is v57 = (cellFlareDepth - bias)*scale.
float FlareVertexAlpha(float band, float rawAlpha);

// Fills `outGrid` (>= 48 GroundVertex) with the bilinearly-interpolated and
// normalised flare grid corners.  The four projected edge vectors are the
// original's v30/v31/v32/v33 (each 3 floats).  For each ring r in [0,6) and
// segment s in [0,8):
//   rowA = lerp(c30, c32, r*0.2);  rowB = lerp(c31, c33, r*0.2);
//   outGrid[r*8 + s] = normalize( lerp(rowA, rowB, s/7) ).
void ComputeFlareGrid(const float c30[3], const float c31[3],
                      const float c32[3], const float c33[3],
                      GroundVertex outGrid[48]);

} // namespace guild::render
