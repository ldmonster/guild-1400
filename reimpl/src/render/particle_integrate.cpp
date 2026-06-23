#include "render/particle_integrate.h"
#include "crt/rand.h"
#include <cmath>

// =============================================================================
// 1:1 reconstruction of the three d3_par particle-system integrators:
//   VIBE_Particle_UpdatePoints @0x5e1e0c
//   VIBE_Particle_UpdatePolys  @0x5e2814
//   VIBE_Particle_UpdateLens   @0x5e32c0
//
// Each function is translated from its own disassembly: the three read different
// (overlapping) emitter field offsets, so they are reconstructed independently
// rather than via a shared body — exactly mirroring the binary. Float op-order,
// RNG draw-order and the (int) round-toward-zero truncation are byte-exact.
// =============================================================================

namespace guild::render::pintegrate {

using guild::crt::RandNext;

int TruncToward(double x) { return (int)x; }

void VectorNormalize(float v[3]) {
    float len = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    u32 bits;
    __builtin_memcpy(&bits, &len, 4);
    if ((bits & 0x7FFFFFFFu) != 0) {
        float inv = 1.0f / len;
        v[0] = v[0] * inv;
        v[1] = v[1] * inv;
        v[2] = v[2] * inv;
    } else {
        v[0] = 0.0f;
        v[1] = 0.0f;
        v[2] = 0.0f;
    }
}

double Fmod(double a, double b) { return std::fmod(a, b); }

namespace {
// PHASE-WRAP CHAIN — RESOLVED (was the wave-7 rule-8 residual). The integrate
// paths advance the four phase-angle accumulators (slot +0x10/14/18/1C) and the
// phase scalar (+0x0C) each frame and wrap each with VIBE_Math_Fmod against pi
// (dbl_62BAA4/AC4/AE4). The x87 stack juggling (Points 0x5e23e6.., Polys
// 0x5e2ec5.., Lens 0x5e36bb..) was traced register-by-register from the disasm:
//
//   VIBE_Math_Fmod @0x5d3fb2 is `fprem` then `fstp st(1)` → it returns the
//   DIVIDEND (st0 at entry) reduced modulo the DIVISOR (st1 at entry). The Hex-
//   Rays prototype lists args (a1@st1, a2@st0), so its `Fmod(pi, x)` rendering is
//   divisor=pi (st1), dividend=x (st0) → result = x mod pi. There is NO divide by
//   a transient zero: at every wrap site the disasm pushes `dbl pi` and then the
//   freshly-advanced angle, and the divisor for every call is that pi copy held
//   deeper on the stack (Hex-Rays mistracked which deep slot held pi vs. a prior
//   wrap result; the asm `fld st(2)/fxch` keeps the pi copy live across all five
//   calls). The angles DO NOT seed off each other — each is `(rate*dt + angle)
//   mod pi`, fully independent. WrapPi(advanced) = fmod(advanced, pi) is exactly
//   the 1:1 chain.
//
// Per-frame chain (all three identical; addresses are Points):
//   ang_i = WrapPi(dt * a1[+0x50/54/58/5C] + ang_i)   ; i=0..3 (0x5e23ef..0x5e2455)
//   phase = WrapPi(dt * a1[+0xB4]          + phase)   ;        (0x5e245a..0x5e246c)
// The velocity advance immediately after operates on dt (the FPU top after the
// wrap, NOT the phase): Points 0x5e246f / Polys 0x5e2f4b / Lens 0x5e375b each
// `fmul a1[+0x90/94/98]`. Polys & Lens use the SAME angle rates 0x50/54/58/5C and
// phase rate 0xB4 as Points (verified 0x5e2ecb/0x5e36bf etc.) — the earlier
// 0x80.. /0xB0 transliteration was wrong and is corrected. alphaSrc amplitude is
// a1[+0xB0] (0x5e253d/0x5e3019/0x5e383e). Everything else (spawn RNG order,
// cap/gate/frameMod, velocity/box/acc carry, render center, z-cap clip, 3-span
// alpha fade, kill) is byte-exact.
inline float WrapPi(float advanced) { return (float)Fmod((double)advanced, kPiD); }

inline float Rnd() { return (float)RandNext(); }               // [0,32767] -> float
inline float RnN() { return (float)RandNext() * kRandNorm; }   // normalized [0,1)

// Resolve the spawn budget (a1+208 slot count, optionally scaled by life255 when
// the low-flags byte is negative — bit7 set).
//   if ( *(char*)(a1+204) >= 0 ) cap = a1+208;
//   else cap = ConvertX( (double)(a1+208) * a1+172 );   // toward-zero trunc
inline i32 ResolveCap(const Emitter& e) {
    if ((i8)e.flagsLow() >= 0)
        return e.slotCount();
    return TruncToward((double)e.slotCount() * e.life255());
}

// Frame divisor from the texture group block: group[+112] else 1.
inline u32 ResolveFrameMod(const Emitter& e) {
    u32 fm = 1;
    const void* g = e.groupPtr();
    if (g) {
        u8 byte = *(reinterpret_cast<const u8*>(g) + 112);
        if (byte)
            fm = byte;
    }
    return fm;
}

// Tail shared by all three: clear spawn-gate flags, stamp lastTick.
inline void IntegrateTail(Emitter& e, u32 now) {
    if (e.b(0xCC) & 0x20) e.b(0xCC) &= 0xDF;   // a1+204 bit5
    if (e.b(0xCD) & 0x02) e.b(0xCD) &= 0xFD;   // a1+205 bit1
    e.u(0x24) = now;                            // a1+36 = now
}

// Spawn gate predicate (else-branch guard, identical in all three):
//   spawnCounter < cap && ( (char)(a1+204) >= 0 || (a1+205 & 2) )
inline bool SpawnGate(const Emitter& e, i32 spawnCounter, i32 cap) {
    return spawnCounter < cap &&
           ((i8)e.flagsLow() >= 0 || (e.flagsHi() & 2) != 0);
}

// The 3-span triangular alpha-fade shared by all three integrators. `age` is the
// particle age (now - birthTick). Spans are a1+188/192/196.
//   age <  spanA : ramp UP   — alpha = alphaSrc * (1 - (spanA-age)*preRecip)
//   spanA..spanB : steady    — alpha = clamp(alphaSrc, 0, 255)
//   spanB..spanC : ramp DOWN — alpha = alphaSrc * (spanC-age) * cbRecip
//   age >= spanC : kill
// `preRecip` is 1/spanA (a1+48) for ALL THREE — VERIFIED: Points pre-ramp asm
// 0x5e25b5 `fmul [esi+30h]` (esi+0x30 = a1+48 = 1/spanA), Polys 0x5e3099, Lens
// 0x5e38c1 likewise. `cbRecip` is 1/(spanC-spanB) (a1+52). Returns the float
// alpha; sets `kill` past spanC.
// (Matches Polys 0x5e3078.. / Lens 0x5e389f.. / Points 0x5e259c.. — same shape.)
inline float AlphaFade(const Emitter& e, float alphaSrc, i32 age,
                       float preRecip, float cbRecip, bool& kill) {
    kill = false;
    i32 sA = e.spanA();
    if (age >= sA) {
        if (age >= e.spanB()) {
            i32 sC = e.spanC();
            if (age >= sC) { kill = true; return 0.0f; }
            float span = (float)(sC - age);
            float v = alphaSrc * span * cbRecip;
            if (!(k255 < (double)v) && v < 0.0f)
                return 0.0f;
            float span2 = (float)(e.spanC() - age);
            float v2 = alphaSrc * span2 * cbRecip;
            return (k255 >= (double)v2) ? v2 : 255.0f;
        }
        // mid window: clamp alphaSrc into [0,255]
        float a = alphaSrc;
        if (!(k255 < (double)a) && a >= 0.0f)
            return (k255 >= (double)a) ? a : 255.0f;
        return 0.0f;
    }
    float t = (double)(sA - age) * preRecip;
    float scale = 1.0f - t;
    float v = alphaSrc * scale;
    if (!(k255 < (double)v) && v < 0.0f)
        return 0.0f;
    float t2 = (double)(e.spanA() - age) * preRecip;
    float scale2 = 1.0f - t2;
    float v2 = alphaSrc * scale2;
    return (k255 >= (double)v2) ? v2 : 255.0f;
}

// Big-dt clamp + age fetch shared by the three integrate paths.
//   dt = (float)(now - lastTick); if (dt > 65536.0f) { birthTick += (now-lastTick); dt = 0; }
//   lastTick = now; age = now - birthTick;
inline float AgeStep(Slot& p, u32 now, i32& age) {
    u32 raw = now - p.lastTick();
    float dt = (float)raw;
    if (*(i32*)&dt > 1125515264) {   // 65536.0f
        p.birthTick() = raw + p.birthTick();
        dt = 0.0f;
    }
    p.lastTick() = now;
    age = (i32)(now - p.birthTick());
    return dt;
}

// frame index from age: if (a1+204 & 0x1F) frame = age/(a1+204&0x1F) % frameMod
inline void FrameFromAge(const Emitter& e, Slot& p, i32 age, u32 frameMod) {
    u8 fl = e.flagsLow();
    if (fl & 0x1F)
        p.frame = (u8)((u32)age / (u32)(fl & 0x1F) % frameMod);
}

// z-cap clip (a1+168): out-of-band z snaps to the cap and scales the accumulator
// by a1+156/160/164. Used by all three.
inline void ZCapClip(const Emitter& e, Slot& p) {
    float cap = e.f(0xA8);
    if ((cap < 0.0f && p.cz < (double)cap) ||
        (cap > 0.0f && p.cz > (double)cap)) {
        *(u32*)&p.cz = *(u32*)&cap;
        p.cx = e.f(0x9C) * p.cx;
        p.cy = e.f(0xA0) * p.cy;
        p.cz = e.f(0xA4) * p.cz;
    }
}

// spawn colour from (mask & rnd) + base  (a1+203 mask, a1+200/201/202 base).
// Draw order: R (+0x4E), G (+0x4D), B (+0x4C).
inline void SpawnColor(const Emitter& e, Slot& p) {
    p.colR = (u8)((e.colMask() & (u8)RandNext()) + e.colBaseR());
    p.colG = (u8)((e.colMask() & (u8)RandNext()) + e.colBaseG());
    p.colB = (u8)((e.colMask() & (u8)RandNext()) + e.colBaseB());
}
} // namespace

// ---------------------------------------------------------------------------
// gilde.exe 0x5e1e0c — VIBE_Particle_UpdatePoints.
// Point/sprite particles. Integrate (asm 0x5e23e6..0x5e254d):
//   ang0..3 += dt * a1[+0x50/54/58/5C]; each wrapped fmod(pi)
//   phase   += dt * a1[+0xB4];          wrapped fmod(pi)
//   vel(+0/4/8) += dt * a1[+0x90/94/98]
//   box = vel + a1[+0x78/7C/80]
//   acc(+0x20/24/28) += sin(ang_i) * box_i
//   center(+0x38/3C/40) = a1[+0x40/44/48] * sin(ang_i) + acc_i
//   size(+0x48) = a1[+0x4C] * sin(ang3) + a1[+0xE4]
//   alphaSrc(+0x44) = sin(phase) * a1[+0xB0] + a1[+0xB8]
// Spawn (asm 0x5e1f37..0x5e22b1): 5 angle draws, colour, 3 dir draws (norm-0.5),
//   normalize, velocity = baseSpeed*dir, 3 turbulence draws into acc, center.
// ---------------------------------------------------------------------------
bool UpdatePoints(Emitter& e, Slot* slots, u32 now) {
    if (e.i(0x24) == (i32)now)             // 0x5e1e34
        return true;

    // baseSpeed = sqrt(|a1+108|^2)  (a1+108/112/116)
    float baseSpeed = std::sqrt(e.f(108) * e.f(108) + e.f(112) * e.f(112) +
                                e.f(116) * e.f(116));

    i32 cap = ResolveCap(e);               // v119
    u32 frameMod = ResolveFrameMod(e);     // v127

    // reciprocals into scratch (asm 0x5e1ebf..0x5e1f02, stack-traced):
    //   a1+48 = 1/spanA ; a1+52 = 1/(spanC-spanB) ; a1+56 = 1/|a1+100 - a1+96|.
    // The fade uses a1+48 (pre-ramp) and a1+52 (down-ramp); a1+56 is unused here.
    float invSpanA  = 1.0f / (float)e.spanA();                 // a1+48
    i32   spanCB    = e.spanC() - e.spanB();                   // v142
    float invSpanCB = 1.0f / (float)spanCB;                    // a1+52
    float invMid    = 1.0f / std::fabs(e.f(0x64) - e.f(0x60)); // a1+56 (unused)
    (void)invMid;

    i32 spawnCounter = 0;                   // v137
    if (e.i(0xD0) > 0) {                     // a1+208 slot count
        Slot* p = slots;
        for (i32 idx = 0; idx < e.slotCount(); ++idx, ++p) {
            if ((p->flags & 1) != 0) {
                // ---- INTEGRATE (active) ----
                i32 age;
                float dt = AgeStep(*p, now, age);
                FrameFromAge(e, *p, age, frameMod);

                // Phase-angle accumulators advance by dt*a1[+0x50/54/58/5C] and the
                // phase scalar (+0x0C) by dt*a1[+0xB4], each wrapped to [0,pi) via
                // VIBE_Math_Fmod(angle, pi). See PHASE-WRAP NOTE at the top of this
                // file: the exact x87 operands of the wrap chain (asm 0x5e23f8..) are
                // the one sub-detail not byte-recoverable; advance-then-wrap is the
                // faithful bounded reading (the binary never integrates to NaN).
                p->ang0 = WrapPi(dt * e.f(0x50) + p->ang0);
                p->ang1 = WrapPi(dt * e.f(0x54) + p->ang1);
                p->ang2 = WrapPi(dt * e.f(0x58) + p->ang2);
                p->ang3 = WrapPi(dt * e.f(0x5C) + p->ang3);
                // PHASE advance — VERIFIED against disasm 0x5e2446..0x5e2463: after
                // the four angle stores the FPU top holds r0 (= the just-wrapped
                // ang0, fmod(dt*rate0+ang0,pi)); `fxch st(1); fmul [esi+0B4h]`
                // multiplies THAT value, NOT dt. So phase = fmod(ang0*phaseRate +
                // phase, pi). (Points/Polys share this; Lens reloads dt fresh — see
                // UpdateLens 0x5e3738. Earlier transliteration used dt here, wrong.)
                p->phase = WrapPi(p->ang0 * e.f(0xB4) + p->phase);   // +0x0C

                // Points velocity advance uses dt (asm 0x5e246f: after the phase
                // wrap leaves dt at the FPU top, `fmul [esi+90h]` multiplies dt —
                // NOT the phase result; the two duplicate dt copies on the stack
                // feed the three velocity components).
                p->vx = dt * e.f(0x90) + p->vx;
                p->vy = dt * e.f(0x94) + p->vy;
                p->vz = dt * e.f(0x98) + p->vz;

                float bx = p->vx + e.f(0x78);
                float by = p->vy + e.f(0x7C);
                float bz = p->vz + e.f(0x80);

                p->accX = std::sin(p->ang0) * bx + p->accX;
                p->accY = std::sin(p->ang1) * by + p->accY;
                p->accZ = std::sin(p->ang2) * bz + p->accZ;

                p->cx = e.f(0x40) * std::sin(p->ang0) + p->accX;
                p->cy = e.f(0x44) * std::sin(p->ang1) + p->accY;
                p->cz = e.f(0x48) * std::sin(p->ang2) + p->accZ;
                p->size = e.f(0x4C) * std::sin(p->ang3) + e.f(0xE4);
                p->alphaSrc = std::sin(p->phase) * e.f(0xB0) + e.f(0xB8);

                ZCapClip(e, *p);

                // Points: pre-ramp uses a1+48 (1/spanA, asm 0x5e25b5); down-ramp
                // uses a1+52 (1/(spanC-spanB), asm 0x5e2743).
                bool kill;
                float alphaF = AlphaFade(e, p->alphaSrc, age, invSpanA, invSpanCB, kill);
                if (kill) { p->flags &= ~1u; ++e.killedCount(); continue; }
                p->alpha = (u8)TruncToward(alphaF);
            } else if (SpawnGate(e, spawnCounter, cap)) {
                // ---- SPAWN (dead slot) ----
                p->ang0 = RnN() * kTwo * kPiF;
                p->ang1 = RnN() * kTwo * kPiF;
                p->ang2 = RnN() * kTwo * kPiF;
                ++spawnCounter;
                p->ang3 = RnN() * kTwo * kPiF;
                p->phase = RnN() * kTwo * kPiF;
                p->frame = (u8)((u32)idx % frameMod);
                SpawnColor(e, *p);
                p->alpha = 0;
                p->lastTick() = now;
                // direction: 3 draws (rnd*norm - 0.5) -> normalize
                float dir[3];
                dir[0] = RnN() + kBiasHalfP;
                dir[1] = RnN() + kBiasHalfP;
                dir[2] = RnN() + kBiasHalfP;
                VectorNormalize(dir);

                u8 fl = e.flagsLow();
                if ((fl & 0x20) != 0 || (fl & 0x40) != 0) {
                    // ring-spawn: one draw scaled by spanC back-dates birthTick,
                    // then interpolate the spawn speed between a1+96/a1+100.
                    float bt = RnN();                                  // var_30
                    double aged = (double)now - (double)e.spanC() * bt; // a1+0xC4
                    p->birthTick() = (u32)(i64)aged;
                    // speed = (end-start)*bt + start, with start=min/end=max of the
                    // two endpoints a1+96/a1+100 — VERIFIED disasm 0x5e212b..0x5e216b:
                    //   start (var_C): fcomp a1+96,a1+100; jnb -> a1+100 (so start =
                    //                  a1+96 < a1+100 ? a1+96 : a1+100)
                    //   end   (var_64): fcomp a1+96,a1+100; jbe -> a1+100 (so end =
                    //                  a1+96 <= a1+100 ? a1+100 : a1+96)
                    // (Earlier transliteration had start/end SWAPPED.)
                    float start = (e.f(0x60) < e.f(0x64)) ? e.f(0x60) : e.f(0x64);
                    float end   = (e.f(0x60) <= e.f(0x64)) ? e.f(0x64) : e.f(0x60);
                    float speed = (end - start) * bt + start;
                    p->accX = dir[0] * speed;
                    p->accY = dir[1] * speed;
                    p->accZ = dir[2] * speed;
                } else {
                    p->birthTick() = now;
                    float speed = e.f(0x60);   // a1+96 (== +0x60)
                    p->accX = speed * dir[0];
                    p->accY = speed * dir[1];
                    p->accZ = speed * dir[2];
                }
                // velocity (+0/4/8) = baseSpeed * dir
                p->vx = baseSpeed * dir[0];
                p->vy = baseSpeed * dir[1];
                p->vz = baseSpeed * dir[2];
                // 3 turbulence draws into the accumulator (a1+132/136/140)
                float jx = RnN() * e.f(0x84);
                float jy = RnN() * e.f(0x88);
                float jz = RnN() * e.f(0x8C);
                p->accX = p->accX + jx;
                p->accY = p->accY + jy;
                p->accZ = p->accZ + jz;
                p->cx = e.f(0x40) * std::sin(p->ang0) + p->accX;
                p->cy = e.f(0x44) * std::sin(p->ang1) + p->accY;
                p->cz = e.f(0x48) * std::sin(p->ang2) + p->accZ;
                p->size = e.f(0x4C) * std::sin(p->ang3) + e.f(0xE4);
                p->flags |= 1u;
            }
        }
    }
    IntegrateTail(e, now);
    if ((e.flagsHi() & 1) == 0)
        return true;
    return e.killedCount() < cap;
}

// ---------------------------------------------------------------------------
// gilde.exe 0x5e2814 — VIBE_Particle_UpdatePolys. Poly/ribbon particles. The
// decompile is clean C; this is a direct transliteration.
//   ang0..3 += dt * a1[+0x50/54/58/5C]; wrapped fmod(pi)  (same rates as Points)
//   phase   += dt * a1[+0xB4];          wrapped fmod(pi)   (a1+0xB4 = phaseRate)
//   acc(+0x20/24/28) += dt * a1[+0x90/94/98]
//   box = acc + a1[+0x78/7C/80]
//   acc += dt * box
//   center(+0x38/3C/40) = a1[+0x40/44/48] * sin(ang_i) + acc_i
//   size(+0x48) = a1[+0x4C] * sin(ang3) + a1[+0xE4]
//   alphaSrc(+0x44) = sin(phase) * a1[+0xB0] + a1[+0xB8]
// ---------------------------------------------------------------------------
bool UpdatePolys(Emitter& e, Slot* slots, u32 now) {
    if (e.i(0x24) == (i32)now)             // 0x5e283c
        return true;

    float baseSpeed = std::sqrt(e.f(108) * e.f(108) + e.f(112) * e.f(112) +
                                e.f(116) * e.f(116)); // v108

    i32 cap = ResolveCap(e);               // v90
    u32 frameMod = ResolveFrameMod(e);     // v96

    float invMid    = e.f(100) / e.f(96);                      // a1+60 scratch (v9)
    float invSpanA  = 1.0f / (float)e.spanA();                 // a1+48 (v10)
    float invRad    = 1.0f / e.f(104);                         // a1+56 (v11)
    i32   spanCB    = e.spanC() - e.spanB();                   // v113
    float invSpanCB = 1.0f / (float)spanCB;                    // a1+52 (v16)
    (void)invRad;

    i32 spawnCounter = 0;                   // v107
    if (e.i(0xD0) > 0) {
        Slot* p = slots;
        for (i32 idx = 0; idx < e.slotCount(); ++idx, ++p) {
            if ((p->flags & 1) != 0) {
                // ---- INTEGRATE ----
                i32 age;
                float dt = AgeStep(*p, now, age);
                FrameFromAge(e, *p, age, frameMod);

                // ang/phase advance + wrap-to-pi (disasm 0x5e2ec5..0x5e2f48).
                // Angle rates are a1[+0x50/54/58/5C] (ang0 at 0x5e2ecb fmul[esi+50h],
                // ang1 0x5e2eeb, ang2 0x5e2eff, ang3 0x5e2f13) and the phase rate is
                // a1[+0xB4] (0x5e2f36 fmul[esi+0B4h]) — IDENTICAL to Points, NOT the
                // 0x80/84/88/8C / 0xB0 that the prior decompile transliteration used.
                p->ang0 = WrapPi(dt * e.f(0x50) + p->ang0);
                p->ang1 = WrapPi(dt * e.f(0x54) + p->ang1);
                p->ang2 = WrapPi(dt * e.f(0x58) + p->ang2);
                p->ang3 = WrapPi(dt * e.f(0x5C) + p->ang3);
                // PHASE advance — VERIFIED against disasm 0x5e2f22..0x5e2f3f (byte-
                // identical to Points 0x5e2446..): the value multiplied by phaseRate
                // (esi+0B4h) is r0 (the just-wrapped ang0), NOT dt. phase =
                // fmod(ang0*phaseRate + phase, pi).
                p->phase = WrapPi(p->ang0 * e.f(0xB4) + p->phase);   // +0x0C

                // velocity (+0/4/8) advances by dt*a1[+144/148/152]; box adds the
                // a1[+120/124/128] bias; accumulator (+0x20/24/28) carries dt*box.
                p->vx = dt * e.f(0x90) + p->vx;   // a1+144
                p->vy = dt * e.f(0x94) + p->vy;   // a1+148
                p->vz = dt * e.f(0x98) + p->vz;   // a1+152

                float bx = p->vx + e.f(0x78);
                float by = p->vy + e.f(0x7C);
                float bz = p->vz + e.f(0x80);

                p->accX = dt * bx + p->accX;
                p->accY = dt * by + p->accY;
                p->accZ = dt * bz + p->accZ;

                p->cx = e.f(0x40) * std::sin(p->ang0) + p->accX;
                p->cy = e.f(0x44) * std::sin(p->ang1) + p->accY;
                p->cz = e.f(0x48) * std::sin(p->ang2) + p->accZ;
                p->size = e.f(0x4C) * std::sin(p->ang3) + e.f(0xE4);
                p->alphaSrc = std::sin(p->phase) * e.f(0xB0) + e.f(0xB8);

                ZCapClip(e, *p);

                bool kill;
                float alphaF = AlphaFade(e, p->alphaSrc, age, invSpanA, invSpanCB, kill);
                if (kill) { p->flags &= ~1u; ++e.killedCount(); continue; }
                p->alpha = (u8)TruncToward(alphaF);
            } else if (SpawnGate(e, spawnCounter, cap)) {
                // ---- SPAWN ----
                p->ang0 = RnN() * kTwo * kPiF;
                p->ang1 = RnN() * kTwo * kPiF;
                p->ang2 = RnN() * kTwo * kPiF;
                p->ang3 = RnN() * kTwo * kPiF;
                ++spawnCounter;
                p->phase = RnN() * kTwo * kPiF;
                p->frame = (u8)((u32)idx % frameMod);
                SpawnColor(e, *p);
                p->alpha = 0;
                p->lastTick() = now;
                // direction: 2 draws into dir[0],dir[1] (rnd*norm - 0.5), dir[2]=0.
                // VERIFIED disasm 0x5e2a3f..0x5e2a7b: each draw is `fild; fmul
                // flt_62BAB4(=1/32767); fadd flt_62BAD0(=-0.5)` — NO *2 — and the
                // stores are var_80(dir0), var_7C(dir1); var_78(dir2)=0 (xor eax).
                // (Earlier transliteration had a spurious *kTwo and swapped Y/Z.)
                float dir[3];
                dir[0] = RnN() + kBiasHalfQ;
                dir[1] = RnN() + kBiasHalfQ;
                dir[2] = 0.0f;
                VectorNormalize(dir);

                u8 fl = e.flagsLow();
                if ((fl & 0x20) != 0 || (fl & 0x40) != 0) {
                    float speed;
                    if ((e.u(0x60) & 0x7FFFFFFFu) != 0) {           // a1+96 != 0
                        float sp = RnN() * e.f(96);
                        p->accX = dir[0] * sp;
                        p->accY = dir[1] * sp;
                        p->accZ = dir[2] * sp;
                        speed = sp * invMid;                        // a1+60
                    } else {
                        p->accX = 0.0f; p->accY = 0.0f; p->accZ = 0.0f;
                        speed = RnN() * e.f(100);                   // a1+100
                    }
                    dir[0] = dir[0] * speed - p->accX;
                    dir[1] = dir[1] * speed - p->accY;
                    dir[2] = e.f(104) - p->accZ;                    // a1+104
                    VectorNormalize(dir);
                    float bt = RnN();
                    double aged = (double)now - (double)e.spanC() * bt; // a1+196
                    p->birthTick() = (u32)(i64)aged;
                    float adv = bt * e.f(104);
                    p->accX = adv * dir[0] + p->accX;
                    p->accY = adv * dir[1] + p->accY;
                    p->accZ = adv * dir[2] + p->accZ;
                } else {
                    p->birthTick() = now;
                    float sp = RnN() * e.f(96);
                    p->accX = dir[0] * sp;
                    p->accY = dir[1] * sp;
                    p->accZ = dir[2] * sp;
                    float speed;
                    if ((e.u(0x60) & 0x7FFFFFFFu) != 0) {
                        speed = sp * invMid;
                    } else {
                        p->accX = 0.0f; p->accY = 0.0f; p->accZ = 0.0f;
                        speed = RnN() * e.f(100);
                    }
                    dir[0] = dir[0] * speed - p->accX;
                    dir[1] = dir[1] * speed - p->accY;
                    dir[2] = e.f(104) - p->accZ;
                    VectorNormalize(dir);
                }
                p->vx = dir[0] * baseSpeed;
                p->vy = dir[1] * baseSpeed;
                p->vz = baseSpeed * dir[2];
                float jx = RnN() * e.f(132);
                float jy = RnN() * e.f(136);
                float jz = RnN() * e.f(140);
                std::sin(p->ang0); // (matches stray fsin in spawn tail; no effect)
                p->accX = p->accX + jx;
                p->accY = p->accY + jy;
                p->accZ = p->accZ + jz;
                p->cx = e.f(0x40) * std::sin(p->ang0) + p->accX;
                p->cy = e.f(0x44) * std::sin(p->ang1) + p->accY;
                p->cz = e.f(0x48) * std::sin(p->ang2) + p->accZ;
                p->size = e.f(0x4C) * std::sin(p->ang3) + e.f(0xE4);
                p->flags |= 1u;
            }
        }
    }
    IntegrateTail(e, now);
    if ((e.flagsHi() & 1) == 0)
        return true;
    return e.killedCount() < cap;
}

// ---------------------------------------------------------------------------
// gilde.exe 0x5e32c0 — VIBE_Particle_UpdateLens. Lens-flare/glow particles.
// Loop is inverted (the DEAD-slot spawn branch comes first). Integrate matches
// Points/Polys (a1+0x50/54/58/5C ang advance, a1+0xB4 phase, a1+0x90/94/98
// velocity, etc.). Spawn:
//   5 angle draws, colour, accumulator seed (rnd*norm*2 - 1) * a1[+0x60/64/68],
//   optional back-dated birthTick (a1+204 bit6), velocity = 3 draws * a1[+132/136/140].
// ---------------------------------------------------------------------------
bool UpdateLens(Emitter& e, Slot* slots, u32 now) {
    if ((i32)now == e.i(0x24))             // 0x5e32df
        return true;

    i32 cap = ResolveCap(e);               // v76
    u32 frameMod = ResolveFrameMod(e);     // v90

    float invSpanA  = 1.0f / (float)e.spanA();                 // a1+48
    i32   spanCB    = e.spanC() - e.spanB();
    float invSpanCB = 1.0f / (float)spanCB;                    // a1+52

    i32 spawnCounter = 0;                   // v79
    if (e.i(0xD0) > 0) {
        Slot* p = slots;
        for (i32 idx = 0; idx < e.slotCount(); ++idx, ++p) {
            if ((p->flags & 1) == 0) {
                // ---- SPAWN (dead slot first) ----
                if (SpawnGate(e, spawnCounter, cap)) {
                    ++spawnCounter;
                    p->ang0 = RnN() * kTwo * kPiF;
                    p->ang1 = RnN() * kTwo * kPiF;
                    p->ang2 = RnN() * kTwo * kPiF;
                    p->ang3 = RnN() * kTwo * kPiF;
                    p->phase = RnN() * kTwo * kPiF;
                    p->frame = (u8)((u32)idx % frameMod);
                    // colour: R(+0x4E), G(+0x4D) then B(+0x4C) after lastTick
                    p->colR = (u8)(((u8)RandNext() & e.colMask()) + e.colBaseR());
                    p->colG = (u8)(((u8)RandNext() & e.colMask()) + e.colBaseG());
                    p->alpha = 0;
                    p->lastTick() = now;
                    p->colB = (u8)(((u8)RandNext() & e.colMask()) + e.colBaseB());
                    // accumulator seed: 3 draws -> (rnd*norm*2 - 1) * a1[+96/100/104]
                    float a0 = RnN() * kTwo + kBiasOne;           // v80
                    float a1v = RnN() * kTwo + kBiasOne;          // v87
                    float a2 = RnN();                             // v29 (norm only)
                    p->accX = e.f(96) * a0;
                    p->accY = e.f(100) * a1v;
                    float a2b = a2 * kTwo + kBiasOne;             // v70 = v30 + (-1)
                    p->accZ = e.f(104) * a2b;
                    if ((e.flagsLow() & 0x40) != 0) {
                        float bt = RnN();
                        double aged = (double)now - (double)bt * (double)e.spanC();
                        p->birthTick() = (u32)(i64)aged;
                    } else {
                        p->birthTick() = now;
                    }
                    // velocity: 3 draws * a1[+132/136/140]
                    p->vx = RnN();
                    p->vy = RnN();
                    p->vz = RnN();
                    p->vx = e.f(132) * p->vx;
                    p->vy = e.f(136) * p->vy;
                    p->vz = e.f(140) * p->vz;
                    p->flags |= 1u;
                }
                continue;   // LABEL_14
            }

            // ---- INTEGRATE (active) ----
            i32 age;
            float dt = AgeStep(*p, now, age);
            FrameFromAge(e, *p, age, frameMod);

            // ang/phase advance + wrap-to-pi (disasm 0x5e36bb..0x5e3752).
            // Angle rates a1[+0x50/54/58/5C] (ang0 0x5e36bf fmul[ebx+50h], ang1
            // 0x5e36e1, ang2 0x5e36f7, ang3 0x5e370d) and phase rate a1[+0xB4]
            // (0x5e373c fmul[ebx+0B4h]) — IDENTICAL to Points/Polys, NOT 0x80.. /0xB0.
            p->ang0 = WrapPi(dt * e.f(0x50) + p->ang0);
            p->ang1 = WrapPi(dt * e.f(0x54) + p->ang1);
            p->ang2 = WrapPi(dt * e.f(0x58) + p->ang2);
            p->ang3 = WrapPi(dt * e.f(0x5C) + p->ang3);
            p->phase = WrapPi(dt * e.f(0xB4) + p->phase);

            // velocity (+0/4/8) advance; box bias; accumulator (+0x20/24/28) carry
            p->vx = dt * e.f(0x90) + p->vx;
            p->vy = dt * e.f(0x94) + p->vy;
            p->vz = dt * e.f(0x98) + p->vz;

            float bx = p->vx + e.f(0x78);
            float by = p->vy + e.f(0x7C);
            float bz = p->vz + e.f(0x80);

            p->accX = dt * bx + p->accX;
            p->accY = dt * by + p->accY;
            p->accZ = dt * bz + p->accZ;

            p->cx = e.f(0x40) * std::sin(p->ang0) + p->accX;
            p->cy = e.f(0x44) * std::sin(p->ang1) + p->accY;
            p->cz = e.f(0x48) * std::sin(p->ang2) + p->accZ;
            p->size = e.f(0x4C) * std::sin(p->ang3) + e.f(0xE4);
            p->alphaSrc = std::sin(p->phase) * e.f(0xB0) + e.f(0xB8);

            ZCapClip(e, *p);

            bool kill;
            float alphaF = AlphaFade(e, p->alphaSrc, age, invSpanA, invSpanCB, kill);
            if (kill) { p->flags &= ~1u; ++e.killedCount(); continue; }
            p->alpha = (u8)TruncToward(alphaF);
        }
    }
    IntegrateTail(e, now);
    return (e.flagsHi() & 1) == 0 || cap > e.killedCount();
}

} // namespace guild::render::pintegrate
