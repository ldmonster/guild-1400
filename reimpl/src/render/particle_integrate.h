#pragma once
#include "guild/common/types.h"

// =============================================================================
// guild::render::pintegrate — the per-frame particle INTEGRATORS for the three
// d3_par particle SYSTEM TYPES (gilde.exe d3_engine particle cluster). These are
// the kernels VIBE_Particle_SpawnSystemByType (0x5e3ae0) installs into the system
// block's update slot (offset +0x304) according to the template byte:
//
//   type 0  -> 0x5e1e0c  VIBE_Particle_UpdatePoints  (point / sprite particles)
//   type 1  -> 0x5e2814  VIBE_Particle_UpdatePolys   (poly / ribbon particles)
//   type 2  -> 0x5e32c0  VIBE_Particle_UpdateLens    (lens-flare / glow particles)
//
// Each kernel walks the system's flat 84-byte (0x54) slot array (base = sys+40,
// count = sys+208), spawning dead slots and advancing live ones. The integrate
// COMPUTES the per-slot RENDER CENTER (slot+0x38/+0x3C/+0x40), the sprite SIZE
// (slot+0x48) and the output ALPHA byte (slot+0x4F) that VIBE_Particle_RenderSystem
// (0x5e1278, reconstructed in fx_recon3_particle_render.*) reads to build the
// billboard quads. i.e. this is the per-frame update that *feeds*
// render_system_to_surface.
//
// 1:1 FIDELITY (rule 1): float op-order, constants, RNG draw-order, and the x87
// round-toward-zero truncation (VIBE_Coord_ConvertX @0x5c6b08 -> (int)x) are
// byte-exact with the Hex-Rays decompile / disassembly (the reference of record).
// RNG is crt::RandNext (the 15-bit LCG, [0,32767]); jitter is scaled by 1/32767
// (flt_62BA94/B4/D4 = 3.0518509e-05).
//
// CALLEES (rule 7):
//   VIBE_Coord_ConvertX        0x5c6b08  fpu round-toward-zero  -> (int) trunc
//   VIBE_Math_Fmod             0x5d3fb2  fmodl (x87 fprem loop) -> std::fmod
//   VIBE_Math_VectorNormalize  0x5cb148  normalize 3-vec in place (zero if |v|==0)
//   VIBE_Util_RandNext         0x5cb8bc  crt::RandNext
//
// STRUCT NOTE: the emitter is the 0x310 system block. The three kernels read
// DIFFERENT (overlapping) field offsets out of it, so rather than a typed struct
// we model it as a raw byte block (`Emitter`) with explicit offset accessors — the
// exact offsets each kernel's asm dereferences. The slot is the renderer's 84-byte
// record (`Slot`, == fxrecon3::ParticleSlot image): +56/60/64 center, +72 size,
// +76..79 rgba, +80 frame, +81 flags.
// =============================================================================

namespace guild::render::pintegrate {

using guild::u8;
using guild::i8;
using guild::u32;
using guild::i32;
using guild::i64;

// ---------------------------------------------------------------------------
// Recovered float / double constants (get_bytes, bit-exact).
//   flt_62BA94 / flt_62BAB4 / flt_62BAD4 = 0x38000100 = 3.0518509e-05 (1/32767)
//   flt_62BA98 / flt_62BAB8 / flt_62BAD8 = 0x40000000 = 2.0
//   flt_62BA9C / flt_62BABC / flt_62BADC = 0x40490FDB = 3.14159265 (pi, float)
//   dbl_62BAA4 / dbl_62BAC4 / dbl_62BAE4 = 0x401921FB54442EEA = pi (double)
//   flt_62BAAC / flt_62BACC / flt_62BAEC = 0x437F0000 = 255.0
//   flt_62BAB0 (Points) / flt_62BAD0 (Polys) = 0xBF000000 = -0.5  (spawn bias)
//   flt_62BAF0 (Lens)                         = 0xBF800000 = -1.0  (spawn bias)
// ---------------------------------------------------------------------------
constexpr float  kRandNorm = 3.0518509447574615e-05f; // 1/32767
constexpr float  kTwo      = 2.0f;
constexpr float  kPiF      = 3.1415927410125732f;      // float pi
constexpr double kPiD      = 3.141592653589793;        // double pi
constexpr float  k255      = 255.0f;
constexpr float  kBiasHalfP= -0.5f;                    // Points spawn bias (62BAB0)
constexpr float  kBiasHalfQ= -0.5f;                    // Polys  spawn bias (62BAD0)
constexpr float  kBiasOne  = -1.0f;                    // Lens   spawn bias (62BAF0)

// x87 round-toward-zero truncate (VIBE_Coord_ConvertX @0x5c6b08): (int)x.
int TruncToward(double x);

// VIBE_Math_VectorNormalize @0x5cb148.
void VectorNormalize(float v[3]);

// VIBE_Math_Fmod @0x5d3fb2 — fmodl (x87 fprem); std::fmod is bit-identical here.
double Fmod(double a, double b);

// ---------------------------------------------------------------------------
// The 0x310 system block, accessed by raw byte offset (the kernels each read a
// different overlapping set). Helpers below are typed views over `raw`.
// ---------------------------------------------------------------------------
struct Emitter {
    u8 raw[0x310];

