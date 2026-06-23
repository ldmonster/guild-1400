// Wave-6 W6-PARTICLE — tests for the particle billboard RENDER into the frame
// (the visible-output completion of VIBE_Particle_RenderSystem @0x5e1278) plus
// golden vectors for the per-particle INTEGRATE math kernels (particle.cpp,
// VIBE_Particle_* @0x42b930..0x42cde8).
//
//   render: guild::render::fxrecon3::render_system_to_surface
//           (project_slot @0x5e13ac + the blend textured-triangle leaf)
//   integrate: guild::render::SeedParticles / UpdateGravity / UpdateScatter
//
// Golden values for the integrate path are computed inline from the same exact
// float op-order the reconstruction uses (the kernels are deterministic for a
// fixed RNG seed via crt::RandNext).
#include "render/fx_recon3_particle_render.h"
#include "render/particle.h"
#include "render/surface.h"
#include "render/texture.h"
#include "crt/rand.h"
#include "test.h"

#include <cmath>
#include <cstring>
#include <vector>

using namespace guild;
using namespace guild::render;
namespace fx = guild::render::fxrecon3;

namespace {

Surface* Make16(int w, int h) {
    Surface* s = SurfaceCreate(w, h, 16);
    std::memset(s->pixels, 0, (size_t)s->pitch * h);
    return s;
}

// A solid 4x4 texture: every texel index 1, palette[1] = a bright RGB565 value.
// Index 1 (not 0) so the masked/colour-key paths would still draw it.
Texture MakeSolidTex(u16 colour) {
    Texture t;
    TextureSetSize(t, 4);            // widthShift=2, texelMask=15, alloc 16 texels
    for (int i = 0; i < 16; ++i) t.texels[i] = 1;
    t.mipWidth = 4;
    return t;
}

void MakePalette(std::vector<u16>& pal, u16 colour) {
    pal.assign(256, 0);
    pal[1] = colour;
}

fx::Mat4 Identity() {
    fx::Mat4 m{};
    for (int i = 0; i < 16; ++i) m.m[i] = 0.0f;
    m.m[0] = m.m[5] = m.m[10] = m.m[15] = 1.0f;
    return m;
}

// A projection that maps eye XY straight to screen with a generous scissor.
fx::ProjState MakeProj(int w, int h) {
    fx::ProjState ps{};
    ps.sx = 1.0f; ps.ox = (float)(w / 2);
    ps.sy = 1.0f; ps.oy = (float)(h / 2);
    ps.minDistSq = 1e9f;     // always full alpha (255), keep colour deterministic
    ps.fadeBias = 0.0f; ps.fadeScale = 0.0f;
    ps.zFar = 1e9f; ps.zNear = 0.001f;
    ps.scLeft = 0; ps.scTop = w;     // ECE58 left, ECE60 right
    ps.scRight = 0; ps.scBottom = h; // ECE5C top, ECE64 bottom
    return ps;
}

fx::ParticleSlot MakeSlot(float cx, float cy, float cz, float size,
                          u8 cr, u8 cg, u8 cb, u8 alpha, u8 flags) {
    fx::ParticleSlot s{};
    std::memset(s.raw, 0, sizeof(s.raw));
    *reinterpret_cast<float*>(s.raw + 56) = cx;
    *reinterpret_cast<float*>(s.raw + 60) = cy;
    *reinterpret_cast<float*>(s.raw + 64) = cz;
    *reinterpret_cast<float*>(s.raw + 72) = size;
    s.raw[76] = cr; s.raw[77] = cg; s.raw[78] = cb;
    s.raw[79] = alpha; s.raw[81] = flags;
    return s;
}

int CountNonZero(const Surface* s) {
    const u16* p = (const u16*)s->pixels;
    int n = 0;
    for (int i = 0; i < s->width * s->height; ++i) if (p[i] != 0) ++n;
    return n;
}

} // namespace

