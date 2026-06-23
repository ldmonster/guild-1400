#include "render/object_light_shade.h"

#include "render/light.h"   // AccumulatePointLight (the per-light walk kernels)

#include <cmath>   // std::lrint — the bare-fistp round-nearest-even convention

// =============================================================================
// guild::render — per-vertex object-light shade finalize kernels. See the header.
// Each kernel is a verbatim translation of the corresponding inner loop body
// (BuildObjectCache @0x5c8218 / ApplyVertexShading @0x5c7f04).
//
// ROUNDING (verified disasm @0x5c83e0/0x5c83f1/0x5c8402, 0x5c8506, 0x5c8017):
// EVERY float->byte store in these kernels is a BARE `fistp` (no preceding ConvertX
// frndint, no cvtt truncate). Under the default x87/SSE control word that is
// round-to-NEAREST-EVEN, NOT truncate-toward-zero. std::lrint honours the FE
// rounding mode (round-to-nearest-even by default), matching `fistp` 1:1.
// =============================================================================
namespace guild::render {

// One bare-fistp store: round-to-nearest-even, then the engine keeps the LOW BYTE
// (mov al, byte ptr [...]) — i.e. (u8) of the rounded int.
static inline int FistpRound(float v) { return static_cast<int>(std::lrint(v)); }

// gilde.exe 0x5c8218 — software finalize: max-channel normalise to 255, B/G/R bytes.
ShadeBytes FinalizeVertexShadeSoftware(float r, float g, float b) {
    // v13 = max(R, G) ; v31 = max(v13, B)  (the original's two cmp/branch pairs).
    float mx = (r <= g) ? g : r;
    if (mx <= b) mx = b;
    if (mx > kLightNormCap) {                 // v14 > 255.0f (bit compare on positive floats)
        const float s = kLightNormCap / mx;   // flt_628C94 / max
        r *= s; g *= s; b *= s;
    }
    ShadeBytes out;
    out.r = (u8)FistpRound(r);   // vertex +70 (bare fistp @0x5c83e0)
    out.g = (u8)FistpRound(g);   // vertex +69 (bare fistp @0x5c83f1)
    out.b = (u8)FistpRound(b);   // vertex +68 (bare fistp @0x5c8402)
    return out;
}

// gilde.exe 0x5c8218 — hardware finalize: luma reduce + clamp to a byte.
u8 FinalizeVertexShadeLuma(float r, float g, float b) {
    // v22 accumulated in x87 80-bit, then a BARE fistp (@0x5c8506) => round-nearest.
    const int l = FistpRound(g * kLumaG + r * kLumaR + b * kLumaB);
    // if ((unsigned)(int)v22 >= 0xFF) -> 255 ; else (int)v22 (cmp ebp,0FFh / jnb).
    return ((unsigned)l >= 0xFFu) ? (u8)255 : (u8)l;
}

// gilde.exe 0x5c7f04 — VIBE_Light_ApplyVertexShading per-vertex modulation.
u8 ApplyMaterialVertexShade(float r, float g, float b, u8 texHi, u8 texLo) {
    float v = (r * kLumaR + g * kLumaG + b * kLumaB + (float)texHi)
              * kMaterialShadeScale * (float)texLo;
    // Upper clamp ONLY (the disasm has no lower 0.0 clamp): fcomp flt_628C74 / jnb.
    if (kLightNormCap < v)            // if (255.0 >= v) keep v else v = 255.0
        v = kLightNormCap;
    // Store is a BARE fistp (@0x5c8017) => round-nearest-even, then low byte kept.
    return (u8)FistpRound(v);
}

// gilde.exe 0x5c7804 — VIBE_Light_IlluminateObject per-vertex point-light diffuse.
LightRgb PointLightDiffuse(const float vpos[3], const float vnormal[3],
                           const float lpos[3], const float lcolor[3],
                           float range2, float falloff, float intensityScaled,
                           const float* ramp, int rampLen) {
    LightRgb out{0.0f, 0.0f, 0.0f};
    const float dx = vpos[0] - lpos[0];
    const float dy = vpos[1] - lpos[1];
    const float dz = vpos[2] - lpos[2];
    const float dist2 = dx * dx + dy * dy + dz * dz;   // v37 / v66 (kept pre-normalize)
    if (dist2 >= range2)                                // gate: if (v37 < v61) ... else skip
        return out;

    const float len = std::sqrt(dist2);                // VIBE_Math_VectorNormalize
    if (len <= 0.0f)
        return out;
    const float inv = 1.0f / len;
    const float ndx = dx * inv, ndy = dy * inv, ndz = dz * inv;
    const float NdotL = vnormal[0] * ndx + vnormal[1] * ndy + vnormal[2] * ndz;  // v38
    if (NdotL >= 0.0f)                                  // gate: if (v38 < 0.0)
        return out;

    // (int)(v38 * flt_628C28) is a BARE fistp @0x5c7427 => round-nearest-even, NOT
    // truncate. The engine does NOT clamp (unit normals keep it in [0,1023]); the
    // bounds guard below is a memory-safety net that never fires for valid input.
    int idx = FistpRound(NdotL * kRampIndexScale);     // fistp(v38 * -1023.0)
    if (idx < 0) idx = 0;
    if (rampLen > 0 && idx >= rampLen) idx = rampLen - 1;

    const float atten = 1.0f / (falloff * dist2) * intensityScaled;  // 1/(v60*v66)*v59
    const float rampV = (ramp && rampLen > 0) ? ramp[idx] : 0.0f;
    const float factor = atten * rampV;                // v46 = v66 * flt_1405110[idx]
    out.r = lcolor[0] * factor;                        // vertex +48 = light+92 * factor
    out.g = lcolor[1] * factor;                        // vertex +52 = light+96 * factor
    out.b = lcolor[2] * factor;                        // vertex +56 = light+100 * factor
    return out;
}

// =============================================================================
// gilde.exe 0x5c8218 + 0x5c6f90 — the consolidated per-mesh vertex-lighting flow.
// seed ambient -> walk lights (ApplyToCachedVertices) -> finalize shade.
// =============================================================================
namespace {
// flt_628C28 / flt_628C50 = -1023.0: the falloff LUT index scale. The engine
// indexes the type-7 sun branch (@0x5c7241) with (int)(NdotL * flt_628C28).
inline int SunFalloffIndex(float ndotl) {
    // (int)(v62 * flt_628C28) is a BARE fistp @0x5c7241 => round-nearest-even.
    // The engine does not clamp (unit normals keep it in [0,1023]); the bounds
    // guard below is a memory-safety net that does not fire for valid input.
    int i = static_cast<int>(std::lrint(ndotl * -1023.0f));
    if (i < 0) i = 0; else if (i > 1023) i = 1023;
    return i;
}
} // namespace

void LightMeshVertices(const MeshLightVertex* verts, int count,
                       const float ambient[3],
                       const float sunDir[3], const float sunColor[3],
                       float sunIntensity,
                       const MeshPointLight* pointLights, int pointCount,
                       float objScale, const float lut[1024],
                       ShadeBytes* outShade) {
    if (!verts || count <= 0 || !outShade || !lut)
        return;

    // The engine omits the sun entirely when its intensity is 0 (the
    // `0.0 == light+148` early-out in VIBE_Light_CollectAffectedObject @0x5c80f8).
    const bool hasSun = (sunDir && sunColor && sunIntensity > 0.0f);
    // Normalize the sun direction once (the per-poly/vertex branches normalize the
    // direction vector before the NdotL dot; the sun's stored dir need not be unit).
    float sx = 0.0f, sy = 0.0f, sz = 0.0f;
    if (hasSun) {
        const float l = std::sqrt(sunDir[0]*sunDir[0] + sunDir[1]*sunDir[1] + sunDir[2]*sunDir[2]);
        if (l > 1e-6f) { sx = sunDir[0]/l; sy = sunDir[1]/l; sz = sunDir[2]/l; }
    }

    for (int i = 0; i < count; ++i) {
        const MeshLightVertex& mv = verts[i];
        // 1. seed the accumulator from the global ambient (flt_64A074/78/7C).
        float acc[3] = {ambient[0], ambient[1], ambient[2]};

        // 2a. point lights (object type != 7) — the ApplyToCachedVertices point arm.
        // (Memory-safety guard: the engine never walks a null light list, so a null
        // `pointLights` means an empty list — skip it rather than deref. The
        // in-bounds path with a real list is byte-identical.)
        if (pointLights) {
            for (int p = 0; p < pointCount; ++p) {
                const MeshPointLight& L = pointLights[p];
                AccumulatePointLight(mv.vpos, mv.vnormal, L.pos, L.color, L.range,
                                     L.intensity, L.rangeParam, objScale, lut, acc);
            }
        }

        // 2b. the directional sun (object type == 7) — the per-vertex sun branch
        //     @0x5c7129..0x5c7297: factor = intensity * 0.01 (flt_628C30) * objScale
        //     * LUT[(int)(NdotL * -1023)] ; accum += color * factor, when NdotL < 0.
        if (hasSun) {
            const float ndotl = mv.vnormal[0]*sx + mv.vnormal[1]*sy + mv.vnormal[2]*sz;
            if (ndotl < 0.0f) {                                  // if (v19 < 0.0)
                const float f = sunIntensity * kSunVertexIntensityScale * objScale
                              * lut[SunFalloffIndex(ndotl)];     // v100 * flt_1405110[idx]
                acc[0] += sunColor[0] * f;
                acc[1] += sunColor[1] * f;
                acc[2] += sunColor[2] * f;
            }
        }

        // 3. finalize to per-vertex shade bytes (software colour branch @0x5c8218:
        //    hue-preserving max-channel normalise to 255, then truncate to B/G/R).
        outShade[i] = FinalizeVertexShadeSoftware(acc[0], acc[1], acc[2]);
    }
}

// =============================================================================
// GAP-1 — the per-vertex sun normal transform of the type-7 vertex arm @0x5c7129.
//   ny  = (n.y - boneY) * (flt_628C24 / boneScale)         (the Y pre-bias)
//   t   = M3x3 * (n.x, ny, n.z)   (4-stride rotate: m[0,4,8 / 1,5,9 / 2,6,10])
//   normalize(t)                  (VIBE_Math_VectorNormalize: zero-length -> 0)
// The disasm reads the object-space normal as floats off a vertex pointer
// (mov edx,[ecx+48h]; the three components at +0/+4/+8) and forms ny BEFORE the
// matrix multiply (fsub var_38; fmul var_34). The matrix-vector multiply uses the
// SAME 0x10-stride layout as TransformPointByWorldMatrix (no translation row).
// =============================================================================
void TransformVertexLightingNormal(const float n[3], const float m16[16],
                                   float boneY, float boneScale, float outN[3]) {
    // v99 = flt_628C24 / bone[+0x1D4]  (a reciprocal-scale applied only to Y).
    const float sy = (boneScale != 0.0f) ? (kSunNormalYScale / boneScale) : 0.0f;
    const float nx = n[0];
    const float ny = (n[1] - boneY) * sy;   // (n.y - v98) * v99
    const float nz = n[2];

    // 4-stride 3x3 rotate (cols 0..2 of rows 0..2), the @0x5c71b5 fmul chain.
    float t[3];
    t[0] = nx * m16[0] + ny * m16[4] + nz * m16[8];   // v81
    t[1] = nx * m16[1] + ny * m16[5] + nz * m16[9];   // v82
    t[2] = nx * m16[2] + ny * m16[6] + nz * m16[10];  // v83

    // VIBE_Math_VectorNormalize @0x5cb148 — zero-length collapses to (0,0,0).
    const float len2 = t[0]*t[0] + t[1]*t[1] + t[2]*t[2];
    if (len2 != 0.0f) {
        const float inv = 1.0f / std::sqrt(len2);
        outN[0] = t[0] * inv; outN[1] = t[1] * inv; outN[2] = t[2] * inv;
    } else {
        outN[0] = 0.0f; outN[1] = 0.0f; outN[2] = 0.0f;
    }
}

// =============================================================================
// gilde.exe 0x5c6f90 (type-7 vertex arm @0x5c7129) — the bone-matrix overload.
// Identical to LightMeshVertices, but the sun NdotL is taken against the FAITHFUL
// per-vertex transformed normal (object-space normal -> bone-world rotate ->
// renormalize) instead of a pre-supplied world normal. Ambient seed, point arm,
// and finalize are byte-for-byte the same; ambient-only path is unchanged.
// =============================================================================
void LightMeshVertices(const MeshLightVertex* verts, int count,
                       const float ambient[3],
                       const float sunDir[3], const float sunColor[3],
                       float sunIntensity,
                       const MeshPointLight* pointLights, int pointCount,
                       float objScale, const float lut[1024],
                       const float boneMatrix[16], float boneY, float boneScale,
                       ShadeBytes* outShade) {
    if (!verts || count <= 0 || !outShade || !lut)
        return;

    // Sun omitted when its intensity is 0 (CollectAffectedObject @0x5c80f8 early-out)
    // OR when no bone matrix is supplied (the type-7 vertex arm needs the transform).
    const bool hasSun = (sunDir && sunColor && sunIntensity > 0.0f && boneMatrix != nullptr);
    // The sun's stored direction (*(light+488)+392..400) is used as-is in the dot;
    // the original does NOT renormalize the sun dir in the vertex arm (only the
    // transformed vertex normal is normalized). Use it verbatim.
    const float sx = hasSun ? sunDir[0] : 0.0f;
    const float sy = hasSun ? sunDir[1] : 0.0f;
    const float sz = hasSun ? sunDir[2] : 0.0f;

    for (int i = 0; i < count; ++i) {
        const MeshLightVertex& mv = verts[i];
        // 1. seed the accumulator from the global ambient (flt_64A074/78/7C).
        float acc[3] = {ambient[0], ambient[1], ambient[2]};

        // 2a. point lights — the ApplyToCachedVertices point arm (cache normal).
        // (Memory-safety guard: a null `pointLights` == empty list; see overload above.)
        if (pointLights) {
            for (int p = 0; p < pointCount; ++p) {
                const MeshPointLight& L = pointLights[p];
                AccumulatePointLight(mv.vpos, mv.vnormal, L.pos, L.color, L.range,
                                     L.intensity, L.rangeParam, objScale, lut, acc);
            }
        }

        // 2b. the directional sun (type 7), per-vertex arm @0x5c7129: transform the
        //     object-space normal by the bone-world matrix + renormalize, THEN dot.
        if (hasSun) {
            float tn[3];
            TransformVertexLightingNormal(mv.vnormal, boneMatrix, boneY, boneScale, tn);
            const float ndotl = tn[0]*sx + tn[1]*sy + tn[2]*sz;   // v19
            if (ndotl < 0.0f) {                                    // if (v19 < 0.0)
                const float f = sunIntensity * kSunVertexIntensityScale * objScale
                              * lut[SunFalloffIndex(ndotl)];       // v100 * flt_1405110[idx]
                acc[0] += sunColor[0] * f;
                acc[1] += sunColor[1] * f;
                acc[2] += sunColor[2] * f;
            }
        }

        // 3. finalize (software colour branch @0x5c8218).
        outShade[i] = FinalizeVertexShadeSoftware(acc[0], acc[1], acc[2]);
    }
}

} // namespace guild::render
