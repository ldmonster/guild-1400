#pragma once
#include "guild/common/types.h"

// =============================================================================
// guild::render — particle EMITTER update suite (gilde.exe d3_engine.c / particle
// cluster). Faithful 1:1 reconstruction of the per-frame emitter update kernels
// that advance a flat array of fixed-size particle records using crt::RandNext
// for spawn jitter:
//
//   0x42b930  VIBE_Particle_UpdateEmitter     (sticky directional emitter)
//   0x42bec0  VIBE_Particle_SeedParticles     (initial fill + gravity bounce)
//   0x42c140  VIBE_Particle_UpdateTrail       (respawn-into-trail emitter)
//   0x42c42c  VIBE_Particle_UpdateGravity     (ballistic w/ partial re-emit)
//   0x42c7c8  VIBE_Particle_UpdateCosineWave  (cosine-windowed pulse)
//   0x42cadc  VIBE_Particle_UpdateFadeOut     (linear position + alpha fade)
//   0x42cde8  VIBE_Particle_UpdateScatter     (radial scatter + ground bounce)
//
// RNG FIDELITY: every spawn jitter is `crt::RandNext()` (the 15-bit ANSI LCG,
// range [0,32767]) scaled by 1/32767 (== flt_6119A8 = 3.0518509e-05). The call
// ORDER below is byte-exact with the originals, so a fixed RNG seed reproduces
// the full trajectory. Truncation-to-byte uses the x87 round-toward-zero path
// (VIBE_Coord_ConvertX @0x5c6b08); here that is (int)x toward zero.
//
// STRUCT LAYOUTS recovered from the raw *(type*)(base+off) access (no UDTs in
// the IDB). Offsets are the ORIGINAL 32-bit byte offsets. Emitter is indexed as
// _DWORD* in some originals (a1[8] == byte +0x20, etc.); Particle stride is the
// hard-coded 0x54 = 84 bytes (`add edx, 54h` in the loops).
// =============================================================================
namespace guild::render {

// ---------------------------------------------------------------------------
// Particle record — 84-byte (0x54) stride. Fields recovered from the update
// kernels' disassembly.
//   +0x00/+04/+08  velocity x,y,z          (float)
//   +0x10          life / size accumulator (float; 437F0000h = 255.0 at spawn)
//   +0x16/+18/+1A  rgb (b,g,r) bytes        — written by scatter/trail
//   +0x30          (emitter clip dword, scatter writes +0x30 = edi)
//   +0x38/+3C/+40  position x,y,z          (float)
//   +0x48          birth/seed time scalar  (float)
//   +0x4C          texture/material handle (dword)
//   +0x4C..        (color bytes live at +0x4C/4D/4E in spawn paths)
//   +0x4F          shade/alpha output byte (the rasterizer-facing value)
//   +0x50          frame index byte
//   +0x51          alive flags byte (bit0 = active)
// (NB: a few kernels alias +0x10/+0x14/+0x18 as a secondary accel triple and
//  +0x38/+3C/+40 as the live position; see per-function comments.)
// ---------------------------------------------------------------------------
struct Particle {
    float vx;      // +0x00  velocity x
    float vy;      // +0x04  velocity y
    float vz;      // +0x08  velocity z
    float _0c;     // +0x0C
    float life;    // +0x10  life accumulator / size (255.0 at spawn)
    float a1;      // +0x14  secondary accel y (gravity path uses +16/+20/+24)
    float a2;      // +0x18  secondary accel z
    float _1c;     // +0x1C  per-particle fade threshold (UpdateFadeOut +28dec)
    u8    _20[16]; // +0x20 .. +0x2F
    u32   birthTime; // +0x30 (decimal +48)  last-update / birth tick
    u8    _34[4];  // +0x34
    float px;      // +0x38 (decimal +56)  position x  (scratch in gravity path)
    float py;      // +0x3C (decimal +60)  position y
    float pz;      // +0x40 (decimal +64)  position z
    u8    _44[4];  // +0x44
    float seed;    // +0x48 (decimal +72)  per-particle phase / spawn lifetime
    // +0x4C..4E doubles as: a dword material handle (UpdateEmitter spawn) OR a
    // 3-byte colour triple (scatter/trail spawn). Modelled as raw bytes; the
    // handle write uses the matHandle() helper, colours use colB/colG/colR.
    u8    colB;    // +0x4C (decimal +76)
    u8    colG;    // +0x4D (decimal +77)
    u8    colR;    // +0x4E (decimal +78)
    u8    shade;   // +0x4F (decimal +79)  shade/alpha output byte
    u8    frame;   // +0x50 (decimal +80)  frame index
    u8    flags;   // +0x51 (decimal +81)  bit0 = active