// ===========================================================================
// RENDER — the screen-space billboard corners (project_slot).
// ===========================================================================
TEST(ParticleRenderW6, ScreenCornersFromProjection) {
    fx::ProjState ps = MakeProj(64, 64);
    fx::Mat4 m = Identity();
    // center at (0,0,4), size 8 -> halfW = sx*4*0.5*(1/4)=0.5; cxScr=32.
    fx::ParticleSlot s = MakeSlot(0.0f, 0.0f, 4.0f, 8.0f, 255, 255, 255, 255, 1);
    fx::SlotProjection r = fx::project_slot(s, m, ps, false);
    CHECK(r.visible);
    // halfW = sx * (size*0.5) * (1/z) = 1 * 4 * 0.25 = 1.0; halfH = -1.0
    CHECK_EQ(r.halfW, 1.0f);
    CHECK_EQ(r.halfH, -1.0f);
    CHECK_EQ(r.cxScr, 32.0f);
    CHECK_EQ(r.cyScr, 32.0f);
    // left = cxScr-halfW = 31, right = 33; top = cyScr-halfH = 33, bottom = 31.
    CHECK_EQ(r.sxc[0], 31.0f); // TL x
    CHECK_EQ(r.syc[0], 33.0f); // TL y (top = cyScr - halfH, halfH negative -> +1)
    CHECK_EQ(r.sxc[1], 33.0f); // RB x
    CHECK_EQ(r.syc[1], 31.0f); // RB y
    CHECK_EQ(r.sxc[2], 31.0f); // LB x
    CHECK_EQ(r.syc[2], 31.0f);
    CHECK_EQ(r.sxc[3], 33.0f); // RT x
    CHECK_EQ(r.syc[3], 33.0f);
}

// ===========================================================================
// RENDER — a particle actually draws into the surface (alpha blend).
// ===========================================================================
TEST(ParticleRenderW6, AlphaBlendDrawsIntoFrame) {
    Surface* fb = Make16(64, 64);
    const u16 kWhite = 0xFFFF;
    Texture tex = MakeSolidTex(kWhite);
    std::vector<u16> pal; MakePalette(pal, kWhite);

    // A big-enough billboard so its quad covers several pixels.
    fx::ParticleSlot slots[1] = {
        MakeSlot(0.0f, 0.0f, 1.0f, 16.0f, 255, 255, 255, 255, 1)};
    fx::ParticleSystemView sys{};
    sys.slots = slots; sys.slotCount = 1;
    sys.defTex = &tex; sys.palette = pal.data();

    fx::ProjState ps = MakeProj(64, 64);
    fx::Mat4 m = Identity();

    CHECK_EQ(CountNonZero(fb), 0);
    int drawn = fx::render_system_to_surface(fb, sys, m, ps, fx::kBlendAlpha, false);
    CHECK_EQ(drawn, 1);
    // Pixels were written; the blend of white over black = (0xFFFF>>1)&mask = ~half.
    int nz = CountNonZero(fb);
    CHECK(nz > 0);
    // 50/50 blend of white(0xFFFF) over black(0) with RGB565 mask 0xF7DE:
    //   ((0xFFFF>>1)&0xF7DE) + ((0>>1)&0xF7DE) = 0x77DE
    const u16* px = (const u16*)fb->pixels;
    // center pixel (32,32) is inside the quad.
    CHECK_EQ((int)px[32 * fb->widthPx + 32], 0x77DE);
    SurfaceDestroy(fb);
}

// ===========================================================================
// RENDER — additive (OR) blend mode.
// ===========================================================================
TEST(ParticleRenderW6, AdditiveOrBlend) {
    Surface* fb = Make16(64, 64);
    // pre-fill with a dim green so OR is observable.
    u16* px = (u16*)fb->pixels;
    for (int i = 0; i < fb->width * fb->height; ++i) px[i] = 0x0020; // a green bit
    const u16 kRed = 0xF800;
    Texture tex = MakeSolidTex(kRed);
    std::vector<u16> pal; MakePalette(pal, kRed);

    fx::ParticleSlot slots[1] = {
        MakeSlot(0.0f, 0.0f, 1.0f, 16.0f, 255, 255, 255, 255, 1)};
    fx::ParticleSystemView sys{};
    sys.slots = slots; sys.slotCount = 1;
    sys.defTex = &tex; sys.palette = pal.data();

    fx::ProjState ps = MakeProj(64, 64);
    fx::Mat4 m = Identity();
    int drawn = fx::render_system_to_surface(fb, sys, m, ps, fx::kBlendAdd, false);
    CHECK_EQ(drawn, 1);
    // OR of red(0xF800) onto 0x0020 = 0xF820 in the covered pixels.
    CHECK_EQ((int)px[32 * fb->widthPx + 32], 0xF820);
    SurfaceDestroy(fb);
}

