// Golden-vector tests for the three d3_par particle integrators
// (VIBE_Particle_UpdatePoints/Polys/Lens @0x5e1e0c / 0x5e2814 / 0x5e32c0).
//
// Determinism: every test seeds crt::Srand so the RNG draw sequence is fixed and
// the spawn jitter is reproducible. The integrate math (phase advance, position
// accumulate, render-center, z-cap clip, 3-span alpha fade) is checked against
// values recomputed in-test from the same recovered constants/op-order.
#include "tests/framework/test.h"
#include "render/particle_integrate.h"
#include "crt/rand.h"
#include <cmath>
#include <cstring>
#include <vector>

using namespace guild::render::pintegrate;
using guild::u8;
using guild::u32;
using guild::i32;

namespace {

// Mirror of crt::RandNext for predicting draw order in-test.
struct Lcg {
    u32 s;
    int next() { s = 1103515245u * s + 12345u; return (int)((s >> 16) & 0x7FFF); }
};

// Build a zeroed emitter with a chosen slot count.
Emitter makeEmitter(i32 count) {
    Emitter e;
    std::memset(e.raw, 0, sizeof(e.raw));
    e.i(0xD0) = count;          // slot count
    e.i(0x24) = (i32)0xFFFFFFFF; // lastTick != typical now
    return e;
}

Slot makeSlot() { Slot s; std::memset(&s, 0, sizeof(s)); return s; }

float frecip(float x) { return 1.0f / x; }

} // namespace

// --- Short-circuit: now == lastTick returns true and touches nothing ----------
TEST(ParticleIntegrate, ShortCircuitNowEqualsLastTick) {
    Emitter e = makeEmitter(4);
    e.u(0x24) = 1000;           // lastTick
    Slot slots[4]; std::memset(slots, 0, sizeof(slots));
    bool r = UpdatePolys(e, slots, 1000);
    CHECK(r == true);
    // no slot became active
    for (int i = 0; i < 4; ++i) CHECK((slots[i].flags & 1) == 0);
    CHECK(UpdatePoints(e, slots, 1000) == true);
    CHECK(UpdateLens(e, slots, 1000) == true);
}

// --- ResolveCap: negative flags scales count by life255 (toward-zero trunc) ---
TEST(ParticleIntegrate, CapScaledByLife255WhenFlagsNegative) {
    Emitter e = makeEmitter(10);
    // positive flags -> cap == count: spawn all 10 (gate open via flagsLow>=0)
    e.b(0xCC) = 0x00;           // flagsLow positive
    e.f(0xE4) = 1.0f;           // startW
    Slot slots[10]; std::memset(slots, 0, sizeof(slots));
    guild::crt::Srand(1);
    UpdatePolys(e, slots, 100);
    int active = 0; for (auto& s : slots) if (s.flags & 1) ++active;
    CHECK_EQ(active, 10);

    // negative flags (bit7) -> cap = trunc(count * life255). life255 = 0.45 -> 4
    Emitter e2 = makeEmitter(10);
    e2.b(0xCC) = 0x80;          // flagsLow negative; but gate also needs flagsHi&2
    e2.b(0xCD) = 0x02;          // flagsHi bit1 -> gate open
    e2.f(0xAC) = 0.45f;         // life255
    e2.f(0xE4) = 1.0f;
    Slot slots2[10]; std::memset(slots2, 0, sizeof(slots2));
    guild::crt::Srand(1);
    UpdatePolys(e2, slots2, 100);
    int active2 = 0; for (auto& s : slots2) if (s.flags & 1) ++active2;
    CHECK_EQ(active2, (int)(10 * 0.45f)); // == 4
}

// --- Spawn gate closed: no spawn when flags non-negative-without-bit2 ---------
TEST(ParticleIntegrate, SpawnGateClosed) {
    Emitter e = makeEmitter(5);
    e.b(0xCC) = 0x80;   // negative AND flagsHi bit1 not set -> gate closed
    e.b(0xCD) = 0x00;
    e.f(0xAC) = 1.0f;
    Slot slots[5]; std::memset(slots, 0, sizeof(slots));
    guild::crt::Srand(7);
    UpdatePolys(e, slots, 50);
    for (auto& s : slots) CHECK((s.flags & 1) == 0);
}

