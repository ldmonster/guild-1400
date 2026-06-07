#include "test.h"

#include "render/render_leaves5.h"
#include "render/particle_spawn.h"
#include "crt/rand.h"

#include <cmath>
#include <cstdint>
#include <cstring>

using namespace guild;
using guild::render::EffectHeader;
using guild::render::ParticleSystem;
using guild::render::Particle;

namespace {

bool FEq(float a, float b) {
    std::uint32_t ua, ub;
    std::memcpy(&ua, &a, 4);
    std::memcpy(&ub, &b, 4);
    return ua == ub;
}
template <class T> T Get(const void* base, int off) {
    T v; std::memcpy(&v, static_cast<const unsigned char*>(base) + off, sizeof(T)); return v;
}

// f32 rounding helper for golden oracles (mirror the host float casts).
float F(double x) { return static_cast<float>(x); }

// --- A clean in-process spawn backend so AllocSystem succeeds deterministically.
// (The default backend already does this; we just reset stats between cases.)

} // namespace

// ---------------------------------------------------------------------------
// InitColors — RNG per-slot colour seeding. Golden vectors computed with the
// LCG oracle (Srand(12345)). Loop seeds count-1 slots (verbatim guard).
// ---------------------------------------------------------------------------
TEST(RenderLeaves5_InitColors, GoldenSeed12345) {
    render::ResetSpawnStats();
    crt::Srand(99);
    ParticleSystem* sys = render::AllocSystem(7, reinterpret_cast<const u8*>("x"),
                                              1, 1.0f, 7, nullptr, /*slots*/ 5, 0);
    CHECK(sys != nullptr);
    if (!sys) return;

    // Snapshot slot 4's colour bytes (InitColors must NOT touch the last slot).
    Particle* pp = static_cast<Particle*>(sys->particles);
    u8 s4r = Get<u8>(&pp[4], 0x4C), s4g = Get<u8>(&pp[4], 0x4D), s4b = Get<u8>(&pp[4], 0x4E);

    crt::Srand(12345);
    u8 ret = render::InitColors(sys, 0, 0, 0, 5);

    const u8 expB[4] = {218, 232, 219, 227};
    const u8 expG[4] = {104, 131, 126, 144};
    const u8 expR[4] = {87, 95, 76, 77};
    Particle* p = static_cast<Particle*>(sys->particles);
    for (int i = 0; i < 4; ++i) {
        u8 b = Get<u8>(&p[i], 0x4E);
        u8 g = Get<u8>(&p[i], 0x4D);
        u8 r = Get<u8>(&p[i], 0x4C);
        CHECK_EQ((int)b, (int)expB[i]);
        CHECK_EQ((int)g, (int)expG[i]);
        CHECK_EQ((int)r, (int)expR[i]);
        CHECK_EQ((int)(Get<u8>(&p[i], 0x51) & 1), 0);  // active bit cleared
    }
    CHECK_EQ((int)ret, 77);
    // Slot 4 (the last) is NOT seeded by InitColors (guard i+1<count): its colour
    // bytes are byte-for-byte unchanged from the pre-call snapshot.
    CHECK_EQ((int)Get<u8>(&p[4], 0x4C), (int)s4r);
    CHECK_EQ((int)Get<u8>(&p[4], 0x4D), (int)s4g);
    CHECK_EQ((int)Get<u8>(&p[4], 0x4E), (int)s4b);
}

// ---------------------------------------------------------------------------
// SpawnBloodEffect — fully deterministic (no header overlay). Golden vectors
// from the LCG oracle (Srand(777)).
// ---------------------------------------------------------------------------
TEST(RenderLeaves5_SpawnBloodEffect, GoldenSeed777) {
    render::ResetSpawnStats();
    crt::Srand(777);
    int owner = 5;
    ParticleSystem* sys = render::SpawnBloodEffect(&owner, /*now*/ 0);
    CHECK(sys != nullptr);
    if (!sys) return;
    CHECK_EQ(sys->count, 100);

    // Re-run the oracle for the first 3 slots.
    crt::Srand(777);
    Particle* p = static_cast<Particle*>(sys->particles);
    const float kFanStep = render::kBloodFanStep;
    const float kFanScale = render::kBloodFanScale;
    for (int i = 0; i < 3; ++i) {
        float yfan = F(static_cast<double>(F((double)i)) * kFanStep);
        float s60 = F(static_cast<double>(yfan) * kFanScale);
        float rx = static_cast<float>(crt::RandNext());
        float s0 = F(static_cast<double>(rx) * render::kBloodRandNorm
                     * render::kBloodSpread + render::kBloodBiasX);
        float ry = static_cast<float>(crt::RandNext());
        float s4 = F(render::kBloodBaseY - static_cast<double>(ry)
                     * render::kBloodRandNorm * render::kBloodSpread + render::kBloodBiasY);
        float s8 = F(render::kBloodSumZ - static_cast<double>(s4));
        double fade = static_cast<double>(s60) * render::kBloodFadeScale + render::kBloodFadeBias;
        double fc = (fade < 0.0) ? 0.0 : fade;
        u8 fb = static_cast<u8>(static_cast<int>(fc));

        CHECK(FEq(Get<float>(&p[i], 0x3C), s60));
        CHECK(FEq(Get<float>(&p[i], 0x00), s0));
        CHECK(FEq(Get<float>(&p[i], 0x04), s4));
        CHECK(FEq(Get<float>(&p[i], 0x08), s8));
        CHECK_EQ((int)Get<u8>(&p[i], 0x4F), (int)fb);
        // slot+16/20/24 mirror slot+56/60/64 (which were 0/s60/0 pre-rand).
        CHECK(FEq(Get<float>(&p[i], 0x14), s60));
    }
}