    float& f(int off)       { return *reinterpret_cast<float*>(raw + off); }
    float  f(int off) const { return *reinterpret_cast<const float*>(raw + off); }
    i32&   i(int off)       { return *reinterpret_cast<i32*>(raw + off); }
    i32    i(int off) const { return *reinterpret_cast<const i32*>(raw + off); }
    u32&   u(int off)       { return *reinterpret_cast<u32*>(raw + off); }
    u32    u(int off) const { return *reinterpret_cast<const u32*>(raw + off); }
    u8&    b(int off)       { return raw[off]; }
    u8     b(int off) const { return raw[off]; }

    // Frequently-used named views (offsets straight from the asm).
    i32&  killedCount() { return i(0x20); }   // a1+32 — killed-this-frame counter
    u32&  lastTick()    { return u(0x24); }   // a1+36 — short-circuit tick
    i32   slotCount() const { return i(0xD0); } // a1+208
    // a1+212 group block pointer. In the 32-bit original this is a 4-byte pointer;
    // on the 64-bit host we store a native pointer (the 0x310 block has room).
    // [groupPtr + 112] holds the frame divisor.
    const void* groupPtr() const {
        const void* p; __builtin_memcpy(&p, raw + 0xD4, sizeof(p)); return p;
    }
    void setGroupPtr(const void* p) { __builtin_memcpy(raw + 0xD4, &p, sizeof(p)); }
    i32   spanA()     const { return i(0xBC); } // a1+188
    i32   spanB()     const { return i(0xC0); } // a1+192
    i32   spanC()     const { return i(0xC4); } // a1+196
    u8    flagsLow()  const { return b(0xCC); } // a1+204
    u8    flagsHi()   const { return b(0xCD); } // a1+205
    u8    colBaseB()  const { return b(0xC8); } // a1+200
    u8    colBaseG()  const { return b(0xC9); } // a1+201
    u8    colBaseR()  const { return b(0xCA); } // a1+202
    u8    colMask()   const { return b(0xCB); } // a1+203
    float life255()   const { return f(0xAC); } // a1+172  (scaled-count factor)
};

// The renderer-facing slot — 84-byte image shared with fx_recon3::ParticleSlot.
#pragma pack(push, 1)
struct Slot {
    float vx;        // +0x00 velocity x
    float vy;        // +0x04
    float vz;        // +0x08
    float phase;     // +0x0C fmod'd phase scalar
    float ang0;      // +0x10 phase-angle accumulator 0
    float ang1;      // +0x14
    float ang2;      // +0x18
    float ang3;      // +0x1C
    float accX;      // +0x20 position accumulator x
    float accY;      // +0x24
    float accZ;      // +0x28
    float bright;    // +0x2C (unused by these three; renderer scratch)
    u8    _30[8];    // +0x30 lastTick (u32) , +0x34 birthTick (u32)
    float cx;        // +0x38 (56) render center x
    float cy;        // +0x3C (60) render center y
    float cz;        // +0x40 (64) render center z
    float alphaSrc;  // +0x44 (68) alpha source
    float size;      // +0x48 (72) sprite size
    u8    colB;      // +0x4C (76)
    u8    colG;      // +0x4D (77)
    u8    colR;      // +0x4E (78)
    u8    alpha;     // +0x4F (79) output alpha byte (renderer reads this)
    u8    frame;     // +0x50 (80) frame index
    u8    flags;     // +0x51 (81) bit0 = active
    u8    _52[2];    // +0x52..+0x53 padding to 84

    u32&  lastTick()  { return *reinterpret_cast<u32*>(_30 + 0); }   // +0x30
    u32&  birthTick() { return *reinterpret_cast<u32*>(_30 + 4); }   // +0x34
};
#pragma pack(pop)
static_assert(sizeof(Slot) == 84, "Slot must be the 84-byte particle record");

// ---------------------------------------------------------------------------
// The three integrators. `now` is the current tick (the @<edx> argument).
// Return value mirrors the original al/return ("still has live particles").
// ---------------------------------------------------------------------------
bool UpdatePoints(Emitter& e, Slot* slots, u32 now);  // 0x5e1e0c
bool UpdatePolys (Emitter& e, Slot* slots, u32 now);  // 0x5e2814
bool UpdateLens  (Emitter& e, Slot* slots, u32 now);  // 0x5e32c0

// ---------------------------------------------------------------------------
// System type byte (the VIBE_Particle_SpawnSystemByType template[0] selector,
// 0x5e3ae0): 0 -> Points, 1 -> Polys, 2 -> Lens. The render loop already holds a
// system block (sys+0) and its slot array (sys+40); UpdateSystem dispatches the
// matching integrator before the system is rendered (the integrate computes the
// +56/60/64 center / +79 alpha that VIBE_Particle_RenderSystem reads). This is
// the live-callable seam for the orchestrator (see progress doc handoff).
// ---------------------------------------------------------------------------
enum class SystemType : u8 { Points = 0, Polys = 1, Lens = 2 };

inline bool UpdateSystem(SystemType type, Emitter& e, Slot* slots, u32 now) {
    switch (type) {
        case SystemType::Polys: return UpdatePolys(e, slots, now);
        case SystemType::Lens:  return UpdateLens(e, slots, now);
        case SystemType::Points:
        default:                return UpdatePoints(e, slots, now);
    }
}

} // namespace guild::render::pintegrate
