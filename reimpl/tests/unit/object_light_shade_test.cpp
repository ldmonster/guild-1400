#include "render/object_light_shade.h"
#include "render/light.h"   // BuildFalloffLUT, Accumulate* (cross-check the walk)

#include "tests/framework/test.h"

#include <cmath>
#include <vector>

// =============================================================================
// ObjectLightShade — golden tests for the per-vertex object-light shade FINALIZE
// kernels (VIBE_Light_BuildObjectCache @0x5c8218 / ApplyVertexShading @0x5c7f04),
// against the exact recovered constants (ambient 200; luma 0.30/0.59/0.11; cap 255;
// material scale 1/256). No assets.
// =============================================================================
using namespace guild;
using guild::render::FinalizeVertexShadeSoftware;
using guild::render::FinalizeVertexShadeLuma;
using guild::render::ApplyMaterialVertexShade;
using guild::render::ShadeBytes;
using guild::render::kVertexLightAmbient;

// (a) Ambient seed (200,200,200): below the cap -> B/G/R bytes all 200.
TEST(ObjectLightShade, SoftwareAmbientPassThrough) {
    ShadeBytes s = FinalizeVertexShadeSoftware(kVertexLightAmbient, kVertexLightAmbient,
                                               kVertexLightAmbient);
    CHECK_EQ((int)s.r, 200);
    CHECK_EQ((int)s.g, 200);
    CHECK_EQ((int)s.b, 200);
}

// (b) Software normalise: max(R,G,B) > 255 -> scale all by 255/max, then ROUND
// (bare fistp @0x5c83e0, round-to-nearest-even).
TEST(ObjectLightShade, SoftwareNormalizeOnOverflow) {
    // R=300 G=100 B=50, max=300 -> s = 255/300 -> (255, 85, 42.5->42 [even]).
    ShadeBytes s = FinalizeVertexShadeSoftware(300.0f, 100.0f, 50.0f);
    CHECK_EQ((int)s.r, 255);
    CHECK_EQ((int)s.g, 85);
    CHECK_EQ((int)s.b, 42);   // 42.5 rounds to even -> 42
    // A triple exactly at the cap is NOT scaled (strict >): (255, 10, 0).
    ShadeBytes t = FinalizeVertexShadeSoftware(255.0f, 10.0f, 0.0f);
    CHECK_EQ((int)t.r, 255);
    CHECK_EQ((int)t.g, 10);
    CHECK_EQ((int)t.b, 0);
}

// (c) Luma finalize: G*0.59 + R*0.30 + B*0.11, clamped to 255. The weights are the
// imprecise 32-bit floats (0.59f = 0.5899999…) and the store is a BARE fistp
// (@0x5c8506) => round-to-nearest-even — golden values are the rounded float results.
TEST(ObjectLightShade, LumaReduceAndClamp) {
    // Ambient (200,200,200): 200*(0.30+0.59+0.11) == 200.
    CHECK_EQ((int)FinalizeVertexShadeLuma(200.0f, 200.0f, 200.0f), 200);
    // Single-channel weights (one multiply each): green 200*0.59f=117.99…→118 (round),
    // red 200*0.30f=60, blue 200*0.11f=22.0→22.
    CHECK_EQ((int)FinalizeVertexShadeLuma(0.0f, 200.0f, 0.0f), 118);
    CHECK_EQ((int)FinalizeVertexShadeLuma(200.0f, 0.0f, 0.0f), 60);
    CHECK_EQ((int)FinalizeVertexShadeLuma(0.0f, 0.0f, 200.0f), 22);
    // Overflow clamps to 255.
    CHECK_EQ((int)FinalizeVertexShadeLuma(1000.0f, 1000.0f, 1000.0f), 255);
}

// (d) Material modulation: (R*0.30+G*0.59+B*0.11 + texHi) * (1/256) * texLo, clamp 255,
// then ROUND (bare fistp @0x5c8017). No lower 0 clamp in the binary.
TEST(ObjectLightShade, MaterialModulation) {
    // Untextured opaque (texHi=0, texLo=255): 200 * (1/256) * 255 = 199.2 -> 199.
    CHECK_EQ((int)ApplyMaterialVertexShade(200.0f, 200.0f, 200.0f, /*texHi*/0, /*texLo*/255), 199);
    // texLo=128 halves: 200 * (1/256) * 128 = 100.
    CHECK_EQ((int)ApplyMaterialVertexShade(200.0f, 200.0f, 200.0f, 0, 128), 100);
    // texHi additive brightness: (200 + 55) * (1/256) * 255 = 254.0 -> 254.
    CHECK_EQ((int)ApplyMaterialVertexShade(200.0f, 200.0f, 200.0f, /*texHi*/55, /*texLo*/255), 254);
    // Saturates to 255: huge luma -> clamp.
    CHECK_EQ((int)ApplyMaterialVertexShade(5000.0f, 5000.0f, 5000.0f, 255, 255), 255);
}