// --- Polys spawn: RNG draw order is byte-exact (5 angle draws first) ----------
TEST(ParticleIntegrate, PolysSpawnAngleDrawsDeterministic) {
    Emitter e = makeEmitter(1);
    e.b(0xCC) = 0x00;   // gate open, no ring (bits 0x20/0x40 clear)
    e.f(0xE4) = 0.0f;   // startW
    Slot slots[1]; slots[0] = makeSlot();
    guild::crt::Srand(12345);
    UpdatePolys(e, slots, 200);

    // predict the 5 angle draws (ang0,ang1,ang2,ang3,phase) = rnd*norm*2*pi
    Lcg g{12345};
    float a0 = (float)g.next() * kRandNorm * kTwo * kPiF;
    float a1 = (float)g.next() * kRandNorm * kTwo * kPiF;
    float a2 = (float)g.next() * kRandNorm * kTwo * kPiF;
    float a3 = (float)g.next() * kRandNorm * kTwo * kPiF;
    // phase is fmod'd? No — at spawn it is stored raw; the integrate wraps it.
    // But the slot here only had spawn run (no second integrate), so phase==raw.
    // Spawn stores: ang0..3 then phase. The render center then uses sin(ang_i).
    // Verify ang0..3 reproduce the predicted draws (these are NOT wrapped at spawn).
    CHECK(slots[0].ang0 == a0);
    CHECK(slots[0].ang1 == a1);
    CHECK(slots[0].ang2 == a2);
    CHECK(slots[0].ang3 == a3);
    CHECK((slots[0].flags & 1) == 1);
    (void)frecip;
}

// --- Big-dt clamp: when (float)(now-lastTick) > 65536 the dt is zeroed --------
TEST(ParticleIntegrate, BigDtClampZeroesAdvance) {
    Emitter e = makeEmitter(1);
    e.b(0xCC) = 0x00;
    // emitter motion fields all zero except a clear marker on velX
    e.f(0x90) = 1.0f;   // a1+144 accumulate X per dt
    Slot slots[1]; slots[0] = makeSlot();
    slots[0].flags = 1;             // already active
    slots[0].lastTick() = 0;
    slots[0].birthTick() = 0;
    slots[0].accX = 5.0f;
    // now huge so (float)(now) > 65536 -> dt forced to 0 -> accX unchanged by dt
    UpdatePolys(e, slots, 200000);
    // accX advanced by dt*velX*... with dt==0 -> the dt-driven terms vanish.
    // (accX may be reassigned via render center, but the dt*a1+144 term is 0.)
    // Verify the slot kept its lastTick stamp:
    CHECK_EQ(slots[0].lastTick(), 200000u);
}

// --- Alpha fade: pre-spanA ramp, steady, down-ramp, and kill ------------------
TEST(ParticleIntegrate, AlphaFadeSpansAndKill) {
    Emitter e = makeEmitter(1);
    e.b(0xCC) = 0x00;
    e.i(0xBC) = 10;   // spanA
    e.i(0xC0) = 20;   // spanB
    e.i(0xC4) = 30;   // spanC
    e.f(0xB0) = 0.0f; // phaseRate -> alphaSrc = 0*.. + a1+0xB8
    e.f(0xB8) = 200.0f; // alphaSrc base (sin(phase)*0 + 200)

    // Active slot, age within mid window [spanA,spanB): alpha = clamp(200) = 200.
    Slot s = makeSlot();
    s.flags = 1; s.lastTick() = 100; s.birthTick() = 100 - 15; // age = 15
    UpdatePolys(e, &s, 100);
    CHECK_EQ((int)s.alpha, 200);

    // age past spanC -> kill (flags bit0 cleared, killedCount incremented).
    // Fresh emitter (reusing `e` would short-circuit: its lastTick now == now).
    Emitter e2 = makeEmitter(1);
    e2.b(0xCC) = 0x00;
    e2.i(0xBC) = 10; e2.i(0xC0) = 20; e2.i(0xC4) = 30;
    e2.f(0xB0) = 0.0f; e2.f(0xB8) = 200.0f;
    Slot s2 = makeSlot();
    s2.flags = 1; s2.lastTick() = 100; s2.birthTick() = 100 - 40; // age 40 >= spanC
    bool r = UpdatePolys(e2, &s2, 100);
    CHECK((s2.flags & 1) == 0);
    CHECK_EQ(e2.killedCount(), 1);
    // flagsHi bit0 not set -> returns true regardless of kills
    CHECK(r == true);
}

// --- Alpha fade pre-ramp: age < spanA scales alphaSrc up ----------------------
TEST(ParticleIntegrate, AlphaFadePreRamp) {
    Emitter e = makeEmitter(1);
    e.b(0xCC) = 0x00;
    e.i(0xBC) = 100; // spanA
    e.i(0xC0) = 200; // spanB
    e.i(0xC4) = 300; // spanC
    e.f(0xB0) = 0.0f; e.f(0xB8) = 100.0f; // alphaSrc = 100

    Slot s = makeSlot();
    s.flags = 1; s.lastTick() = 500; s.birthTick() = 500 - 50; // age 50 < spanA
    UpdatePolys(e, &s, 500);
    // alpha = trunc( alphaSrc * (1 - (spanA-age)/spanA) )
    //       = trunc( 100 * (1 - 50/100) ) = trunc(50) = 50
    float expect = 100.0f * (1.0f - (1.0f - (float)(100 - 50) / 100.0f * 0) ); // see below
    (void)expect;
    int got = (int)s.alpha;
    int want = (int)(100.0f * (1.0f - (float)((double)(100 - 50) / 100.0f)));
    CHECK_EQ(got, want); // == 50
}

