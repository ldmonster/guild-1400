// Golden-vector tests for gilde.exe 0x5e1278 VIBE_Particle_RenderSystem
// inner projection math (guild::render::fxrecon3::project_slot).
#include "tests/framework/test.h"
#include "render/fx_recon3_particle_render.h"

#include <cmath>
#include <cstring>

using namespace guild::render::fxrecon3;

namespace {

Mat4 identity() {
    Mat4 m{};
    for (int i = 0; i < 16; ++i) m.m[i] = 0.0f;
    m.m[0] = m.m[5] = m.m[10] = m.m[15] = 1.0f;
    return m;
}

ProjState makeProj() {
    ProjState ps{};
    ps.sx = 2.0f; ps.ox = 320.0f;
    ps.sy = 2.0f; ps.oy = 240.0f;
    ps.minDistSq = 4.0f; ps.fadeBias = 10.0f; ps.fadeScale = 0.5f;
    ps.zFar = 1000.0f; ps.zNear = 0.1f;
    // Scissor globals use the engine's compare polarity (see header / cull test):
    //   scLeft (ECE58) = left X bound, scTop (ECE60) = right X bound,
    //   scRight (ECE5C) = top Y bound, scBottom (ECE64) = bottom Y bound.
    ps.scLeft = 0; ps.scTop = 640; ps.scRight = 0; ps.scBottom = 480;
    return ps;
}

ParticleSlot makeSlot(float cx, float cy, float cz, float size,
                      guild::u8 cr, guild::u8 cg, guild::u8 cb,
                      guild::u8 alpha, guild::u8 flags) {
    ParticleSlot s{};
    std::memset(s.raw, 0, sizeof(s.raw));
    *reinterpret_cast<float*>(s.raw + 56) = cx;
    *reinterpret_cast<float*>(s.raw + 60) = cy;
    *reinterpret_cast<float*>(s.raw + 64) = cz;
    *reinterpret_cast<float*>(s.raw + 72) = size;
    s.raw[76] = cr; s.raw[77] = cg; s.raw[78] = cb;
    s.raw[79] = alpha; s.raw[81] = flags;
    return s;
}

} // namespace

TEST(FxRecon3, HooksDefaultInstall) {
    RenderHooks h{};
    install_default_render_hooks(h);
    CHECK(h.freeObjectNode != nullptr);
    CHECK(h.computeBoneWorld != nullptr);
    CHECK(h.coordConvertX != nullptr);
    CHECK(h.findGroupMember != nullptr);
    // inert behaviours
    CHECK_EQ(h.freeObjectNode(nullptr, nullptr), 0);
    int dummy = 7;
    CHECK_EQ(h.findGroupMember(&dummy, 3), (void*)&dummy);
    Mat4 m{};
    h.computeBoneWorld(nullptr, nullptr, 0, &m);
    CHECK_EQ(m.m[0], 1.0f);
    CHECK_EQ(m.m[5], 1.0f);
    CHECK_EQ(m.m[10], 1.0f);
    CHECK_EQ(m.m[1], 0.0f);
}

TEST(FxRecon3, InactiveSlotNotVisible) {
    ProjState ps = makeProj();
    Mat4 m = identity();
    // flags bit0 clear => inactive
    ParticleSlot s = makeSlot(0, 0, 10, 4, 200, 100, 50, 128, /*flags*/0);
    SlotProjection r = project_slot(s, m, ps, false);
    CHECK(!r.visible);
    // alpha == 0 => inactive even with flag set
    ParticleSlot s2 = makeSlot(0, 0, 10, 4, 200, 100, 50, 0, /*flags*/1);
    SlotProjection r2 = project_slot(s2, m, ps, false);
    CHECK(!r2.visible);
}

TEST(FxRecon3, CenterOnAxis) {
    ProjState ps = makeProj();
    Mat4 m = identity();
    ParticleSlot s = makeSlot(0.0f, 0.0f, 10.0f, 4.0f, 200, 100, 50, 128, 1);
    SlotProjection r = project_slot(s, m, ps, false);
    CHECK(r.visible);
    CHECK_EQ(r.ex, 0.0f);
    CHECK_EQ(r.ey, 0.0f);
    CHECK_EQ(r.ez, 10.0f);
    CHECK_EQ(r.invZ, 0.1f);
    CHECK_EQ(r.halfW, 0.4f);
    CHECK_EQ(r.halfH, -0.4f);
    CHECK_EQ(r.cxScr, 320.0f);
    CHECK_EQ(r.cyScr, 240.0f);
    CHECK_EQ(r.distSq, 100.0f);
    // distSq(100) > minDistSq(4): sqrt(100)=10, (10-10)*0.5=0, 255-0=255
    CHECK_EQ(r.fadeAlpha, 255);
    CHECK_EQ((int)r.colA, 255);
    // non-replaced color preserved
    CHECK_EQ((int)r.colR, 200);
    CHECK_EQ((int)r.colG, 100);
    CHECK_EQ((int)r.colB, 50);
}