// (e) Point-light diffuse (IlluminateObject inner loop): range gate, backface gate,
// NdotL ramp index, attenuation. Uses a synthetic linear ramp (ramp[i] = i/1023) so
// the formula is exercised; with the engine's all-zero runtime ramp the term is 0.
TEST(ObjectLightShade, PointLightDiffuseKernel) {
    using guild::render::PointLightDiffuse;
    using guild::render::LightRgb;
    auto feq = [](float a, float b) { return std::fabs(a - b) <= 1e-4f; };

    std::vector<float> ramp(1024);
    for (int i = 0; i < 1024; ++i) ramp[i] = (float)i / 1023.0f;   // ramp[1023] = 1.0

    const float lpos[3]   = {0.0f, 0.0f, 0.0f};
    const float lcolor[3] = {1.0f, 1.0f, 1.0f};

    // Vertex at +z, normal facing the light (-z): d=(0,0,2)->nd=(0,0,1); NdotL=-1 ->
    // idx 1023, ramp=1.0; atten = 1/(falloff*dist2)*intensityScaled = 1/(1*4)*4 = 1 ->
    // factor 1.0 -> rgb (1,1,1).
    const float vpos[3]   = {0.0f, 0.0f, 2.0f};
    const float nFace[3]  = {0.0f, 0.0f, -1.0f};
    LightRgb lit = PointLightDiffuse(vpos, nFace, lpos, lcolor, /*range2*/100.0f,
                                     /*falloff*/1.0f, /*intensityScaled*/4.0f,
                                     ramp.data(), (int)ramp.size());
    CHECK(feq(lit.r, 1.0f)); CHECK(feq(lit.g, 1.0f)); CHECK(feq(lit.b, 1.0f));

    // Out of range (dist2=4 >= range2=1) -> {0,0,0}.
    LightRgb oor = PointLightDiffuse(vpos, nFace, lpos, lcolor, 1.0f, 1.0f, 4.0f,
                                     ramp.data(), (int)ramp.size());
    CHECK(feq(oor.r, 0.0f)); CHECK(feq(oor.g, 0.0f)); CHECK(feq(oor.b, 0.0f));

    // Back-facing (normal away from light, NdotL >= 0) -> {0,0,0}.
    const float nAway[3] = {0.0f, 0.0f, 1.0f};
    LightRgb bf = PointLightDiffuse(vpos, nAway, lpos, lcolor, 100.0f, 1.0f, 4.0f,
                                    ramp.data(), (int)ramp.size());
    CHECK(feq(bf.r, 0.0f)); CHECK(feq(bf.g, 0.0f)); CHECK(feq(bf.b, 0.0f));

    // The ENGINE state: an all-zero ramp -> the diffuse term is exactly 0 (ambient
    // dominant), regardless of geometry.
    std::vector<float> zeroRamp(1024, 0.0f);
    LightRgb z = PointLightDiffuse(vpos, nFace, lpos, lcolor, 100.0f, 1.0f, 4.0f,
                                   zeroRamp.data(), (int)zeroRamp.size());
    CHECK(feq(z.r, 0.0f)); CHECK(feq(z.g, 0.0f)); CHECK(feq(z.b, 0.0f));
}

// =============================================================================
// LightMeshVertices — the consolidated per-mesh entry (seed ambient -> walk the
// sun + point lights -> finalize to shade bytes). BuildObjectCache @0x5c8218 +
// ApplyToCachedVertices @0x5c6f90.
// =============================================================================
using guild::render::LightMeshVertices;
using guild::render::MeshLightVertex;
using guild::render::MeshPointLight;
using guild::render::BuildFalloffLUT;
using guild::render::kSunVertexIntensityScale;

// (f) Ambient-only (no lights): every vertex finalizes to the ambient shade.
TEST(ObjectLightShade, MeshAmbientOnly) {
    std::vector<float> lut(1024);
    BuildFalloffLUT(lut.data());
    MeshLightVertex v{{0, 0, 0}, {0, 0, 1}};
    ShadeBytes out{};
    const float amb[3] = {200.0f, 200.0f, 200.0f};
    // No sun (intensity 0), no point lights -> ambient passes through unchanged.
    LightMeshVertices(&v, 1, amb, nullptr, nullptr, 0.0f, nullptr, 0, 1.0f,
                      lut.data(), &out);
    CHECK_EQ((int)out.r, 200);
    CHECK_EQ((int)out.g, 200);
    CHECK_EQ((int)out.b, 200);
}