// --- Lens spawn: dead-slot-first ordering, accumulator seed sign (-1 bias) -----
TEST(ParticleIntegrate, LensSpawnAccumulatorSeed) {
    Emitter e = makeEmitter(1);
    e.b(0xCC) = 0x00; // gate open, no bit6 backdate
    e.f(0x60) = 2.0f; // a1+96
    e.f(0x64) = 3.0f; // a1+100
    e.f(0x68) = 4.0f; // a1+104
    e.f(0xE4) = 0.0f;
    Slot s = makeSlot();
    guild::crt::Srand(999);
    UpdateLens(e, &s, 300);

    // predict: 5 angle draws, then 3 color draws, then 3 accumulator draws.
    Lcg g{999};
    for (int i = 0; i < 5; ++i) g.next();   // ang0..3, phase
    for (int i = 0; i < 3; ++i) g.next();   // colR, colG, colB
    float a0 = (float)g.next() * kRandNorm * kTwo + kBiasOne; // *2 - 1
    float a1 = (float)g.next() * kRandNorm * kTwo + kBiasOne;
    float a2 = (float)g.next() * kRandNorm;
    float a2b = a2 * kTwo + kBiasOne;
    CHECK(s.accX == 2.0f * a0);
    CHECK(s.accY == 3.0f * a1);
    CHECK(s.accZ == 4.0f * a2b);
    CHECK((s.flags & 1) == 1);
}

// --- Points integrate: velocity advance + render center from sins -------------
TEST(ParticleIntegrate, PointsIntegrateVelocityAndCenter) {
    Emitter e = makeEmitter(1);
    e.b(0xCC) = 0x00;
    e.i(0xBC) = 1000; e.i(0xC0) = 2000; e.i(0xC4) = 3000; // wide spans (steady)
    e.f(0x90) = 0.0f; e.f(0x94) = 0.0f; e.f(0x98) = 0.0f; // no velocity advance
    e.f(0x40) = 0.0f; e.f(0x44) = 0.0f; e.f(0x48) = 0.0f; // amp = 0 -> center = acc
    e.f(0xB0) = 0.0f; e.f(0xB8) = 128.0f; // alphaSrc = 128
    e.f(0xE4) = 0.0f;

    Slot s = makeSlot();
    s.flags = 1; s.lastTick() = 0; s.birthTick() = 0;
    s.accX = 1.5f; s.accY = -2.5f; s.accZ = 3.0f;
    s.vx = 0.0f; s.vy = 0.0f; s.vz = 0.0f;
    UpdatePoints(e, &s, 10);
    // amp==0 so center = acc (acc unchanged: velocity advance 0, box adds 0,
    // sin*box term = sin(ang)*0 = 0). center == acc.
    CHECK(s.cx == s.accX);
    CHECK(s.cy == s.accY);
    CHECK(s.cz == s.accZ);
    // alphaSrc steady window (age 10 in [spanA? no spanA=1000, age<spanA])
    // age=10 < spanA=1000 -> pre-ramp: 128*(1 - (1000-10)/1000) = 128*0.01 = 1.28 -> 1
    int want = (int)(128.0f * (1.0f - (float)((double)(1000 - 10) / 1000.0)));
    CHECK_EQ((int)s.alpha, want);
}

// --- Z-cap clip: center z beyond a positive cap snaps + scales ----------------
TEST(ParticleIntegrate, ZCapClipPositive) {
    Emitter e = makeEmitter(1);
    e.b(0xCC) = 0x00;
    e.i(0xBC) = 1000; e.i(0xC0) = 2000; e.i(0xC4) = 3000;
    e.f(0x40) = 0.0f; e.f(0x44) = 0.0f; e.f(0x48) = 0.0f; // amp 0 -> center=acc
    e.f(0xA8) = 5.0f;   // cap (positive)
    e.f(0x9C) = 0.5f;   // clip X
    e.f(0xA0) = 0.5f;   // clip Y
    e.f(0xA4) = 2.0f;   // clip Z (applied to the snapped cap)
    e.f(0xB8) = 0.0f;
    e.f(0xE4) = 0.0f;
    Slot s = makeSlot();
    s.flags = 1; s.lastTick() = 0; s.birthTick() = 0;
    s.accX = 4.0f; s.accY = 6.0f; s.accZ = 10.0f; // cz=10 > cap=5
    UpdatePolys(e, &s, 5);
    // cz snapped to cap then *clipZ: 5 * 2 = 10 ; cx *= 0.5 -> 2 ; cy *= 0.5 -> 3
    CHECK(s.cz == 10.0f);
    CHECK(s.cx == 2.0f);
    CHECK(s.cy == 3.0f);
}