TEST(FxRecon3, OffAxisAlphaByteWrap) {
    ProjState ps = makeProj();
    Mat4 m = identity();
    ParticleSlot s = makeSlot(1.0f, 2.0f, 5.0f, 8.0f, 200, 100, 50, 128, 1);
    SlotProjection r = project_slot(s, m, ps, false);
    CHECK(r.visible);
    CHECK_EQ(r.ex, 1.0f);
    CHECK_EQ(r.ey, 2.0f);
    CHECK_EQ(r.ez, 5.0f);
    CHECK_EQ(r.invZ, 0.2f);
    CHECK_EQ(r.halfW, 1.6f);
    CHECK_EQ(r.halfH, -1.6f);
    CHECK_EQ(r.cxScr, 320.4f);
    CHECK_EQ(r.cyScr, 240.8f);
    CHECK_EQ(r.distSq, 30.0f);
    // sqrt(30)=5.4772, (5.4772-10)*0.5 = -2.26..., 255-(-2.26)=257.26 -> int 257
    CHECK_EQ(r.fadeAlpha, 257);
    // alpha byte truncation: 257 & 0xFF == 1 (genuine wraparound)
    CHECK_EQ((int)r.colA, 1);
}

TEST(FxRecon3, QuadCorners) {
    ProjState ps = makeProj();
    Mat4 m = identity();
    ParticleSlot s = makeSlot(1.0f, 2.0f, 5.0f, 8.0f, 200, 100, 50, 128, 1);
    SlotProjection r = project_slot(s, m, ps, false);
    CHECK(r.visible);
    const float v121 = 8.0f * 0.5f; // 4.0
    // TL: (ex - h, ey + h)
    CHECK_EQ(r.qx[0], 1.0f - v121);
    CHECK_EQ(r.qy[0], 2.0f + v121);
    // BR: (ex + h, ey - h)
    CHECK_EQ(r.qx[1], 1.0f + v121);
    CHECK_EQ(r.qy[1], 2.0f - v121);
    CHECK_EQ(r.qx[2], 1.0f - v121);
    CHECK_EQ(r.qy[2], 2.0f - v121);
    CHECK_EQ(r.qx[3], 1.0f + v121);
    CHECK_EQ(r.qy[3], 2.0f + v121);
}

TEST(FxRecon3, NearFullAlpha) {
    ProjState ps = makeProj();
    Mat4 m = identity();
    // distSq = 0.25+0.25+1 = 1.5 <= minDistSq(4) => full alpha 255
    ParticleSlot s = makeSlot(0.5f, 0.5f, 1.0f, 2.0f, 10, 20, 30, 64, 1);
    SlotProjection r = project_slot(s, m, ps, false);
    CHECK(r.visible);
    CHECK_EQ(r.distSq, 1.5f);
    CHECK_EQ(r.fadeAlpha, 255);
    CHECK_EQ(r.invZ, 1.0f);
    CHECK_EQ(r.halfW, 2.0f);
    CHECK_EQ(r.cxScr, 321.0f);
    CHECK_EQ(r.cyScr, 241.0f);
}

TEST(FxRecon3, ColorReplacedModulation) {
    ProjState ps = makeProj();
    Mat4 m = identity();
    ParticleSlot s = makeSlot(0.0f, 0.0f, 10.0f, 4.0f, 200, 100, 50, 128, 1);
    SlotProjection r = project_slot(s, m, ps, /*colorReplaced*/true);
    CHECK(r.visible);
    // chan = (alpha * chan) >> 8, alpha=128
    CHECK_EQ((int)r.colR, (128 * 200) >> 8); // 100
    CHECK_EQ((int)r.colG, (128 * 100) >> 8); // 50
    CHECK_EQ((int)r.colB, (128 * 50) >> 8);  // 25
}

TEST(FxRecon3, NearFarCull) {
    ProjState ps = makeProj();
    Mat4 m = identity();
    // z >= zFar(1000) culled
    ParticleSlot sFar = makeSlot(0.0f, 0.0f, 1000.0f, 4.0f, 200, 100, 50, 128, 1);
    CHECK(!project_slot(sFar, m, ps, false).visible);
    // z < zNear(0.1) culled
    ParticleSlot sNear = makeSlot(0.0f, 0.0f, 0.05f, 4.0f, 200, 100, 50, 128, 1);
    CHECK(!project_slot(sNear, m, ps, false).visible);
}

TEST(FxRecon3, ScissorCull) {
    ProjState ps = makeProj();
    Mat4 m = identity();
    // Push center far to the right so cxScr - halfW > scRight test fails the
    // bounds. cx large, z moderate -> cxScr beyond right edge.
    ParticleSlot s = makeSlot(10000.0f, 0.0f, 10.0f, 1.0f, 200, 100, 50, 128, 1);
    SlotProjection r = project_slot(s, m, ps, false);
    CHECK(!r.visible);
}

