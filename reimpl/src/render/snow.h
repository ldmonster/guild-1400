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
    i32 count = 0;       // dword[0]   (*(int*)a1)
    i32 capacity = 0;    // dword[1]
    // Per-system wind velocity, read by VIBE_Snow_UpdateFlake @0x42a644:
    //   a1+68 = sysVelX (dword[17]), a1+72 = sysVelZ (dword[18]).
    // (These alias dirX/dirZ that VIBE_Snow_Render @0x42b5b0 interpolates.)
    float sysVelX = 0.0f; // a1+68
    float sysVelZ = 0.0f; // a1+72
    SnowFlake* flakes = nullptr; // dword[14] (a1+56)
    // Frame-to-frame camera snapshots the update integrates against (stateful):
    //   a1+112/116/120 = previous anchor; a1+128/132/136 = previous eye.
    // UpdateFlake reads (prev - current) deltas, then overwrites with current.
    float prevAnchor[3] = {0.0f, 0.0f, 0.0f}; // a1+112,+116,+120
    float prevEye[3]    = {0.0f, 0.0f, 0.0f}; // a1+128,+132,+136
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
    // view rotation. The kernel reads twelve floats at +396..+436 (a 4x3 block,
    // skipping +408/+424/+440). We pack the nine USED entries row-major:
    //   m[0]=+396 m[1]=+400 m[2]=+404
    //   m[3]=+412 m[4]=+416 m[5]=+420
    //   m[6]=+428 m[7]=+432 m[8]=+436
    // A "column" of the multiply is col0=(m[0],m[3],m[6]) = (+396,+412,+428),
    // col1=(m[1],m[4],m[7]) = (+400,+416,+432), col2=(m[2],m[5],m[8]) = (+404,+420,+436).
    float m[9];
};

struct SnowViewport {
    i32 x0, y0, x1, y1; // dword_13ECE58, _5C, _60, _64
};

// ---------------------------------------------------------------------------
// Snow system time-interpolation header (gilde.exe 0x42a014 Snow_Create layout).
// These are the dword slots VIBE_Snow_Render @0x42b5b0 reads/writes per frame to
// blend the snow direction + a "count" ramp over a time window. They are kept on
// the same SnowSystem so the faithful renderer (SnowRender) can drive them; the
// simplified SnowUpdateFlake path (used by the headless atmos bridge) ignores
// them and remains byte-stable. dword indices (×4 = byte offset):
//   [0..3] count fields (all = requested flake count; [0] is the live loop bound)
//   [17]=dirX  [18]=dirZ      current interpolated direction (a1+68 / a1+72)
//   [19]=tgtX  [20]=tgtZ      target direction              (a1+76 / a1+80)
//   [15]=oldX  [16]=oldZ      previous direction snapshot   (a1+60 / a1+64)
//   [23] lastUpdateMs         (a1+92)  — drives dt = (now-last)*0.1
//   [24]=cBeg [25]=cEnd       count-ramp time window        (a1+96 / a1+100)
//   [26]=dBeg [27]=dEnd       direction-ramp time window     (a1+104 / a1+108)
//   [2]=cFrom [3]=cTo         count-ramp endpoints           (a1+8 / a1+12)
// All times are the engine ms clock dword_62EB38, supplied to SnowRender as `now`.
// ---------------------------------------------------------------------------
struct SnowSystemHdr {
    i32 count = 0;     // [0]  live flake count (loop bound)
    i32 capacity = 0;  // [1]
    i32 cFrom = 0;     // [2]
    i32 cTo = 0;       // [3]
    float oldX = 1.0f; // [15]
    float oldZ = 0.0f; // [16]
    float dirX = 1.0f; // [17]
    float dirZ = 0.0f; // [18]
    float tgtX = 1.0f; // [19]
    float tgtZ = 0.0f; // [20]
    i32 lastUpdateMs = 0; // [23]
    i32 cBeg = 0;      // [24]
    i32 cEnd = 0;      // [25]
    i32 dBeg = 0;      // [26]
    i32 dEnd = 0;      // [27]
    SnowFlake* flakes = nullptr; // [14]
};