// (g) Sun gating: a back-facing vertex (NdotL >= 0) gets NO sun contribution;
// a facing vertex adds the verbatim sun term and finalizes identically to a
// hand-rolled accumulate.
TEST(ObjectLightShade, MeshSunBranchMatchesKernel) {
    std::vector<float> lut(1024);
    BuildFalloffLUT(lut.data());
    const float amb[3] = {50.0f, 50.0f, 50.0f};
    const float sunDir[3]   = {0.0f, 0.0f, 1.0f};      // already unit
    const float sunColor[3] = {255.0f, 200.0f, 100.0f};
    const float sunI = 30.0f, objScale = 1.0f;

    // Back-facing: normal aligned with sunDir -> NdotL = +1 >= 0 -> ambient only.
    MeshLightVertex away{{0, 0, 0}, {0, 0, 1}};
    ShadeBytes oA{};
    LightMeshVertices(&away, 1, amb, sunDir, sunColor, sunI, nullptr, 0, objScale,
                      lut.data(), &oA);
    // ambient (50,50,50) finalize: max=50 < 255 -> bytes 50.
    CHECK_EQ((int)oA.r, 50); CHECK_EQ((int)oA.g, 50); CHECK_EQ((int)oA.b, 50);

    // Facing: normal opposite sunDir -> NdotL = -1 -> idx 1023, full LUT term.
    MeshLightVertex face{{0, 0, 0}, {0, 0, -1}};
    ShadeBytes oF{};
    LightMeshVertices(&face, 1, amb, sunDir, sunColor, sunI, nullptr, 0, objScale,
                      lut.data(), &oF);

    // Hand-roll the exact same accumulate + finalize as a cross-check.
    const float ndotl = -1.0f;
    int idx = (int)(ndotl * -1023.0f); if (idx > 1023) idx = 1023;
    const float f = sunI * kSunVertexIntensityScale * objScale * lut[idx];
    float acc[3] = {amb[0] + sunColor[0]*f, amb[1] + sunColor[1]*f, amb[2] + sunColor[2]*f};
    ShadeBytes ref = guild::render::FinalizeVertexShadeSoftware(acc[0], acc[1], acc[2]);
    CHECK_EQ((int)oF.r, (int)ref.r);
    CHECK_EQ((int)oF.g, (int)ref.g);
    CHECK_EQ((int)oF.b, (int)ref.b);
    // The facing vertex must be brighter than ambient-only (sun added light).
    CHECK((int)oF.r >= 50);
}

// (h) Point light + sun together: the walk sums BOTH contributions into the
// accumulator before the finalize, matching AccumulatePointLight + the sun term.
TEST(ObjectLightShade, MeshPointPlusSun) {
    std::vector<float> lut(1024);
    BuildFalloffLUT(lut.data());
    const float amb[3] = {20.0f, 20.0f, 20.0f};
    const float sunDir[3]   = {0.0f, 0.0f, 1.0f};
    const float sunColor[3] = {100.0f, 100.0f, 100.0f};

    MeshPointLight pl{};
    pl.pos[0] = 0; pl.pos[1] = 0; pl.pos[2] = -2.0f;   // light below the vertex
    pl.color[0] = pl.color[1] = pl.color[2] = 255.0f;
    pl.range = 10.0f; pl.intensity = 5.0f; pl.rangeParam = 1.0f;

    MeshLightVertex v{{0, 0, 0}, {0, 0, -1}};           // faces both sun and point
    ShadeBytes out{};
    LightMeshVertices(&v, 1, amb, sunDir, sunColor, 30.0f, &pl, 1, 1.0f,
                      lut.data(), &out);

    // Hand-roll: ambient + AccumulatePointLight + sun term, then finalize.
    float acc[3] = {amb[0], amb[1], amb[2]};
    guild::render::AccumulatePointLight(v.vpos, v.vnormal, pl.pos, pl.color, pl.range,
                                        pl.intensity, pl.rangeParam, 1.0f, lut.data(), acc);
    int idx = 1023;
    const float f = 30.0f * kSunVertexIntensityScale * 1.0f * lut[idx];
    acc[0] += sunColor[0]*f; acc[1] += sunColor[1]*f; acc[2] += sunColor[2]*f;
    ShadeBytes ref = guild::render::FinalizeVertexShadeSoftware(acc[0], acc[1], acc[2]);
    CHECK_EQ((int)out.r, (int)ref.r);
    CHECK_EQ((int)out.g, (int)ref.g);
    CHECK_EQ((int)out.b, (int)ref.b);
}