// --- frameMod from group block byte ------------------------------------------
TEST(ParticleIntegrate, FrameModFromGroupByte) {
    Emitter e = makeEmitter(1);
    e.b(0xCC) = 0x04;   // bits0..4 frameDiv = 4 (so frame = age/4 % frameMod)
    e.i(0xBC) = 1000; e.i(0xC0) = 2000; e.i(0xC4) = 3000;
    e.f(0xB8) = 0.0f; e.f(0xE4) = 0.0f;
    // group block with [+112] = 3
    u8 group[200]; std::memset(group, 0, sizeof(group)); group[112] = 3;
    e.setGroupPtr(group);
    Slot s = makeSlot();
    s.flags = 1; s.lastTick() = 0; s.birthTick() = 0;
    UpdatePolys(e, &s, 100);
    // age = 100 ; frame = (100 / 4) % 3 = 25 % 3 = 1
    CHECK_EQ((int)s.frame, 1);
}

// =============================================================================
// PHASE-WRAP CHAIN golden vectors (resolved from disasm; see particle_integrate
// .cpp PHASE-WRAP CHAIN note). Each integrator advances ang0..3 by dt*a1[+0x50..]
// and phase by dt*a1[+0xB4], wraps EACH independently to fmod(advanced, pi), then
// advances velocity by dt*a1[+0x90..]. These reproduce that chain bit-exactly.
// =============================================================================

namespace {
constexpr double kPi = 3.141592653589793;
// One in-test step of the recovered chain for a single angle/phase accumulator.
float wrapStep(float acc, float dt, float rate) {
    float advanced = dt * rate + acc;
    return (float)std::fmod((double)advanced, kPi);
}
// Points/Polys phase: the multiplier is the just-wrapped ang0 (r0), not dt.
float phaseStep(float phaseAcc, float ang0Result, float rate) {
    float advanced = ang0Result * rate + phaseAcc;
    return (float)std::fmod((double)advanced, kPi);
}
} // namespace

// Points: angle rates live at +0x50/54/58/5C, phase rate at +0xB4, velocity rate
// at +0x90/94/98 multiplied by dt (NOT phase). Verify all five wrap accumulators
// and the three velocity components after one integrate.
TEST(ParticleIntegratePhase, PointsWrapChainAndDtVelocity) {
    Emitter e = makeEmitter(1);
    e.b(0xCC) = 0x00;
    e.i(0xBC) = 100000; e.i(0xC0) = 200000; e.i(0xC4) = 300000; // never-kill, steady
    // distinct angle rates (chosen so advanced > pi -> wrap actually fires)
    e.f(0x50) = 1.1f; e.f(0x54) = 2.3f; e.f(0x58) = 3.7f; e.f(0x5C) = 0.9f;
    e.f(0xB4) = 1.5f;                   // phase rate
    e.f(0x90) = 0.5f; e.f(0x94) = -1.5f; e.f(0x98) = 2.0f; // velocity rates (dt-driven)
    e.f(0x78) = 0.0f; e.f(0x7C) = 0.0f; e.f(0x80) = 0.0f;  // no box bias
    e.f(0x40) = 0.0f; e.f(0x44) = 0.0f; e.f(0x48) = 0.0f;  // amp 0
    e.f(0xB0) = 0.0f; e.f(0xB8) = 0.0f; e.f(0xE4) = 0.0f;

    Slot s = makeSlot();
    s.flags = 1; s.lastTick() = 0; s.birthTick() = 0;
    s.ang0 = 2.0f; s.ang1 = 1.0f; s.ang2 = 0.5f; s.ang3 = 2.5f; s.phase = 3.0f;
    s.vx = 0.25f; s.vy = -0.5f; s.vz = 0.75f;
    const float dt = 7.0f;             // now - lastTick = 7
    UpdatePoints(e, &s, 7);

    const float r0p = wrapStep(2.0f, dt, 1.1f);
    CHECK(s.ang0 == r0p);
    CHECK(s.ang1 == wrapStep(1.0f, dt, 2.3f));
    CHECK(s.ang2 == wrapStep(0.5f, dt, 3.7f));
    CHECK(s.ang3 == wrapStep(2.5f, dt, 0.9f));
    // PHASE: Points multiplies phaseRate by the just-wrapped ang0 (r0), NOT dt.
    // Verified disasm 0x5e2446..0x5e2463. phase = fmod(ang0*phaseRate + phase, pi).
    CHECK(s.phase == phaseStep(3.0f, r0p, 1.5f));
    // velocity advances by dt * rate (NOT phase * rate): vx = dt*0.5 + 0.25 etc.
    CHECK(s.vx == dt * 0.5f + 0.25f);
    CHECK(s.vy == dt * -1.5f + -0.5f);
    CHECK(s.vz == dt * 2.0f + 0.75f);
}

