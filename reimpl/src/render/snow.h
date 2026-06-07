#pragma once
#include "guild/common/types.h"

// =============================================================================
// guild::render — snow weather particle system (gilde.exe, d3_sky.c / weather).
//
//   0x42a014  VIBE_Snow_Create        (alloc system + seed flakes via RandNext)
//   0x42a644  VIBE_Snow_UpdateFlake   (per-frame integrate + wrap + project)
//   0x42ab78  VIBE_Snow_AccumulateOnTexture (mask-thresholded snow blit to tex)
//
// A snow system is a flat array of 40-byte (10-float) flake records updated
// every frame. The flake's normalized position drifts in a unit cube [-1,1]^3
// (wrapping at the faces) and is projected onto the viewport using the active
// camera basis; gravity + wind are transformed into camera space first.
//
// RNG FIDELITY: Create seeds each flake with six crt::RandNext() draws, in the
// exact order px,py,pz,size,vel4,vel5. RandNext() is the 15-bit ANSI LCG (range
// [0,32767]); the seed value is scaled by 1/32767 (flt_6117AC). A fixed RNG seed
// therefore reproduces the whole flake field byte-for-byte. Truncation toward
// zero uses render::TruncToward (VIBE_Coord_ConvertX @0x5c6b08).
//
// CAMERA COUPLING: the update reads the active camera/world block (dword_13FCD1C
// @0x13FCD1C). Only the fields it touches are modelled in SnowCamera; original
// 32-bit byte offsets are in the comments. The viewport rect comes from
// dword_13ECE58/5C/60/64. These are passed in explicitly so the kernel is
// testable in isolation.
// =============================================================================
namespace guild::render {

// ---------------------------------------------------------------------------
// Flake record — 40-byte (0x28) stride = 10 floats. Recovered from the
// `v14 += 10` loop in VIBE_Snow_UpdateFlake and the seed loop in Snow_Create.
//   +0x00 [0]  px   normalized position x, wrapped to [-1,1)
//   +0x04 [1]  py   normalized position y, wrapped to [-1,1)
//   +0x08 [2]  pz   normalized position z, wrapped to [-1,1) (own constants)
//   +0x0C [3]  size flake size / parallax factor
//   +0x10 [4]  d0   per-flake direction weight 0  (vel.x basis weight)
//   +0x14 [5]  d1   per-flake direction weight 1  (vel.z basis weight)
//   +0x18 [6]  sx   projected screen x (output)
//   +0x1C [7]  sy   projected screen y (output)
//   +0x20 [8]  sx2  projected screen x of the flake's tail end (output)
//   +0x24 [9]  sy2  projected screen y of the flake's tail end (output)
// ---------------------------------------------------------------------------
struct SnowFlake {
    float px;   // +0x00
    float py;   // +0x04
    float pz;   // +0x08
    float size; // +0x0C
    float d0;   // +0x10
    float d1;   // +0x14
    float sx;   // +0x18
    float sy;   // +0x1C
    float sx2;  // +0x20
    float sy2;  // +0x24
};
static_assert(sizeof(SnowFlake) == 40, "SnowFlake stride must be 40 bytes");

// ---------------------------------------------------------------------------
// Snow system header (gilde.exe alloc 0x94 = 148 bytes). Only the fields the
// update + create kernels touch are modelled. dword indices from Snow_Create:
//   [0]  count        active flake count (loop bound in UpdateFlake, *(int*)a1)
//   [1]  capacity     allocated slots ( >=1 )
//   [4]  =5           (fixed init constant)
//   [14] flakes       SnowFlake* base
//   [36] texture      texture handle (Schneeflocke) — not needed for the math
// ---------------------------------------------------------------------------
struct SnowSystem {
    i32 count = 0;       // dword[0]
    i32 capacity = 0;    // dword[1]
    SnowFlake* flakes = nullptr; // dword[14]
};

// ---------------------------------------------------------------------------
// Active camera/world block view (dword_13FCD1C). Byte offsets are the
// ORIGINAL ones the kernel dereferences.
//   +76/+80/+84   eyePos x,y,z        (float)  — *(v3+76..)
//   +132/136/140  anchor x,y,z        (float)  — *(v3+132..)  (flake +112..)
//   +396..+436    3x3 view rotation, ROW-MAJOR as the kernel multiplies:
//                 col0 = (+396,+400,+404), col1=(+412,+416,+420),
//                 col2=(+428,+432,+436)
// The kernel computes  basis * vector  using exactly these 9 floats.
// ---------------------------------------------------------------------------
struct SnowCamera {
    float eye[3];      // +76,+80,+84
    float anchor[3];   // +132,+136,+140
    // view rotation, indexed [r][c]; the multiply reads m[c][r] groupings.
    // m[0..2] = +396,+400,+404 ; m[3..5] = +412,+416,+420 ; m[6..8]=+428,+432,+436
    float m[9];
};

struct SnowViewport {
    i32 x0, y0, x1, y1; // dword_13ECE58, _5C, _60, _64
};

// gilde.exe 0x42a014 — VIBE_Snow_Create (seed path only). Fills `capacity`
// flake slots with RandNext()-jittered positions/velocities in the exact draw
// order of the original. `count`/`capacity` must be set on `sys` beforehand
// (the original copies them from the caller's request). Allocation, the camera
// snapshot, and texture load are out of scope (the report lists them).
void SnowSeedFlakes(SnowSystem& sys);

// gilde.exe 0x42a644 — VIBE_Snow_UpdateFlake (__userpurge: a1@eax = system,
// a2 = frame dt). Integrates every active flake's normalized position, wraps it
// into the unit cube, then projects to screen coordinates using `cam`/`vp`.
void SnowUpdateFlake(SnowSystem& sys, float dt, const SnowCamera& cam,
                     const SnowViewport& vp);

// ---------------------------------------------------------------------------
// gilde.exe 0x42ab78 — VIBE_Snow_AccumulateOnTexture core (per-bpp masked copy).
// The original locks a DDraw surface, picks a mip level by log2(srcDim/scale),
// and copies the snow texture's pixels into the destination where the mask byte
// (snow alpha) is below `threshold`. The DDraw lock/copy/unlock is out of scope;
// the recoverable, testable kernel is the masked per-pixel copy. `bpp` selects
// the element width (8/16/32 bits). `mask`/`src`/`dst` are tile-major rows of
// `dim` x `dim`; for each pixel, dst = src iff mask < threshold.
// ---------------------------------------------------------------------------
void SnowAccumulateMaskedCopy8(u8* dst, const u8* src, const u8* mask,
                               int dim, int threshold);
void SnowAccumulateMaskedCopy16(u16* dst, const u16* src, const u8* mask,
                                int dim, int threshold);
void SnowAccumulateMaskedCopy32(u32* dst, const u32* src, const u8* mask,
                                int dim, int threshold);

// log2-style mip level the original computes: i = floor(log2(srcDim/scale))
// counting down while (srcDim/scale) > 1. Returns the level index.
int SnowMipLevel(unsigned srcDim, unsigned scale);

} // namespace guild::render