// =============================================================================
// GAP-1 (wave-7) — the REAL per-vertex sun normal transform (type-7 vertex arm
// @0x5c7129): object-space normal -> Y pre-bias -> bone-world rotate -> renormalize
// -> sun NdotL. VIBE_Light_ApplyToCachedVertices @0x5c6f90.
// =============================================================================
using guild::render::TransformVertexLightingNormal;
using guild::render::kSunNormalYScale;

// Identity-matrix 4-stride 3x3 in the engine's flat 16-float layout.
static void IdentityMat16(float m[16]) {
    for (int i = 0; i < 16; ++i) m[i] = 0.0f;
    m[0] = m[5] = m[10] = m[15] = 1.0f;
}

// (i) Normal transform — identity matrix, no Y pre-bias: pass-through + normalize.
TEST(ObjectLightShade, NormalTransformIdentity) {
    auto feq = [](float a, float b) { return std::fabs(a - b) <= 1e-5f; };
    float m[16]; IdentityMat16(m);
    // boneY=0, boneScale=20 -> sy = 20/20 = 1.0 (Y unchanged), identity rotate.
    float n[3] = {0.0f, 0.0f, -2.0f};       // un-normalized -z
    float out[3];
    TransformVertexLightingNormal(n, m, /*boneY*/0.0f, /*boneScale*/kSunNormalYScale, out);
    // renormalized -> (0,0,-1).
    CHECK(feq(out[0], 0.0f)); CHECK(feq(out[1], 0.0f)); CHECK(feq(out[2], -1.0f));
}

// (j) Normal transform — Y pre-bias scales the Y component by 20/boneScale, then a
// 90-degree rotation about X maps +Y -> +Z (verifies the 4-stride matrix layout).
TEST(ObjectLightShade, NormalTransformYBiasAndRotate) {
    auto feq = [](float a, float b) { return std::fabs(a - b) <= 1e-5f; };
    // Rotation about X by +90 deg in the engine's flat layout (cols 0,4,8 / 1,5,9 / 2,6,10):
    //   row0 = (1,0,0), row1 = (0,0,1), row2 = (0,-1,0)  ->  t = (nx, -nz, ny)
    float m[16] = {0};
    m[0] = 1.0f;                 // m[0,4,8]  -> x' = nx
    m[5] = 0.0f; m[9] = 1.0f;    // m[1,5,9]  -> y' = nz
    m[6] = -1.0f; m[10] = 0.0f;  // m[2,6,10] -> z' = -ny
    m[15] = 1.0f;
    // boneScale=10 -> sy = 20/10 = 2.0; boneY=1 -> ny=(n.y-1)*2.
    float n[3] = {0.0f, 2.0f, 0.0f};    // n.y -> (2-1)*2 = 2 ; nx=nz=0
    // pre-bias gives (0,2,0); rotate -> x'=0, y'=nz=0, z'=-ny=-2 ; normalize -> (0,0,-1).
    float out[3];
    TransformVertexLightingNormal(n, m, /*boneY*/1.0f, /*boneScale*/10.0f, out);
    CHECK(feq(out[0], 0.0f)); CHECK(feq(out[1], 0.0f)); CHECK(feq(out[2], -1.0f));
}

// (k) Zero-length transformed normal collapses to (0,0,0) (VIBE_Math_VectorNormalize).
TEST(ObjectLightShade, NormalTransformZeroCollapses) {
    float m[16]; IdentityMat16(m);
    float n[3] = {0.0f, 0.0f, 0.0f};
    float out[3] = {9, 9, 9};
    TransformVertexLightingNormal(n, m, 0.0f, kSunNormalYScale, out);
    CHECK_EQ((int)out[0], 0); CHECK_EQ((int)out[1], 0); CHECK_EQ((int)out[2], 0);
}