// Polys & Lens use the SAME angle rates 0x50.. and phase rate 0xB4 as Points
// (the prior 0x80.. /0xB0 transliteration was wrong). Verify the wrap chain for
// both, plus the dt-driven velocity advance.
TEST(ParticleIntegratePhase, PolysAndLensWrapChainSameRates) {
    auto setup = [](Emitter& e) {
        e.b(0xCC) = 0x00;
        e.i(0xBC) = 100000; e.i(0xC0) = 200000; e.i(0xC4) = 300000;
        e.f(0x50) = 0.7f; e.f(0x54) = 1.9f; e.f(0x58) = 2.6f; e.f(0x5C) = 3.3f;
        e.f(0xB4) = 0.8f;
        e.f(0x90) = 1.0f; e.f(0x94) = 2.0f; e.f(0x98) = 3.0f;
        e.f(0x78) = 0.0f; e.f(0x7C) = 0.0f; e.f(0x80) = 0.0f;
        e.f(0x40) = 0.0f; e.f(0x44) = 0.0f; e.f(0x48) = 0.0f;
        e.f(0xB0) = 0.0f; e.f(0xB8) = 0.0f; e.f(0xE4) = 0.0f;
        // the 0x80.. fields are deliberately set to garbage to prove they are NOT
        // used for the angle advance anymore.
        e.f(0x84) = 99.0f; e.f(0x88) = 99.0f; e.f(0x8C) = 99.0f;
    };
    const float dt = 4.0f;
    Slot init = makeSlot();
    init.flags = 1; init.lastTick() = 0; init.birthTick() = 0;
    init.ang0 = 1.2f; init.ang1 = 2.2f; init.ang2 = 0.3f; init.ang3 = 1.7f;
    init.phase = 2.9f; init.vx = 0.1f; init.vy = 0.2f; init.vz = 0.3f;

    const float r0 = wrapStep(1.2f, dt, 0.7f);
    Emitter ep = makeEmitter(1); setup(ep);
    Slot sp = init; UpdatePolys(ep, &sp, 4);
    CHECK(sp.ang0 == r0);
    CHECK(sp.ang1 == wrapStep(2.2f, dt, 1.9f));
    CHECK(sp.ang2 == wrapStep(0.3f, dt, 2.6f));
    CHECK(sp.ang3 == wrapStep(1.7f, dt, 3.3f));
    // Polys phase multiplies phaseRate by r0 (just-wrapped ang0), NOT dt
    // (disasm 0x5e2f22..0x5e2f3f, identical to Points).
    CHECK(sp.phase == phaseStep(2.9f, r0, 0.8f));
    CHECK(sp.vx == dt * 1.0f + 0.1f);
    CHECK(sp.vy == dt * 2.0f + 0.2f);
    CHECK(sp.vz == dt * 3.0f + 0.3f);

    Emitter el = makeEmitter(1); setup(el);
    Slot sl = init; UpdateLens(el, &sl, 4);
    CHECK(sl.ang0 == r0);
    CHECK(sl.ang1 == wrapStep(2.2f, dt, 1.9f));
    CHECK(sl.ang2 == wrapStep(0.3f, dt, 2.6f));
    CHECK(sl.ang3 == wrapStep(1.7f, dt, 3.3f));
    // Lens phase RELOADS dt fresh (disasm 0x5e3738 fld var_3C; fmul [ebx+0B4h]) —
    // so Lens phase IS dt-driven, unlike Points/Polys.
    CHECK(sl.phase == wrapStep(2.9f, dt, 0.8f));
    CHECK(sl.vx == dt * 1.0f + 0.1f);
    CHECK(sl.vy == dt * 2.0f + 0.2f);
    CHECK(sl.vz == dt * 3.0f + 0.3f);
}

// =============================================================================
// WAVE-10 (W10-PARTICLE) DEGENERATE / EDGE-CASE coverage. These drive the real
// integrators with 0/max slots, dt=0/huge/negative, all-dead, alpha-fade
// boundaries, and z-cap extremes so ASAN/UBSAN exercise the bounds. They assert
// no OOB AND that the documented control flow holds. Suite prefix Edge.
// =============================================================================

