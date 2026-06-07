#pragma once
#include "guild/common/types.h"
#include "render/particle.h"        // Emitter / Particle / kRandNorm (reused)
#include "render/particle_spawn.h"  // ParticleSystem / AllocSystem / SetPosition (reused)

// =============================================================================
// guild::render — render-math leaves, batch 5 (gilde.exe d3_par.c particle SPAWN
// constructors + the rain integrator).
//
// A fifth slice of self-contained, deterministic VIBE_Particle_* leaves
// translated 1:1 from the Hex-Rays reference. These are the *typed effect
// constructors*: each fills the freshly allocated particle SYSTEM header + seeds
// every particle slot with a deterministic (RNG-driven) initial state, then
// returns the system. They are pure arithmetic / RNG / pointer-graph bookkeeping
// over the records the (already reconstructed) allocator handed back — no
// DDraw/GDI/device calls — so they are golden-testable.
//
// Cross-module callees that ARE reconstructed are reused directly (no hook):
//   guild::render::AllocSystem   (0x5e1000 particle_spawn.cpp) — system + slot alloc
//   guild::render::SetPosition   (0x5e1228 particle_spawn.cpp) — place the system
//   guild::crt::RandNext         (0x5cb8bc rand.cpp)           — the LCG
//   guild::render::TruncToward   (0x5c6b08 VIBE_Coord_ConvertX via particle.cpp) — x87 chop
//   guild::render::UpdateFadeOut / UpdateCosineWave / UpdateGravity /
//   guild::render::UpdateScatter / UpdateEmitter (particle.cpp) — stored as the
//                                  system's per-frame integrator pointer (+0x304).
//
// The one callee that is NOT yet reconstructed (InitColors' sibling lens-colour
// seeder is reconstructed HERE; SpawnRefLens' position seeder is the only inert
// dependency) is routed through RenderLeaves5Hooks (inert default).
//
// Translated functions (all verified UNTRANSLATED at time of writing, case-
// insensitively, by both address and bare name across ALL of src/render):
//   0x42be58  VIBE_Particle_InitColors      (RNG per-slot R/G/B seeding)
//   0x42bd6c  VIBE_Particle_SpawnEffect     (sticky directional emitter ctor)
//   0x42c728  VIBE_Particle_SpawnBlood      (ballistic blood ctor)
//   0x42c8e0  VIBE_Particle_SpawnExplosion  (cosine-wave radial burst ctor)
//   0x42cbc4  VIBE_Particle_SpawnSmoke      (fade-out smoke ctor, drift + colour)
//   0x42d188  VIBE_Particle_SpawnDebris     (scatter debris ctor)
//   0x42c3ac  VIBE_Particle_SpawnRefLens    (reflection-lens single-system ctor)
//   0x4d880c  VIBE_Particle_SpawnSmokeEffect(SpawnEffect convenience wrapper)
//   0x43fa1c  VIBE_Particle_SpawnBloodEffect(blood-spray ctor, fan seeding)
//   0x43f858  VIBE_Particle_UpdateRainStep  (per-frame rain integrator + respawn)
//
// Recovered constant tables (decoded from gilde.exe raw bytes; bit-exact):
//   Explosion  flt_611AC0..ADC  (RNG-norm, scale halves, 2pi, pi/2, axis damps)
//   Smoke      flt_611AE8/AEC   (RNG-norm, 0.5 drift bias)
//   SpawnEffect flt_6119C4      (0.75 duration scale)
//   Rain       dbl_617590..D8   (dt-scale, gravity, respawn box, fade)
//   BloodEffect flt_617618..dbl_617658 (fan step, spread, fade)
// =============================================================================
namespace guild::render {

// ---------------------------------------------------------------------------
// Recovered constants (get_bytes; bit-exact float/double bit patterns).
// ---------------------------------------------------------------------------
// kRandNorm (== 1/32767) is shared from particle.h.

// Explosion (0x611AC0 table).
constexpr float  kExpRandNorm    = 3.0518509447574615e-05f; // flt_611AC0 (1/32767)
constexpr float  kExpScaleHalf   = 0.10000000149011612f;    // flt_611AC4
constexpr float  kExpScaleHalf2  = 0.5f;                     // flt_611AC8
constexpr float  kExpAxisDampZ   = 0.05000000074505806f;    // flt_611ACC
constexpr float  kExpTwoPi       = 6.2831854820251465f;      // flt_611AD0
constexpr float  kExpHalfPi      = 1.5707963705062866f;      // flt_611AD4
constexpr float  kExpRingScaleX  = 0.4000000059604645f;      // flt_611AD8
constexpr float  kExpRingScaleY  = 0.20000000298023224f;     // flt_611ADC

// Smoke (0x611AE8 table).
constexpr float  kSmokeRandNorm  = 3.0518509447574615e-05f;  // flt_611AE8 (1/32767)
constexpr float  kSmokeDriftBias = 0.5f;                     // flt_611AEC

// SpawnEffect (0x6119C4).
constexpr float  kEffectDurScale = 0.75f;                    // flt_6119C4

// Rain integrator (0x617590 table).
constexpr double kRainDtScale    = 0.001;                    // dbl_617590
constexpr double kRainGravity    = 2.0;                      // dbl_617598
constexpr float  kRainRandNorm   = 3.0518509447574615e-05f;  // flt_6175A0 (1/32767)
constexpr double kRainSpread     = 0.2;                      // dbl_6175A8
constexpr double kRainFadeScale  = 1.5;                      // dbl_6175B0
constexpr double kRainBiasX      = -0.1;                     // dbl_6175B8
constexpr double kRainBaseY      = -0.2;                     // dbl_6175C0
constexpr double kRainBiasY      = 0.1;                      // dbl_6175C8
constexpr double kRainSumZ       = -0.5;                     // dbl_6175D0
constexpr double kRainFadeBias   = -20.0;                    // dbl_6175D8

// BloodEffect (0x617618 table).
constexpr float  kBloodFanStep   = 16.0f;                    // flt_617618
constexpr float  kBloodFanScale  = 0.009999999776482582f;    // flt_61761C
constexpr float  kBloodRandNorm  = 3.0518509447574615e-05f;  // flt_617620 (1/32767)
constexpr double kBloodSpread    = 0.2;                      // dbl_617628
constexpr double kBloodFadeScale = 1.5;                      // dbl_617630
constexpr double kBloodBiasX     = -0.1;                     // dbl_617638
constexpr double kBloodBaseY     = -0.2;                     // dbl_617640
constexpr double kBloodBiasY     = 0.1;                      // dbl_617648
constexpr double kBloodSumZ      = -0.5;                     // dbl_617650
constexpr double kBloodFadeBias  = -20.0;                    // dbl_617658

// ---------------------------------------------------------------------------
// Effect-system HEADER overlay. In the 32-bit original the typed-effect ctors
// write a cluster of scalars into the FRONT of the 0x310 system block (sys+0 ..
// sys+0x20) — the emitter velocity/radius/colour/flags the per-frame integrator
// later reads. The reconstructed (host-native) ParticleSystem does NOT preserve
// those raw offsets (its first fields are the engine-bookkeeping ones), so we
// model the spawn-time header as a separate caller-supplied overlay whose field
// ORDER and byte offsets are exactly the original's. Each ctor fills it; the
// math that re-reads sys+4 (the smoke fade pivot, explosion radius) reads it
// back from here, so the arithmetic stays bit-for-bit 1:1.
//   +0x00 (0)   w0   (i32/float) — ctor-specific dword (a8 / hdr0 / radius src)
//   +0x04 (4)   w1   (i32/float) — ctor-specific (a6 / radius / fade pivot)
//   +0x08 (8)   w2   (i32/float)
//   +0x0C (12)  w3   (i32/float)
//   +0x10 (16)  px   (float)     — emitter base position x (smoke/effect)
//   +0x14 (20)  py   (float)
//   +0x18 (24)  pz   (float)
//   +0x20 (32)  flags(i32)       — emitter flag dword
// (offsets 0x1C/+0x24/+0x28 are written by some ctors as extra header dwords)
struct EffectHeader {
    i32   w[10];   // +0x00,+0x04,+0x08,+0x0C, +0x10..  (10 dwords spans 0..0x24)
    float& f(int byteOff) { return *reinterpret_cast<float*>(&w[byteOff / 4]); }
    i32&   i(int byteOff) { return w[byteOff / 4]; }
    EffectHeader() { for (int k = 0; k < 10; ++k) w[k] = 0; }
};

// ---------------------------------------------------------------------------
// Cross-module hooks (inert defaults in render_leaves5.cpp).
// ---------------------------------------------------------------------------
struct RenderLeaves5Hooks {
    // 0x5e1228 VIBE_Particle_SetPosition is reconstructed and reused directly.
    // 0x42be58 VIBE_Particle_InitColors is reconstructed HERE.
    // SpawnRefLens' only non-reconstructed callee is the lens-colour seeder
    // VIBE_Sound3d_SetActiveListener (passed as an opaque tag pointer in the
    // original — a verbatim quirk of the decompile, NOT a real audio call); we
    // route it through this hook so SpawnRefLens stays exercisable. The default
    // is inert (the tag pointer is stored but never dereferenced by the math).
    void* (*lensColourTag)();
};

void InstallRenderLeaves5Hooks(const RenderLeaves5Hooks& hooks);
const RenderLeaves5Hooks& CurrentRenderLeaves5Hooks();

// ---------------------------------------------------------------------------
// 0x42be58 — VIBE_Particle_InitColors (__userpurge al=f(eax,edx=base,ecx,ebx)).
// Stores the three caller colour-base dwords into sys[+0/+4/+8] and, for every
// particle slot, clears the active bit and seeds a random colour triple:
//   B(+0x4E) = (rand & 0x3F) - 66 ;  G(+0x4D) = (rand & 0x3F) + 100 ;
//   R(+0x4C) = (rand & 0x3F) + 50  (the loop runs count-1 slots — verbatim).
// `sys` is the raw 0x310 system block; `slotCount` == sys[+0xD0]. Returns the
// last R byte the original left in al.
u8 InitColors(void* sys, i32 baseW0, i32 baseW1, i32 baseW2, i32 slotCount);

// ---------------------------------------------------------------------------
// 0x42bd6c — VIBE_Particle_SpawnEffect (__userpurge, sticky directional emitter).
// Validates the owner node (a1+460 mesh, mesh+16 frame) then allocates a
// UpdateEmitter-driven system, places it at the origin, and copies the velocity
// triple from `velRec` plus the recovered duration/turn-rate fields into `hdr`.
// `meshTurnRate` is the value the original reads from the bound mesh's frame at
// [[a1+460]+16]+468 (the per-frame turn rate); callers pass it explicitly since
// the mesh-frame chain is the engine's. Returns the system (0 on any validation/
// alloc failure). `slotCount`/`now` thread the explicit AllocSystem params.
ParticleSystem* SpawnEffect(void* ownerNode, int tagA2, const u8* texName,
                            char triggerBit, const float velRec[3], int userTag,
                            float life, int matHandleA9, int colA10, int colA11,
                            int colA12, float meshTurnRate, EffectHeader& hdr,
                            int slotCount, u32 now);

// ---------------------------------------------------------------------------
// 0x42c728 — VIBE_Particle_SpawnBlood (__userpurge). Ballistic (UpdateGravity)
// system: sets hdr+0x20 flags = 1 | 2*startActive, stores four header dwords,
// clears every slot's active bit. Returns the system (0 on alloc failure).
ParticleSystem* SpawnBlood(int ownerA1, int hdr0, int hdr1, int hdr2, int hdr3,
                           int startActive, EffectHeader& hdr, int slotCount,
                           u32 now);

// ---------------------------------------------------------------------------
// 0x42c8e0 — VIBE_Particle_SpawnExplosion (__userpurge). Cosine-wave radial burst
// (UpdateCosineWave) — seeds each slot's velocity as a randomized ring around the
// emitter radius (hdr+4 = radius), with a sin()-shaped vertical profile and a
// per-sextant damping. Returns the system (0 on alloc failure). `radius` is the
// emitter radius the original reads from sys+4 (header w1); callers set it.
ParticleSystem* SpawnExplosion(int ownerA1, int hdr0, float radius, int hdr2,
                               float life, int hdr8, EffectHeader& hdr,
                               int slotCount, u32 now);

// ---------------------------------------------------------------------------
// 0x42cbc4 — VIBE_Particle_SpawnSmoke (__userpurge). Fade-out drifting smoke
// (UpdateFadeOut): copies the base position from `posRec` into the header and
// every slot, adds a random drift biased by `spread`, derives the per-slot fade
// (+0x1C/+0 reciprocal pair), seeds a random colour from `colTriple`, and stamps
// a randomized birth frame. `hdr+4` (= hdr4A6 as a float) is the fade pivot the
// slot loop divides by. Returns the system (0 on alloc failure).
ParticleSystem* SpawnSmoke(int ownerA1, const float setPos[3],
                           const float posRec[3], int texSlotA3, float life,
                           int hdr4A6, int matHandleA7, int hdr0A8, float spreadA9,
                           int colExtremaA11, int frameStepA12, int colTripleA13,
                           EffectHeader& hdr, int slotCount, u32 now);

// ---------------------------------------------------------------------------
// 0x42d188 — VIBE_Particle_SpawnDebris (__userpurge). Scatter (UpdateScatter)
// system: places it at `posRec`, sets hdr+0x20 flags=1, stores eight header
// dwords. Returns the system (0 on alloc failure).
ParticleSystem* SpawnDebris(int ownerA1, const float posRec[3], int texSlotA3,
                            float life, int h0, int h1, int h2, int h3, int h4,
                            int h5, int h6, int h9, EffectHeader& hdr,
                            int slotCount, u32 now);

// ---------------------------------------------------------------------------
// 0x42c3ac — VIBE_Particle_SpawnRefLens (__usercall). Single reflection-lens
// system: allocates a UpdateScatter-seeded ("Ref-Map-Linse") system, seeds its
// colours via InitColors with the recovered literals, and places it. Returns the
// system (0 on alloc failure). `tagA2` is the opaque lens tag (hdr+4).
ParticleSystem* SpawnRefLens(int ownerA1, int tagA2, int texSlotA3,
                             const float posRec[3], EffectHeader& hdr,
                             int slotCount, u32 now);

// ---------------------------------------------------------------------------
// 0x4d880c — VIBE_Particle_SpawnSmokeEffect (__usercall). Convenience wrapper:
// builds the recovered velocity record + colour/flag literals and tail-calls
// SpawnEffect with the "Rauch" texture. `owner` is the a1+97 owner read.
ParticleSystem* SpawnSmokeEffect(void* owner, int tagA2, float meshTurnRate,
                                 EffectHeader& hdr, int slotCount, u32 now);

// ---------------------------------------------------------------------------
// 0x43fa1c — VIBE_Particle_SpawnBloodEffect (__usercall). Blood-spray ctor:
// allocates a 100-slot UpdateRainStep system and fan-seeds each slot's velocity
// (Y from the fan index, X/Z randomized), then sets the per-slot fade byte.
// Returns the system. `ownerHolder` is the *a1 owner read.
ParticleSystem* SpawnBloodEffect(const int* ownerHolder, u32 now);

// ---------------------------------------------------------------------------
// 0x43f858 — VIBE_Particle_UpdateRainStep (__usercall al=f(edx:eax = {sys,now})).
// Per-frame integrator for the rain/blood systems: integrates each active slot's
// position by velocity, applies gravity to vy, respawns slots that fall below the
// ground plane (vy<0) with a fresh randomized velocity, and derives the per-slot
// fade byte (+0x4F). Returns the last fade byte (al residue). Operates on the raw
// system block + 84-byte slots.
u8 UpdateRainStep(void* sys, u32 now);

} // namespace guild::render
