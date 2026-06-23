// mesh_recon3_geometry.cpp — VIBE_Mesh_* geometry leaves (recon3 cluster)
//
//   gilde.exe 0x5d1a6c — VIBE_Mesh_ComputeVertexNormals
//   gilde.exe 0x5d1b54 — VIBE_Mesh_ComputeBoundingExtents
//
// Both reuse the shared math helpers already reconstructed in src/util/math.cpp:
//   guild::util::TriangleNormal  @0x5cb824
//   guild::util::VectorNormalize @0x5cb148
// so float math (operation order, zero-length guard) matches the original 1:1.

#include "render/mesh_recon3_geometry.h"
#include "util/math.h"

#include <cmath>

namespace guild::render::mesh_recon3 {

// gilde.exe 0x628fc0 — flt_628FC0 = 0.125f (1/8, averages the 8 AABB corners).
static const f32 kCornerAvg = 0.125f;

// gilde.exe 0x5d1a6c — VIBE_Mesh_ComputeVertexNormals (__usercall, eax = mesh)
void ComputeVertexNormals(MeshView& mesh) {
    // --- per-face: face normal = TriangleNormal(v[idx0], v[idx1], v[idx2]) ---
    // Original walks faces with v2 += 14 (dwords = 56 bytes) and the out-normal
    // pointer v4 starting at dword 11 (+44). The original call is
    //   VIBE_Math_TriangleNormal(v8, v7, v4, v6)   (IDA arg order: a1,a2,a3=out,a4)
    // with v8 = verts[idx[6/0]], v7 = verts[idx[7/1]], v6 = verts[idx[8/2]].
    // In the reimpl TriangleNormal(a,b,c,out) the out is the 4th arg, so this is
    //   TriangleNormal(v8, v7, v6, out=&face.face_normal).
    {
        i32 i = 0;
        if (mesh.face_count > 0) {
            do {
                MeshFace& f = mesh.faces[i];
                f32* v8 = mesh.verts[f.idx[0]].pos;
                f32* v7 = mesh.verts[f.idx[1]].pos;
                f32* v6 = mesh.verts[f.idx[2]].pos;
                guild::util::TriangleNormal(v8, v7, v6, f.face_normal);
                ++i;
            } while (i < mesh.face_count);
        }
    }

    // --- per-vertex: sum face normals of every face touching the vertex ---
    for (i32 i = 0; i < mesh.vert_count; ++i) {
        f32 acc[3];           // v15 (x), v16 (y), v17 (z)
        acc[1] = 0.0f;        // v16
        acc[2] = 0.0f;        // v17
        acc[0] = 0.0f;        // v15
        for (i32 j = 0; j < mesh.face_count; ++j) {
            const MeshFace& f = mesh.faces[j];
            if (i == f.idx[0] || i == f.idx[1] || i == f.idx[2]) {
                acc[0] += f.face_normal[0];   // v15 += [j+44]
                acc[1] += f.face_normal[1];   // v16 += [j+48]
                acc[2] += f.face_normal[2];   // v17 += [j+52]
            }
        }
        guild::util::VectorNormalize(acc);
        mesh.verts[i].normal[0] = acc[0];
        mesh.verts[i].normal[1] = acc[1];
        mesh.verts[i].normal[2] = acc[2];
    }
}

// gilde.exe 0x5d1b54 — VIBE_Mesh_ComputeBoundingExtents (__usercall, eax = obj)
void ComputeBoundingExtents(BoundsObject& obj) {
    // --- pass 1: bounding radius = max |vertex| from origin ---
    f32 maxLen = 0.0f;        // i
    for (i32 v = 0; v < obj.vert_count; ++v) {
        const f32* p = obj.verts[v].pos;
        // 0x5d1b75..0x5d1b94: the binary forms each product fld/fmul in 80-bit x87,
        // sums + fsqrt in 80-bit, then fcomp against the float accumulator `i` and,
        // on the taken branch, recomputes sqrt and stores it as float. Standard C++
        // has no 80-bit; promoting to double makes each float*float product exact
        // and the sum/sqrt closest to the x87 path. Compare in double, store float.
        // (Kept identical to mesh_postprocess.cpp's copy so all copies agree.)
        f64 len = std::sqrt((f64)p[0] * p[0] + (f64)p[1] * p[1] + (f64)p[2] * p[2]);
        if (len > (f64)maxLen)
            maxLen = (f32)len;
    }

    obj.center[0] = 0.0f;     // a1+104
    obj.center[1] = 0.0f;     // a1+108
    obj.center[2] = 0.0f;     // a1+112
    obj.radius = maxLen;          // a1+472
    obj.radius_alias = obj.radius;// a1+468 = *(a1+472)

    if (obj.vert_count > 0) {
        // --- pass 2: axis-aligned bounds (compare-and-select order verbatim) ---
        f32 maxX = -1.0e10f;  // v31
        f32 maxY = -1.0e10f;  // v33
        f32 maxZ = -1.0e10f;  // v35
        f32 minX = 1.0e10f;   // v38
        f32 minY = 1.0e10f;   // v40
        f32 minZ = 1.0e10f;   // v42

        for (i32 v = 0; v < obj.vert_count; ++v) {
            const f32* j = obj.verts[v].pos;
            minX = (minX < (f64)j[0]) ? minX : j[0];
            minY = (minY >= (f64)j[1]) ? j[1] : minY;
            minZ = (minZ >= (f64)j[2]) ? j[2] : minZ;
            maxX = (maxX <= (f64)j[0]) ? j[0] : maxX;
            maxY = (maxY <= (f64)j[1]) ? j[1] : maxY;
            maxZ = (maxZ <= (f64)j[2]) ? j[2] : maxZ;
        }

        // --- write the 8 AABB corners at vertex indices [count .. count+7] ---
        MeshVertex* corner = &obj.verts[obj.vert_count];
        // corner 0: (minX, minY, minZ)
        corner[0].pos[0] = minX; corner[0].pos[1] = minY; corner[0].pos[2] = minZ;
        // corner 1: (maxX, minY, minZ)
        corner[1].pos[0] = maxX; corner[1].pos[1] = minY; corner[1].pos[2] = minZ;
        // corner 2: (minX, maxY, minZ)
        corner[2].pos[0] = minX; corner[2].pos[1] = maxY; corner[2].pos[2] = minZ;
        // corner 3: (maxX, maxY, minZ)
        corner[3].pos[0] = maxX; corner[3].pos[1] = maxY; corner[3].pos[2] = minZ;
        // corner 4: (minX, minY, maxZ)
        corner[4].pos[0] = minX; corner[4].pos[1] = minY; corner[4].pos[2] = maxZ;
        // corner 5: (maxX, minY, maxZ)
        corner[5].pos[0] = maxX; corner[5].pos[1] = minY; corner[5].pos[2] = maxZ;
        // corner 6: (minX, maxY, maxZ)
        corner[6].pos[0] = minX; corner[6].pos[1] = maxY; corner[6].pos[2] = maxZ;
        // corner 7: (maxX, maxY, maxZ)
        corner[7].pos[0] = maxX; corner[7].pos[1] = maxY; corner[7].pos[2] = maxZ;

        // --- radius = length of AABB diagonal (max - min) ---
        f32 dx = maxX - minX;   // v32
        f32 dy = maxY - minY;   // v34
        f32 dz = maxZ - minZ;   // v36
        obj.radius = (f32)std::sqrt((f64)(dx * dx + dy * dy + dz * dz));

        // --- center = (sum of the 8 corner coords) * 0.125 ---
        // Original accumulates in order: sumZ(v18) += z, sumY(v19) += y, sumX(v20) += x.
        f64 sumZ = 0.0;   // v18
        f64 sumY = 0.0;   // v19
        f64 sumX = 0.0;   // v20
        for (int k = 0; k < 8; ++k) {
            f64 px = corner[k].pos[0];   // v21
            f64 py = corner[k].pos[1];   // v22
            f64 pz = corner[k].pos[2];   // v23
            sumY = py + sumY;
            sumZ = pz + sumZ;
            sumX = px + sumX;
        }
        obj.center[0] = (f32)sumX * kCornerAvg;   // a1+104 = v39 * flt
        obj.center[1] = (f32)sumY * kCornerAvg;   // a1+108 = v41 * flt
        obj.center[2] = (f32)sumZ * kCornerAvg;   // a1+112 = v43 * flt
    }
}

} // namespace guild::render::mesh_recon3