// (l) The bone-matrix sun overload — a lit (transformed normal faces the sun) vs
// unlit (faces away) vertex. Mirrors the type-7 vertex arm end-to-end.
TEST(ObjectLightShade, MeshSunBoneMatrixLitVsUnlit) {
    std::vector<float> lut(1024);
    BuildFalloffLUT(lut.data());
    const float amb[3] = {50.0f, 50.0f, 50.0f};
    const float sunDir[3]   = {0.0f, 0.0f, 1.0f};      // sun pointing +z
    const float sunColor[3] = {255.0f, 200.0f, 100.0f};
    const float sunI = 30.0f, objScale = 1.0f;
    float m[16]; IdentityMat16(m);

    // LIT: object-space normal -z; identity transform, boneScale=20 (sy=1) -> tn=-z,
    // NdotL = (-z).(+z) = -1 < 0 -> sun contributes.
    MeshLightVertex lit{{0, 0, 0}, {0.0f, 0.0f, -1.0f}};
    ShadeBytes oLit{};
    LightMeshVertices(&lit, 1, amb, sunDir, sunColor, sunI, nullptr, 0, objScale,
                      lut.data(), m, /*boneY*/0.0f, /*boneScale*/kSunNormalYScale, &oLit);

    // UNLIT: normal +z -> tn=+z, NdotL=+1 >= 0 -> ambient only (50,50,50).
    MeshLightVertex unlit{{0, 0, 0}, {0.0f, 0.0f, 1.0f}};
    ShadeBytes oUn{};
    LightMeshVertices(&unlit, 1, amb, sunDir, sunColor, sunI, nullptr, 0, objScale,
                      lut.data(), m, 0.0f, kSunNormalYScale, &oUn);
    CHECK_EQ((int)oUn.r, 50); CHECK_EQ((int)oUn.g, 50); CHECK_EQ((int)oUn.b, 50);

    // Hand-roll the lit reference using the SAME transform + sun term + finalize.
    float tn[3];
    TransformVertexLightingNormal(lit.vnormal, m, 0.0f, kSunNormalYScale, tn);
    const float ndotl = tn[0]*sunDir[0] + tn[1]*sunDir[1] + tn[2]*sunDir[2];
    CHECK(ndotl < 0.0f);
    int idx = (int)(ndotl * -1023.0f); if (idx > 1023) idx = 1023; if (idx < 0) idx = 0;
    const float f = sunI * kSunVertexIntensityScale * objScale * lut[idx];
    float acc[3] = {amb[0] + sunColor[0]*f, amb[1] + sunColor[1]*f, amb[2] + sunColor[2]*f};
    ShadeBytes ref = guild::render::FinalizeVertexShadeSoftware(acc[0], acc[1], acc[2]);
    CHECK_EQ((int)oLit.r, (int)ref.r);
    CHECK_EQ((int)oLit.g, (int)ref.g);
    CHECK_EQ((int)oLit.b, (int)ref.b);
    // Lit must be brighter than unlit (the sun added light to a facing vertex).
    CHECK((int)oLit.r >= (int)oUn.r);
}

// (m) Sun disabled when no bone matrix is supplied (the type-7 vertex arm needs the
// transform) — falls back to ambient-only, identical to the no-sun path.
TEST(ObjectLightShade, MeshSunBoneMatrixNullDisablesSun) {
    std::vector<float> lut(1024);
    BuildFalloffLUT(lut.data());
    const float amb[3] = {77.0f, 77.0f, 77.0f};
    const float sunDir[3]   = {0.0f, 0.0f, 1.0f};
    const float sunColor[3] = {255.0f, 255.0f, 255.0f};
    MeshLightVertex v{{0, 0, 0}, {0.0f, 0.0f, -1.0f}};   // would be lit if sun ran
    ShadeBytes out{};
    LightMeshVertices(&v, 1, amb, sunDir, sunColor, 99.0f, nullptr, 0, 1.0f,
                      lut.data(), /*boneMatrix*/nullptr, 0.0f, kSunNormalYScale, &out);
    CHECK_EQ((int)out.r, 77); CHECK_EQ((int)out.g, 77); CHECK_EQ((int)out.b, 77);
}

// =============================================================================
// WAVE-10 HARDENING — degenerate / edge / capacity-boundary coverage (ASAN+UBSAN).
// =============================================================================

// (W10-a) LightMeshVertices with 0 vertices / 0 lights / a null light list: no
// reads off any buffer, no writes to outShade. Exercises the light-list-walk guard
// (pointCount>0 with a NULL pointLists must NOT deref — faithful empty-list skip).
TEST(ObjectLightShadeHarden, MeshZeroVertsZeroLightsNullList) {
    std::vector<float> lut(1024);
    BuildFalloffLUT(lut.data());
    const float amb[3] = {123.0f, 45.0f, 67.0f};
    // count == 0: returns immediately, outShade untouched.
    ShadeBytes sentinel{0xAA, 0xBB, 0xCC};
    ShadeBytes out = sentinel;
    LightMeshVertices(nullptr, 0, amb, nullptr, nullptr, 0.0f, nullptr, 0, 1.0f,
                      lut.data(), &out);
    CHECK_EQ((int)out.r, 0xCC); CHECK_EQ((int)out.g, 0xBB); CHECK_EQ((int)out.b, 0xAA);
    // pointCount > 0 but pointLists == null: must be treated as an empty list, NOT a
    // deref. (Before the guard this was an OOB/null read under ASAN.) -> ambient only.
    MeshLightVertex v{{0, 0, 0}, {0, 0, -1}};
    LightMeshVertices(&v, 1, amb, nullptr, nullptr, 0.0f, /*pointLights*/nullptr,
                      /*pointCount*/4, 1.0f, lut.data(), &out);
    CHECK_EQ((int)out.r, 123); CHECK_EQ((int)out.g, 45); CHECK_EQ((int)out.b, 67);
    // Same for the bone-matrix overload.
    float m[16]; IdentityMat16(m);
    LightMeshVertices(&v, 1, amb, nullptr, nullptr, 0.0f, /*pointLights*/nullptr,
                      /*pointCount*/3, 1.0f, lut.data(), m, 0.0f, kSunNormalYScale, &out);
    CHECK_EQ((int)out.r, 123); CHECK_EQ((int)out.g, 45); CHECK_EQ((int)out.b, 67);
}

