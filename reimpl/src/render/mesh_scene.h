#pragma once
#include "guild/common/types.h"

#include <cstdint>

// =============================================================================
// guild::render — VIBE_Mesh_* geometry / memory-accounting leaves + a couple of
// pure AABB-math helpers, reconstructed 1:1 from gilde.exe.
//
// SCOPE (this file): the self-contained, deterministic, golden-testable corner of
// the mesh module that the rest of the reconstruction had not yet covered:
//
//   * The memory-size estimators (VIBE_Mesh_Compute*MemorySize family at 0x5f4d80
//     .. 0x5f5658). In the original these walk opaque object/surface/scene/bitmap
//     records by raw *(type*)(base + byteOffset) access — there were no named UDTs
//     in the IDB. To stay faithful AND testable we mirror that exactly: each
//     function takes a base byte-address (a small typed handle over a caller-owned
//     buffer) and reads fields with explicit little-endian accessors at the SAME
//     byte offsets the binary used. The arithmetic (the +524 node header, the 80*/
//     40*/24*/56* per-primitive byte costs, the 0.25*0.333.. mip-pyramid factor,
//     the >>3 round-up, the deliberate integer truncation) is verbatim.
//
//   * Two pure float AABB helpers: VIBE_Mesh_ComputeAabbExtents (0x42756c, the
//     per-triangle min/max overlap test) and VIBE_Mesh_AccumulateVertexAabb
//     (0x5b29d8, fold a vertex list into a 6-float min/max box).
//
// Cross-module leaves the originals reach (scene-graph walkers, the per-mesh anim
// estimator, the iterated global object pool / dirty-flag lists) are NOT owned by
// this module. They are routed through MeshMemoryHooks, an installable hook block
// with INERT DEFAULTS defined in mesh_scene.cpp, so the deterministic arithmetic
// above can be exercised in isolation without pulling in the sim/scene-graph.
// =============================================================================
namespace guild::render {

// ---------------------------------------------------------------------------
// Byte-addressed record handle. The originals treat object/surface/scene/bitmap
// records as a base address plus fixed byte offsets; this thin wrapper keeps that
// 1:1 while reading from a caller-owned little-endian buffer. A null base mirrors
// the binary's `if ( a1 )` guards (the address being 0 / NULL).
// ---------------------------------------------------------------------------
struct ByteRec {
    std::uint8_t* p = nullptr;

    ByteRec() = default;
    explicit ByteRec(std::uint8_t* base) : p(base) {}
    explicit ByteRec(void* base) : p(static_cast<std::uint8_t*>(base)) {}

    explicit operator bool() const { return p != nullptr; }

    // *(_DWORD *)(base + off)  /  signed view  /  byte views.
    std::uint32_t       u32(int off) const;
    std::int32_t        i32(int off) const;
    std::uint8_t        u8 (int off) const;
    std::int8_t         i8 (int off) const;
    std::uint16_t       u16(int off) const;
    void                setU32(int off, std::uint32_t v);
    void                setU8 (int off, std::uint8_t  v);

