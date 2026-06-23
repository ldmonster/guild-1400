#include "render/render_leaves7.h"

#include "util/math.h"

#include <cmath>

namespace guild::render {
namespace {

// ----- inert default hook implementations (defined IN the library) ----------

// VIBE_Coord_ConvertX rounds toward zero (x87 chop). The inert default is the
// plain C truncation toward zero, which matches the integer-part the original
// reads back off the FP register for the in-range caster colour key.
i32 DefaultTruncToward(double v) { return static_cast<i32>(v); }

// VIBE_Texture_LoadByName("Schatten", ...) is the projected-shadow texture
// acquire. Returning null forces BuildGroundShadow's rasterize path (the math
// we translate), exactly as a cache-miss would; this keeps the leaf inert.
void* DefaultLoadShadowTexture(const char* /*name*/) { return nullptr; }

RenderLeaves7Hooks g_hooks = {
    &DefaultTruncToward, &DefaultLoadShadowTexture,
};

} // namespace

void InstallRenderLeaves7Hooks(const RenderLeaves7Hooks& h) {
    g_hooks.truncToward       = h.truncToward       ? h.truncToward       : &DefaultTruncToward;
    g_hooks.loadShadowTexture = h.loadShadowTexture ? h.loadShadowTexture : &DefaultLoadShadowTexture;
}

const RenderLeaves7Hooks& CurrentRenderLeaves7Hooks() { return g_hooks; }

// ===========================================================================
// 0x5f3048 — VIBE_Shadow_BuildGroundShadow (clip-rect derivation).
//
// Faithful translation of the 0x5f3104..0x5f31e2 block.  The early-out tests
// reject a degenerate / fully-off-map shadow footprint; the surviving rect is
// clamped to [0,dim) and the four interpolation parameters are derived.
// Variable names mirror the decompile: v51=x0, v49=x1, v14=y0, v50=y1,
// v45=uOff, v44=uStep, v43=vOff, v39=vStep.
// ===========================================================================
i32 ComputeShadowClipRect(i32 dim, i32 minX, i32 maxX, i32 minY, i32 maxY,
                          ShadowClipRect* out) {
    if (out) out->valid = false;

    // 0x5f310a..0x5f3147 — early-out rejections (verbatim order).
    if (dim <= minX)        return 0;   // 0x5f310a
    if (maxX < 0)           return 0;   // 0x5f3115
    if (dim <= minY)        return 0;   // 0x5f311e
    if (maxY < 0)           return 0;   // 0x5f3129
    if (maxX - minX <= 0)   return 0;   // 0x5f3138
    if (maxY - minY <= 0)   return 0;   // 0x5f3147

    // 0x5f314d.. — derive the interpolation params, then clamp the rect.
    i32   v51   = minX;                       // x0
    i32   v49   = maxX;                       // x1
    i32   v14   = minY;                       // y0
    i32   v50   = maxY;                       // y1
    float v40   = static_cast<float>(v49 - v51);
    // HARDEN: v44 = 1.0 / v40 is computed 80-bit (fld1; fdiv [float v40] @0x5f316e)
    // then rounded to float; model as (float)(1.0/(double)v40), not float division.
    float v44   = static_cast<float>(1.0 / static_cast<double>(v40)); // uStep
    float v39   = static_cast<float>(1.0 / static_cast<double>(maxY - minY)); // vStep
    float v43   = 0.0f;                       // vOff
    float v45   = 0.0f;                       // uOff

    if (v51 < 0) {                            // 0x5f31a2
        v45 = static_cast<float>(static_cast<double>(-v51) * v44);
        v51 = 0;
    }
    if (v49 >= dim) v49 = dim - 1;            // 0x5f31b1

    if (v14 < 0) {                            // 0x5f31bc
        v43 = static_cast<float>(static_cast<double>(-v14)
                                 / static_cast<double>(v50 - v14));
        v14 = 0;
    }
    if (v50 >= dim) v50 = dim - 1;            // 0x5f31cb

    if (out) {
        out->x0 = v51; out->x1 = v49;
        out->y0 = v14; out->y1 = v50;
        out->uOff = v45; out->uStep = v44;
        out->vOff = v43; out->vStep = v39;
        out->valid = true;
    }
    return 1;
}

// ===========================================================================
// 0x5f216c — VIBE_Shadow_ProjectGroundQuad (per-cell world-space projection).
//
// Inner kernel (0x5f2495..0x5f2565):
//   v51 = (double)col * xStep + xBase;          // -> vertex X
//   i   = (double)row * zStep + zBase;          // -> vertex Z
//   v15 = (double)heightSample * yScale;        // height contribution
//   y   = v15 + (groundY + 0.5);                // 0.5 == flt_62C26C (kShadowZBias)
// (the original stores X=v70, Z=i, Y=v15+v37 where v37 = groundY + 0.5).
// ===========================================================================
GroundVertex ProjectGroundVertex(i32 col, i32 row, u8 heightSample,
                                 float xBase, float xStep,
                                 float zBase, float zStep,
                                 float yScale, float groundY) {
    GroundVertex v;
    v.x = static_cast<float>(static_cast<double>(col) * xStep + xBase);
    v.z = static_cast<float>(static_cast<double>(row) * zStep + zBase);
    // HARDEN: v15 = height*yScale stays 80-bit on the x87 stack (fmul @0x5f2547)
    // and is added to v37 (the precomputed float groundY+0.5) before the single
    // fstp @0x5f2565. Keep the product as double; round once. The prior code
    // rounded height*yScale to float first, double-rounding the Y.
    double h = static_cast<double>(heightSample) * yScale;       // v15 (80-bit)
    float bias = groundY + kShadowZBias;                          // v37 (float)
    v.y = static_cast<float>(h + bias);                           // 0x5f255c/0x5f2565
    return v;
}

// ===========================================================================
// 0x5f2a58 — VIBE_Shadow_RasterizeHeightField (vertex emit + edge flag).
// ===========================================================================
float HeightFieldBias(bool highDetail) {
    // 0x5f2b09: byte_64A351 ? 1.5 : 1.0
    return highDetail ? 1.5f : 1.0f;
}

float RasterizeHeightVertexY(u8 heightSample, float bias, float yStep,
                             float baseY) {
    // 0x5f2bfb: ((double)*v49 + v32) * a5[1] + v35,  v35 = a3[1] + bias.
    return static_cast<float>(
        (static_cast<double>(heightSample) + bias) * yStep + (baseY + bias));
}

bool HeightEdgeDiscontinuous(float hA, float hB, float hC) {
    // 0x5f2dd5: fabs(hA-hB) > 25.0 || fabs(hA-hC) > 25.0
    return std::fabs(hA - hB) > kEdgeTolerance
        || std::fabs(hA - hC) > kEdgeTolerance;
}

// ===========================================================================
// 0x5c6f90 — VIBE_Light_ApplyToCachedVertices (per-vertex lighting kernels).
// ===========================================================================
float PointLightAttenuation(const float vertexPos[3], const float lightPos[3],
                            float range2, float falloffDenom, float intensity) {
    // 0x5c74c0..0x5c757a (the omni branch):
    //   v75 = *v38 - lightPos[0]; ...
    //   v40 = v75*v75 + v76*v76 + v77*v77;
    //   if (v40 < v105/*range2*/) v89 = 1.0/(v104/*denom*/ * v40) * v103/*intensity*/;
    float dx = vertexPos[0] - lightPos[0];
    float dy = vertexPos[1] - lightPos[1];
    float dz = vertexPos[2] - lightPos[2];
    float d2 = dx * dx + dy * dy + dz * dz;
    if (d2 < range2) {
        return static_cast<float>(1.0 / (static_cast<double>(falloffDenom) * d2)
                                  * intensity);
    }
    return 0.0f;
}

float DirectionalFalloffScale(float NdotL, float intensity, const float* lut) {
    // 0x5c73e4..0x5c7440 (spot/area branch):
    //   if (v33 < 0.0) { v84 = (int)(v33 * flt_628C28/*-1023*/);
    //                    scale = intensity * flt_1405110[v84]; }
    if (NdotL < 0.0f) {
        int idx = static_cast<int>(NdotL * kFalloffIdxScale);
        return intensity * lut[idx];
    }
    return 0.0f;
}

// ===========================================================================
// 0x5ef19c — VIBE_Render_ComputeSkyFlarePositions (lens-flare grid).
// ===========================================================================
float FlareRingT(i32 ring) {
    // 0x5ef40b: (double)v55 * flt_62C13C (0.2)
    return static_cast<float>(static_cast<double>(ring) * kFlareRingStep);
}

float FlareSegT(i32 seg) {
    // 0x5ef4a5: (double)SLODWORD(v11) * flt_62C140 (1/7)
    return static_cast<float>(static_cast<double>(seg) * kFlareSegStep);
}

double FlareSunAngle(const float up[3], const float viewDir[3]) {
    // 0x5ef222: VectorAngleBetween(&flt_5CA2B0/*up=(0,0,1)*/, viewDir)
    // 0x5ef22f: Fmod(angle, dbl_62C134/*2*pi*/)
    double angle = util::VectorAngleBetween(const_cast<float*>(up),
                                            const_cast<float*>(viewDir));
    return std::fmod(angle, kTwoPi);
}

float FlareVertexAlpha(float band, float rawAlpha) {
    // 0x5ef77e: if (v48 < 0.0 || flt_62C144/*255*/ >= v48) {
    //   0x5ef528: v17 = v57/*rawAlpha*/ >= 0.0 ? v57 : 0.0; alpha = v17;
    // } else { 0x5ef784: alpha = 255.0; }
    if (band < 0.0f || kFlareAlphaCeil >= band) {
        return rawAlpha >= 0.0f ? rawAlpha : 0.0f;
    }
    return kFlareAlphaCeil;
}

void ComputeFlareGrid(const float c30[3], const float c31[3],
                      const float c32[3], const float c33[3],
                      GroundVertex outGrid[48]) {
    // 0x5ef3bf..0x5ef5c8 — 6 rings x 8 segments.
    float a[3], b[3], c[3], d[3];
    a[0] = c30[0]; a[1] = c30[1]; a[2] = c30[2];
    b[0] = c31[0]; b[1] = c31[1]; b[2] = c31[2];
    c[0] = c32[0]; c[1] = c32[1]; c[2] = c32[2];
    d[0] = c33[0]; d[1] = c33[1]; d[2] = c33[2];

    for (i32 ring = 0; ring < 6; ++ring) {
        float ringT = FlareRingT(ring);
        float rowA[4], rowB[4];                       // VectorLerp writes 3, pad
        // 0x5ef419: VectorLerp(v30, v32, ringT, rowA)
        util::VectorLerp(a, c, ringT, rowA);
        // 0x5ef44a: VectorLerp(v31, v33, ringT, rowB)
        util::VectorLerp(b, d, ringT, rowB);
        for (i32 seg = 0; seg < 8; ++seg) {
            float segT = FlareSegT(seg);
            float v[4];
            // 0x5ef4a8: VectorLerp(rowA, rowB, segT, v)
            util::VectorLerp(rowA, rowB, segT, v);
            // 0x5ef4af: VectorNormalize(v)
            util::VectorNormalize(v);
            GroundVertex& g = outGrid[ring * 8 + seg];
            g.x = v[0]; g.y = v[1]; g.z = v[2];
        }
    }
}

} // namespace guild::render