// ---------------------------------------------------------------------------
// SpawnBlood — ballistic ctor. Header flags + per-slot active-bit clear.
// ---------------------------------------------------------------------------
TEST(RenderLeaves5_SpawnBlood, HeaderAndSlotClear) {
    render::ResetSpawnStats();
    crt::Srand(1);
    EffectHeader hdr;
    ParticleSystem* sys = render::SpawnBlood(/*owner*/ 9, 11, 22, 33, 44,
                                             /*startActive*/ 1, hdr, /*slots*/ 8, 0);
    CHECK(sys != nullptr);
    if (!sys) return;
    CHECK_EQ(hdr.i(32), 1 | (2 * 1));   // 3
    CHECK_EQ(hdr.i(0), 11);
    CHECK_EQ(hdr.i(4), 22);
    CHECK_EQ(hdr.i(8), 33);
    CHECK_EQ(hdr.i(12), 44);
    Particle* p = static_cast<Particle*>(sys->particles);
    for (int i = 0; i < 8; ++i)
        CHECK_EQ((int)(Get<u8>(&p[i], 0x51) & 1), 0);

    // startActive=0 -> flags = 1.
    EffectHeader hdr2;
    ParticleSystem* s2 = render::SpawnBlood(9, 0, 0, 0, 0, 0, hdr2, 4, 0);
    CHECK(s2 != nullptr);
    if (s2) CHECK_EQ(hdr2.i(32), 1);
}

// ---------------------------------------------------------------------------
// SpawnDebris — scatter ctor header layout.
// ---------------------------------------------------------------------------
TEST(RenderLeaves5_SpawnDebris, HeaderLayout) {
    render::ResetSpawnStats();
    float pos[3] = {1.0f, 2.0f, 3.0f};
    EffectHeader hdr;
    ParticleSystem* sys = render::SpawnDebris(/*owner*/ 4, pos, /*texSlot*/ 2, 5.0f,
                                              10, 11, 12, 13, 14, 15, 16, 90, hdr, 4, 0);
    CHECK(sys != nullptr);
    if (!sys) return;
    CHECK_EQ(hdr.i(32), 1);
    CHECK_EQ(hdr.i(0), 10);
    CHECK_EQ(hdr.i(4), 11);
    CHECK_EQ(hdr.i(8), 12);
    CHECK_EQ(hdr.i(12), 13);
    CHECK_EQ(hdr.i(16), 14);
    CHECK_EQ(hdr.i(20), 15);
    CHECK_EQ(hdr.i(24), 16);
    CHECK_EQ(hdr.i(36), 90);
    // Placed at pos.
    CHECK(FEq(sys->position[0], 1.0f));
    CHECK(FEq(sys->position[1], 2.0f));
    CHECK(FEq(sys->position[2], 3.0f));
}