// (W10-b) A large point-light list (max-ish) fully walked under ASAN: every entry is
// read in-bounds, the accumulator stays finite, the finalize clamps to a byte.
TEST(ObjectLightShadeHarden, MeshManyPointLightsInBounds) {
    std::vector<float> lut(1024);
    BuildFalloffLUT(lut.data());
    const float amb[3] = {10.0f, 10.0f, 10.0f};
    const int N = 256;
    std::vector<MeshPointLight> lights(N);
    for (int i = 0; i < N; ++i) {
        lights[i].pos[0] = 0; lights[i].pos[1] = 0; lights[i].pos[2] = -2.0f;
        lights[i].color[0] = lights[i].color[1] = lights[i].color[2] = 255.0f;
        lights[i].range = 10.0f; lights[i].intensity = 1.0f; lights[i].rangeParam = 1.0f;
    }
    MeshLightVertex v{{0, 0, 0}, {0, 0, -1}};
    ShadeBytes out{};
    LightMeshVertices(&v, 1, amb, nullptr, nullptr, 0.0f, lights.data(), N, 1.0f,
                      lut.data(), &out);
    // With the engine's all-zero runtime ramp the contribution is 0, but a built LUT
    // gives real light; either way the byte stays a valid 0..255 value (no overflow).
    CHECK((int)out.r >= 0 && (int)out.r <= 255);
}

// (W10-c) A zero-length object-space normal renormalizes to (0,0,0) in the sun arm
// (VIBE_Math_VectorNormalize zero-collapse); the dot is 0 -> not < 0 -> ambient only.
TEST(ObjectLightShadeHarden, MeshSunZeroNormal) {
    std::vector<float> lut(1024);
    BuildFalloffLUT(lut.data());
    const float amb[3] = {60.0f, 60.0f, 60.0f};
    const float sunDir[3]   = {0.0f, 0.0f, 1.0f};
    const float sunColor[3] = {255.0f, 255.0f, 255.0f};
    float m[16]; IdentityMat16(m);
    MeshLightVertex v{{0, 0, 0}, {0.0f, 0.0f, 0.0f}};   // zero normal
    ShadeBytes out{};
    LightMeshVertices(&v, 1, amb, sunDir, sunColor, 30.0f, nullptr, 0, 1.0f,
                      lut.data(), m, 0.0f, kSunNormalYScale, &out);
    CHECK_EQ((int)out.r, 60); CHECK_EQ((int)out.g, 60); CHECK_EQ((int)out.b, 60);
}

// (W10-d) A zero-vector sun direction in the WORLD-normal overload: the sun dir is
// normalized once; a zero dir collapses to (0,0,0) (l <= 1e-6 guard) -> NdotL 0 ->
// not < 0 -> ambient only. No NaN, no OOB.
TEST(ObjectLightShadeHarden, MeshSunZeroDir) {
    std::vector<float> lut(1024);
    BuildFalloffLUT(lut.data());
    const float amb[3] = {88.0f, 88.0f, 88.0f};
    const float sunDir[3]   = {0.0f, 0.0f, 0.0f};       // degenerate
    const float sunColor[3] = {255.0f, 255.0f, 255.0f};
    MeshLightVertex v{{0, 0, 0}, {0, 0, -1}};
    ShadeBytes out{};
    LightMeshVertices(&v, 1, amb, sunDir, sunColor, 30.0f, nullptr, 0, 1.0f,
                      lut.data(), &out);
    CHECK_EQ((int)out.r, 88); CHECK_EQ((int)out.g, 88); CHECK_EQ((int)out.b, 88);
}