    // Pointer-bearing field. In the original this is *(_DWORD *)(base + off) holding
    // a 32-bit pointer; for a faithful-yet-64-bit-testable reconstruction we read a
    // native-width pointer from the same buffer offset (test buffers reserve a
    // pointer-sized slot there). A stored null mirrors the binary's `if (ptr)` guard.
    ByteRec  rec(int off) const;   // dereference a stored pointer field
    void     setRec(int off, ByteRec r);
};

// ---------------------------------------------------------------------------
// Recovered constants (gilde.exe).
//   flt_62C2E8 = 4.0f         (mip-pyramid 1/(1-1/4) numerator term -> *4)
//   flt_62C2EC = 0.33333334f  (0x3eaaaaab)
// ComputeObjectMemorySize forms  (w*w*v6) * 4.0f * 0.3333.. = w*w*v6 * 1.3333..,
// i.e. the geometric mip-chain area sum, then truncates toward zero.
// ---------------------------------------------------------------------------
inline constexpr float kMipAreaScaleA = 4.0f;        // flt_62C2E8
inline constexpr float kMipAreaScaleB = 0.33333334f; // flt_62C2EC

// VIBE_Coord_ConvertX (0x5c6b08): an FPU round-toward-zero (chop) of st(0) via a
// temporarily forced control word. For a value already produced as the result of a
// multiply/divide the net effect is truncation toward zero, identical to a C cast.
inline int CoordTruncate(double v) { return static_cast<int>(v); }

// ---------------------------------------------------------------------------
// Cross-module hooks (inert defaults in mesh_scene.cpp). These stand in for the
// scene-graph walkers and the iterated global pools the originals reach into.
// ---------------------------------------------------------------------------
struct MeshMemoryHooks {
    // VIBE_Anim_ComputeMeshMemorySize (0x5cfd24) — per-mesh animation byte cost.
    int (*computeMeshMemorySize)(ByteRec mesh) = nullptr;

    // Active object pool used by VIBE_Mesh_SumObjectPoolMemory / ClearDirtyFlags
    // (dword_1406A84 base, dword_1406A80 count, 128-byte stride).
    std::uint8_t* objectPoolBase  = nullptr;
    std::uint32_t objectPoolCount = 0;
};

MeshMemoryHooks&       MeshMemoryHooksMut();
const MeshMemoryHooks& Hooks();

// ---------------------------------------------------------------------------
// Memory-size estimators.
// ---------------------------------------------------------------------------

// 0x5f4f7c — VIBE_Mesh_ComputeBitmapMemorySize (__usercall, eax = a1@<eax>)
int ComputeBitmapMemorySize(ByteRec bitmap);

// 0x5f4e08 — VIBE_Mesh_ComputeNodeMemorySize (__usercall, eax = a1@<eax>)
int ComputeNodeMemorySize(ByteRec node);

// 0x5f4e8c — VIBE_Mesh_ComputeObjectMemorySize (__usercall, eax=a1, dl=a2)
int ComputeObjectMemorySize(ByteRec object, bool countLodCopies);

// 0x5f4fb8 — VIBE_Mesh_ComputeSurfaceMemorySize (__usercall, eax=a1, dl=a2)
int ComputeSurfaceMemorySize(ByteRec surface, bool clearDirty);

// 0x5f51e4 — VIBE_Mesh_ComputeSceneMemorySize (__usercall, eax=a1, dx=mask, ebx=out)
bool ComputeSceneMemorySize(ByteRec scene, std::uint16_t mask, int* outSize);

// 0x5f5588 — VIBE_Mesh_SumObjectPoolMemory (cdecl)
int SumObjectPoolMemory();

// 0x5f4d80 — VIBE_Mesh_ClearDirtyFlags (__usercall, eax = flags)
//   Only the object-pool branch (bit 0x100) is owned here; the two scene-list
//   branches reach module-private globals (dword_13FCCFC / dword_13FC760) that
//   live elsewhere and are guarded out via the hooks pool when not installed.
void ClearObjectPoolDirtyFlags(int flags);

// ---------------------------------------------------------------------------
// Pure float AABB math.
// ---------------------------------------------------------------------------

// 0x42756c — VIBE_Mesh_ComputeAabbExtents (__usercall, eax=tri, edx=lo, ebx=hi)
//   tri = 3 vertex pointers; lo/hi = 3-float min/max bounds. Returns whether the
//   triangle's own min/max extents overlap [lo, hi] on all three axes.
bool ComputeAabbExtents(float* const tri[3], const float* lo, const float* hi);

// NOTE: VIBE_Mesh_AccumulateVertexAabb (0x5b29d8) is already reconstructed in
// render/mesh_transform.cpp (guild::render::AccumulateVertexAabb(void*, float*),
// box layout {min0,min1,min2, _, max0,max1,max2}); it is reused here, not redefined.

} // namespace guild::render