TEST(FxRecon3, Constants) {
    CHECK_EQ(kHalf, 0.5f);
    CHECK_EQ(kLumaScale, 0.25f);
    CHECK_EQ(k2p24, 16777216.0f);
    CHECK_EQ(kDbl3, 3.0);
    CHECK_EQ(kDbl255, 255.0);
}

// =============================================================================
// WAVE-10 (W10-PARTICLE) DEGENERATE / EDGE coverage for project_slot. These hit
// the active gate (alpha==0), the near/far cull at the exact boundary polarity,
// all four scissor edges, the min-dist full-alpha branch, and the distance-fade
// clamp at extremes. Suite prefix Edge.
// =============================================================================

// alpha == 0 fails the active gate ((flags&1) && alpha!=0) even with flags set.
TEST(FxRecon3Edge, ZeroAlphaInactive) {
    ProjState ps = makeProj();
    Mat4 m = identity();
    ParticleSlot s = makeSlot(0, 0, 10, 4, 255, 255, 255, /*alpha*/0, /*flags*/1);
    CHECK(!project_slot(s, m, ps, false).visible);
}

// Near/far cull boundary polarity: z == zFar culled (>=), z == zNear visible (the
// test is `z < zNear`, so exactly zNear passes), z just below zNear culled.
TEST(FxRecon3Edge, ZCullBoundaries) {
    ProjState ps = makeProj(); // zFar=1000, zNear=0.1
    Mat4 m = identity();
    // z == zFar -> `z >= zFar` true -> culled.
    CHECK(!project_slot(makeSlot(0,0,1000.0f,4,200,100,50,128,1), m, ps, false).visible);
    // z just below zFar -> visible.
    CHECK(project_slot(makeSlot(0,0,999.9f,4,200,100,50,128,1), m, ps, false).visible);
    // z == zNear -> `z < zNear` false -> visible.
    CHECK(project_slot(makeSlot(0,0,0.1f,4,200,100,50,128,1), m, ps, false).visible);
    // z just below zNear -> culled.
    CHECK(!project_slot(makeSlot(0,0,0.099f,4,200,100,50,128,1), m, ps, false).visible);
}

// All four scissor edges cull when the quad falls fully outside each bound.
TEST(FxRecon3Edge, ScissorAllEdges) {
    ProjState ps = makeProj(); // scLeft=0 (left X), scTop=640 (right X),
                               // scRight=0 (top Y), scBottom=480 (bottom Y)
    Mat4 m = identity();
    // far right: cxScr - halfW >= scTop(640).
    CHECK(!project_slot(makeSlot( 10000,0,10,1,200,100,50,128,1), m, ps, false).visible);
    // far left: cxScr + halfW < scLeft? the test is scLeft > cxScr+halfW -> cull.
    CHECK(!project_slot(makeSlot(-10000,0,10,1,200,100,50,128,1), m, ps, false).visible);
    // far down: cyScr - halfH >= scBottom(480).
    CHECK(!project_slot(makeSlot(0, 10000,10,1,200,100,50,128,1), m, ps, false).visible);
    // far up: cyScr + halfH < scRight(0) -> scRight > cyScr+halfH -> cull.
    CHECK(!project_slot(makeSlot(0,-10000,10,1,200,100,50,128,1), m, ps, false).visible);
    // dead-center stays visible (sanity).
    CHECK(project_slot(makeSlot(0,0,10,1,200,100,50,128,1), m, ps, false).visible);
}

// Min-dist branch: distSq <= minDistSq -> full alpha 255 (no sqrt fade path).
TEST(FxRecon3Edge, MinDistFullAlpha) {
    ProjState ps = makeProj(); // minDistSq = 4
    Mat4 m = identity();
    // center at z=1 -> distSq ~= 1 <= 4 -> fadeAlpha 255.
    SlotProjection r = project_slot(makeSlot(0,0,1.0f,4,10,20,30,200,1), m, ps, false);
    CHECK(r.visible);
    CHECK_EQ(r.fadeAlpha, 255);
    CHECK_EQ((int)r.colA, 255);
}

// Distance-fade clamp: a far center beyond the fade range drives v93 toward 0 but
// never negative (the (sqrt-bias)*scale then 255-min(255,..) clamp). Assert the
// resulting fade alpha is within [0,255] and the slot still projects in-bounds.
TEST(FxRecon3Edge, FadeAlphaClampRange) {
    ProjState ps = makeProj();
    ps.minDistSq = 0.0f;     // force the sqrt fade path
    ps.fadeBias = 0.0f; ps.fadeScale = 1000.0f; // huge scale -> v91 huge -> v93 -> 0
    Mat4 m = identity();
    SlotProjection r = project_slot(makeSlot(0,0,5.0f,4,10,20,30,200,1), m, ps, false);
    CHECK(r.visible);
    CHECK(r.fadeAlpha >= 0 && r.fadeAlpha <= 255);
    CHECK_EQ(r.fadeAlpha, 0); // clamped to 0 at the far end
}