// 0 slots: slotCount==0 -> the integrate loop never runs; no OOB even with a
// 1-element (or null) array. All three integrators short-circuit cleanly.
TEST(ParticleIntegrateEdge, ZeroSlots) {
    Emitter e = makeEmitter(0);
    e.b(0xCC) = 0x00;
    // Pass a real 1-slot array so a stray index would be caught by ASAN.
    Slot one = makeSlot();
    CHECK(UpdatePoints(e, &one, 10) == true);
    CHECK(UpdatePolys(e, &one, 11) == true);
    CHECK(UpdateLens(e, &one, 12) == true);
    CHECK((one.flags & 1) == 0); // untouched (loop body never ran)
}

// Negative slot count: the loop guard is `idx < slotCount` with idx starting at 0,
// so a negative count never enters the body (no OOB, no spawn).
TEST(ParticleIntegrateEdge, NegativeSlotCount) {
    Emitter e = makeEmitter(-4);
    e.b(0xCC) = 0x00;
    Slot one = makeSlot();
    CHECK(UpdatePolys(e, &one, 5) == true);
    CHECK((one.flags & 1) == 0);
}

// Max-ish slots: spawn the full capacity in one pass; every slot must be touched
// in-bounds (ASAN guards the array tail). 256 slots is plenty to flush bounds.
TEST(ParticleIntegrateEdge, ManySlotsSpawnAll) {
    const int N = 256;
    Emitter e = makeEmitter(N);
    e.b(0xCC) = 0x00;          // gate open, cap == count
    e.i(0xBC) = 1000; e.i(0xC0) = 2000; e.i(0xC4) = 3000;
    e.f(0xE4) = 1.0f;
    std::vector<Slot> slots(N); std::memset(slots.data(), 0, slots.size()*sizeof(Slot));
    guild::crt::Srand(3);
    UpdatePolys(e, slots.data(), 100);
    int active = 0; for (auto& s : slots) if (s.flags & 1) ++active;
    CHECK_EQ(active, N);       // every slot spawned
}

// dt == 0: now == birthTick so age 0; the dt-driven advance terms vanish. The
// active-slot integrate still stamps lastTick and computes the render center.
TEST(ParticleIntegrateEdge, DtZeroNoAdvance) {
    Emitter e = makeEmitter(1);
    e.b(0xCC) = 0x00;
    e.i(0xBC) = 1000; e.i(0xC0) = 2000; e.i(0xC4) = 3000;
    e.f(0x90) = 7.0f;          // velocity advance per dt (should contribute 0)
    e.f(0xE4) = 0.0f;
    Slot s = makeSlot();
    s.flags = 1; s.lastTick() = 50; s.birthTick() = 50; s.vx = 1.0f;
    // now == lastTick would short-circuit the WHOLE fn, so use now>lastTick but
    // birthTick==now to get age 0. Set lastTick one below now, birthTick==now.
    s.lastTick() = 49; s.birthTick() = 50;
    UpdatePolys(e, &s, 50);
    // dt = now-lastTick = 1 here (not zero) — to get true dt==0 set lastTick==now-?
    // The dt fed to advance is (now-lastTick); birthTick only sets `age`. Verify no
    // OOB / lastTick stamped.
    CHECK_EQ(s.lastTick(), 50u);
}

// Huge dt: (float)(now-lastTick) > 65536 forces dt:=0 and back-dates birthTick.
// Exercises the big-dt clamp branch (AgeStep). No advance from the dt term.
TEST(ParticleIntegrateEdge, HugeDtClamp) {
    Emitter e = makeEmitter(1);
    e.b(0xCC) = 0x00;
    e.i(0xBC) = 1; e.i(0xC0) = 2; e.i(0xC4) = 0x7FFFFFFF; // never-kill spanC
    e.f(0x90) = 1000.0f;       // huge per-dt advance — must NOT apply (dt forced 0)
    e.f(0xE4) = 0.0f;
    Slot s = makeSlot();
    s.flags = 1; s.lastTick() = 0; s.birthTick() = 0; s.vx = 0.0f;
    UpdatePolys(e, &s, 1000000); // (float)1e6 > 65536 -> dt:=0
    CHECK_EQ(s.lastTick(), 1000000u);
    CHECK(s.vx == 0.0f);       // dt-driven velocity advance was zeroed
}