    u32 matHandle() const { u32 v; __builtin_memcpy(&v, &colB, 4); return v; }
    void setMatHandle(u32 h) { __builtin_memcpy(&colB, &h, 4); }
};

// ---------------------------------------------------------------------------
// Emitter record — the per-system header. Only the fields the update kernels
// touch are modelled; original byte offsets in comments. The original indexed
// it both as a byte base (e.g. *(float*)(a1+12)) and as _DWORD* (a1[8]==+0x20).
//   +0x00/+04/+08  base spawn velocity x,y,z      (float)
//   +0x0C          velocity scale / speed cap     (float)
//   +0x10          max speed / start speed        (float)
//   +0x14          min speed threshold            (float)
//   +0x18          over-speed damping factor      (float)
//   +0x1C          turn rate (sticky dir)         (float)
//   +0x20          flags byte  (bit0 init, bit1=done)
//   +0x24          max alive count                (u32)
//   +0x28          particle array base ptr        (Particle*)
//   +0xC8 (200)    spawn material handle          (u32) -> particle +0x4C
//   +0xD0 (208)    particle slot count            (i32)
//   +0xE4 (228)    spawn lifetime base            (float) -> particle +0x48
//   +0xD4 (212)    scatter palette block ptr (UpdateScatter +0xD4)
// The decompiler reads gravity-path fields as a1[10]=+0x28 array ptr,
// a1[52]=+0xD0 count, a1[8]=+0x20 flags — consistent with the above.
// ---------------------------------------------------------------------------
struct Emitter {
    float baseVx;   // +0x00
    float baseVy;   // +0x04
    float baseVz;   // +0x08
    float velScale; // +0x0C
    float maxSpeed; // +0x10
    float minSpeed; // +0x14
    float damping;  // +0x18
    float turnRate; // +0x1C
    // +0x20 is overloaded by emitter type: the flag-driven kernels (UpdateEmitter,
    // UpdateGravity, UpdateScatter) read it as a flags byte (bit0=init,
    // bit1=exhausted); the window kernels (UpdateCosineWave, UpdateFadeOut) read
    // the same dword as the lifetime "duration". Modelled as a u32 so both views
    // coexist; flags() exposes the low byte.
    u32   hdr20;    // +0x20  flags byte / duration dword
    u32   maxAlive; // +0x24
    Particle* particles; // +0x28  array base
    u32   material; // +0xC8  spawn material handle (-> particle seed/handle)
    i32   count;    // +0xD0  number of particle slots
    float lifeBase; // +0xE4  spawn lifetime base
    const u8* scatterPalette; // +0xD4 (UpdateScatter only; may be null)
};

// ---------------------------------------------------------------------------
// Recovered float constants (verified via get_bytes).
//   flt_6119A8 = 1/32767 (RNG normalizer, == 3.0518509e-05).
// ---------------------------------------------------------------------------
constexpr float kRandNorm = 3.0518509447574615e-05f; // flt_6119A8/C8/A2C/AF0 = 1/32767

// x87 round-toward-zero truncate (VIBE_Coord_ConvertX @0x5c6b08).
// Returns (int)x truncated toward zero, matching `frndint` under the chop
// rounding mode the original set (HIBYTE(cw)=31 -> RC=11 = toward zero).
int TruncToward(double x);

// VIBE_Math_VectorNormalize @0x5cb148 — normalize a 3-vector in place; if the
// length's low 31 bits are zero (|v|==0) the vector is zeroed.
void VectorNormalize(float v[3]);

// The 7 named emitter update kernels. `now` is the current tick (the @<edx>
// argument). Each returns the original's al/return semantics: generally
// "still has live particles" / "keep running".
char UpdateEmitter(Emitter& e, u32 now);       // 0x42b930
char SeedParticles(Emitter& e, u32 now);        // 0x42bec0
char UpdateTrail(Emitter& e, u32 now);          // 0x42c140
char UpdateGravity(Emitter& e, u32 now);        // 0x42c42c
bool UpdateCosineWave(Emitter& e, u32 now);     // 0x42c7c8
bool UpdateFadeOut(Emitter& e, u32 now);        // 0x42cadc
bool UpdateScatter(Emitter& e, u32 now);        // 0x42cde8

} // namespace guild::render