// ===========================================================================
// RENDER — inactive slots draw nothing; whole-system gate is respected.
// ===========================================================================
TEST(ParticleRenderW6, InactiveSlotsAndGuards) {
    Surface* fb = Make16(32, 32);
    Texture tex = MakeSolidTex(0xFFFF);
    std::vector<u16> pal; MakePalette(pal, 0xFFFF);
    fx::ProjState ps = MakeProj(32, 32);
    fx::Mat4 m = Identity();

    // flags=0 -> inactive
    fx::ParticleSlot dead[1] = {MakeSlot(0, 0, 1, 8, 255, 255, 255, 255, 0)};
    fx::ParticleSystemView sys{};
    sys.slots = dead; sys.slotCount = 1; sys.defTex = &tex; sys.palette = pal.data();
    CHECK_EQ(fx::render_system_to_surface(fb, sys, m, ps, fx::kBlendAlpha, false), 0);
    CHECK_EQ(CountNonZero(fb), 0);

    // null guards
    sys.slots = nullptr;
    CHECK_EQ(fx::render_system_to_surface(fb, sys, m, ps, fx::kBlendAlpha, false), 0);
    SurfaceDestroy(fb);
}

// ===========================================================================
// INTEGRATE — golden vectors. UpdateGravity ballistic step with a fixed seed.
// (gilde.exe 0x42c42c VIBE_Particle_UpdateGravity)
// ===========================================================================
TEST(ParticleRenderW6, GravityIntegrateStep) {
    // One already-active particle; emitter has no re-emit (hdr20 bit0 clear,
    // not all dead). Advance one tick; verify the ballistic position update and
    // the gravity decrement match the exact float op-order.
    Emitter e{};
    Particle p{};
    p.flags = 1;
    p.vx = 0.5f; p.vy = 1.0f; p.vz = -0.25f;     // velocity
    p.life = 2.0f; p.a1 = 3.0f; p.a2 = 4.0f;     // +16/+20/+24 (scratch pos)
    p.seed = 0.0f;
    p.birthTime = 0;
    e.particles = &p; e.count = 1;
    e.hdr20 = 0;          // bit0 clear: no re-emit path; not-all-dead won't return 0

    // dt = (now - birthTime) * kGrDt; now=1000 -> dt = 1000 * 0.001 = 1.0
    const u32 now = 1000;
    const double dt = (double)now * 0.0010000000474974513; // kGrDt
    // px = life + vx ; py = a1 + vy ; pz = a2 + vz   (then life/a1/a2 = px/py/pz)
    const float exp_px = 2.0f + 0.5f;
    const float exp_py = 3.0f + 1.0f;
    const float exp_pz = 4.0f + (-0.25f);
    const float exp_seed = (float)(dt * 0.800000011920929) + 0.0f; // kGrTime
    // 0x42c656 fmul flt_611A68 leaves dt in the 80-bit x87 register; 0x42c69b
    // fsubr [+4] subtracts the UN-rounded dt from vy, and only 0x42c69f fstp
    // rounds the RESULT to float. So vy = (float)(vy - dt) with dt kept double,
    // NOT 1.0f - (float)dt (which would round dt to 1.0f first -> 0.0).
    const float exp_vy = (float)(1.0 - dt);
    char ret = UpdateGravity(e, now);
    CHECK_EQ(ret, (char)1);
    CHECK_EQ(p.life, exp_px);   // life = px
    CHECK_EQ(p.a1, exp_py);     // a1 = py
    CHECK_EQ(p.a2, exp_pz);     // a2 = pz
    CHECK_EQ(p.seed, exp_seed);
    CHECK_EQ(p.vy, exp_vy);
    CHECK(p.flags & 1);         // py >= -10 so still alive
    // shade = trunc(max(0, py*1.5 + 50))
    float shadeF = exp_py * 1.5f + 50.0f;
    CHECK_EQ((int)p.shade, (int)shadeF);
}

// ===========================================================================
// INTEGRATE — UpdateGravity kills a particle that falls below the floor.
// ===========================================================================
TEST(ParticleRenderW6, GravityKillsBelowFloor) {
    Emitter e{};
    Particle p{};
    p.flags = 1;
    p.vx = 0; p.vy = 0; p.vz = 0;
    p.life = 0; p.a1 = -20.0f; p.a2 = 0;  // py will be -20 + 0 = -20 < -10
    p.birthTime = 0;
    e.particles = &p; e.count = 1; e.hdr20 = 0;
    UpdateGravity(e, 1);
    CHECK_EQ((int)(p.flags & 1), 0);   // killed (py < -10)
    // all dead, hdr20 bit0 clear -> returns 0 (system exhausted)
    Particle p2 = p;                   // already dead
    Emitter e2{}; e2.particles = &p2; e2.count = 1; e2.hdr20 = 0;
    CHECK_EQ((int)UpdateGravity(e2, 2), 0);
}