// One snow vertex as VIBE_Snow_Render builds it: a D3D TLVERTEX, 8 dwords / 32
// bytes (the renderer indexes the temp buffer with `shl eax,5` = ×32). The FVF is
// D3DFVF_TLVERTEX (0x1C4); the original passes 28 as the DrawPrimitive stride arg
// but steps the buffer by 32 — we reproduce the 32-byte record bit-for-bit.
//   [0] sx  (float)  screen x       [1] sy (float)  screen y
//   [2] sz  (float)  depth (= (1-pz)*0.025)
//   [3] rhw (0x3F800000 = 1.0)      [4] diffuse (D3DCOLOR 0x50646464)
//   [5] specular (0)                [6] tu (float)  [7] tv (float)
struct SnowVertex {
    float x;        // +0x00
    float y;        // +0x04
    float z;        // +0x08
    u32   rhw;      // +0x0C  (1.0f bits)
    u32   color;    // +0x10  (diffuse)
    u32   specular; // +0x14  (0)
    float u;        // +0x18
    float v;        // +0x1C
};
static_assert(sizeof(SnowVertex) == 32, "SnowVertex (TLVERTEX) stride must be 32 bytes");

// gilde.exe 0x42b5b0 — VIBE_Snow_Render header interpolation (the math the renderer
// runs before emitting quads). Updates the system's count ramp ([0]) and direction
// ([17]/[18]) from the two time windows, given the engine clock `now` (dword_62EB38),
// then returns the per-frame integration dt = (now - lastUpdate) * 0.1 (flt_611990)
// and advances lastUpdate := now. This is the exact 1:1 of 0x42b5c9..0x42b648.
float SnowRenderStepHeader(SnowSystemHdr& h, i32 now);

// gilde.exe 0x42b5b0 inner loop (0x42b689..0x42b7c8) — build the snow quad vertex
// stream. For each flake whose projected screen segment (sx,sy)->(sx2,sy2) lies
// inside the viewport rect (dword_13ECE58/5C/60/64), emit THREE vertices forming
// the flake's triangle (the original's degenerate quad-as-triangle). Writes into
// `out` (caller-sized) and returns the number of vertices written; `cap` caps the
// output. The depth/v-coord = (1 - pz) * 0.025 (flt_611994); the x of the first
// vertex = (sx + sx2) * 0.5 (flt_611998). The diffuse + uv bit patterns are the
// originals (kSnowColor / 0.5 / 0 / 1.0). Pure + testable: no DDraw.
int SnowBuildQuads(const SnowSystem& sys, const SnowViewport& vp,
                   SnowVertex* out, int cap);

// gilde.exe 0x58339c — VIBE_GameTime_GetSeasonFromDay: season = day % 4. Snow/rain
// weather is only created when season == 3 (winter): see VIBE_Sky_InitScene
// @0x4b1e94 (`if (season == 3)`). Helper for the seasonal render gate.
inline int SnowSeasonFromDay(int day) { return day % 4; }
inline bool SnowIsWinterDay(int day) { return SnowSeasonFromDay(day) == 3; }

// ---------------------------------------------------------------------------
// gilde.exe 0x42a2cc — VIBE_Snow_UpdateScene. Despite the name this is the snow
// TEARDOWN / floor-texture restore run by VIBE_Sky_InitScene @0x4b1e94 when the
// season leaves winter (weather mode 0) while a snow system still exists. It:
//   1. releases the snow system's texture entry        (VIBE_Texture_ReleaseEntry)
//   2. frees the flake list                            ([+56], FreeDebug)
//   3. walks the global texture table (dword_1406A84, stride 128, count
//      dword_1406A80): for each "DC_"-prefixed (byte_6117C8) opaque texture, sets
//      its transparency flag to (!winterFlag && level>8) and clears [+88]
//   4. resets the texture cache, reloads the floor textures for the 64 active
//      universe slots, then frees the system block.
// winterFlag is dword_140809C (snow/winter texture flag set by the texture loader,
// VIBE_Texture_LoadByName @0x5da714 / CreateRecord @0x5db724). The per-texture and
// floor-reload steps depend on the engine texture table + universe slots, which are
// owned by other modules; this entry reconstructs the PURE, testable decision the
// original makes per texture (the transparency-flag rule), and documents the rest
// as inert hooks (rule 8). `winterFlag` is dword_140809C; `texLevel` is *(u8*)(v4+125).
// Returns the transparency flag the original would assign for one texture entry.
bool SnowResetSceneTexTransparency(int winterFlag, int texLevel);

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
