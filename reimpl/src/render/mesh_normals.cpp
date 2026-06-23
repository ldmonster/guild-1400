#include "render/mesh_normals.h"

#include "util/math.h"   // TriangleNormal, VectorNormalize (1:1 of 0x5cb824 / 0x5cb148)

namespace guild::render {

// gilde.exe 0x5D1A6C — VIBE_Mesh_ComputeVertexNormals.
//
// Disasm-faithful structure:
//   v2  = *(result+18)   poly base       (here: triangles)
//   v3  = *(result+19)   poly count
//   --- per poly: TriangleNormal(&vert[vtx0], &vert[vtx1], &vert[vtx2], &poly+44)
//       (each face normal is UNIT length — TriangleNormal normalizes).
//   --- per vertex i in [0, *(result+17)):
//         acc = sum of poly+44 over every poly with i in {vtx0,vtx1,vtx2}
//         VectorNormalize(&acc); store at vertex+12/16/20.
//
// The original walks the poly array twice (once to fill +44, once per vertex to
// accumulate); we keep an explicit faceNormals scratch (the engine's poly+44 slot)
// so the second loop reads exactly what the first wrote — behaviour-identical.
void GenerateVertexNormals(SourceMeshVertex* verts, int vertexCount,
                           const NormalTriangle* triangles, int triangleCount,
                           float* faceNormalsOut) {
    if (!verts || vertexCount <= 0)
        return;
    if (!triangles || triangleCount <= 0) {
        // No topology -> every vertex normal collapses (VectorNormalize of 0).
        for (int i = 0; i < vertexCount; ++i) {
            verts[i].normal[0] = 0.0f;
            verts[i].normal[1] = 0.0f;
            verts[i].normal[2] = 0.0f;
        }
        return;
    }

    // ----- pass 1: per-triangle unit face normals (poly+44) ----------------
    // faceScratch is value-initialised to {0,0,0}; any triangle skipped below keeps
    // that zero face normal (which a zero-length VectorNormalize in pass 2 collapses).
    std::vector<std::array<float, 3>> faceScratch(static_cast<size_t>(triangleCount));
    const u32 vc = static_cast<u32>(vertexCount);
    for (int t = 0; t < triangleCount; ++t) {
        const NormalTriangle& tri = triangles[t];
        // MEMORY-SAFETY GUARD (hardening, not behavioral): the engine's poly index
        // triples are always in [0, vertexCount) for a well-formed .BGF, so for valid
        // input this guard never fires and the in-bounds path is byte-identical. A
        // corrupt/out-of-range index would otherwise read `verts[]` out of bounds
        // (an OOB read of the source vertex pos). Skip such a triangle rather than
        // deref past the array. (The original would also read OOB here; faithfully
        // it never reaches this with a bad index — see progress/harden-mesh-wave10.md.)
        if (tri.vtx[0] >= vc || tri.vtx[1] >= vc || tri.vtx[2] >= vc)
            continue;
        // The original passes (v8=&vtx0, v7=&vtx1, v6=&vtx2, OUT=&poly+44). The util
        // signature is (a, b, c, out) with the OUT buffer 4th — the same vertices.
        float* a = verts[tri.vtx[0]].pos;
        float* b = verts[tri.vtx[1]].pos;
        float* c = verts[tri.vtx[2]].pos;
        util::TriangleNormal(a, b, c, faceScratch[static_cast<size_t>(t)].data());
    }

    // ----- pass 2: per-vertex averaged + normalized normals ----------------
    for (int i = 0; i < vertexCount; ++i) {
        // The original seeds {y,x,z} = {0,0,0} in (v16,v15,v17) order; the sum order
        // does not affect the float result here (commutative adds of the same terms).
        float acc[3] = {0.0f, 0.0f, 0.0f};   // v15=x, v16=y, v17=z
        for (int t = 0; t < triangleCount; ++t) {
            const NormalTriangle& tri = triangles[t];
            if (i == static_cast<int>(tri.vtx[0]) ||
                i == static_cast<int>(tri.vtx[1]) ||
                i == static_cast<int>(tri.vtx[2])) {
                const std::array<float, 3>& fn = faceScratch[static_cast<size_t>(t)];
                acc[0] += fn[0];   // +44
                acc[1] += fn[1];   // +48
                acc[2] += fn[2];   // +52
            }
        }
        util::VectorNormalize(acc);   // zero-length -> (0,0,0)
        verts[i].normal[0] = acc[0];  // v10[3]
        verts[i].normal[1] = acc[1];  // v10[4]
        verts[i].normal[2] = acc[2];  // v10[5]
    }

    if (faceNormalsOut) {
        for (int t = 0; t < triangleCount; ++t) {
            const std::array<float, 3>& fn = faceScratch[static_cast<size_t>(t)];
            faceNormalsOut[3 * t + 0] = fn[0];
            faceNormalsOut[3 * t + 1] = fn[1];
            faceNormalsOut[3 * t + 2] = fn[2];
        }
    }
}

void GenerateVertexNormals(std::vector<SourceMeshVertex>& verts,
                           const std::vector<NormalTriangle>& triangles,
                           std::vector<std::array<float, 3>>* faceNormalsOut) {
    const int vc = static_cast<int>(verts.size());
    const int tc = static_cast<int>(triangles.size());
    if (faceNormalsOut) {
        faceNormalsOut->assign(static_cast<size_t>(tc < 0 ? 0 : tc), {0.0f, 0.0f, 0.0f});
        GenerateVertexNormals(verts.data(), vc, triangles.data(), tc,
                              tc > 0 ? faceNormalsOut->front().data() : nullptr);
    } else {
        GenerateVertexNormals(verts.data(), vc, triangles.data(), tc, nullptr);
    }
}

// ---------------------------------------------------------------------------
// Per-instance source-normal binding (the engine's instVert+0x48 wiring).
// ---------------------------------------------------------------------------
void BindInstanceNormals(const SourceMeshVertex* verts, int count,
                         MeshNormalSource mode,
                         InstanceVertexNormalBinding* out) {
    if (!verts || !out || count <= 0)
        return;
    for (int i = 0; i < count; ++i) {
        const SourceMeshVertex& sv = verts[i];
        // The pointer the engine stores at instVert+0x48:
        //   kBindToVertexBase   -> &sv         (readers deref +12 for the normal)
        //   kBindToVertexNormal -> &sv.normal  (sun cache derefs +0 for the normal)
        out[i].sourcePtr = (mode == MeshNormalSource::kBindToVertexNormal)
                               ? static_cast<const void*>(sv.normal)
                               : static_cast<const void*>(&sv);
        // The resolved object-space normal is ALWAYS the source vertex's +12 normal.
        out[i].normal[0] = sv.normal[0];
        out[i].normal[1] = sv.normal[1];
        out[i].normal[2] = sv.normal[2];
    }
}

void BindInstanceNormals(const std::vector<SourceMeshVertex>& verts,
                         MeshNormalSource mode,
                         std::vector<InstanceVertexNormalBinding>& out) {
    out.assign(verts.size(), InstanceVertexNormalBinding{});
    BindInstanceNormals(verts.data(), static_cast<int>(verts.size()), mode, out.data());
}

void FlattenInstanceNormals(const InstanceVertexNormalBinding* bindings, int count,
                            float* outNormals) {
    if (!bindings || !outNormals || count <= 0)
        return;
    for (int i = 0; i < count; ++i) {
        outNormals[3 * i + 0] = bindings[i].normal[0];
        outNormals[3 * i + 1] = bindings[i].normal[1];
        outNormals[3 * i + 2] = bindings[i].normal[2];
    }
}

} // namespace guild::render