// ===========================================================================
// INTEGRATE — SeedParticles fills an inactive slot then gravity-integrates.
// Deterministic RNG: re-seed crt and reproduce the exact draw order.
// ===========================================================================
TEST(ParticleRenderW6, SeedParticlesDeterministicWithSeed) {
    crt::Srand(12345);
    // Reproduce the exact 8-draw spawn the kernel does for one inactive slot.
    auto nf = []() { return (double)crt::RandNext() * 3.0518509447574615e-05; };
    double px = nf() * 4.0 + (-2.0);
    double py = nf() * 3.0 + (-1.5);
    double pz = nf() * 4.0 + (-2.0);
    double vx = nf() * 10.0 + (-5.0);
    double vy = nf() * 4.0 + 0.5;
    // kSdBias_48 is a FLOAT (-4.800000190734863f); the kernel adds (double + float)
    // so the float promotes — reproduce that exact promoted value, not a -4.8 double.
    double vz = nf() * 9.6 + (double)(-4.800000190734863f);
    u8 r = (u8)crt::RandNext();
    float life = (float)((r & 0x7F) + 127);
    double phase = (double)crt::RandNext() * 3.0518509447574615e-05;
    float seed = (float)phase + 1.0f;

    // Now run the real kernel from the same seed and compare.
    crt::Srand(12345);
    Emitter e{};
    Particle p{};
    p.flags = 0;            // inactive -> gets seeded
    e.particles = &p; e.count = 1;
    e.baseVx = 0.0f;        // gravity accel (pass 2): py>=baseVz path uses baseVx
    e.baseVy = 0.0f;        // life decay rate
    e.baseVz = -1.0e9f;     // floor far below -> pass 2 takes the no-bounce branch
    SeedParticles(e, 0);    // now == birthTime so dt==0: positions+velocities kept

    CHECK_EQ(p.px, (float)px);
    CHECK_EQ(p.py, (float)py);
    CHECK_EQ(p.pz, (float)pz);
    CHECK_EQ(p.vx, (float)vx);
    CHECK_EQ(p.vy, (float)vy);
    CHECK_EQ(p.vz, (float)vz);
    CHECK_EQ(p.life, life);
    CHECK_EQ(p.seed, seed);
    CHECK(p.flags & 1);     // now active
}

// ===========================================================================
// INTEGRATE — blend mask derivation (RGB565 / RGB555).
// ===========================================================================
TEST(ParticleRenderW6, BlendMaskFormats) {
    // RGB565: clear bit0 of R@11, G@5, B@0 -> ~(0x800|0x20|0x01) = 0xF7DE, so a
    // 50/50 white-over-black pixel == (0xFFFF>>1)&0xF7DE = 0x77DE.
    Surface* s565 = Make16(64, 64);        // SurfaceCreate default fmt = 565
    Texture tex = MakeSolidTex(0xFFFF);
    std::vector<u16> pal; MakePalette(pal, 0xFFFF);
    fx::ParticleSlot slots[1] = {MakeSlot(0, 0, 1, 16, 255, 255, 255, 255, 1)};
    fx::ParticleSystemView sys{}; sys.slots = slots; sys.slotCount = 1;
    sys.defTex = &tex; sys.palette = pal.data();
    fx::ProjState ps = MakeProj(64, 64);
    fx::render_system_to_surface(s565, sys, Identity(), ps, fx::kBlendAlpha, false);
    const u16* px = (const u16*)s565->pixels;
    bool found = false;
    for (int i = 0; i < 64 * 64; ++i) if (px[i] == 0x77DE) found = true;
    CHECK(found);
    SurfaceDestroy(s565);
}

// ===========================================================================
// WAVE-10 (W10-PARTICLE) DEGENERATE / EDGE coverage for the particle.cpp emitter
// kernels and the fx_recon3 render path. These drive null/zero/all-dead inputs so
// ASAN/UBSAN exercise the bounds + the wave-10 null-array guards.
// ===========================================================================

