#include "render/particle.h"
#include "crt/rand.h"
#include <cmath>

namespace guild::render {

// ---------------------------------------------------------------------------
// Local recovered float constants (see particle.h for provenance addresses).
// All verified bit-exact via get_bytes.
// ---------------------------------------------------------------------------
namespace {
// UpdateEmitter (0x61199C..0x6119C0)
constexpr float kEmTurn      = 2.0f;        // flt_61199C
constexpr float kEmDt        = 0.3333333432674408f; // flt_6119A0
constexpr float kEmShade255  = 255.0f;      // flt_6119A4
constexpr double kEmSpread10 = 10.0;        // dbl_6119AC
constexpr double kEmSpread2  = 2.0;         // dbl_6119B4
constexpr float kEmFade02    = 0.20000000298023224f; // flt_6119BC
constexpr float kEmBias5     = -5.0f;       // flt_6119C0

// SeedParticles (0x6119CC..0x611A14)
constexpr double kSdSp4   = 4.0;            // dbl_6119CC
constexpr double kSdSp3   = 3.0;            // dbl_6119D4
constexpr double kSdSp10  = 10.0;           // dbl_6119DC
constexpr float  kSdV0    = 4.0f;           // flt_6119E4
constexpr double kSdSp96  = 9.6;            // dbl_6119EC
constexpr float  kSdDt    = 0.3333333432674408f; // flt_6119F4
constexpr float  kSdBounceY = 0.3700000047683716f; // flt_6119F8
constexpr float  kSdBounceX = 0.6100000143051147f; // flt_6119FC
constexpr float  kSdBounceZ = 0.5899999737739563f; // flt_611A00
constexpr float  kSdBias_2 = -2.0f;         // flt_611A04
constexpr float  kSdBias_15 = -1.5f;        // flt_611A08
constexpr float  kSdBias_5 = -5.0f;         // flt_611A0C
constexpr float  kSdBias05 = 0.5f;          // flt_611A10
constexpr float  kSdBias_48 = -4.800000190734863f; // flt_611A14

// UpdateTrail (0x611A1C..0x611A54)
constexpr double kTrSpA = 0.2857142857142857;   // dbl_611A1C
constexpr double kTrHalf2 = 2.0;                 // dbl_611A24
constexpr double kTrSpB = 0.22222222222222224;   // dbl_611A34
constexpr double kTrQ  = 0.25;                   // dbl_611A3C
constexpr float  kTrDt = 0.3333333432674408f;    // flt_611A44
constexpr double kTrHalf = -0.5;                 // dbl_611A4C
constexpr float  kTrBias_2 = -2.0f;              // flt_611A54

// UpdateGravity (0x611A68..0x611A9C)
constexpr float  kGrDt    = 0.0010000000474974513f; // flt_611A68
constexpr float  kGrTime  = 0.800000011920929f;     // flt_611A6C
constexpr float  kGrShadeM = 1.5f;                  // flt_611A70
constexpr float  kGrV2    = 2.0f;                   // flt_611A78
constexpr float  kGrSp10  = 0.10000000149011612f;   // flt_611A7C
constexpr float  kGrSp05  = 0.05000000074505806f;   // flt_611A80
constexpr double kGrKill  = -10.0;                  // dbl_611A84
constexpr float  kGrShadeB = 50.0f;                 // flt_611A8C
constexpr float  kGrBias05 = -0.5f;                 // flt_611A90
constexpr float  kGrBias55 = -55.0f;                // flt_611A94
constexpr float  kGrA      = -0.20000000298023224f; // flt_611A98
constexpr float  kGrB      = 0.4000000059604645f;   // flt_611A9C

// UpdateCosineWave (0x611AA8..0x611AB0)
constexpr float  kCwHalfPi = 1.5707963705062866f;   // flt_611AA8
constexpr float  kCwAmp    = 0.4000000059604645f;   // flt_611AAC
constexpr float  kCwShade  = 255.0f;                // flt_611AB0

// UpdateScatter (0x611AF4..0x611B2C)
constexpr double kScSp4   = 4.0;                    // dbl_611AF4
constexpr float  kScV15   = 1.5f;                   // flt_611AFC
constexpr double kScSp2   = 2.0;                    // dbl_611B04
constexpr double kScR04   = 0.4;                    // dbl_611B0C
constexpr float  kScRej   = 0.25f;                  // flt_611B14
constexpr float  kScDt    = 0.3333333432674408f;    // flt_611B18
constexpr float  kScBX    = 0.28999999165534973f;   // flt_611B1C
constexpr float  kScBY    = 0.3400000035762787f;    // flt_611B20
constexpr float  kScBZ    = 0.3100000023841858f;    // flt_611B24
constexpr float  kScBias_2 = -2.0f;                 // flt_611B28
constexpr double kScStop  = 0.2;                    // dbl_611B2C

inline float Rnd() { return (float)crt::RandNext(); } // [0,32767] -> float
} // namespace

int TruncToward(double x) {
    // x87 frndint under round-toward-zero; identical to C truncation toward 0.
    return (int)x;
}

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

// gilde.exe 0x42b930 — VIBE_Particle_UpdateEmitter (__usercall, al = f(eax,edx)).
// Sticky directional emitter: live particles follow the emitter's velocity field
// and are snapped to a target speed; dead/over-budget slots are respawned with
// random jitter. RNG order: 3 position-spread draws, then 2 axis-spread draws.
char UpdateEmitter(Emitter& e, u32 now) {
    // v43 = e.maxSpeed / (e.velScale * 2.0 + e.baseVy)   (top-of-function)
    float v43 = e.maxSpeed / (e.velScale * kEmTurn + e.baseVy);
    int aliveDone = 0;            // v36 — count snapped this frame
    int spawnCounter = 0;         // v6 — respawned-this-frame counter
    e.hdr20 = (u8)(e.hdr20 & 0xFD); // clear bit1 (will set if none snapped)
    if (e.count > 0 && e.particles) {  // wave-10: + null-array guard (was count>0)
        Particle* p = e.particles;
        for (int idx = 0; idx < e.count; ++idx, ++p) {
            bool followBranch =
                ((e.hdr20 & 1) == 0) &&
                (((p->flags & 1) != 0) || (u32)spawnCounter >= e.maxAlive);

            if (followBranch) {
                if ((p->flags & 1) != 0) {
                    // disasm 0x42bb1d: dt (st0) is fst-stored to a FLOAT slot but
                    // kept on the x87 stack; px uses the un-rounded 80-bit dt while
                    // py/pz reload the float-rounded fdt. Mirror: px from double dt,
                    // py/pz from float fdt.
                    double dt = (double)(u32)(now - p->birthTime) * kEmDt;
                    float fdt = (float)dt;
                    p->px = (float)(dt * p->vx + p->px);
                    p->py = fdt * p->vy + p->py;
                    float npz = fdt * p->vz;
                    p->birthTime = now;
                    p->pz = npz + p->pz;
                    float curPosLen = std::sqrt(p->px * p->px + p->py * p->py + p->pz * p->pz);
                    float velLen = std::sqrt(p->vx * p->vx + p->vy * p->vy + p->vz * p->vz);
                    if (curPosLen > (double)e.velScale || velLen <= (double)e.minSpeed) {
                        p->vy = p->vy - fdt * e.turnRate;
                    } else {
                        float n[3] = {p->px, p->py, p->pz};
                        VectorNormalize(n);
                        if (n[1] >= 0.0f) {
                            p->px = e.velScale * n[0];
                            p->py = e.velScale * n[1];
                            p->pz = e.velScale * n[2];
                            float dot = -(p->vx * n[0] + p->vy * n[1] + p->vz * n[2]) * kEmTurn;
                            p->vx = dot * n[0] + p->vx;
                            p->vy = dot * n[1] + p->vy;
                            p->vz = dot * n[2] + p->vz;
                            if (velLen > (double)e.minSpeed) {
                                p->vx = e.damping * p->vx;
                                p->vy = e.damping * p->vy;
                                p->vz = e.damping * p->vz;
                            }
                        } else {
                            p->flags &= ~1u;
                        }
                    }
                    // shade from py/baseVy fraction
                    float frac = p->py / e.baseVy;
                    float clamped = frac >= 0.0f ? frac : 0.0f;
                    float shadeF;
                    if (clamped + kEmFade02 <= 1.0f) {
                        float frac2 = p->py / e.baseVy;
                        float base = frac2 >= 0.0f ? (frac2 + kEmFade02) : (kEmFade02 + 0.0f);
                        shadeF = base;
                    } else {
                        shadeF = 1.0f;
                    }
                    float lifeF = shadeF * kEmShade255;
                    p->life = lifeF;
                    if (lifeF <= 0.0f || p->py <= 0.0f) {
                        p->flags &= ~1u;
                        ++aliveDone;
                    } else {
                        p->shade = (u8)TruncToward(p->life);
                        ++aliveDone;
                    }
                }
            } else {
                // Respawn with jitter. RNG: 3 spread draws + 2 axis draws.
                p->px = (double)Rnd() * kRandNorm * kEmSpread10 + kEmBias5;
                p->py = (double)Rnd() * kRandNorm * kEmSpread10 + kEmBias5;
                p->pz = (double)Rnd() * kRandNorm * kEmSpread10 + kEmBias5;
                p->px = e.baseVx + p->px;
                p->py = e.baseVy + p->py;
                p->pz = e.baseVz + p->pz;
                p->vy = -(e.baseVy - e.velScale);
                double spread = (double)e.velScale * kEmTurn * kEmSpread2;
                p->vx = (double)Rnd() * kRandNorm * spread - (double)e.velScale * kEmTurn;
                spread = (double)e.velScale * kEmTurn * kEmSpread2;
                p->vz = (double)Rnd() * kRandNorm * spread - (double)e.velScale * kEmTurn;
                p->vx = p->vx * v43;
                p->vy = p->vy * v43;
                p->vz = v43 * p->vz;
                p->life = 255.0f; // 437F0000h
                p->setMatHandle(e.material); // [edx+4Ch] = material handle (dword)
                ++spawnCounter;
                p->birthTime = now;   // [edx+30h] = now (last-update tick)
                p->seed = e.lifeBase; // [edx+48h] = spawn lifetime base
                p->flags |= 1u;
            }
        }
    }
    u8 base = (u8)(e.hdr20 & 0xFE);
    e.hdr20 = base;
    if (!aliveDone)
        e.hdr20 = (u8)(base | 2);
    return 1;
}

// gilde.exe 0x42bec0 — VIBE_Particle_SeedParticles. Pass 1: fill any inactive
// slot with a random velocity/position spread (8 RNG draws). Pass 2: advance all
// active slots with gravity + ground bounce. RNG order is byte-exact.
char SeedParticles(Emitter& e, u32 now) {
    // Pass 1 gated on e.particles != null (original: *((_DWORD*)a1+8)).
    // We treat e.hdr20-independent: original reads a1[8] (=+0x20) as a guard;
    // here it is the "has array" flag — replicate as e.particles != nullptr.
    if (e.particles) {
        if (e.count > 0) {
            Particle* p = e.particles;
            for (int i = 0; i < e.count; ++i, ++p) {
                if ((p->flags & 1) == 0) {
                    p->px = (double)Rnd() * kRandNorm * kSdSp4 + kSdBias_2;
                    p->py = (double)Rnd() * kRandNorm * kSdSp3 + kSdBias_15;
                    p->pz = (double)Rnd() * kRandNorm * kSdSp4 + kSdBias_2;
                    p->vx = (double)Rnd() * kRandNorm * kSdSp10 + kSdBias_5;
                    p->vy = (double)Rnd() * kRandNorm * kSdV0 + kSdBias05;
                    p->vz = (double)Rnd() * kRandNorm * kSdSp96 + kSdBias_48;
                    u8 r = (u8)crt::RandNext();
                    p->life = (float)((r & 0x7F) + 127);
                    double phase = (double)crt::RandNext() * kRandNorm;
                    p->birthTime = now;
                    p->flags |= 1u;
                    p->seed = (float)phase + 1.0f;
                }
            }
        }
    }
    // Pass 2: gravity integrate + bounce for active slots.
    // wave-10 (W10-PARTICLE): the original derefs the slot array unconditionally
    // in this loop; guard the null array (never the in-bounds path) so a degenerate
    // system can't OOB. With a valid array the loop is byte-identical.
    Particle* p = e.particles;
    for (int i = 0; e.particles && i < e.count; ++i, ++p) {
        if ((p->flags & 1) != 0) {
            // disasm 0x42c054: dt fst-stored to a float slot but kept 80-bit; px
            // uses the un-rounded 80-bit dt, py/pz reload the float-rounded fdt.
            double dt = (double)(u32)(now - p->birthTime) * kSdDt;
            float fdt = (float)dt;
            p->px = (float)(dt * p->vx + p->px);
            p->py = fdt * p->vy + p->py;
            float npz = fdt * p->vz + p->pz;
            p->birthTime = now;
            p->pz = npz;
            if (p->py >= (double)e.baseVz) {
                p->vy = p->vy - fdt * e.baseVx;
            } else {
                p->py = e.baseVz;
                p->vy = -p->vy * kSdBounceY;
                p->vx = p->vx - p->vx * kSdBounceX;
                p->vz = p->vz - p->vz * kSdBounceZ;
            }
            float nlife = p->life - fdt * e.baseVy;
            p->life = nlife;
            if (nlife <= 0.0f) {
                p->flags &= ~1u;
            } else {
                p->shade = (u8)TruncToward(p->life);
            }
        }
    }
    return 1;
}

// gilde.exe 0x42c140 — VIBE_Particle_UpdateTrail. Active slots integrate + age;
// dead slots respawn into a trail (10 RNG draws). Returns 1.
char UpdateTrail(Emitter& e, u32 now) {
    // wave-10 (W10-PARTICLE): null-array guard (the original loops the slot array
    // unconditionally; a null array is a never-reached crash). Valid-array path
    // is byte-identical.
    Particle* p = e.particles;
    for (int i = 0; e.particles && i < e.count; ++i, ++p) {
        if ((p->flags & 1) != 0) {
            // disasm 0x42c17c: dt (st0) is kept at extended x87 precision and used
            // for all four updates WITHOUT a float round-trip (no fst to a dword
            // slot, unlike SeedParticles). Keep dt as double — never round to float.
            double dt = (double)(u32)(now - p->birthTime) * kTrDt;
            p->px = (float)(dt * p->vx + p->px);
            p->py = (float)(dt * p->vy + p->py);
            float npz = (float)(dt * p->vz + p->pz);
            p->birthTime = now;
            p->pz = npz;
            float nlife = (float)(p->life - dt * e.baseVy);
            p->life = nlife;
            if (nlife <= 0.0f)
                p->flags &= ~1u;
            else
                p->shade = (u8)TruncToward(p->life);
        } else {
            // Respawn into a trail. Field map (disasm 0x42c1f0..0x42c3a3):
            //   px(+38) = rnd*norm*(lifeBase*A*2) - lifeBase*A
            //   py(+3C) = rnd*norm*(lifeBase*B*2) - lifeBase*B
            //   pz(+40) = rnd*norm*(lifeBase*A*2) - lifeBase*A
            //   vx(+0)  = rnd*norm*(baseVx*Q*2)  - baseVx*Q
            //   vy(+4)  = rnd*norm + (-0.5) + baseVx
            //   vz(+8)  = rnd*norm*(baseVx*Q*2)  - baseVx*Q
            //   colR/G/B(+4E/4D/4C) = rnd & 0x3F ; life(+10) = (rnd & 0x3F)
            //   +30 = now ; seed(+48) = rnd*norm*((lifeBase-2)*2) - (lifeBase-2) + lifeBase
            double s1 = (double)e.lifeBase * kTrSpA * kTrHalf2;
            p->px = (double)Rnd() * kRandNorm * s1 - (double)e.lifeBase * kTrSpA;
            double s2 = (double)e.lifeBase * kTrSpB * kTrHalf2;
            p->py = (double)Rnd() * kRandNorm * s2 - (double)e.lifeBase * kTrSpB;
            double s3 = (double)e.lifeBase * kTrSpA * kTrHalf2;
            p->pz = (double)Rnd() * kRandNorm * s3 - (double)e.lifeBase * kTrSpA;
            double s4 = (double)e.baseVx * kTrQ * kTrHalf2;
            p->vx = (double)Rnd() * kRandNorm * s4 - (double)e.baseVx * kTrQ;
            p->vy = (float)((double)Rnd() * kRandNorm + kTrHalf + e.baseVx);
            double s6 = (double)e.baseVx * kTrQ * kTrHalf2;
            p->vz = (double)Rnd() * kRandNorm * s6 - (double)e.baseVx * kTrQ;
            p->colR = (u8)(crt::RandNext() & 0x3F);
            p->colG = (u8)(crt::RandNext() & 0x3F);
            p->colB = (u8)(crt::RandNext() & 0x3F);
            p->life = (float)((u32)crt::RandNext() & 0x3F);
            double sl = ((double)e.lifeBase + kTrBias_2) * kTrHalf2;
            double life = (double)Rnd() * kRandNorm * sl - ((double)e.lifeBase + kTrBias_2);
            p->birthTime = now;   // [edx+30h] = now
            p->flags |= 1u;
            p->seed = (float)(life + e.lifeBase); // [edx+48h]
        }
    }
    return 1;
}

// gilde.exe 0x42c42c — VIBE_Particle_UpdateGravity. Ballistic motion using a
// secondary accel triple at +0x10/+14/+18; partial re-emit of half the dead
// slots when bit0 of flags is set. RNG order: 5 draws per re-emit.
char UpdateGravity(Emitter& e, u32 now) {
    int dead = 0;                 // v4
    int halfCount = (int)((u32)e.hdr20 >> 1); // v32 = a1[8] >> 1
    // (original uses a1[8] = the +0x20 flags dword; >>1 == half of it)
    if (e.count > 0 && e.particles) {  // wave-10: + null-array guard (was count>0)
        Particle* p = e.particles;
        for (int idx = 0; idx < e.count; ++idx, ++p) {
            if ((p->flags & 1) != 0) {
                // disasm 0x42c656: dt (st0) stays at extended x87 precision and is
                // used WITHOUT a float round-trip for both the seed accum and vy.
                double dt = (double)(u32)(now - p->birthTime) * kGrDt;
                p->px = p->life + p->vx;   // +56 = +16 + +0
                p->py = p->a1 + p->vy;     // +60 = +20 + +4
                p->pz = p->a2 + p->vz;     // +64 = +24 + +8
                p->seed = (float)(dt * kGrTime + p->seed); // +72 += dt*0.8 (80-bit)
                p->vy = (float)(p->vy - dt);
                if (p->py < kGrKill)
                    p->flags &= ~1u;
                p->life = p->px; // +16 = +56
                p->a1 = p->py;   // +20 = +60
                p->a2 = p->pz;   // +24 = +64
                float shadeF = p->py * kGrShadeM + kGrShadeB;
                if (shadeF < 0.0f) shadeF = 0.0f;
                p->shade = (u8)TruncToward(shadeF);
            }
            if ((p->flags & 1) == 0)
                ++dead;
        }
    }
    if ((e.hdr20 & 1) == 0 && dead >= e.count)
        return 0;
    if ((e.hdr20 & 1) != 0 && dead > halfCount) {
        Particle* p = e.particles;
        for (int i = 0; e.particles && i < e.count && i < halfCount; ++i, ++p) {
            if ((p->flags & 1) == 0) {
                p->flags |= 1u;
                p->px = (double)Rnd() * kRandNorm + kGrBias05;
                p->py = (double)Rnd() * kRandNorm * (e.baseVy + 1.0f) + e.velScale;
                p->pz = (double)Rnd() * kRandNorm + kGrBias05;
                p->vx = (double)Rnd() * kRandNorm * kGrV2 * e.baseVy - e.baseVy;
                // disasm 0x42c549: t1 = (py + -55.0) * baseVz  ([edx+3Ch]=py,
                // [ecx+8]=baseVz). The Hex-Rays "a1 * damping" reading is wrong.
                float t1 = (p->py + kGrBias55) * e.baseVz;
                // vy (st0) is kept at 80-bit for the t2 mul (binary fst, not fstp);
                // store the float vy but feed the un-rounded value into t2.
                double vyEx = (double)kGrA - (double)Rnd() * kRandNorm * kGrSp10 + t1;
                p->vy = (float)vyEx;
                float t2 = (float)(vyEx * e.baseVx + kGrB);
                float t3 = (p->py + kGrBias55) * e.baseVx;
                p->vz = (float)((double)Rnd() * kRandNorm * kGrSp05 + t2 + t3);
                p->birthTime = now;
                p->seed = e.lifeBase;
                p->life = p->px;
                p->a1 = p->py;
                p->a2 = p->pz;
                float shadeF = p->py * kGrShadeM + kGrShadeB;
                if (shadeF < 0.0f) shadeF = 0.0f;
                p->shade = (u8)TruncToward(shadeF);
            }
        }
        e.hdr20 &= ~1u;
    }
    return 1;
}

// gilde.exe 0x42c7c8 — VIBE_Particle_UpdateCosineWave. Cosine-windowed pulse
// over the [0,1] normalized life window; no RNG. Returns "still pulsing".
bool UpdateCosineWave(Emitter& e, u32 now) {
    Particle* p0 = e.particles;
    // wave-10 (W10-PARTICLE) memory-safety guard: the original unconditionally
    // dereferences particle[0] for the time base (it is only ever called on a
    // system whose slot array is allocated). A null array here would be a hard
    // crash the original never reached; treat it as "no live particles" so the
    // headless/degenerate path is safe. (FAITHFUL: the in-bounds path with a
    // valid array is byte-identical; this only intercepts the never-taken null.)
    if (!p0)
        return false;
    u32 dur = e.hdr20; // *((u32*)a1+8) = emitter+0x20 = window duration
    double t = (double)(now - p0->birthTime) / (double)dur;
    if (t >= 0.0 && t <= 1.0) {
        float w = 1.0f - (float)(std::cos(((float)t + 1.0f) * kCwHalfPi) + 1.0);
        if (e.count > 0) {
            float ampShade = w * kCwAmp;     // v11
            float shade = (1.0f - w) * kCwShade; // v10
            Particle* p = e.particles;
            for (int i = 0; i < e.count; ++i, ++p) {
                p->px = p->life * w;
                p->py = p->a1 * w + w * e.baseVz;
                p->pz = p->a2 * w;
                p->seed = ampShade * e.lifeBase + e.lifeBase; // v2[57] = emitter+228
                p->shade = (u8)TruncToward(shade);
            }
        }
    }
    return now < p0->birthTime + dur;
}

// gilde.exe 0x42cadc — VIBE_Particle_UpdateFadeOut. Linear position scale with a
// triangular alpha envelope keyed on the per-particle threshold at +0x1C. No RNG.
bool UpdateFadeOut(Emitter& e, u32 now) {
    int dead = 0;
    Particle* p = e.particles;
    u32 dur = e.hdr20; // *(u32*)(v2+32) = emitter+0x20 -> emitter duration
    // wave-10 (W10-PARTICLE): null-array guard (see SeedParticles). Valid-array
    // path byte-identical.
    for (int i = 0; e.particles && i < e.count; ++i, ++p) {
        u32 birth = p->birthTime;
        if (now < birth || now > dur + birth) {
            p->flags &= ~1u;
        } else {
            float tnorm = (float)((double)(now - birth) / (double)dur);
            p->px = p->life * tnorm;   // +56 = +16 * t
            p->py = p->a1 * tnorm;     // +60 = +20 * t
            p->pz = p->a2 * tnorm;     // +64 = +24 * t
            double shadeF;
            float thresh = p->_1c;     // +0x1C
            if (tnorm >= (double)thresh)
                shadeF = (1.0 - (tnorm - thresh) * p->vy) * (double)e.baseVx;
            else
                shadeF = (double)e.baseVx * tnorm * p->vx;
            p->shade = (u8)TruncToward(shadeF);
            p->flags |= 1u;
        }
        if ((p->flags & 1) == 0)
            ++dead;
    }
    return dead < e.count;
}

// gilde.exe 0x42cde8 — VIBE_Particle_UpdateScatter. On the init pass (bit0 set)
// every dead slot is re-seeded with a random colour, direction and speed; the
// direction is rejection-sampled to lie in a spherical shell. Then all live
// slots integrate with a ground bounce + stop threshold. RNG order is byte-exact.
bool UpdateScatter(Emitter& e, u32 now) {
    if ((e.hdr20 & 1) != 0) {
        u32 frameMod = 1;             // v12
        u32 colMask = e.maxAlive;     // v44 = *(u32*)(a1+36) = emitter+0x24 packed RGB base
        e.hdr20 &= ~1u;
        if (e.scatterPalette && e.scatterPalette[112])
            frameMod = e.scatterPalette[112];
        // wave-10 (W10-PARTICLE): null-array guard (see SeedParticles).
        Particle* p = e.particles;
        for (int i = 0; e.particles && i < e.count; ++i, ++p) {
            if ((p->flags & 1) == 0) {
                // AND mask is byte3 of colMask for all three channels (loaded
                // once into dl); the additive base is byte2/byte1/byte0.
                u8 mask = (u8)(colMask >> 24);
                u8 r;
                r = (u8)crt::RandNext();
                p->colR = (u8)((mask & r) + ((colMask >> 16) & 0xFF)); // +0x4E
                r = (u8)crt::RandNext();
                p->colG = (u8)((mask & r) + ((colMask >> 8) & 0xFF));  // +0x4D
                r = (u8)crt::RandNext();
                p->colB = (u8)((mask & r) + (colMask & 0xFF));         // +0x4C
                p->frame = (u8)((u32)i % frameMod);                    // +0x50
                p->px = (double)Rnd() * kRandNorm * kScSp4 + kScBias_2;
                p->py = (double)Rnd() * kRandNorm * kScV15;
                p->pz = (double)Rnd() * kRandNorm * kScSp4 + kScBias_2;
                p->vx = (double)Rnd() * kRandNorm * ((double)e.maxSpeed * kScSp2) - (double)e.maxSpeed;
                p->vy = (double)Rnd() * kRandNorm * e.damping + e.minSpeed;
                float vz = (double)Rnd() * kRandNorm * ((double)e.maxSpeed * kScSp2) - (double)e.maxSpeed;
                p->vz = vz;
                float rad = std::sqrt(vz * p->vz + p->vx * p->vx);
                if (rad > e.maxSpeed || rad < (double)e.maxSpeed * kScR04) {
                    double range = (double)e.maxSpeed * kScRej * kScSp2;
                    float scale = (float)(((double)Rnd() * kRandNorm * range -
                                           (double)e.maxSpeed * kScRej + e.maxSpeed) / rad);
                    p->vx = p->vx * scale;
                    p->vz = p->vz * scale;
                }
                u8 rr = (u8)crt::RandNext();
                p->life = (float)((rr & 0x3F) + 192);
                double lifeJit = (double)Rnd() * kRandNorm * ((double)e.baseVx * kScSp2) - (double)e.baseVx;
                p->birthTime = now;
                p->flags |= 1u;
                p->seed = (float)(lifeJit + e.lifeBase);
            }
        }
    }
    int dead = 0;
    Particle* p = e.particles;
    // wave-10 (W10-PARTICLE): null-array guard (see SeedParticles).
    for (int j = 0; e.particles && j < e.count; ++j, ++p) {
        if ((p->flags & 1) != 0) {
            // disasm 0x42ce2e: dt fst-stored to a float slot but kept 80-bit; px
            // uses the un-rounded 80-bit dt, py/pz reload the float-rounded dt.
            double dtEx = (double)(u32)(now - p->birthTime) * kScDt;
            float dt = (float)dtEx;
            p->px = (float)(dtEx * p->vx + p->px);
            p->py = dt * p->vy + p->py;
            float npz = dt * p->vz + p->pz;
            p->birthTime = now;
            p->pz = npz;
            if (p->py >= (double)e.velScale) {
                u32 vyBits;
                __builtin_memcpy(&vyBits, &p->vy, 4);
                if ((vyBits & 0x7FFFFFFFu) != 0)
                    p->vy = p->vy - dt * e.baseVy;
            } else {
                p->py = e.velScale;
                p->vx = p->vx - p->vx * kScBX;
                p->vy = -p->vy * kScBY;
                p->vz = p->vz - p->vz * kScBZ;
                if (std::fabs(p->vy) < kScStop) {
                    p->vx = 0.0f;
                    p->vy = 0.0f;
                    p->vz = 0.0f;
                }
            }
            // disasm 0x42ceb7: fmul [ebx+8] == emitter+0x8 == baseVz (NOT damping
            // +0x18). Hex-Rays' "a1+8" is baseVz; the prior reading was wrong.
            float nlife = p->life - dt * e.baseVz;
            p->life = nlife;
            if (nlife <= 0.0f)
                p->flags &= ~1u;
            else
                p->shade = (u8)TruncToward(p->life);
        }
        if ((p->flags & 1) == 0)
            ++dead;
    }
    return dead < e.count || (e.hdr20 & 2) != 0;
}

} // namespace guild::render