// ---------------------------------------------------------------------------
// SpawnExplosion — cosine-wave radial burst. Verify the deterministic sin/cos
// ring math for the first slot against the in-test oracle (Srand reproducible).
// ---------------------------------------------------------------------------
TEST(RenderLeaves5_SpawnExplosion, RingMathSlot0) {
    render::ResetSpawnStats();
    const int slots = 12;
    const float radius = 4.0f;
    crt::Srand(5150);
    EffectHeader hdr;
    ParticleSystem* sys = render::SpawnExplosion(/*owner*/ 3, /*hdr0*/ 1, radius,
                                                 /*hdr2*/ 2, /*life*/ 2.0f,
                                                 /*hdr8*/ 7, hdr, slots, 0);
    CHECK(sys != nullptr);
    if (!sys) return;
    CHECK(FEq(hdr.f(4), radius));
    CHECK_EQ(hdr.i(0), 1);
    CHECK_EQ(hdr.i(8), 2);
    CHECK_EQ(hdr.i(32), 7);

    // Oracle for slot 0.
    crt::Srand(5150);
    int sextant = slots / 6;
    float inv = 1.0f / static_cast<float>(slots);
    float r0 = static_cast<float>(crt::RandNext()) * render::kExpRandNorm;
    float r1 = static_cast<float>(crt::RandNext()) * render::kExpRandNorm;
    float r2 = static_cast<float>(crt::RandNext()) * render::kExpRandNorm;
    float a = radius * render::kExpScaleHalf;
    float vx = r0 * a, vy = r1 * a, vz = a * r2;
    float b = radius * render::kExpScaleHalf2;
    float c = render::kExpScaleHalf * b;
    float ox = vx - c, oy = vy - b * render::kExpAxisDampZ, oz = vz - c;
    float angle = static_cast<float>(crt::RandNext()) * render::kExpRandNorm * render::kExpTwoPi;
    float yprof = F(std::sin(static_cast<double>(0) * render::kExpHalfPi * inv) * radius);
    float ring = F(std::sqrt(static_cast<double>(radius) * radius
                    - static_cast<double>(yprof) * yprof));
    float ex = F(std::sin(angle)) * ring + ox;
    float ey = yprof + oy;
    float ez = F(std::cos(angle)) * ring + oz;
    if (sextant != 0 && (0 % sextant) == 0) {
        ex = ex * render::kExpRingScaleX;
        ey = ey * render::kExpRingScaleY;
        ez = render::kExpRingScaleX * ez;
    }
    Particle* p = static_cast<Particle*>(sys->particles);
    CHECK(FEq(Get<float>(&p[0], 0x10), ex));
    CHECK(FEq(Get<float>(&p[0], 0x14), ey));
    CHECK(FEq(Get<float>(&p[0], 0x18), ez));
    CHECK_EQ((int)(Get<u8>(&p[0], 0x51) & 1), 1);  // active bit set
}

// ---------------------------------------------------------------------------
// SpawnSmoke — fade-out drift + colour. Verify slot 0 position/fade/colour.
// ---------------------------------------------------------------------------
TEST(RenderLeaves5_SpawnSmoke, DriftAndFadeSlot0) {
    render::ResetSpawnStats();
    float setPos[3] = {0, 0, 0};
    float posRec[3] = {10.0f, 20.0f, 30.0f};
    const float spread = 2.0f;
    const int a6 = 0x40000000;        // 2.0f as the fade pivot (sys+4)
    const int a7 = 0x12345678;
    const int col = 0x405060;         // colour triple
    crt::Srand(4242);
    EffectHeader hdr;
    ParticleSystem* sys = render::SpawnSmoke(/*owner*/ 8, setPos, posRec, /*texSlot*/ 3,
                                             /*life*/ 3.0f, a6, a7, /*a8*/ 5, spread,
                                             /*a11*/ 0, /*frameStep*/ 4, col, hdr, 6, 100);
    CHECK(sys != nullptr);
    if (!sys) return;
    CHECK_EQ(hdr.i(0), 5);
    CHECK_EQ(hdr.i(4), a6);

    // Oracle slot 0.
    crt::Srand(4242);
    float pivot;
    std::memcpy(&pivot, &a6, 4);
    float driftBias = spread * render::kSmokeDriftBias;
    // slot init: +0x10=posRec0, +0x14=a7(overwrite), +0x18=posRec2
    float p10 = posRec[0];
    float p14; std::memcpy(&p14, &a7, 4);
    float p18 = posRec[2];
    float r0 = static_cast<float>(crt::RandNext()) * render::kSmokeRandNorm;
    float r1 = static_cast<float>(crt::RandNext()) * render::kSmokeRandNorm;
    float r2 = static_cast<float>(crt::RandNext()) * render::kSmokeRandNorm;
    float d0 = r0 * spread - driftBias;
    float d1 = r1 * spread - driftBias;
    float d2 = r2 * spread - driftBias;
    p10 += d0; p14 += d1; p18 += d2;
    float fade = (p14 - pivot) / p14;

    Particle* p = static_cast<Particle*>(sys->particles);
    CHECK(FEq(Get<float>(&p[0], 0x10), p10));
    CHECK(FEq(Get<float>(&p[0], 0x14), p14));
    CHECK(FEq(Get<float>(&p[0], 0x18), p18));
    CHECK(FEq(Get<float>(&p[0], 0x1C), fade));
    CHECK(FEq(Get<float>(&p[0], 0x00), 1.0f / fade));
    CHECK_EQ((int)Get<u8>(&p[0], 0x4F), 0);  // shade cleared at spawn

    // colour bytes
    u8 rb = static_cast<u8>(crt::RandNext());
    u8 eb = static_cast<u8>(((col >> 24) & rb) + ((col >> 16) & 0xFF));
    CHECK_EQ((int)Get<u8>(&p[0], 0x4E), (int)eb);
}

