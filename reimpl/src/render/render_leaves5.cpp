#include "render/render_leaves5.h"

#include "crt/rand.h"            // crt::RandNext (0x5cb8bc, reused)

#include <cmath>
#include <cstdint>
#include <cstring>

// The per-frame integrator pointers stored in the system header. They are the
// REAL reconstructed kernels (particle.cpp); we keep their addresses as opaque
// void* exactly like the original stored a code pointer at sys+0x304. Declared
// here only to take their address for that storage.
namespace guild::render {
char UpdateEmitter(Emitter&, u32);
char UpdateGravity(Emitter&, u32);
bool UpdateCosineWave(Emitter&, u32);
bool UpdateFadeOut(Emitter&, u32);
bool UpdateScatter(Emitter&, u32);
char SeedParticles(Emitter&, u32);   // 0x42bec0 — SpawnRefLens' real integrator
int  TruncToward(double);   // VIBE_Coord_ConvertX (0x5c6b08), particle.cpp
} // namespace guild::render

namespace guild::render {

// ===========================================================================
// Particle-slot accessors. The 84-byte Particle layout is faithfully modelled
// in particle.h; we touch a few fields the spawn ctors use via raw byte offsets
// to keep the arithmetic visibly 1:1 with the decompiler's `*(t*)(slot+off)`.
// ===========================================================================
namespace {

inline u8*       SB(void* p)             { return static_cast<u8*>(p); }
template <class T> inline T  Rd(const void* p, int off) { T v; std::memcpy(&v, static_cast<const u8*>(p) + off, sizeof(T)); return v; }
template <class T> inline void Wr(void* p, int off, T v) { std::memcpy(SB(p) + off, &v, sizeof(T)); }

inline float Rf(const void* p, int off) { return Rd<float>(p, off); }
inline void  Wf(void* p, int off, float v) { Wr<float>(p, off, v); }

// The default per-particle slot count the original AllocSystem derives from a
// global; the reimpl AllocSystem takes it explicitly, so the ctors thread it.

// ===========================================================================
// Cross-module hook (inert default).
// ===========================================================================
void* DefaultLensColourTag() { return nullptr; }

RenderLeaves5Hooks g_hooks = { &DefaultLensColourTag };

} // namespace

void InstallRenderLeaves5Hooks(const RenderLeaves5Hooks& h) {
    g_hooks.lensColourTag = h.lensColourTag ? h.lensColourTag : &DefaultLensColourTag;
}
const RenderLeaves5Hooks& CurrentRenderLeaves5Hooks() { return g_hooks; }

// ===========================================================================
// 0x42be58 — VIBE_Particle_InitColors
//   sys[0]=baseW0; sys[1]=baseW1; sys[2]=baseW2;
//   if (count > 0) do {   // do-while: entry guard `count > 0` (jle @0x42be80)
//     slot.flags &= ~1;
//     slot+0x4E = (rand & 0x3F) - 66;   slot+0x4D = (rand & 0x3F) + 100;
//     result = slot+0x4C = (rand & 0x3F) + 50;
//   } while (++i < count);   // continue guard `ecx < count` (jl @0x42beb6)
// HARDEN: the original seeds ALL `count` slots (0..count-1). The post-increment
// compare `ecx < count` runs the body exactly `count` times — NOT count-1. The
// prior code/comment ("v15+1 < count", "seeds count-1") was wrong: there is no
// `+1` in the disasm; ecx starts at 0 (xor) and is compared after `inc ecx`.
// The three base words ARE written unconditionally (`*a1=a2; a1[1]=a3; a1[2]=a4`
// @0x42be66/72/7b). The reconstructed ParticleSystem does not preserve those raw
// sys+0/4/8 offsets, so they are accepted as params and dropped (engine
// bookkeeping); the deterministic per-slot seeding is reproduced exactly.
// ===========================================================================
u8 InitColors(void* sysVoid, i32 /*baseW0*/, i32 /*baseW1*/, i32 baseW2, i32 slotCount) {
    ParticleSystem* sys = static_cast<ParticleSystem*>(sysVoid);
    Particle* parts = static_cast<Particle*>(sys->particles);
    // result = a4 (baseW2 low byte) @0x42be75 — the al residue if count <= 0.
    u8 result = static_cast<u8>(baseW2);
    for (int i = 0; i < slotCount; ++i) {   // 0x42be80 entry / 0x42beb6 continue
        u8* s = SB(&parts[i]);
        s[0x51] &= ~1u;               // 0x42be82
        s[0x4E] = static_cast<u8>((crt::RandNext() & 0x3F) - 66);   // 0x42be8f
        s[0x4D] = static_cast<u8>((crt::RandNext() & 0x3F) + 100);  // 0x42be9b
        result  = static_cast<u8>((crt::RandNext() & 0x3F) + 50);   // 0x42bea5
        s[0x4C] = result;             // 0x42bea7
    }
    return result;
}

// ===========================================================================
// 0x42bd6c — VIBE_Particle_SpawnEffect
// ===========================================================================
ParticleSystem* SpawnEffect(void* ownerNode, int tagA2, const u8* texName,
                            char triggerBit, const float velRec[3], int userTag,
                            float life, int matHandleA9, int colA10, int colA11,
                            int colA12, float meshTurnRate, EffectHeader& hdr,
                            int slotCount, u32 now) {
    if (!ownerNode)                                       // 0x42bd80
        return nullptr;
    // v16 = [a1+460] (bound mesh); guard mesh && [mesh+16] (frame). The reimpl
    // owner record models these as the leading two pointers; treat as raw.
    void* mesh = Rd<void*>(ownerNode, 460);               // 0x42bd82
    if (!mesh)
        return nullptr;
    if (!Rd<void*>(mesh, 16))                             // 0x42bd8c
        return nullptr;

    // tag dword: LOWORD=127, HIWORD=(triggerBit & 1) | 2  (0x42bdbe/0x42bdc6).
    i32 tag = 0x7F | (((triggerBit & 1) | 2) << 16);
    static const void* kUpdateEmitter = reinterpret_cast<const void*>(&UpdateEmitter);
    ParticleSystem* sys = AllocSystem(/*owner*/ userTag, texName, tag,
                                      life, /*userTag*/ tagA2,
                                      const_cast<void*>(kUpdateEmitter), slotCount, now);
    if (!sys)
        return nullptr;

    // VIBE_Particle_SetPosition(sys, &flt_5CA2E0) — origin (0,0,0).
    const float origin[3] = {0.0f, 0.0f, 0.0f};
    SetPosition(sys, origin);                             // 0x42bde8

    // Header writes (sys+0/+4/+8 = velRec; +36 = 2; +12 = meshTurnRate; ...).
    hdr.i(0) = Rd<i32>(velRec, 0);                        // [v19+0] = [a5+0]   0x42bdf0
    hdr.i(4) = Rd<i32>(velRec, 4);                        // [v19+4] = [a5+4]   0x42bdf5
    hdr.f(8) = velRec[2];                                 // [v19+8] = [a5+8]   0x42be02
    hdr.i(36) = 2;                                        // [v19+36] = 2       0x42bdfb (+0x24)
    // The original also writes [v19+200]=a2(tagA2): +200 is outside the header
    // overlay (engine bookkeeping in the reconstructed layout), so it is dropped.
    (void)tagA2;
    hdr.i(16) = matHandleA9;                              // [v19+16] = a9      0x42be1e
    hdr.f(12) = meshTurnRate;                             // [v19+12] = turnRate 0x42be21
    hdr.f(4)  = meshTurnRate + hdr.f(4);                  // [v19+4] += turnRate 0x42be2b
    hdr.f(12) = hdr.f(12) * kEffectDurScale;              // [v19+12] *= 0.75   0x42be45
    hdr.i(20) = colA10;                                   // [v19+20] = a10     0x42be31
    hdr.i(24) = colA11;                                   // [v19+24] = a11     0x42be3e
    hdr.i(28) = colA12;                                   // [v19+28] = a12     0x42be48
    return sys;
}

// ===========================================================================
// 0x42c728 — VIBE_Particle_SpawnBlood
// ===========================================================================
ParticleSystem* SpawnBlood(int ownerA1, int hdr0, int hdr1, int hdr2, int hdr3,
                           int startActive, EffectHeader& hdr, int slotCount,
                           u32 now) {
    static const void* kUpdateGravity = reinterpret_cast<const void*>(&UpdateGravity);
    // HARDEN: AllocSystem(owner=200, "Blut", 131199, 1.0, userTag=a1, UpdateGravity)
    // @0x42c75f — the original hardcodes owner=200 (eax) and passes a1 as userTag,
    // NOT owner=a1. The prior code used ownerA1 for both, which diverges on the
    // null-owner gate (AllocSystem returns null when owner==0; owner=200 never is)
    // and on the stored sys->owner / sys->userTag values.
    ParticleSystem* sys = AllocSystem(/*owner*/ 200, reinterpret_cast<const u8*>("Blut"),
                                      131199, 1.0f, /*userTag*/ ownerA1,
                                      const_cast<void*>(kUpdateGravity), slotCount, now);
    if (!sys)
        return nullptr;
    hdr.i(32) = 1 | (2 * startActive);                    // 0x42c76c / 0x42c775
    hdr.i(0)  = hdr0;                                     // 0x42c77c
    hdr.i(4)  = hdr1;                                     // 0x42c782
    hdr.i(8)  = hdr2;                                     // 0x42c789
    hdr.i(12) = hdr3;                                     // 0x42c796
    Particle* parts = static_cast<Particle*>(sys->particles);
    for (int i = 0; i < slotCount; ++i)                   // 0x42c79d..0x42c7b5
        SB(&parts[i])[0x51] &= ~1u;
    return sys;
}

// ===========================================================================
// 0x42c8e0 — VIBE_Particle_SpawnExplosion
// ===========================================================================
ParticleSystem* SpawnExplosion(int ownerA1, int ownerA2, int hdr0, float radius,
                               int hdr2, float life, int hdr8, EffectHeader& hdr,
                               int slotCount, u32 now) {
    static const void* kUpdateCosine = reinterpret_cast<const void*>(&UpdateCosineWave);
    // HARDEN: AllocSystem(owner=a2(ebx), "Explosion", 65791, life=a6, userTag=a1(eax),
    // UpdateCosineWave) @0x42c916. The original's owner is a2 (ebx) and userTag is
    // a1 (eax) — NOT both a1. The real caller @0x486822 passes a1=0, a2=30.
    ParticleSystem* sys = AllocSystem(/*owner*/ ownerA2,
                                      reinterpret_cast<const u8*>("Explosion"),
                                      65791, life, /*userTag*/ ownerA1,
                                      const_cast<void*>(kUpdateCosine), slotCount, now);
    if (!sys)
        return nullptr;

    int sextant = slotCount / 6;                          // 0x42c94f (v12)
    float inv   = 1.0f / static_cast<float>(slotCount);   // 0x42c955 (v29)
    const float zero[3] = {0.0f, 0.0f, 0.0f};
    SetPosition(sys, zero);                               // 0x42c959

    hdr.i(0) = hdr0;                                      // 0x42c962
    hdr.f(4) = radius;                                    // 0x42c969 (emitter radius)
    hdr.i(8) = hdr2;                                      // 0x42c970
    hdr.i(32) = hdr8;                                     // 0x42c977 (v9[8])

    Particle* parts = static_cast<Particle*>(sys->particles);
    for (int i = 0; i < slotCount; ++i) {                 // 0x42c987..0x42caad
        u8* s = SB(&parts[i]);
        s[0x51] |= 1u;                                    // 0x42c989

        float r0 = static_cast<float>(crt::RandNext()) * kExpRandNorm;   // v23
        float r1 = static_cast<float>(crt::RandNext()) * kExpRandNorm;   // v26
        float r2 = static_cast<float>(crt::RandNext()) * kExpRandNorm;   // v30
        float a = radius * kExpScaleHalf;                 // 0x42c9db (v15)
        float vx = r0 * a;                                // v24
        float vy = r1 * a;                                // v27
        float vz = a * r2;                                // v31
        float b  = radius * kExpScaleHalf2;               // 0x42c9fc (v16)
        float c  = kExpScaleHalf * b;                     // 0x42ca02 (v17)
        float ox = vx - c;                                // v25
        float oy = vy - b * kExpAxisDampZ;                // v28
        float oz = vz - c;                                // v32

        // v19 = (double)(int)RandNext() * flt_611AD0(2pi) * flt_611AC0(norm), all
        // 80-bit on the x87 stack (fild;fmul 2pi;fmul norm @0x42ca2d..37), fed
        // straight to fsin/fcos. Modelled in double; order is i*2pi*norm.
        double angle = static_cast<double>(crt::RandNext()) * kExpTwoPi * kExpRandNorm; // 0x42ca37
        // v20 = sin(i * (pi/2) * inv) * radius  -> slot+0x14  (radius == sys+4 == a4)
        float yprof = static_cast<float>(
            std::sin(static_cast<double>(i) * kExpHalfPi * inv) * radius);            // 0x42ca51
        Wf(s, 0x14, yprof);                               // fst -> slot+0x14 (v18[5])
        // HARDEN: the sqrt's first term is (float)sys[0] == hdr0 REINTERPRETED as a
        // float (fld dword ptr [esi] @0x42ca5a), NOT radius. The prior code used
        // radius*radius which diverges whenever hdr0's float bits != radius (the
        // real caller @0x486822 passes hdr0=119.0f bits with radius=80.0f). The
        // 80-bit x87 chain (fsubrp/fsqrt @0x42ca5e/60, sin*ring @0x42ca75) is
        // modelled in double; the stored yprof (float) is re-read for the square
        // (fmul dword ptr [ecx+14h] @0x42ca57).
        float hdr0f;
        std::memcpy(&hdr0f, &hdr0, 4);                    // (float)sys[0]
        double ring = std::sqrt(static_cast<double>(hdr0f) * hdr0f
                       - static_cast<double>(yprof) * Rf(s, 0x14));      // 0x42ca60
        Wf(s, 0x10, static_cast<float>(std::sin(angle) * ring)); // v18[4] 0x42ca77
        Wf(s, 0x18, static_cast<float>(std::cos(angle) * ring)); // v18[6] 0x42ca7c
        Wf(s, 0x10, Rf(s, 0x10) + ox);                    // 0x42ca86
        Wf(s, 0x14, Rf(s, 0x14) + oy);                    // 0x42ca90
        Wf(s, 0x18, Rf(s, 0x18) + oz);                    // 0x42ca9a

        if (sextant != 0 && (i % sextant) == 0) {         // 0x42ca71
            Wf(s, 0x10, Rf(s, 0x10) * kExpRingScaleX);    // 0x42cac3
            Wf(s, 0x14, Rf(s, 0x14) * kExpRingScaleY);    // 0x42cacf
            Wf(s, 0x18, kExpRingScaleX * Rf(s, 0x18));    // 0x42cad5
        }
    }
    return sys;
}

// ===========================================================================
// 0x42cbc4 — VIBE_Particle_SpawnSmoke
//   eax=a1(owner), edx=a2(setPos), ebx=a3(texSlot); ecx=posRec; stack args:
//   life(a4), a6, a7, a8, spread(a9), a11, frameStep(a12), col(a13).
// ===========================================================================
ParticleSystem* SpawnSmoke(int ownerA1, const float setPos[3],
                           const float posRec[3], int texSlotA3, float life,
                           int hdr4A6, int matHandleA7, int hdr0A8, float spreadA9,
                           int colExtremaA11, int frameStepA12, int colTripleA13,
                           EffectHeader& hdr, int slotCount, u32 now) {
    (void)colExtremaA11;
    static const void* kUpdateFade = reinterpret_cast<const void*>(&UpdateFadeOut);
    ParticleSystem* sys = AllocSystem(texSlotA3, reinterpret_cast<const u8*>("Rauch"),
                                      131199, life, ownerA1,
                                      const_cast<void*>(kUpdateFade), slotCount, now);
    if (!sys)
        return sys;

    u32 birthBase = now;                                  // 0x42cc1d dword_62EB38
    SetPosition(sys, setPos);                             // 0x42cc2f (edx=a2)

    hdr.i(0) = hdr0A8;                                    // 0x42cc38 (*ebx = a8)
    hdr.i(4) = hdr4A6;                                    // 0x42cc3e ([ebx+4] = a6)
    hdr.f(16) = posRec[0];                                // 0x42cc52 ([ebx+16])
    hdr.f(20) = posRec[1];                                // 0x42cc58 ([ebx+20])
    hdr.f(24) = posRec[2];                                // 0x42cc5e ([ebx+24])
    hdr.i(32) = colExtremaA11;                            // 0x42cc61 ([ebx+32] = a11)

    if (slotCount <= 0)                                   // 0x42cc66
        return sys;

    float driftBias = spreadA9 * kSmokeDriftBias;         // 0x42cc81 (v41)
    u32 frame = birthBase;                                // 0x42cc85 (v22)
    Particle* parts = static_cast<Particle*>(sys->particles);
    float fadePivot = hdr.f(4);                            // sys+4 read in fade calc

    for (int i = 0; i < slotCount; ++i) {                 // 0x42cc8d..0x42cdd4
        u8* s = SB(&parts[i]);
        Wf(s, 0x10, posRec[0]);                           // [edx+10h] = *ecx   0x42cc8d
        Wr<i32>(s, 0x14, Rd<i32>(posRec, 4));             // [edx+14h] = [ecx+4] 0x42cc93
        Wr<i32>(s, 0x18, Rd<i32>(posRec, 8));             // [edx+18h] = [ecx+8] 0x42cc99
        Wr<i32>(s, 0x14, matHandleA7);                    // [edx+14h] = eax(a7) 0x42cc9c (overwrites)

        float r0 = static_cast<float>(crt::RandNext()) * kSmokeRandNorm; // v33
        float r1 = static_cast<float>(crt::RandNext()) * kSmokeRandNorm; // v42
        float r2 = static_cast<float>(crt::RandNext()) * kSmokeRandNorm; // v36
        float d0 = r0 * spreadA9 - driftBias;             // v35
        float d1 = r1 * spreadA9 - driftBias;             // v44
        float d2 = r2 * spreadA9 - driftBias;             // v38
        Wf(s, 0x10, Rf(s, 0x10) + d0);                    // [edx+10h] += d0  0x42cd2d
        Wf(s, 0x14, Rf(s, 0x14) + d1);                    // [edx+14h] += d1  0x42cd37
        Wf(s, 0x18, Rf(s, 0x18) + d2);                    // [edx+18h] += d2  0x42cd41

        float fade = (Rf(s, 0x14) - fadePivot) / Rf(s, 0x14); // 0x42cd4a (v24)
        Wf(s, 0x1C, fade);                                // 0x42cd4d ([+0x1C])
        Wf(s, 0x00, 1.0f / fade);                         // 0x42cd54 (*v23 = 1/v24)
        float inv2 = 1.0f / (1.0f - Rf(s, 0x1C));         // 0x42cd5d (v25)
        s[0x4F] = 0;                                      // 0x42cd5f
        Wf(s, 0x04, inv2);                                // 0x42cd63

        u8 rb = static_cast<u8>(crt::RandNext());         // v26
        s[0x4E] = static_cast<u8>(((colTripleA13 >> 24) & rb) + ((colTripleA13 >> 16) & 0xFF)); // 0x42cd7d
        u8 rg = static_cast<u8>(crt::RandNext());         // v28
        s[0x4D] = static_cast<u8>(((colTripleA13 >> 24) & rg) + ((colTripleA13 >> 8) & 0xFF));  // 0x42cd97
        u8 rr = static_cast<u8>(crt::RandNext());         // v39
        s[0x4C] = static_cast<u8>(((colTripleA13 >> 24) & rr) + (colTripleA13 & 0xFF));         // 0x42cdb1
        u8 rf = static_cast<u8>(crt::RandNext());         // v31
        // *((_DWORD*)v20 - 9) = frame + (rf & 7)  ; v20 had advanced by +84,
        // so this writes slot+48 (the birth/frame dword).
        Wr<i32>(s, 0x30, static_cast<i32>(frame + (rf & 7)));   // 0x42cdc3
        frame += static_cast<u32>(frameStepA12);          // 0x42cdd0
    }
    return sys;
}

// ===========================================================================
// 0x42d188 — VIBE_Particle_SpawnDebris
// ===========================================================================
ParticleSystem* SpawnDebris(int ownerA1, const float posRec[3], int texSlotA3,
                            float life, int h0, int h1, int h2, int h3, int h4,
                            int h5, int h6, int h9, EffectHeader& hdr,
                            int slotCount, u32 now) {
    static const void* kUpdateScatter = reinterpret_cast<const void*>(&UpdateScatter);
    // HARDEN: the texture name is "Erdbrocken_an0" (aErdbrockenAn0 @0x611b34), not
    // "Erdbrocken anim" — the prior string was wrong. AllocSystem(owner=a3(texSlot),
    // name, 131199, life=a4, userTag=a1, UpdateScatter) @0x42d1ba.
    ParticleSystem* sys = AllocSystem(texSlotA3,
                                      reinterpret_cast<const u8*>("Erdbrocken_an0"),
                                      131199, life, ownerA1,
                                      const_cast<void*>(kUpdateScatter), slotCount, now);
    if (!sys)
        return nullptr;
    SetPosition(sys, posRec);                             // 0x42d1d3
    hdr.i(32) = 1;                                        // 0x42d1dc (v16[8])
    hdr.i(0)  = h0;  hdr.i(4)  = h1;  hdr.i(8)  = h2;     // 0x42d1e3/e9/f0
    hdr.i(12) = h3;  hdr.i(16) = h4;  hdr.i(20) = h5;     // 0x42d1f7/fe/205
    hdr.i(24) = h6;  hdr.i(36) = h9;                      // 0x42d20c/213
    return sys;
}

// ===========================================================================
// 0x42c3ac — VIBE_Particle_SpawnRefLens
// ===========================================================================
ParticleSystem* SpawnRefLens(int ownerA1, int tagA2, int texSlotA3,
                             const float posRec[3], EffectHeader& hdr,
                             int slotCount, u32 now) {
    hdr.i(4) = tagA2;                                     // v8[1] = a2 (0x42c3ad)
    hdr.i(0) = 131199;                                    // v8[0] = 131199 (0x42c3b7)
    // HARDEN: the stored integrator is VIBE_Particle_SeedParticles (0x42bec0), NOT
    // UpdateScatter — the original passes (int)VIBE_Particle_SeedParticles as the
    // updateFn @0x42c3df. AllocSystem(owner=a3(texSlot), "Ref-Map-Linse", 131199,
    // 7.0, userTag=a1, SeedParticles).
    static const void* kSeedParticles = reinterpret_cast<const void*>(&SeedParticles);
    ParticleSystem* sys = AllocSystem(texSlotA3,
                                      reinterpret_cast<const u8*>("Ref-Map-Linse"),
                                      131199, 7.0f, ownerA1,
                                      const_cast<void*>(kSeedParticles), slotCount, now);
    if (!sys)
        return nullptr;
    // InitColors(sys, v8(=&{131199,tagA2}), lensTag, 1063675494, 1082130432,
    //            -1063256064). The first two extra dwords are the colour base
    // words; the lens tag is the opaque "active-listener" pointer (inert hook).
    (void)g_hooks.lensColourTag();
    InitColors(sys, 1063675494, 1082130432, -1063256064, slotCount); // 0x42c416
    SetPosition(sys, posRec);                             // 0x42c41d
    return sys;
}

// ===========================================================================
// 0x4d880c — VIBE_Particle_SpawnSmokeEffect
//   v3 = {0, 1128792064(=0x43480000=200.0f), 0};  v4 colour/flag literals;
//   tail-call SpawnEffect(owner=[a1+97], v4, "Rauch", 1, v3, 300, 1, 30.0,
//                         0x40000000, 1089470464, 1053609165, 1065353216).
// ===========================================================================
ParticleSystem* SpawnSmokeEffect(void* owner, int tagA2, float meshTurnRate,
                                 EffectHeader& hdr, int slotCount, u32 now) {
    float vel[3];
    Wr<i32>(vel, 0, 0);                                   // v3[0] = 0
    Wr<i32>(vel, 4, 1128792064);                          // v3[1] = 200.0f
    Wr<i32>(vel, 8, 0);                                   // v3[2] = 0
    // v4: LOBYTE=100, [+1..+2]=-24321 (0xA0FF) -> the (tag,trigger) dword.
    i32 v4 = 0;
    reinterpret_cast<u8*>(&v4)[0] = 100;                  // 0x4d883b
    std::int16_t hw = static_cast<std::int16_t>(-24321);
    std::memcpy(reinterpret_cast<u8*>(&v4) + 1, &hw, 2);  // 0x4d8858
    (void)tagA2; (void)v4;
    return SpawnEffect(owner, v4, reinterpret_cast<const u8*>("Rauch"),
                       /*triggerBit*/ 1, vel, /*userTag*/ 300, /*life*/ 30.0f,
                       /*matHandleA9*/ 0x40000000, /*colA10*/ 1089470464,
                       /*colA11*/ 1053609165, /*colA12*/ 1065353216,
                       meshTurnRate, hdr, slotCount, now);
}

// ===========================================================================
// 0x43fa1c — VIBE_Particle_SpawnBloodEffect
//   100-slot UpdateRainStep system; fan-seed each slot's velocity:
//     vy = (i * 16.0) * 0.01 ;  vx/vz randomized;  per-slot fade derived.
// ===========================================================================
ParticleSystem* SpawnBloodEffect(const int* ownerHolder, u32 now) {
    static const void* kUpdateRain = reinterpret_cast<const void*>(&UpdateRainStep);
    // HARDEN: the texture name is "blut" (lowercase, aBlut @0x6175e0), NOT "Blut".
    // AllocSystem(owner=100, "blut", 196735, 1.0, userTag=*a1, UpdateRainStep).
    ParticleSystem* sys = AllocSystem(/*owner*/ 100,
                                      reinterpret_cast<const u8*>("blut"),
                                      196735, 1.0f, *ownerHolder,
                                      const_cast<void*>(kUpdateRain), /*slotCount*/ 100, now);
    if (!sys)
        return nullptr;
    Particle* parts = static_cast<Particle*>(sys->particles);
    for (int i = 0; i < 100; ++i) {                       // 0x43fa6b..0x43fb6c
        u8* s = SB(&parts[i]);
        float yfan = static_cast<float>(i) * kBloodFanStep; // v4
        Wr<i32>(s, 0x38, 0);                              // slot+56 = 0  (vx scratch)
        Wf(s, 0x3C, yfan * kBloodFanScale);               // slot+60 = v4*0.01
        Wr<i32>(s, 0x40, 0);                              // slot+64 = 0
        Wr<i32>(s, 0x10, Rd<i32>(s, 0x38));               // slot+16 = slot+56
        Wr<i32>(s, 0x14, Rd<i32>(s, 0x3C));               // slot+20 = slot+60
        Wr<i32>(s, 0x18, Rd<i32>(s, 0x40));               // slot+24 = slot+64

        float rx = static_cast<float>(crt::RandNext());
        Wf(s, 0x00, static_cast<float>(static_cast<double>(rx) * kBloodRandNorm
                       * kBloodSpread + kBloodBiasX));     // slot+0 0x43fad4
        float ry = static_cast<float>(crt::RandNext());
        Wf(s, 0x04, static_cast<float>(kBloodBaseY
                       - static_cast<double>(ry) * kBloodRandNorm * kBloodSpread
                       + kBloodBiasY));                    // slot+4 0x43faff
        Wf(s, 0x08, static_cast<float>(kBloodSumZ - Rf(s, 0x04))); // slot+8 0x43fb10

        double fade = static_cast<double>(Rf(s, 0x3C)) * kBloodFadeScale + kBloodFadeBias;
        double fclamped = (fade < 0.0) ? 0.0 : fade;       // 0x43fb32
        int fb = TruncToward(fclamped);                    // VIBE_Coord_ConvertX
        s[0x4F] = static_cast<u8>(fb);                     // slot+79 0x43fb66
    }
    return sys;
}

// ===========================================================================
// 0x43f858 — VIBE_Particle_UpdateRainStep
//   eax:edx = {sys, now}.  For each of count slots:
//     dt = (now - slot+48) * 0.001 ;
//     slot+56 = slot+16 + slot+0 ; slot+60 = slot+20 + slot+4 ;
//     slot+64 = slot+24 + slot+8 ; slot+72 += dt*2.0 ; slot+4 -= dt ;
//     if (slot+60 < 0) respawn (fresh randomized vel + clamp) ;
//     slot+16/20 = slot+56/60 ; slot+24 = slot+64 ;
//     fade = slot+60 * 1.5 + (-20) ; clamp>=0 ; slot+79 = trunc(fade).
// ===========================================================================
u8 UpdateRainStep(void* sysVoid, u32 now) {
    ParticleSystem* sys = static_cast<ParticleSystem*>(sysVoid);
    Particle* parts = static_cast<Particle*>(sys->particles);
    int count = sys->count;                               // v1[52]
    u8 last = 0;
    for (int i = 0; i < count; ++i) {                     // 0x43f86e..0x43f96c
        u8* s = SB(&parts[i]);
        // HARDEN: v4 (dt) stays on the x87 stack as 80-bit (fild;fmul @0x43f888),
        // never rounded to float — it is reused at full precision for both v4*2.0
        // (0x43f8bc) and slot+4 - v4 (0x43f8d0). Keep dt as double.
        double dt = static_cast<double>(
                     static_cast<u32>(now - Rd<u32>(s, 0x30))) * kRainDtScale; // v4
        Wf(s, 0x38, Rf(s, 0x10) + Rf(s, 0x00));           // 0x43f898
        Wf(s, 0x3C, Rf(s, 0x14) + Rf(s, 0x04));           // 0x43f8a7
        Wf(s, 0x40, Rf(s, 0x18) + Rf(s, 0x08));           // 0x43f8b6
        Wf(s, 0x48, static_cast<float>(dt * kRainGravity + Rf(s, 0x48)));  // 0x43f8c9
        Wf(s, 0x04, static_cast<float>(Rf(s, 0x04) - dt));// 0x43f8d4

        if (Rf(s, 0x3C) < 0.0f) {                         // 0x43f8e7
            Wr<i32>(s, 0x38, 0);                          // slot+56 = 0
            // *(_QWORD*)(slot+60) = 1120403456 -> slot+60 = 100.0f, slot+64 = 0
            Wf(s, 0x3C, 100.0f);                          // 0x43f981 (low dword)
            Wr<i32>(s, 0x40, 0);
            float rx = static_cast<float>(crt::RandNext());
            Wf(s, 0x00, static_cast<float>(static_cast<double>(rx) * kRainRandNorm
                           * kRainSpread + kRainBiasX));   // slot+0 0x43f9b2
            float ry = static_cast<float>(crt::RandNext());
            Wf(s, 0x04, static_cast<float>(kRainBaseY
                           - static_cast<double>(ry) * kRainRandNorm * kRainSpread
                           + kRainBiasY));                 // slot+4 0x43f9dd
            Wf(s, 0x08, static_cast<float>(kRainSumZ - Rf(s, 0x04))); // slot+8
            Wr<u32>(s, 0x30, now);                         // slot+48 = now
            // slot+72 = v1[57] (lifeBase)
            Wf(s, 0x48, sys->lifeBase);                    // 0x43fa02
        }

        Wr<i32>(s, 0x10, Rd<i32>(s, 0x38));               // slot+16 = slot+56
        Wr<i32>(s, 0x14, Rd<i32>(s, 0x3C));               // slot+20 = slot+60
        Wr<i32>(s, 0x18, Rd<i32>(s, 0x40));               // slot+24 = slot+64

        double fade = static_cast<double>(Rf(s, 0x3C)) * kRainFadeScale + kRainFadeBias;
        double fclamped = (fade >= 0.0) ? fade : 0.0;     // 0x43f92e
        int fb = TruncToward(fclamped);                    // VIBE_Coord_ConvertX
        last = static_cast<u8>(fb);
        s[0x4F] = last;                                    // slot+79 0x43f95d
    }
    return last;
}

} // namespace guild::render