// (W10-e) Falloff LUT index bounds — the consumer index (int)(NdotL * -1023) must
// stay within [0,1023] for the whole valid NdotL range [-1,0] AND clamp for out-of-
// domain NdotL (a non-unit normal can push it past -1). Exercises AccumulatePointLight
// (which indexes lut[FalloffIndex(ndotl)]) at the extreme so any OOB index trips ASAN.
TEST(ObjectLightShadeHarden, FalloffIndexBoundsAtExtremes) {
    using guild::render::AccumulatePointLight;
    std::vector<float> lut(1024, 1.0f);
    // Over-unit normal (length 2) facing the light -> NdotL == -2 -> (int)(-2*-1023)
    // == 2046, which MUST be clamped to 1023 (else lut[2046] is a hard OOB read).
    const float vpos[3] = {0, 0, 0}, lpos[3] = {0, 0, 10}, col[3] = {1, 1, 1};
    const float vnLong[3] = {0, 0, -2.0f};   // |n| = 2 (deliberately not unit)
    float acc[3] = {0, 0, 0};
    AccumulatePointLight(vpos, vnLong, lpos, col, /*range*/20, /*intensity*/1,
                         /*rangeParam*/1, /*objScale*/1, lut.data(), acc);
    // No crash == the index was clamped. With lut==1 everywhere the term is positive.
    CHECK(acc[0] >= 0.0f);
    // Grazing (NdotL just below 0) -> index 0; facing exactly -1 -> index 1023.
    const float vn1[3] = {0, 0, -1.0f};
    float acc2[3] = {0, 0, 0};
    AccumulatePointLight(vpos, vn1, lpos, col, 20, 1, 1, 1, lut.data(), acc2);
    CHECK(acc2[0] >= 0.0f);
}

// (W10-f) The bone-matrix overload with an IDENTITY matrix and a DEGENERATE (all-
// zero) matrix. Identity: object-space normal passes through (+ Y pre-bias). Zero
// matrix: the transformed normal is (0,0,0) -> renormalize collapses -> ambient only.
TEST(ObjectLightShadeHarden, BoneMatrixIdentityAndDegenerate) {
    std::vector<float> lut(1024);
    BuildFalloffLUT(lut.data());
    const float amb[3] = {40.0f, 40.0f, 40.0f};
    const float sunDir[3]   = {0.0f, 0.0f, 1.0f};
    const float sunColor[3] = {255.0f, 255.0f, 255.0f};

    // Identity matrix, facing normal -> lit (>= ambient).
    float mi[16]; IdentityMat16(mi);
    MeshLightVertex v{{0, 0, 0}, {0, 0, -1.0f}};
    ShadeBytes oId{};
    LightMeshVertices(&v, 1, amb, sunDir, sunColor, 30.0f, nullptr, 0, 1.0f,
                      lut.data(), mi, 0.0f, kSunNormalYScale, &oId);
    CHECK((int)oId.r >= 40);

    // All-zero (degenerate) matrix -> transformed normal (0,0,0) -> ambient only.
    float mz[16]; for (int i = 0; i < 16; ++i) mz[i] = 0.0f;
    ShadeBytes oZ{};
    LightMeshVertices(&v, 1, amb, sunDir, sunColor, 30.0f, nullptr, 0, 1.0f,
                      lut.data(), mz, 0.0f, kSunNormalYScale, &oZ);
    CHECK_EQ((int)oZ.r, 40); CHECK_EQ((int)oZ.g, 40); CHECK_EQ((int)oZ.b, 40);

    // Degenerate boneScale == 0: sy folds to 0 (the (boneScale != 0) guard) -> Y
    // component zeroed, no divide-by-zero (UBSAN clean).
    float tn[3];
    TransformVertexLightingNormal(v.vnormal, mi, /*boneY*/5.0f, /*boneScale*/0.0f, tn);
    CHECK(std::fabs(tn[2] - (-1.0f)) <= 1e-5f);  // x=z*0 path: nx=0,ny=0,nz=-1 -> (0,0,-1)
}

// =============================================================================
// WAVE-13 1:1 CONSTANT PINNING — the exact recovered get_bytes constants the
// per-vertex object-light kernels carry MUST equal their documented values
// (vertex-lighting-wave6.md / vertex-lighting-normals-wave7.md). These pin the
// raw constants directly (the earlier suites use them but never assert the values);
// every value here traces to the progress doc's get_bytes recovery — none invented.
// =============================================================================
using guild::render::kRampIndexScale;
using guild::render::kLightIntensityScale;
using guild::render::kLumaR;
using guild::render::kLumaG;
using guild::render::kLumaB;
using guild::render::kLightNormCap;
using guild::render::kMaterialShadeScale;

