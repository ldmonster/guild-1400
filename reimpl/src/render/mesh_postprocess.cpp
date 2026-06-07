#include "render/mesh_postprocess.h"

#include "util/math.h"   // TriangleNormal, VectorNormalize

#include <cmath>  // sqrt

namespace guild::render {

// gilde.exe 0x5D1B54 — VIBE_Mesh_ComputeBoundingExtents
//
// The original walked the raw 24-byte vertex array twice: once for the bounding
// radius (max |vertex|) and once for the per-axis AABB. It then materialised the
// 8 AABB corner vertices into the 8 slack slots immediately past the live verts,
// set the diagonal length, and averaged the corners into the centroid (× 0.125).
void ComputeBoundingExtents(Mesh& m) {
    MeshVertex* verts = m.vertices.data();
    const int count = m.vertexCount;  // *(a1+68)

    // ----- pass 1: bounding radius = max sqrt(x^2+y^2+z^2) -----------------
    float radius = 0.0f;  // i (ebp-40h), seeded 0.0
    for (int n = 0; n < count; ++n) {
        const float* v = verts[n].pos;
        float len = static_cast<float>(
            std::sqrt(static_cast<double>(v[0]) * v[0] + static_cast<double>(v[1]) * v[1] +
                      static_cast<double>(v[2]) * v[2]));
        if (len > radius)
            radius = len;
    }

    // zero centroid (+104/+108/+112), store radius (+472, then mirrored to +468).
    m.centroid[0] = 0.0f;
    m.centroid[1] = 0.0f;
    m.centroid[2] = 0.0f;
    m.radius  = radius;   // *(a1+472)
    m.radius2 = radius;   // *(a1+468) = *(a1+472)

    if (count <= 0)
        return;

    // ----- pass 2: per-axis AABB -------------------------------------------
    float maxY = -1.0e10f, maxZ = -1.0e10f, maxX = -1.0e10f;  // v33, v35, v31
    float minX = 1.0e10f, minY = 1.0e10f, minZ = 1.0e10f;     // v38, v40, v42
    for (int n = 0; n < count; ++n) {
        const float* v = verts[n].pos;
        // The original compares with the accumulator on the LHS (mixed </>=),
        // reproduced verbatim so NaN handling matches.
        minX = (minX < static_cast<double>(v[0])) ? minX : v[0];
        minY = (minY >= static_cast<double>(v[1])) ? v[1] : minY;
        minZ = (minZ >= static_cast<double>(v[2])) ? v[2] : minZ;
        maxX = (maxX <= static_cast<double>(v[0])) ? v[0] : maxX;
        maxY = (maxY <= static_cast<double>(v[1])) ? v[1] : maxY;
        maxZ = (maxZ <= static_cast<double>(v[2])) ? v[2] : maxZ;
    }

    // ----- 8 AABB corner vertices into the slack slots ---------------------
    // Slots are at indices count+0 .. count+7 (24*(count+k) + base).
    float corners[8][3] = {
        {minX, minY, minZ},  // count+0
        {maxX, minY, minZ},  // count+1
        {minX, maxY, minZ},  // count+2
        {maxX, maxY, minZ},  // count+3
        {minX, minY, maxZ},  // count+4
        {maxX, minY, maxZ},  // count+5
        {minX, maxY, maxZ},  // count+6
        {maxX, maxY, maxZ},  // count+7
    };
    for (int k = 0; k < 8; ++k) {
        MeshVertex& slot = verts[count + k];
        slot.pos[0] = corners[k][0];
        slot.pos[1] = corners[k][1];
        slot.pos[2] = corners[k][2];
    }

    // ----- diagonal length -> radius2 (+472) -------------------------------
    float dx = maxX - minX;  // v32
    float dy = maxY - minY;  // v34
    float dz = maxZ - minZ;  // v36
    m.radius2 = static_cast<float>(
        std::sqrt(static_cast<double>(dx) * dx + static_cast<double>(dy) * dy +
                  static_cast<double>(dz) * dz));

    // ----- centroid = (1/8) * sum of the 8 corner positions ----------------
    double sumX = 0.0, sumY = 0.0, sumZ = 0.0;  // v20/v19/v18 order in the original
    for (int k = 0; k < 8; ++k) {
        const float* c = verts[count + k].pos;
        sumX += c[0];
        sumY += c[1];
        sumZ += c[2];
    }
    m.centroid[0] = static_cast<float>(sumX * kCentroidWeight);  // +104
    m.centroid[1] = static_cast<float>(sumY * kCentroidWeight);  // +108
    m.centroid[2] = static_cast<float>(sumZ * kCentroidWeight);  // +112
}

// gilde.exe 0x5D1A6C — VIBE_Mesh_ComputeVertexNormals
//
//   result+16 = vertex base, result+17 = vertex count, result+18 = poly base,
//   result+19 = poly count. Per polygon the face normal is the TriangleNormal of
//   its three vertices, stored at poly +44. Per vertex the normal is the
//   normalized sum of every referencing polygon's face normal.
void ComputeVertexNormals(Mesh& m) {
    MeshVertex* verts = m.vertices.data();   // *(result+16)
    MeshPolygon* polys = m.polygons.data();  // *(result+18)
    const int vcount = m.vertexCount;        // *(result+17)
    const int pcount = static_cast<int>(m.polygons.size());  // *(result+19)

    // ----- per-polygon face normals ----------------------------------------
    for (int n = 0; n < pcount; ++n) {
        MeshPolygon& q = polys[n];
        // v8 = &vert[vtx[0]], v7 = &vert[vtx[1]], v6 = &vert[vtx[2]], v4 = &poly+44.
        // Original call: TriangleNormal(v8, v7, OUT=v4, v6). In the util reorder
        // the OUT buffer is the 4th arg, so we pass (vtx0, vtx1, vtx2, out).
        float* a = verts[q.vtx[0]].pos;
        float* b = verts[q.vtx[1]].pos;
        float* c = verts[q.vtx[2]].pos;
        util::TriangleNormal(a, b, c, q.normal);
    }

    // ----- per-vertex averaged normals -------------------------------------
    for (int i = 0; i < vcount; ++i) {
        float acc[3] = {0.0f, 0.0f, 0.0f};  // v15/v16/v17 = x/y/z
        for (int n = 0; n < pcount; ++n) {
            const MeshPolygon& q = polys[n];
            if (i == static_cast<int>(q.vtx[0]) ||
                i == static_cast<int>(q.vtx[1]) ||
                i == static_cast<int>(q.vtx[2])) {
                acc[0] += q.normal[0];  // +44
                acc[1] += q.normal[1];  // +48
                acc[2] += q.normal[2];  // +52
            }
        }
        util::VectorNormalize(acc);
        verts[i].normal[0] = acc[0];  // v10[3]
        verts[i].normal[1] = acc[1];  // v10[4]
        verts[i].normal[2] = acc[2];  // v10[5]
    }
}

} // namespace guild::render