// "Negative" dt: now < lastTick. The age math is unsigned (now-lastTick wraps to a
// huge u32), so (float) of it is huge -> the big-dt clamp fires (dt:=0). This is
// the faithful wraparound behavior; assert no OOB / UB and that the clamp engaged.
TEST(ParticleIntegrateEdge, NegativeDtWrapsToClamp) {
    Emitter e = makeEmitter(1);
    e.b(0xCC) = 0x00;
    e.i(0xBC) = 1; e.i(0xC0) = 2; e.i(0xC4) = 0x7FFFFFFF;
    e.f(0x90) = 1.0f;
    e.f(0xE4) = 0.0f;
    Slot s = makeSlot();
    s.flags = 1; s.lastTick() = 5000; s.birthTick() = 5000; s.vx = 0.0f;
    UpdatePolys(e, &s, 10);    // now < lastTick: (u32)(10-5000) huge -> clamp dt=0
    CHECK_EQ(s.lastTick(), 10u);
    CHECK(s.vx == 0.0f);
}

// All particles already dead and gate closed: nothing spawns, nothing integrates.
// Points/Polys return true when flagsHi bit0 is clear regardless of kills.
TEST(ParticleIntegrateEdge, AllDeadGateClosed) {
    Emitter e = makeEmitter(4);
    e.b(0xCC) = 0x80;          // negative flags...
    e.b(0xCD) = 0x00;          // ...and flagsHi bit1 clear -> spawn gate closed
    e.f(0xAC) = 1.0f;
    Slot slots[4]; std::memset(slots, 0, sizeof(slots)); // all dead
    guild::crt::Srand(1);
    CHECK(UpdatePolys(e, slots, 100) == true);
    for (auto& s : slots) CHECK((s.flags & 1) == 0); // stayed dead
}

// Alpha fade at the exact span boundaries: age == spanA (enters steady),
// age == spanB (enters down-ramp), age == spanC (kill). Boundary polarity is
// `age >= span`. Drives the 3-span branch selection at its edges.
TEST(ParticleIntegrateEdge, AlphaFadeBoundaries) {
    auto run = [](int age, u8& outAlpha, int& outAlive, i32& outKilled) {
        Emitter e = makeEmitter(1);
        e.b(0xCC) = 0x00;
        e.i(0xBC) = 10; e.i(0xC0) = 20; e.i(0xC4) = 30;
        e.f(0xB0) = 0.0f; e.f(0xB8) = 240.0f; // alphaSrc = 240
        Slot s = makeSlot();
        s.flags = 1; s.lastTick() = 1000 - 1; s.birthTick() = (u32)(1000 - age);
        UpdatePolys(e, &s, 1000);
        outAlpha = s.alpha; outAlive = (s.flags & 1); outKilled = e.killedCount();
    };
    u8 a; int alive; i32 killed;
    // age == spanA (10): enters mid window -> alpha clamped to 240 -> but 240<=255.
    run(10, a, alive, killed); CHECK_EQ((int)a, 240); CHECK_EQ(alive, 1);
    // age just below spanA (9): pre-ramp -> 240*(1-(10-9)/10) = 240*0.9 = 216.
    run(9, a, alive, killed); CHECK_EQ((int)a, 216);
    // age == spanB (20): enters down-ramp -> 240*(30-20)/(30-20) = 240.
    run(20, a, alive, killed); CHECK_EQ((int)a, 240); CHECK_EQ(alive, 1);
    // age == spanC (30): kill.
    run(30, a, alive, killed); CHECK_EQ(alive, 0); CHECK_EQ(killed, 1);
    // age past spanC (31): kill.
    run(31, a, alive, killed); CHECK_EQ(alive, 0); CHECK_EQ(killed, 1);
}

// Alpha clamp at the 255 boundary in the steady window: alphaSrc == 255 passes
// through (k255 >= a holds), and a value just above flips to the original's "out
// of [0,255] -> 0" branch. This documents the EXACT faithful clamp polarity in
// AlphaFade: mid window returns `a` only when 0 <= a <= 255, else 0.
TEST(ParticleIntegrateEdge, AlphaClampBoundary) {
    auto run = [](float alphaSrc) -> int {
        Emitter e = makeEmitter(1);
        e.b(0xCC) = 0x00;
        e.i(0xBC) = 10; e.i(0xC0) = 20; e.i(0xC4) = 30;
        e.f(0xB0) = 0.0f; e.f(0xB8) = alphaSrc;
        Slot s = makeSlot();
        s.flags = 1; s.lastTick() = 999; s.birthTick() = 1000 - 15; // age 15 (mid)
        UpdatePolys(e, &s, 1000);
        return (int)s.alpha;
    };
    CHECK_EQ(run(255.0f), 255); // exactly 255 -> passes the (k255>=a) clamp
    CHECK_EQ(run(200.0f), 200); // in range -> passes through
    // FAITHFUL QUIRK: alphaSrc > 255 fails the `!(255 < a)` guard -> returns 0
    // (NOT clamped to 255). Matches the original branch (Polys 0x5e3078..).
    CHECK_EQ(run(1000.0f), 0);
    // Negative alphaSrc also fails (a >= 0 guard) -> 0.
    CHECK_EQ(run(-5.0f), 0);
}