// particle.cpp kernels with a NULL slot array + count>0: the wave-10 guards skip
// the loop (the original derefs unconditionally; null was a never-reached crash).
TEST(ParticleW6Edge, KernelsNullArrayCountPositive) {
    Emitter e{};
    e.particles = nullptr;
    e.count = 8;            // count>0 but array null -> guarded
    e.hdr20 = 0;
    e.lifeBase = 1.0f;
    // None of these may OOB; they return their normal "keep running" values.
    CHECK_EQ(UpdateEmitter(e, 100), (char)1);
    CHECK_EQ(SeedParticles(e, 100), (char)1);
    CHECK_EQ(UpdateTrail(e, 100), (char)1);
    UpdateGravity(e, 100);  // gated by count>0 && particles
    UpdateFadeOut(e, 100);
    UpdateScatter(e, 100);
    // UpdateCosineWave dereferences particle[0] for its time base -> null returns
    // false (no live particles) via the wave-10 guard.
    CHECK_EQ(UpdateCosineWave(e, 100), false);
}

// Zero count with a valid 1-element array: loops never run; nothing mutates.
TEST(ParticleW6Edge, ZeroCountValidArray) {
    Emitter e{};
    Particle p{};
    p.flags = 1;            // would be "active" if the loop ran
    e.particles = &p; e.count = 0; e.hdr20 = 0;
    CHECK_EQ(SeedParticles(e, 50), (char)1);
    CHECK_EQ(UpdateTrail(e, 50), (char)1);
    UpdateFadeOut(e, 50);
    UpdateScatter(e, 50);
    CHECK(p.flags & 1);     // untouched
}

// UpdateGravity with all slots already dead and no re-emit -> returns 0
// (system exhausted), array walked fully in-bounds.
TEST(ParticleW6Edge, GravityAllDeadReturnsZero) {
    Emitter e{};
    Particle slots[4]; std::memset(slots, 0, sizeof(slots)); // all dead (flags 0)
    e.particles = slots; e.count = 4; e.hdr20 = 0; // bit0 clear -> no re-emit
    CHECK_EQ((int)UpdateGravity(e, 10), 0);
}

// UpdateCosineWave outside its [0,1] window: t<0 (now < birth) leaves slots
// untouched but still returns the "before window end" predicate, in-bounds.
TEST(ParticleW6Edge, CosineWaveBeforeWindow) {
    Emitter e{};
    Particle p{};
    p.birthTime = 1000; p.flags = 1;
    e.particles = &p; e.count = 1; e.hdr20 = 500; // duration 500
    // now < birth -> t negative -> skip the per-slot loop; return now < birth+dur.
    bool r = UpdateCosineWave(e, 900);
    CHECK(r == true); // 900 < 1000+500
}

// fx_recon3 render: null texture / null palette / null framebuffer all reject
// cleanly (return 0, no deref).
TEST(ParticleW6Edge, RenderNullGuards) {
    Surface* fb = Make16(16, 16);
    Texture tex = MakeSolidTex(0xFFFF);
    std::vector<u16> pal; MakePalette(pal, 0xFFFF);
    fx::ParticleSlot slots[1] = {MakeSlot(0, 0, 1, 8, 255, 255, 255, 255, 1)};
    fx::ProjState ps = MakeProj(16, 16);
    fx::Mat4 m = Identity();

    fx::ParticleSystemView sys{};
    sys.slots = slots; sys.slotCount = 1; sys.defTex = &tex; sys.palette = pal.data();

    // null framebuffer
    CHECK_EQ(fx::render_system_to_surface(nullptr, sys, m, ps, fx::kBlendAlpha, false), 0);
    // null texture
    fx::ParticleSystemView noTex = sys; noTex.defTex = nullptr;
    CHECK_EQ(fx::render_system_to_surface(fb, noTex, m, ps, fx::kBlendAlpha, false), 0);
    // null palette
    fx::ParticleSystemView noPal = sys; noPal.palette = nullptr;
    CHECK_EQ(fx::render_system_to_surface(fb, noPal, m, ps, fx::kBlendAlpha, false), 0);
    // zero slot count
    fx::ParticleSystemView zero = sys; zero.slotCount = 0;
    CHECK_EQ(fx::render_system_to_surface(fb, zero, m, ps, fx::kBlendAlpha, false), 0);
    CHECK_EQ(CountNonZero(fb), 0); // nothing drawn in any rejected case
    SurfaceDestroy(fb);
}
