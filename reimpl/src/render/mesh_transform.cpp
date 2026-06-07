#include "render/mesh_transform.h"

#include <cmath>
#include <cstdint>

// Faithful 1:1 reconstruction of the gilde.exe mesh-geometry cluster. The
// originals read/write opaque object/mesh/vertex blocks by raw byte offset; we do
// the same through small typed accessors so the arithmetic stays verbatim.
namespace guild::render {
namespace {

// Byte-offset accessors mirroring the original *(type*)(base + off) loads.
inline u8*  BytePtr(void* p)                 { return reinterpret_cast<u8*>(p); }
inline u8*  AtPtr(void* p, std::size_t off)  { return BytePtr(p) + off; }
inline std::int32_t&  I32(void* p, std::size_t off) { return *reinterpret_cast<std::int32_t*>(AtPtr(p, off)); }
inline u8&            U8(void* p, std::size_t off)  { return *AtPtr(p, off); }
// Read a stored pointer (the original held 32-bit pointers; here native void*).
inline void*  PtrAt(void* p, std::size_t off) { return *reinterpret_cast<void**>(AtPtr(p, off)); }

// The matrix pivot the transforms feed ComputeBoneWorldMatrix: worldPivot when the
// object's +528 sign byte is clear (>= 0), else null (== the original's
//   v3 = (obj+528 >= 0) ? dword_13FCD1C : 0 ).
inline const float* PivotFor(void* obj, const float* worldPivot)
{
    return (static_cast<std::int8_t>(U8(obj, 528)) >= 0) ? worldPivot : nullptr;
}

} // namespace

// gilde.exe 0x5c9d04 — VIBE_Mesh_TransformPackedVertices
void* TransformPackedVertices(void* obj, void* mesh, const u8* packedSrc,
                              const float* worldPivot)
{
    if (!packedSrc) {
        // (*(int(**)(void))(mesh[4] + 492))() — the mesh's +4 sub-block clear thunk.
        void* sub = PtrAt(mesh, 4);
        using Thunk = void* (*)();
        Thunk thunk = *reinterpret_cast<Thunk*>(AtPtr(sub, 492));
        return thunk ? thunk() : nullptr;
    }

    float m[16];
    ComputeBoneWorldMatrix(reinterpret_cast<float*>(obj), PivotFor(obj, worldPivot), 1, m);

    void* vtxBase = PtrAt(mesh, 0);     // mesh[0] -> vertex array
    int   count   = I32(mesh, 8);       // mesh[2]
    u8*   vtx     = AtPtr(vtxBase, 32); // first vertex's +32 skin slot

    int srcIdx = 0;
    for (int i = 0; i < count; ++i) {
        float ax = (static_cast<float>(static_cast<std::int16_t>(packedSrc[srcIdx + 0])) + kPackedBias) * kPackedScale;
        float ay = (static_cast<float>(static_cast<std::int16_t>(packedSrc[srcIdx + 1])) + kPackedBias) * kPackedScale;
        float az = (static_cast<float>(static_cast<std::int16_t>(packedSrc[srcIdx + 2])) + kPackedBias) * kPackedScale;

        float* out = reinterpret_cast<float*>(vtx);
        out[0] = ax * m[0] + ay * m[4] + az * m[8];
        out[1] = ax * m[1] + ay * m[5] + az * m[9];
        out[2] = ax * m[2] + ay * m[6] + az * m[10];

        vtx += 80;       // v7 += 20 floats
        srcIdx += 3;
    }
    return mesh;
}

// gilde.exe 0x5c9c58 — VIBE_Mesh_TransformVertexNormals
float* TransformVertexNormals(void* obj, void* mesh, const float* worldPivot)
{
    float m[21]; // matches the original's float v10[21] scratch frame
    float* result = ComputeBoneWorldMatrix(reinterpret_cast<float*>(obj),
                                           PivotFor(obj, worldPivot), 0, m);

    void* vtxBase = PtrAt(mesh, 0);
    int   count   = I32(mesh, 8);
    u8*   dst     = AtPtr(vtxBase, 32); // v7: vertex +32 normal-out slot
    u8*   cur     = BytePtr(vtxBase);

    for (int i = 0; i < count; ++i) {
        // result = *(vertex +72) + 12 floats -> the source model normal.
        float* n = reinterpret_cast<float*>(AtPtr(PtrAt(cur, 72), 12));
        result = n;
        float* out = reinterpret_cast<float*>(dst);
        out[0] = n[0] * m[0] + n[1] * m[4] + n[2] * m[8];
        out[1] = n[0] * m[1] + n[1] * m[5] + n[2] * m[9];
        out[2] = n[0] * m[2] + n[1] * m[6] + n[2] * m[10];
        cur += 80;
        dst += 80;
    }
    return result;
}

// gilde.exe 0x5c9e64 — VIBE_Mesh_ComputeBoundingBox
void* ComputeBoundingBox(void* mesh, const float* a2)
{
    if (I32(mesh, 8) == 0)           // *(mesh+8) == 0 -> no vertices
        return nullptr;

    void* vtxBase = PtrAt(mesh, 0);
    int   vcount  = I32(mesh, 8);
    u8*   cornerCursor = AtPtr(vtxBase, 80 * vcount);

    if (U8(mesh, 380) == 0) {
        // Branch A: transform the first 8 corner vertices in place.
        u8* v4 = cornerCursor;
        for (int v3 = 0; v3 < 8; ++v3) {
            float* src = reinterpret_cast<float*>(PtrAt(v4, 72));
            float x = src[0] * a2[0] + src[1] * a2[4] + src[2] * a2[8]  + a2[12];
            float y = src[0] * a2[1] + src[1] * a2[5] + src[2] * a2[9]  + a2[13];
            float z = src[0] * a2[2] + src[1] * a2[6] + src[2] * a2[10] + a2[14];
            float* dst = reinterpret_cast<float*>(v4);
            dst[0] = x;
            dst[1] = y;
            dst[2] = z;
            v4 += 80;
        }
        return cornerCursor;
    }

    // Branch B: reduce all sub-mesh keyframe AABBs to a min/max box.
    // Pseudocode locals: v24=minX v25=minY v26=minZ v27=maxX v23=maxY v28=maxZ.
    float v24 = 1.0e10f, v25 = 1.0e10f, v26 = 1.0e10f;
    float v27 = -1.0e10f, v23 = -1.0e10f, v28 = -1.0e10f;

    u8* sm    = BytePtr(mesh);
    u8* smEnd = AtPtr(mesh, 348);
    do {
        if (I32(sm, 132) != 0) {
            // box record = *(*(sm+132) + 348) + 192 * *(sm+28); float fields
            // [15..20] hold (minX,minY,minZ, maxX,maxY,maxZ). The +348 slot holds
            // a POINTER to the keyframe array (deref, not address-of).
            u8* rec = reinterpret_cast<u8*>(PtrAt(PtrAt(sm, 132), 348)) + 192 * I32(sm, 28);
            float* b = reinterpret_cast<float*>(rec);
            if (v24 >= static_cast<double>(b[15])) v24 = b[15];
            if (v25 >= static_cast<double>(b[16])) v25 = b[16];
            if (v26 >= static_cast<double>(b[17])) v26 = b[17];
            if (v27 <= static_cast<double>(b[18])) v27 = b[18];
            if (v23 <= static_cast<double>(b[19])) v23 = b[19];
            if (v28 <= static_cast<double>(b[20])) v28 = b[20];
        }
        sm += 116;
    } while (sm != smEnd);

    // Expand the min/max box to its 8 corners — exact ordering from the v21[] writes.
    float box8[8][3] = {
        { v24, v25, v26 },
        { v27, v25, v26 },
        { v24, v23, v26 },
        { v27, v23, v26 },
        { v24, v25, v28 },
        { v27, v25, v28 },
        { v24, v23, v28 },
        { v27, v23, v28 },
    };

    u8* dst8 = cornerCursor;
    for (int i = 0; i < 8; ++i) {
        float* c = box8[i];
        float x = c[0] * a2[0] + c[1] * a2[4] + c[2] * a2[8]  + a2[12];
        float y = c[0] * a2[1] + c[1] * a2[5] + c[2] * a2[9]  + a2[13];
        float z = c[0] * a2[2] + c[1] * a2[6] + c[2] * a2[10] + a2[14];
        float* d = reinterpret_cast<float*>(dst8);
        d[0] = x;
        d[1] = y;
        d[2] = z;
        dst8 += 80;
    }
    return cornerCursor;
}

// gilde.exe 0x42698c — VIBE_Mesh_ComputeHeightRange
int ComputeHeightRange(void* obj, float* outMinY, float* outMaxY, float* worldPivot)
{
    if (U8(obj, 533) != 4)           // *(obj+533) != 4 -> not vegetation
        return 1;

    TransformBoundingVolume(obj, worldPivot, 0);

    void* mesh = PtrAt(obj, 460);
    float* mat = reinterpret_cast<float*>(AtPtr(PtrAt(obj, 492), 8));
    void* box  = ComputeBoundingBox(mesh, mat);
    if (!box)
        return 1;

    u8* corner = BytePtr(box);
    // y of corner 0 -> both outputs, then min/max corners 1..7.
    *outMinY = *reinterpret_cast<float*>(corner + 4);
    *outMaxY = *reinterpret_cast<float*>(corner + 4);
    for (int i = 1; i < 8; ++i) {
        float cy = *reinterpret_cast<float*>(corner + 4);
        if (!(*outMinY < static_cast<double>(cy)))
            *outMinY = cy;
        if (*outMaxY <= static_cast<double>(cy))
            *outMaxY = cy;
        corner += 80;
    }
    return 0;
}

// gilde.exe 0x426bec — VIBE_Mesh_ComputeBoundingRadius
u8 ComputeBoundingRadius(void* obj, float* pivot, float* outRadius, float* outCenter,
                         const float* (*cornerBuffer)(void* obj))
{
    u8 ok = 0;
    if (!obj || (!outCenter && !outRadius))
        return ok;

    float radius = 100.0f;
    float cx = 0.0f, cy = 0.0f, cz = 0.0f;

    void* draw = PtrAt(obj, 492);
    const float* corners = nullptr;
    if (I32(draw, 260) != 0) {
        TransformBoundingVolume(obj, pivot, 0);
        corners = cornerBuffer ? cornerBuffer(obj) : nullptr;
    }

    if (corners) {
        // Sum the 8 corners (stride 80 bytes / 20 floats) into a centroid.
        float sx = corners[0], sy = corners[1], sz = corners[2];
        const float* c = corners + 20; // +80 bytes
        for (int v14 = 1; v14 < 8; ++v14) {
            sx += c[0];
            sy += c[1];
            sz += c[2];
            c += 20;
        }
        cx = sx * kCornerMeanScale;
        cy = sy * kCornerMeanScale;
        cz = sz * kCornerMeanScale;

        float maxSq = 0.0f;
        const float* p = corners;
        for (int v21 = 0; v21 < 8; ++v21) {
            float dx = cx - p[0];
            float dy = cy - p[1];
            float dz = cz - p[2];
            float d2 = dx * dx + dy * dy + dz * dz;
            if (d2 >= maxSq)
                maxSq = d2;
            p += 20;
        }
        ok = 1;
        radius = static_cast<float>(std::sqrt(static_cast<double>(maxSq)));
    } else if (!ok) {
        // Original: if corners couldn't be fetched and v7 (ok) is still 0, return.
        return ok;
    }

    if (outRadius)
        *outRadius = radius;
    if (!outCenter)
        return ok;
    outCenter[0] = cx;
    outCenter[1] = cy;
    outCenter[2] = cz;
    return ok;
}

namespace {
// Min/max accumulate shared by AccumulateVertexBounds / AccumulateVertexAabb.
// box[0..2] = running min xyz, box[4..6] = running max xyz; grown by every vertex
// of the object's +460 mesh (80-byte stride, model xyz at +0).
void GrowBoxByMeshVerts(void* mesh, float* box)
{
    void* vtxBase = PtrAt(mesh, 0);
    int   count   = I32(mesh, 8);
    u8*   vtx     = BytePtr(vtxBase);
    for (int i = 0; i < count; ++i) {
        float* v = reinterpret_cast<float*>(vtx);
        for (int k = 0; k < 3; ++k) {
            // box[k] = min(box[k], v[k]); box[k+4] = max(box[k+4], v[k]).
            if (!(box[k] < static_cast<double>(v[k])))
                box[k] = v[k];
            if (box[k + 4] <= static_cast<double>(v[k]))
                box[k + 4] = v[k];
        }
        vtx += 80;
    }
}
} // namespace

// gilde.exe 0x5c5084 — VIBE_Mesh_AccumulateVertexBounds
u8 AccumulateVertexBounds(void* obj, float* box)
{
    void* mesh = PtrAt(obj, 460);
    if (mesh) {
        u8 f = U8(obj, 530);
        if ((f & 0xC) == 0 || (f & 0x10) != 0)
            GrowBoxByMeshVerts(mesh, box);
    }
    return 1;
}

// gilde.exe 0x5b29d8 — VIBE_Mesh_AccumulateVertexAabb
u8 AccumulateVertexAabb(void* obj, float* box)
{
    void* mesh = PtrAt(obj, 460);
    if (mesh)
        GrowBoxByMeshVerts(mesh, box);
    return 1;
}

namespace {
// Shared colour-stamp walk for Reset/SetVertexColors. Walks the +460 mesh's
// sub-meshes (*(mesh+4) -> vertex-ptr array, 10-ptr stride per sub-mesh; mesh+12
// sub-mesh count) and writes the three colour bytes into each vertex's +68/+69/+70.
void StampVertexColors(void* mesh, u8 c68, u8 c69, u8 c70)
{
    void** base = reinterpret_cast<void**>(PtrAt(mesh, 4));
    void** v4   = base;
    void** v6   = base + 3;
    int subCount = I32(mesh, 12);
    for (int s = 0; s < subCount; ++s) {
        void** v7 = v4;
        do {
            void* vtx = *v7;
            U8(vtx, 70) = c70;
            U8(vtx, 69) = c69;
            U8(vtx, 68) = c68;
            ++v7;
        } while (v7 != v6);
        v6 += 10;
        v4 += 10;
    }
}
} // namespace

// gilde.exe 0x428898 — VIBE_Mesh_ResetVertexColors
void* ResetVertexColors(void* obj, int enable)
{
    if (obj) {
        void* mesh = PtrAt(obj, 460);
        if (mesh) {
            if (enable) {
                StampVertexColors(mesh, 0x80, 0x80, 0x80);
                U8(obj, 530) |= 2u;
                U8(obj, 528) |= 4u;
            } else {
                U8(obj, 530) &= static_cast<u8>(~2u);
                BuildObjectCache(obj);
                U8(obj, 528) |= 4u;
            }
        }
    }
    return obj;
}

// gilde.exe 0x428928 — VIBE_Mesh_SetVertexColors  (dl=b, cl=g, bl=r)
void* SetVertexColors(void* obj, u8 b, u8 g, u8 r)
{
    if (obj && PtrAt(obj, 460)) {
        void* mesh = PtrAt(obj, 460);
        if (b || r || g) {
            // *(v+70)=b, *(v+69)=r, *(v+68)=g  (a2=b, a4=r, a3=g in the original).
            StampVertexColors(mesh, /*c68=*/g, /*c69=*/r, /*c70=*/b);
            U8(obj, 530) |= 2u;
            U8(obj, 528) |= 4u;
        } else {
            U8(obj, 530) &= static_cast<u8>(~2u);
            BuildObjectCache(obj);
            U8(obj, 528) |= 4u;
        }
    }
    return obj;
}

} // namespace guild::render