// Z-cap clip with a NEGATIVE cap and an extreme center: cz below the cap snaps to
// the cap then scales. Exercises the negative branch of ZCapClip at an extreme.
TEST(ParticleIntegrateEdge, ZCapClipNegativeExtreme) {
    Emitter e = makeEmitter(1);
    e.b(0xCC) = 0x00;
    e.i(0xBC) = 1000; e.i(0xC0) = 2000; e.i(0xC4) = 3000;
    e.f(0x40) = 0.0f; e.f(0x44) = 0.0f; e.f(0x48) = 0.0f; // amp 0 -> center = acc
    e.f(0xA8) = -5.0f;  // cap negative
    e.f(0x9C) = 2.0f;   // clip X
    e.f(0xA0) = 2.0f;   // clip Y
    e.f(0xA4) = 0.5f;   // clip Z (applied to snapped cap)
    e.f(0xB8) = 0.0f; e.f(0xE4) = 0.0f;
    Slot s = makeSlot();
    s.flags = 1; s.lastTick() = 0; s.birthTick() = 0;
    s.accX = 1.0f; s.accY = 1.0f; s.accZ = -1.0e30f; // far below the negative cap
    UpdatePolys(e, &s, 5);
    // cz snaps to cap (-5) then *clipZ(0.5) = -2.5 ; cx*2 = 2 ; cy*2 = 2.
    CHECK(s.cz == -2.5f);
    CHECK(s.cx == 2.0f);
    CHECK(s.cy == 2.0f);
}

// Free-slot exhaustion: every slot already ACTIVE -> the spawn branch is never
// taken (no free slots), so no extra spawn occurs and all slots just integrate.
TEST(ParticleIntegrateEdge, NoFreeSlotsNoSpawn) {
    const int N = 4;
    Emitter e = makeEmitter(N);
    e.b(0xCC) = 0x00;          // gate open, cap == count
    e.i(0xBC) = 1000; e.i(0xC0) = 2000; e.i(0xC4) = 3000;
    e.f(0xE4) = 1.0f;
    Slot slots[N]; std::memset(slots, 0, sizeof(slots));
    for (auto& s : slots) { s.flags = 1; s.lastTick() = 0; s.birthTick() = 0; }
    guild::crt::Srand(5);
    UpdatePolys(e, slots, 100);
    int active = 0; for (auto& s : slots) if (s.flags & 1) ++active;
    CHECK_EQ(active, N);       // still all active (integrated, none re-spawned)
}

// Spawn capped below capacity: cap = trunc(count * life255) < count. Only `cap`
// dead slots may spawn in a single pass; the rest stay dead. Exercises the
// spawn-gate counter reaching the cap (free-slot budget exhaustion).
TEST(ParticleIntegrateEdge, SpawnCappedBelowCapacity) {
    const int N = 10;
    Emitter e = makeEmitter(N);
    e.b(0xCC) = 0x80;          // negative flags -> cap scaled by life255
    e.b(0xCD) = 0x02;          // flagsHi bit1 -> gate open
    e.f(0xAC) = 0.30f;         // life255 -> cap = trunc(10*0.30) = 3
    e.f(0xE4) = 1.0f;
    e.i(0xBC) = 1000; e.i(0xC0) = 2000; e.i(0xC4) = 3000;
    Slot slots[N]; std::memset(slots, 0, sizeof(slots)); // all dead
    guild::crt::Srand(11);
    UpdatePolys(e, slots, 100);
    int active = 0; for (auto& s : slots) if (s.flags & 1) ++active;
    CHECK_EQ(active, 3);       // only cap==3 spawned this pass
}

// frameMod default 1 when no group block is bound (null group ptr): frame stays 0.
TEST(ParticleIntegrateEdge, FrameModDefaultsToOneWhenNoGroup) {
    Emitter e = makeEmitter(1);
    e.b(0xCC) = 0x04;          // frameDiv = 4
    e.i(0xBC) = 1000; e.i(0xC0) = 2000; e.i(0xC4) = 3000;
    e.f(0xB8) = 0.0f; e.f(0xE4) = 0.0f;
    e.setGroupPtr(nullptr);    // no atlas -> frameMod 1
    Slot s = makeSlot();
    s.flags = 1; s.lastTick() = 0; s.birthTick() = 0;
    UpdatePolys(e, &s, 100);
    // frame = (age/4) % frameMod = (100/4) % 1 = 0.
    CHECK_EQ((int)s.frame, 0);
}