// (W13-a) The ambient seed flt_64A074/78/7C == 200.0 (0x43480000), per channel.
TEST(ObjectLightShade, ConstAmbientSeed200) {
    CHECK_EQ(kVertexLightAmbient, 200.0f);
    // The byte pattern (LE float32) is exactly 0x43480000.
    union { float f; unsigned u; } c; c.f = kVertexLightAmbient;
    CHECK_EQ(c.u, 0x43480000u);
}

// (W13-b) The per-VERTEX sun intensity scale flt_628C30 == 0.01 (0x3c23d70a) — and
// it is NOT the per-POLY 0.001 (flt_628C2C, 0x3a83126f) the OTHER sun arm uses.
// The vertex arm @0x5c7129 multiplies by 0.01; the per-poly arm @0x5c7607 by 0.001.
TEST(ObjectLightShade, ConstSunVertexScale001Bits) {
    union { float f; unsigned u; } c; c.f = kSunVertexIntensityScale;
    CHECK_EQ(c.u, 0x3c23d70au);                    // flt_628C30 == 0.01
    // It must DIFFER from the documented per-poly scale 0.001 (flt_628C2C) — pins the
    // "per-vertex 0.01 vs per-poly 0.001" distinction the wave-6 doc records.
    union { float f; unsigned u; } poly; poly.f = 0.001f;
    CHECK(c.u != poly.u);
    CHECK(kSunVertexIntensityScale > 0.001f * 9.0f);   // 0.01 is ~10x the per-poly 0.001
}

// (W13-c) The per-vertex sun-normal Y pre-bias scale flt_628C24 == 20.0 (0x41a00000).
TEST(ObjectLightShade, ConstSunNormalYScale20) {
    CHECK_EQ(kSunNormalYScale, 20.0f);
    union { float f; unsigned u; } c; c.f = kSunNormalYScale;
    CHECK_EQ(c.u, 0x41a00000u);
}

// (W13-d) The falloff LUT index scale flt_628C28/50 == -1023.0 (0xc47fc000); the
// point-intensity scale flt_628C4C == 10.0 (0x41200000).
TEST(ObjectLightShade, ConstRampIndexAndIntensity) {
    CHECK_EQ(kRampIndexScale, -1023.0f);
    union { float f; unsigned u; } a; a.f = kRampIndexScale;
    CHECK_EQ(a.u, 0xc47fc000u);
    CHECK_EQ(kLightIntensityScale, 10.0f);
    union { float f; unsigned u; } b; b.f = kLightIntensityScale;
    CHECK_EQ(b.u, 0x41200000u);
}

// (W13-e) Luma weights 0.30/0.59/0.11 (flt_628C8C/88/90), the normalize cap 255
// (flt_628C94), and the material modulation scale 1/256 (flt_628C70).
TEST(ObjectLightShade, ConstLumaWeightsAndScales) {
    CHECK_EQ(kLumaR, 0.30f);
    CHECK_EQ(kLumaG, 0.59f);
    CHECK_EQ(kLumaB, 0.11f);
    CHECK_EQ(kLightNormCap, 255.0f);
    CHECK_EQ(kMaterialShadeScale, 1.0f / 256.0f);
    union { float f; unsigned u; } s; s.f = kMaterialShadeScale;
    CHECK_EQ(s.u, 0x3b800000u);                    // flt_628C70 == 0x3b800000
}

// (W13-f) The falloff LUT index helper: (int)(NdotL * -1023) is clamped to [0,1023]
// for the whole valid back-facing range and for out-of-domain over-unit normals.
// Pins the index domain BuildFalloffLUT (the flt_1405110 table) is read with.
TEST(ObjectLightShade, ConstFalloffIndexDomain) {
    std::vector<float> lut(1024);
    BuildFalloffLUT(lut.data());
    // NdotL == 0 (grazing) -> idx 0 ; NdotL == -1 (head-on) -> idx 1023.
    // Exercise via the public sun arm: a head-on vertex picks lut[1023].
    const float amb[3] = {0, 0, 0};
    const float sunDir[3] = {0, 0, 1}, sunColor[3] = {1000.0f, 0, 0};
    MeshLightVertex face{{0, 0, 0}, {0, 0, -1.0f}};   // NdotL = -1 -> idx 1023
    ShadeBytes out{};
    LightMeshVertices(&face, 1, amb, sunDir, sunColor, 100.0f, nullptr, 0, 100.0f,
                      lut.data(), &out);
    // factor = 100*0.01*100*lut[1023]; with lut[1023] ~0.97 the red channel saturates.
    const float f1023 = 100.0f * kSunVertexIntensityScale * 100.0f * lut[1023];
    CHECK(f1023 > 0.0f);          // index resolved inside the table, non-zero ramp
    CHECK_EQ((int)out.r, 255);    // 1000 * f1023 clamps red to 255
}
