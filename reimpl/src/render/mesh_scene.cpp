#include "render/mesh_scene.h"

#include <cstring>

// =============================================================================
// guild::render — VIBE_Mesh_* memory-accounting + AABB-math leaves (gilde.exe).
// See mesh_scene.h for scope. Every function is a 1:1 translation of the Hex-Rays
// pseudocode at the cited address; raw *(type*)(base+off) accesses become ByteRec
// reads at the same byte offset, integer/float arithmetic is verbatim.
// =============================================================================
namespace guild::render {

// ---- ByteRec little-endian accessors (mirror native x86 unaligned access) ----
std::uint32_t ByteRec::u32(int off) const {
    std::uint32_t v;
    std::memcpy(&v, p + off, 4);
    return v;
}
std::int32_t ByteRec::i32(int off) const {
    std::int32_t v;
    std::memcpy(&v, p + off, 4);
    return v;
}
std::uint8_t  ByteRec::u8 (int off) const { return p[off]; }
std::int8_t   ByteRec::i8 (int off) const { return static_cast<std::int8_t>(p[off]); }
std::uint16_t ByteRec::u16(int off) const {
    std::uint16_t v;
    std::memcpy(&v, p + off, 2);
    return v;
}
void ByteRec::setU32(int off, std::uint32_t v) { std::memcpy(p + off, &v, 4); }
void ByteRec::setU8 (int off, std::uint8_t  v) { p[off] = v; }
ByteRec ByteRec::rec(int off) const {
    std::uint8_t* ptr;
    std::memcpy(&ptr, p + off, sizeof(ptr));
    return ByteRec(ptr);
}
void ByteRec::setRec(int off, ByteRec r) {
    std::uint8_t* ptr = r.p;
    std::memcpy(p + off, &ptr, sizeof(ptr));
}

// ---- hooks -----------------------------------------------------------------
MeshMemoryHooks& MeshMemoryHooksMut() {
    static MeshMemoryHooks g;
    return g;
}
const MeshMemoryHooks& Hooks() { return MeshMemoryHooksMut(); }

// ---------------------------------------------------------------------------
// 0x5f4f7c — VIBE_Mesh_ComputeBitmapMemorySize
//   v1 = 0;
//   if (a1) { v1 = 48; if (a1[10]) v1 += a1[8]*a1[8]; if (a1[9]) v1 += 24*a1[8]*a1[8]; }
//   (a1 is a _DWORD*, so a1[N] == *(u32*)(base + 4*N).)
// ---------------------------------------------------------------------------
int ComputeBitmapMemorySize(ByteRec bitmap) {
    int v1 = 0;
    if (bitmap) {
        v1 = 48;
        const std::int32_t dim = bitmap.i32(32); // a1[8]
        if (bitmap.u32(40))                       // a1[10]
            v1 = dim * dim + 48;
        if (bitmap.u32(36))                       // a1[9]
            v1 += 24 * dim * dim;
    }
    return v1;
}

// ---------------------------------------------------------------------------
// 0x5f4e08 — VIBE_Mesh_ComputeNodeMemorySize
//   if a1 && !(node[521]&1):
//     v2 = node+68; v3 = node[480]*node[484] + 524;
//     if (v2 > 0) v3 += 24*(v2+8);
//     v1 = 56*node[76] + v3; node[521] |= 1;
// ---------------------------------------------------------------------------
int ComputeNodeMemorySize(ByteRec node) {
    int v1 = 0;
    if (node && (node.u8(521) & 1) == 0) {
        const std::int32_t v2 = node.i32(68);
        int v3 = node.i32(480) * node.i32(484) + 524;
        if (v2 > 0)
            v3 = node.i32(480) * node.i32(484) + 524 + 24 * (v2 + 8);
        v1 = 56 * node.i32(76) + v3;
        node.setU8(521, static_cast<std::uint8_t>(node.u8(521) | 1));
    }
    return v1;
}

// ---------------------------------------------------------------------------
// 0x5f4e8c — VIBE_Mesh_ComputeObjectMemorySize
//   Walks an object record (128-byte pool slot). +104 is a signed flag byte: bit7
//   marks "already counted", bit1 = aliases another pool slot (+76 index into the
//   pool base dword_1406A84), bit3 selects raw vs mip-pyramid area cost. The mip
//   path multiplies the texel count by 4.0*0.3333.. (the geometric series sum) then
//   truncates. a2 (countLodCopies) optionally multiplies by the +112 LOD-copy count
//   when +113 is clear.
// ---------------------------------------------------------------------------
int ComputeObjectMemorySize(ByteRec object, bool countLodCopies) {
    int v3 = 0;
    if (object) {
        std::int8_t v4 = object.i8(104);
        if (v4 >= 0) {
            v3 = 128;
            if ((v4 & 2) != 0) {
                object.setU8(104, static_cast<std::uint8_t>(v4 | 0x80));
                // a1 = (a1[76] << 7) + dword_1406A84  -> alias to another pool slot.
                std::uint8_t* base = Hooks().objectPoolBase;
                object = ByteRec(base + (static_cast<std::uintptr_t>(object.u32(76)) << 7));
            }
            std::int8_t v5 = object.i8(104);
            if (v5 >= 0) {
                object.setU8(104, static_cast<std::uint8_t>(v5 | 0x80));
                if (object.u32(96)) {
                    int v6 = static_cast<int>(object.u8(124)) >> 3;
                    if ((object.u8(124) & 7) != 0)
                        ++v6;
                    int v10;
                    if ((object.u8(104) & 8) != 0) {
                        v10 = v6 * object.i32(116) * object.i32(116);
                    } else {
                        // double v7 = (double)(unsigned)(w*w*v6) * 4.0f * 0.3333..;
                        const std::uint32_t texels =
                            static_cast<std::uint32_t>(object.i32(116) * object.i32(116) * v6);
                        double v7 = static_cast<double>(texels) *
                                    kMipAreaScaleA * kMipAreaScaleB;
                        v10 = CoordTruncate(v7);
                    }
                    if (countLodCopies) {
                        std::uint8_t v8 = object.u8(112);
                        if (v8) {
                            if (!object.u8(113))
                                v10 *= v8;
                        }
                    }
                    v3 += v10;
                }
            }
        }
    }
    return v3;
}

// ---------------------------------------------------------------------------
// 0x5f4fb8 — VIBE_Mesh_ComputeSurfaceMemorySize
//   Surface record: +0 = dim (square texel side), +4 = a secondary dim. Counts a
//   7288-byte header, optional full-res planes (+16/+20/+24/+28), a 3-level mip
//   pyramid of 0x4000 chunks (+36 .. +48 in 32-byte rows), an embedded object pool
//   at +6624 (count u8 at +7277, 344-byte stride) and 8x8 sub-surface descriptors
//   (800-byte rows from +224, 25-dword descriptors from +1024).
// ---------------------------------------------------------------------------
int ComputeSurfaceMemorySize(ByteRec surface, bool clearDirty) {
    int v4 = 0;
    if (surface) {
        const int dim  = surface.i32(0);
        const int v5   = dim * dim;
        if (clearDirty)
            ClearObjectPoolDirtyFlags(20);
        v4 = 7288;
        if (surface.u32(16)) v4 = v5 + 7288;
        if (surface.u32(20)) v4 += v5;
        if (surface.u32(24)) v4 += v5;
        if (surface.u32(28)) v4 += v5;

        // 3 mip levels; each level scans 8 (32-byte) sub-rows of 0x4000 chunks.
        for (int v19 = 0; v19 < 3; ++v19) {
            // *(a1 + 36 + 4*v19) row-present flag (v6 = a1+4*v19, +36).
            if (surface.u32(36 + 4 * v19))
                v4 += (dim >> v19) * (dim >> v19);
            const int rowBase = 32 * v19; // a1 + 32*v19, scanning +48 .. +48+28
            for (int j = 0; j < 8; ++j) {
                if (surface.u32(rowBase + 48 + 4 * j))
                    v4 += 0x4000;
            }
        }

        if (surface.u32(6624)) {
            const std::uint8_t poolCount = surface.u8(7277);
            v4 += 344 * poolCount;
            int v10 = 0;
            for (int v9 = 0; v9 < poolCount; ++v9) {
                ByteRec slot = surface.rec(6624);
                slot = ByteRec(slot.p + v10);
                if (slot.rec(0))
                    v4 += ComputeObjectMemorySize(slot.rec(0), true);
                v10 += 344;
            }
        }

        // 8 outer x 8 inner sub-surface descriptors (25 dwords = 100 bytes each).
        for (int v18 = 0; v18 < 8; ++v18) {
            int descByte = 800 * v18 + 224;
            for (int inner = 0; inner < 8; ++inner) {
                ByteRec d(surface.p + descByte);
                const int v12 = surface.i32(4) >> (surface.u8(7281) & 0xF);
                if (d.u32(6 * 4))  v4 += 80 * (v12 + 2) * (v12 + 2);
                if (d.u32(10 * 4)) v4 += 40 * (v12 + 1) * (2 * v12 + 2);
                if (d.u32(12 * 4)) v4 += 48 * (v12 + 2);
                if (d.u32(15 * 4)) v4 += 24 * (static_cast<int>(d.u32(17 * 4)) +
                                               (2 * v12 + 2) * (v12 + 1));
                if (d.u32(7 * 4)) {
                    const std::int32_t v13 = d.i32(5 * 4);
                    if (v13 > 0) v4 += 80 * v13;
                }
                if (d.u32(11 * 4)) {
                    const std::int32_t v14 = d.i32(9 * 4);
                    if (v14 > 0) v4 += 40 * v14;
                }
                if (d.u32(13 * 4))
                    v4 += 20 * (surface.i32(4) * surface.i32(4) / 2);
                descByte += 25 * 4; // v11 += 25 (dwords)
            }
        }
    }
    return v4;
}

// ---------------------------------------------------------------------------
// 0x5f4d80 — VIBE_Mesh_ClearDirtyFlags (object-pool branch only; see header).
//   if !(flags & 0x100): for each of dword_1406A80 pool slots clear bit7 of +104
//   (the "counted" marker), pool base dword_1406A84, 128-byte stride.
// ---------------------------------------------------------------------------
void ClearObjectPoolDirtyFlags(int flags) {
    if ((flags & 0x100) == 0) {
        std::uint8_t* base = Hooks().objectPoolBase;
        const std::uint32_t count = Hooks().objectPoolCount;
        if (base) {
            std::uint8_t* p = base;
            for (std::uint32_t i = 0; i < count; ++i) {
                p[104] = static_cast<std::uint8_t>(p[104] & 0x7F);
                p += 128;
            }
        }
    }
}

// ---------------------------------------------------------------------------
// 0x5f5588 — VIBE_Mesh_SumObjectPoolMemory
//   ClearDirtyFlags(20); for each pool slot with +64 > 0 add ComputeObjectMemorySize(slot,0).
// ---------------------------------------------------------------------------
int SumObjectPoolMemory() {
    ClearObjectPoolDirtyFlags(20);
    std::uint8_t* base = Hooks().objectPoolBase;
    const std::uint32_t count = Hooks().objectPoolCount;
    int v1 = 0;
    if (base) {
        std::uint8_t* p = base;
        for (std::uint32_t v0 = 0; v0 < count; ++v0) {
            ByteRec slot(p);
            if (slot.i32(64) > 0)
                v1 += ComputeObjectMemorySize(slot, false);
            p += 128;
        }
    }
    return v1;
}

// ---------------------------------------------------------------------------
// 0x5f51e4 — VIBE_Mesh_ComputeSceneMemorySize
//   Scene record at a1; +492 -> sub-scene block with a u8 node count at +2316 and
//   384-byte entries from +0. mask (high word of the original packed arg) selects
//   which cost groups to accumulate into *outSize. Verbatim group costs below.
// ---------------------------------------------------------------------------
bool ComputeSceneMemorySize(ByteRec scene, std::uint16_t mask, int* outSize) {
    if (!scene || !outSize)
        return false;

    // VIBE_Mesh_ClearDirtyFlags(mask >> 16) in the original; the high-word mask we
    // receive already corresponds to that argument. Only the object-pool branch is
    // owned here (bit 0x100 of the byte arg ~ mask bit 0x1 of the high word path);
    // we forward the masked value so the pool clears identically.
    ClearObjectPoolDirtyFlags(static_cast<int>(mask));

    int& out = *outSize;
    out = 0;
    const std::uint32_t m = mask; // groups live in the low 16 bits we were handed.

    if (m & 0x1) { // 0x10000 in the original packed dword
        int v5 = out + 540;
        out = v5;
        if (scene.u32(488))
            out = v5 + 428;
    }

    ByteRec block = scene.rec(492);
    if (block) {
        if (m & 0x1) {
            out += 2320;
            const std::uint8_t nodeCount = block.u8(2316);
            int v7 = 0;
            for (std::uint8_t v6 = 0; v6 < nodeCount; ++v6) {
                ByteRec e(block.p + v7);
                if (e.u32(244)) {
                    const std::int32_t v10 = e.i32(252);
                    if (v10 > 0) out += 80 * (v10 + 8);
                }
                if (e.u32(248)) out += 40 * e.i32(256);
                if (e.rec(264)) {
                    ByteRec v13 = e.rec(260);
                    if (v13) out += 4 * v13.i32(480);
                }
                v7 += 384;
            }
            // tail block at +2312 / +2304.
            ByteRec tail = block.rec(2312);
            if (tail) {
                if (block.u32(2304)) {
                    const std::int32_t v15 = tail.i32(8);
                    if (v15 > 0) out += 80 * (v15 + 8);
                }
                out += 40 * block.rec(2312).i32(12);
            }
            out += 80 * block.i32(1404);
            out += 40 * block.i32(1408);
        }

        if (m & 0x2) { // 0x20000
            int v17 = 0;
            const std::uint8_t nodeCount = block.u8(2316);
            for (int i = 0; i < nodeCount; ++i) {
                ByteRec e(block.p + v17);
                if (e.rec(260))
                    out += ComputeNodeMemorySize(e.rec(260));
                v17 += 384;
            }
            if (block.rec(1412))
                out += ComputeNodeMemorySize(block.rec(1412));
        }

        if (m & 0x8) { // 0x80000
            int v34 = 0;
            const std::uint8_t nodeCount = block.u8(2316);
            for (int j = 0; j < nodeCount; ++j) {
                if (block.u8(v34 + 624)) {
                    for (int k = 0; k != 348; k += 116) {
                        ByteRec mesh = ByteRec(block.p + v34).rec(k + 376);
                        if (mesh) {
                            int v24 = 0;
                            if (!mesh.u8(362)) {
                                if (Hooks().computeMeshMemorySize)
                                    v24 = Hooks().computeMeshMemorySize(mesh);
                                mesh.setU8(362, 1);
                            }
                            out += v24;
                        }
                    }
                }
                v34 += 384;
            }
        }

        if (m & 0x80) { // 0x800000
            int v37 = 0;
            const std::uint8_t nodeCount = block.u8(2316);
            for (int mm = 0; mm < nodeCount; ++mm) {
                ByteRec e(block.p + v37);
                ByteRec v28 = e.rec(260);
                int v29 = 0;
                if (v28 && e.u32(264))
                    v29 = v28.i32(480);
                if (v29 > 0) {
                    // Original: v30 = 4*v29; for (v31=0; v31<v30; v31+=4) obj =
                    // *(_DWORD*)(v31 + *(_DWORD*)(scene+492-entry+264)). i.e. iterate
                    // an array of v29 object pointers (4-byte stride in 32-bit). We
                    // index by element using the native pointer stride.
                    ByteRec arr = e.rec(264);
                    for (int idx = 0; idx < v29; ++idx) {
                        ByteRec obj = arr.rec(idx * static_cast<int>(sizeof(std::uint8_t*)));
                        out += ComputeObjectMemorySize(obj, true);
                    }
                }
                v37 += 384;
            }
        }
    }

    if ((m & 0x20) && scene.rec(464)) { // 0x200000
        out += 60;
        ByteRec r = scene.rec(464);
        out += 88 * r.i32(0);   // *(_DWORD *)(*(_DWORD *)(a1 + 464))
    }
    if ((m & 0x40) && scene.rec(468)) // 0x400000
        out += 924;

    return true;
}

// ---------------------------------------------------------------------------
// 0x42756c — VIBE_Mesh_ComputeAabbExtents
//   tri = float*[3] (a triangle's 3 vertex pointers, each pointing at xyz). The
//   binary computes, per axis, the running min-of-mins and max-of-maxes across the
//   three vertices using a nested compare ladder, then tests overlap with [lo, hi].
//   Reproduced via the same min/max reductions; result is bit-identical.
// ---------------------------------------------------------------------------
bool ComputeAabbExtents(float* const tri[3], const float* lo, const float* hi) {
    // The original ladder is, per axis: m = min(min(v0,v1) cmp v2 ...). We mirror it
    // explicitly to preserve the exact comparison structure (double-promoted floats).
    auto axisMin = [](float v0, float v1, float v2) -> float {
        float m = (v0 < (double)v1) ? v0 : v1;
        if (m >= (double)v2) return v2;
        return (v0 >= (double)v1) ? v1 : v0;
    };
    auto axisMax = [](float v0, float v1, float v2) -> float {
        float m = (v0 <= (double)v1) ? v1 : v0;
        if (m <= (double)v2) return v2;
        return (v0 <= (double)v1) ? v1 : v0;
    };

    const float v0x = tri[0][0], v1x = tri[1][0], v2x = tri[2][0];
    const float v0y = tri[0][1], v1y = tri[1][1], v2y = tri[2][1];
    const float v0z = tri[0][2], v1z = tri[1][2], v2z = tri[2][2];

    const float minX = axisMin(v0x, v1x, v2x); // v32
    const float minY = axisMin(v0y, v1y, v2y); // v33
    const float minZ = axisMin(v0z, v1z, v2z); // v34
    const float maxX = axisMax(v0x, v1x, v2x); // v31
    const float maxZ = axisMax(v0z, v1z, v2z); // v29

    return maxX >= (double)lo[0]
        && minX <= (double)hi[0]
        && minY <= (double)hi[1]
        && maxZ >= (double)lo[2]
        && minZ <= (double)hi[2];
}

} // namespace guild::render
