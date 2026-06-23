// Unit tests for render/vegetation_anim — VIBE_Light_BuildVegetationCache @0x5c8560
// + VIBE_Mesh_TransformPackedVertices @0x5c9d04 (the i8 unpack + per-vertex
// intensity quantize). Golden vectors computed with python3 against the exact
// decompile arithmetic + get_bytes-recovered float constants.
//
// IMPORTANT (rule 8): the original has NO wind/sine vegetation sway — see the
// header. These tests cover the real per-frame veg update: the unpack scale/bias,
// the unlit grayscale byte, the lit 3-channel clamp, and the driver shape (seed ->
// transform -> light -> quantize).
#include "test.h"

#include "render/vegetation_anim.h"

#include <cmath>
#include <cstring>

using namespace guild;
using namespace guild::render;

namespace {

bool nearF(float a, float b, float eps = 1e-5f) {
    return std::fabs(a - b) <= eps * (1.0f + std::fabs(b));
}

} // namespace

// --- i8 packed-vertex unpack: ((i16)byte - 128) * (1/127.5) ------------------
TEST(VegetationAnim, VegetationAnim_UnpackComponent_GoldenVectors) {
    // python3 oracle (double math, float store):
    CHECK(nearF(UnpackVertexComponent(0),   -1.003921627998352f));
    CHECK(nearF(UnpackVertexComponent(1),   -0.9960784902796149f));
    CHECK(nearF(UnpackVertexComponent(127), -0.007843137718737125f));
    CHECK(nearF(UnpackVertexComponent(128),  0.0f));
    CHECK(nearF(UnpackVertexComponent(255),  0.9960784902796149f));
}

TEST(VegetationAnim, VegetationAnim_UnpackPosition) {
    u8 packed[3] = {128, 255, 0};
    float m[3];
    UnpackVertexPosition(packed, m);
    CHECK(nearF(m[0], 0.0f));
    CHECK(nearF(m[1], 0.9960784902796149f));
    CHECK(nearF(m[2], -1.003921627998352f));
}

// --- UNLIT intensity: G*0.59 + R*0.30 + B*0.11, saturate at 255 --------------
// The engine's (int) is a BARE fistp @0x5c8844 => round-NEAREST-EVEN, NOT
// truncate. Goldens recomputed against that (binary beats the old truncate pins).
TEST(VegetationAnim, VegetationAnim_IntensityUnlit_GoldenVectors) {
    float c0[3] = {200.f, 200.f, 200.f}; // sum ~199.99999702 -> nearest 200
    CHECK_EQ((int)ComputeVertexIntensityUnlit(c0), 200);

    float c1[3] = {100.f, 100.f, 100.f}; // ~99.999998 -> 100
    CHECK_EQ((int)ComputeVertexIntensityUnlit(c1), 100);

    float c2[3] = {50.f, 40.f, 30.f};    // 40*.59 + 50*.30 + 30*.11 = 41.9 -> 42
    CHECK_EQ((int)ComputeVertexIntensityUnlit(c2), 42);

    float c3[3] = {0.f, 0.f, 0.f};
    CHECK_EQ((int)ComputeVertexIntensityUnlit(c3), 0);

    float c4[3] = {300.f, 300.f, 300.f}; // rounds 300 >= 255 -> 0xFF
    CHECK_EQ((int)ComputeVertexIntensityUnlit(c4), 255);
}

// --- LIT intensity: max-channel rescale by 255/max then per-channel truncate --
TEST(VegetationAnim, VegetationAnim_IntensityLit_NoClamp) {
    float c[3] = {200.f, 200.f, 200.f}; // max 200 <= 255: no rescale
    u8 bgr[3];
    ComputeVertexIntensityLit(c, bgr);
    CHECK_EQ((int)bgr[0], 200); // B (+68) = (int)rgb[2]
    CHECK_EQ((int)bgr[1], 200); // G (+69) = (int)rgb[1]
    CHECK_EQ((int)bgr[2], 200); // R (+70) = (int)rgb[0]
}

TEST(VegetationAnim, VegetationAnim_IntensityLit_Clamp) {
    // r=300 (max) -> scale = 255/300 = 0.85; r=255, g=150*0.85=127.5, b=75*0.85=63.75.
    // BARE fistp = round-nearest-even: 127.5 -> 128, 63.75 -> 64 (binary, not trunc).
    float c[3] = {300.f, 150.f, 75.f};
    u8 bgr[3];
    ComputeVertexIntensityLit(c, bgr);
    CHECK_EQ((int)bgr[2], 255); // R = rgb[0] scaled
    CHECK_EQ((int)bgr[1], 128); // G = rgb[1] scaled (127.5 -> nearest-even 128)
    CHECK_EQ((int)bgr[0], 64);  // B = rgb[2] scaled (63.75 -> 64)
}

TEST(VegetationAnim, VegetationAnim_IntensityLit_ChannelOrder) {
    float c[3] = {10.f, 20.f, 30.f}; // max 30 <= 255: no rescale
    u8 bgr[3];
    ComputeVertexIntensityLit(c, bgr);
    CHECK_EQ((int)bgr[0], 30); // B (+68) <- rgb[2]
    CHECK_EQ((int)bgr[1], 20); // G (+69) <- rgb[1]
    CHECK_EQ((int)bgr[2], 10); // R (+70) <- rgb[0]
}