// ---------------------------------------------------------------------------
// UpdateRainStep — per-frame integrator: integrate, gravity, respawn-on-fall,
// fade byte. Build one falling slot and one above-ground slot and check both.
// ---------------------------------------------------------------------------
TEST(RenderLeaves5_UpdateRainStep, IntegrateAndRespawn) {
    render::ResetSpawnStats();
    crt::Srand(2);
    ParticleSystem* sys = render::AllocSystem(3, reinterpret_cast<const u8*>("r"),
                                              1, 1.0f, 3, nullptr, /*slots*/ 2, 0);
    CHECK(sys != nullptr);
    if (!sys) return;
    sys->count = 2;
    sys->lifeBase = 9.0f;
    Particle* p = static_cast<Particle*>(sys->particles);

    auto put = [](void* s, int off, float v) { std::memcpy((unsigned char*)s + off, &v, 4); };
    auto puti = [](void* s, int off, std::uint32_t v) { std::memcpy((unsigned char*)s + off, &v, 4); };

    // Slot 0: stays above ground (slot+20 positive, vy keeps it >0).
    put(&p[0], 0x00, 0.1f);  put(&p[0], 0x04, 0.2f);  put(&p[0], 0x08, 0.3f);
    put(&p[0], 0x10, 1.0f);  put(&p[0], 0x14, 50.0f); put(&p[0], 0x18, 3.0f);
    puti(&p[0], 0x30, 0);    put(&p[0], 0x48, 0.0f);
    // Slot 1: falls below ground -> respawn (slot+60 becomes negative).
    put(&p[1], 0x00, 0.0f);  put(&p[1], 0x04, -5.0f); put(&p[1], 0x08, 0.0f);
    put(&p[1], 0x10, 0.0f);  put(&p[1], 0x14, 1.0f);  put(&p[1], 0x18, 0.0f);
    puti(&p[1], 0x30, 0);    put(&p[1], 0x48, 0.0f);

    const u32 now = 1000;
    crt::Srand(2);
    render::UpdateRainStep(sys, now);

    // Oracle slot 0 (no respawn).
    {
        float dt = F(static_cast<double>(now) * render::kRainDtScale);
        float s56 = 1.0f + 0.1f;
        float s60 = 50.0f + 0.2f;
        float s64 = 3.0f + 0.3f;
        (void)s56; (void)s64;
        // slot+72(0x48) = dt*2 + 0; slot+4 = 0.2 - dt
        float s48 = F(static_cast<double>(dt) * render::kRainGravity) + 0.0f;
        // not falling -> slot+16/20/24 = s56/s60/s64; fade from s60
        double fade = static_cast<double>(s60) * render::kRainFadeScale + render::kRainFadeBias;
        double fc = (fade >= 0.0) ? fade : 0.0;
        u8 fb = static_cast<u8>(static_cast<int>(fc));
        CHECK(FEq(Get<float>(&p[0], 0x14), s60));
        CHECK(FEq(Get<float>(&p[0], 0x48), s48));
        CHECK_EQ((int)Get<u8>(&p[0], 0x4F), (int)fb);
    }
    // Slot 1 respawned: slot+60 == 100.0f (its post-respawn value), slot+48 = lifeBase.
    {
        // After respawn the integrator writes slot+20 = slot+60 = 100.0f.
        CHECK(FEq(Get<float>(&p[1], 0x14), 100.0f));
        CHECK(FEq(Get<float>(&p[1], 0x48), 9.0f));
        CHECK_EQ((int)Get<std::uint32_t>(&p[1], 0x30), (int)now);
    }
}

// ---------------------------------------------------------------------------
// Validation: SpawnEffect rejects a null/invalid owner (no AllocSystem call).
// ---------------------------------------------------------------------------
TEST(RenderLeaves5_SpawnEffect, RejectsInvalidOwner) {
    float vel[3] = {1, 0, 0};
    EffectHeader hdr;
    CHECK(render::SpawnEffect(nullptr, 0, reinterpret_cast<const u8*>("x"), 0, vel,
                              1, 1.0f, 0, 0, 0, 0, 0.0f, hdr, 4, 0) == nullptr);
}