// --- Driver: seed (200,200,200) -> transform -> light -> quantize -------------
namespace {
struct DriverCtx {
    int transformCalls = 0;
    int lightCalls     = 0;
    float seenSeed[3]  = {0, 0, 0}; // seed captured at transform time
};

void recTransform(VegVertex* v, u32 count, void* ctx) {
    auto* c = static_cast<DriverCtx*>(ctx);
    c->transformCalls++;
    if (count > 0) {
        c->seenSeed[0] = v[0].rgb[0];
        c->seenSeed[1] = v[0].rgb[1];
        c->seenSeed[2] = v[0].rgb[2];
    }
    // emulate the bone unpack: write pos from packed so we can assert ordering
    for (u32 i = 0; i < count; ++i)
        UnpackVertexPosition(v[i].packed, v[i].pos);
}

// add a flat +20 of light into every channel (the dynamic light walk's effect)
void recLight(VegVertex* v, u32 count, void* ctx) {
    static_cast<DriverCtx*>(ctx)->lightCalls++;
    for (u32 i = 0; i < count; ++i) {
        v[i].rgb[0] += 20.f;
        v[i].rgb[1] += 20.f;
        v[i].rgb[2] += 20.f;
    }
}
} // namespace

TEST(VegetationAnim, VegetationAnim_Driver_SeedThenTransformThenLight_Unlit) {
    VegVertex verts[2] = {};
    verts[0].packed[0] = 128; verts[0].packed[1] = 255; verts[0].packed[2] = 0;
    verts[1].packed[0] = 0;   verts[1].packed[1] = 128; verts[1].packed[2] = 255;

    DriverCtx ctx;
    VegCacheHooks hooks;
    hooks.transformVertices = recTransform;
    hooks.accumulateLights  = recLight;
    hooks.ctx = &ctx;

    BuildVegetationCache(verts, 2, /*lit=*/false, hooks);

    // seed (200,200,200) was visible to the transform hook (runs after seed).
    CHECK_EQ(ctx.transformCalls, 1);
    CHECK_EQ(ctx.lightCalls, 1);
    CHECK(nearF(ctx.seenSeed[0], kVegBaseColour));
    CHECK(nearF(ctx.seenSeed[1], kVegBaseColour));
    CHECK(nearF(ctx.seenSeed[2], kVegBaseColour));

    // transform produced model-space positions from the packed bytes.
    CHECK(nearF(verts[0].pos[0], 0.0f));
    CHECK(nearF(verts[0].pos[1], 0.9960784902796149f));

    // light added +20 -> rgb (220,220,220); unlit byte rounds to 220 (fistp RNE).
    float lit220[3] = {220.f, 220.f, 220.f};
    CHECK_EQ((int)verts[0].iR, (int)ComputeVertexIntensityUnlit(lit220));
    CHECK_EQ((int)verts[0].iR, 220);
}

TEST(VegetationAnim, VegetationAnim_Driver_LitArm_WritesThreeChannels) {
    VegVertex verts[1] = {};
    DriverCtx ctx;
    VegCacheHooks hooks;
    hooks.transformVertices = nullptr;     // skip transform for this test
    hooks.accumulateLights  = recLight;    // 200 + 20 = 220 each
    hooks.ctx = &ctx;

    BuildVegetationCache(verts, 1, /*lit=*/true, hooks);

    // rgb (220,220,220), max 220 <= 255: no rescale; all three bytes = 220.
    CHECK_EQ((int)verts[0].iB, 220);
    CHECK_EQ((int)verts[0].iG, 220);
    CHECK_EQ((int)verts[0].iR, 220);
}

TEST(VegetationAnim, VegetationAnim_Driver_NullHooks_SeedOnlyQuantize) {
    VegVertex verts[1] = {};
    VegCacheHooks hooks; // both hooks null
    BuildVegetationCache(verts, 1, /*lit=*/false, hooks);
    // seed only (200,200,200) -> unlit byte rounds to 200 (fistp RNE).
    CHECK_EQ((int)verts[0].iR, 200);
}

// =============================================================================
// WAVE-10 HARDENING — degenerate inputs (ASAN+UBSAN).
// =============================================================================

// (w1) count == 0 across both arms + with/without hooks: every loop (seed,
//      transform, light, quantize) must iterate zero times and never deref verts.
TEST(VegetationAnim, Harden_ZeroCount_NoOp) {
    DriverCtx ctx;
    VegCacheHooks hooks;
    hooks.transformVertices = recTransform;
    hooks.accumulateLights  = recLight;
    hooks.ctx = &ctx;

    // Non-null buffer but count 0: hooks still invoked (the engine calls them with
    // count 0), but they must not touch any element.
    VegVertex one = {};
    one.iR = 0x5A;                      // sentinel that must survive untouched
    BuildVegetationCache(&one, 0, /*lit=*/false, hooks);
    CHECK_EQ(ctx.transformCalls, 1);   // driver still calls the hooks once
    CHECK_EQ(ctx.lightCalls, 1);
    CHECK_EQ((int)one.iR, 0x5A);        // element NOT written (count 0)

    // Null buffer, count 0, null hooks: pure no-op, no deref.
    VegCacheHooks none;
    BuildVegetationCache(nullptr, 0, /*lit=*/true, none);
    BuildVegetationCache(nullptr, 0, /*lit=*/false, none);
    CHECK(true);                        // reached without crash
}

// (w2) Single vertex, lit arm, count 1: seed -> quantize writes all three bytes,
//      tight 1-element span (no read/write past it).
TEST(VegetationAnim, Harden_SingleVertexLit) {
    VegVertex v = {};
    VegCacheHooks none;                 // no hooks: seed (200,200,200) only
    BuildVegetationCache(&v, 1, /*lit=*/true, none);
    CHECK_EQ((int)v.iB, 200);
    CHECK_EQ((int)v.iG, 200);
    CHECK_EQ((int)v.iR, 200);
}
