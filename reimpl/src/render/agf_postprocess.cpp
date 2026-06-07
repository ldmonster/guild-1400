// src/render/agf_postprocess.cpp — AGF mesh post-process stages.
//
// 1:1 reconstruction of the post-parse work inside VIBE_Mesh_LoadBgfFile
// @0x5d2348 (gilde.exe). See agf_postprocess.h for the per-stage banner and the
// recovered constants. Additive module: does NOT touch agf_loader.cpp /
// real_mesh_source.cpp.
#include "render/agf_postprocess.h"

#include <cmath>
#include <cstring>
#include <vector>

namespace guild::render {

// gilde.exe 0x5caa4c — VIBE_Math_VectorWithinTolerance.
//   return fabs(b[0]-a[0])<=t && fabs(b[1]-a[1])<=t && fabs(b[2]-a[2])<=t;
bool VectorWithinTolerance(const float* a, const float* b, float tol) {
    return std::fabs(b[0] - a[0]) <= tol &&
           std::fabs(b[1] - a[1]) <= tol &&
           std::fabs(b[2] - a[2]) <= tol;
}

// gilde.exe 0x5d2425..0x5d254d — STAGE 1: vertex dedup.
// Mirrors the engine's double loop (v12 outer "keep" index, v13 inner "scan"
// index): when scan vertex v13 matches keep vertex v12 within tolerance, remap
// every poly vtx index (poly +24/+28/+32) and remove vertex v13 (the array
// shrinks and v13 is re-examined). The +8 vertex slack the engine keeps is not
// part of render::BgfModel (LoadAgfModel sizes the array exactly), so we operate
// on the logical [0, vertexCount) range only.
u32 DeduplicateVertices(BgfModel& m) {
    if (m.vertexCount == 0 || m.vertices.empty())
        return 0;

    const u32 startCount = m.vertexCount;
    int count = static_cast<int>(m.vertexCount);  // engine v150

    for (int keep = 0; keep < count; ++keep) {            // v12
        for (int scan = 0; scan < count; ) {              // v13
            if (keep == scan ||
                !VectorWithinTolerance(m.vertices[keep].pos,
                                       m.vertices[scan].pos,
                                       kAgfVertexMergeTolerance)) {
                ++scan;                                    // 0x5d30fe
                continue;
            }
            // scan is a duplicate of keep: remap every poly's three vtx indices
            // (0x5d24a8..0x5d24fe).
            for (auto& p : m.polygons) {
                for (int k = 0; k < 3; ++k) {
                    int idx = static_cast<int>(p.vtx[k]);
                    if (idx == scan)
                        p.vtx[k] = static_cast<u32>(keep); // 0x5d24cd
                    else if (scan < idx)
                        p.vtx[k] = static_cast<u32>(idx - 1); // 0x5d24e8
                }
            }
            // Remove vertex `scan` (engine MemMove of 24*(--v150 - v13) bytes at
            // 0x5d2526). v13 is NOT incremented — the slot now holds the next
            // vertex and is re-examined.
            --count;
            m.vertices.erase(m.vertices.begin() + scan);
        }
    }

    m.vertexCount = static_cast<u32>(count);
    return startCount - m.vertexCount;
}

// gilde.exe 0x5d257e..0x5d26e9 — STAGE 2: morph-rotation bake.
// Verbatim FP algebra (0x5d25dc..0x5d26d5) with the recovered constants. The
// engine precomputes s1=sin(A),c1=cos(A) (A=dbl_6290CC=-PI/2) and s2=sin(B),
// c2=cos(B) (B=dbl_6290D4=PI), plus the dbl_6290DC=-1.0 z-scale.
void BakeMorphRotation(BgfModel& m) {
    const u32 n = m.vertexCount ? m.vertexCount : static_cast<u32>(m.vertices.size());
    if (n == 0 || m.vertices.empty())
        return;

    const double s1 = std::sin(kAgfMorphAngleA);   // v163 = sin(dbl_6290CC)
    const double c1 = std::cos(kAgfMorphAngleA);    // v164 = cos(dbl_6290CC)
    const double s2 = std::sin(kAgfMorphAngleB);    // v165 = sin(dbl_6290D4)
    const double c2 = std::cos(kAgfMorphAngleB);    // v166 = cos(dbl_6290D4)

    const u32 limit = (n < m.vertices.size()) ? n : static_cast<u32>(m.vertices.size());
    for (u32 i = 0; i < limit; ++i) {
        float* v = m.vertices[i].pos;
        const double x = v[0];                       // v27 / v169
        const double y = v[1];
        const double z = v[2];

        const double t26 = y * c1;                   // 0x5d25dc
        const double t28 = y * s1 + z * c1;          // v172  0x5d2609
        const double t29 = z * s1;                   // 0x5d2609
        const double t30 = t28 * c2 + (-x) * s2;     // v173  0x5d2650
        const double t31 = (t28 * s2 + x * c2) * kAgfMorphZScale; // v31  0x5d267c
        const double t33 = t26 - t29;                // out.y 0x5d2699
        const double t34 = t30 * c2;                 // 0x5d2699
        const double t36 = -t31 * s2;                // 0x5d26be

        v[0] = static_cast<float>(t31 * c2 + t30 * s2); // 0x5d26af
        v[1] = static_cast<float>(t33);                 // 0x5d26c5
        v[2] = static_cast<float>(t34 + t36);           // 0x5d26d5
    }
}

// Byte-record equality of two materials — the faithful equivalent of the engine's
// memcmp(&mat[i], &mat[j], 224) at 0x5d27a5 (render::BgfMaterial carries the same
// information as the 224-byte record: three names + the format/flag bytes).
static bool MaterialsEqual(const BgfMaterial& a, const BgfMaterial& b) {
    return a.name0 == b.name0 && a.name1 == b.name1 && a.name2 == b.name2 &&
           a.flag == b.flag && a.b1 == b.b1 && a.b2 == b.b2 &&
           a.b3 == b.b3 && a.b4 == b.b4 && a.b5 == b.b5;
}

// gilde.exe 0x5d26f2..0x5d2b60 — STAGE 3: material dedup + compact + reorder.
u32 DeduplicateMaterials(BgfModel& m) {
    int matCount = static_cast<int>(m.materialCount);   // v146
    if (matCount <= 0 || m.materials.empty())
        return 0;
    if (static_cast<int>(m.materials.size()) < matCount)
        matCount = static_cast<int>(m.materials.size());
    const u32 startCount = static_cast<u32>(matCount);

    // ---- 3a RemoveDoubleMaterials (0x5d2705..0x5d2862) -----------------------
    // For each material i not yet flagged "done", flag it, then for every later
    // material k byte-identical to i: remap every poly matIndex==k to i, flag k.
    {
        std::vector<char> done(static_cast<size_t>(matCount), 0); // v167
        for (int i = 0; i < matCount; ++i) {                       // v171
            if (done[i])
                continue;
            done[i] = 1;
            // The engine compares every later material regardless of its `done`
            // flag (a later k already merged into an earlier i is byte-distinct
            // from this i unless truly identical, so re-merging is idempotent).
            for (int k = i + 1; k < matCount; ++k) {               // v41
                if (MaterialsEqual(m.materials[i], m.materials[k])) {
                    for (auto& p : m.polygons)                     // 0x5d27bc
                        if (p.matIndex == k)
                            p.matIndex = i;                        // 0x5d27d0
                    done[k] = 1;                                   // 0x5d27e6
                }
            }
        }
    }

    // ---- 3b Compact out unreferenced materials (0x5d2870..0x5d2971) ----------
    // used[i]=1 for every material referenced by a poly; then drop the unused
    // ones, decrementing every poly matIndex above each removed slot.
    {
        std::vector<char> used(static_cast<size_t>(matCount), 0);  // v167 (2nd)
        for (const auto& p : m.polygons)                           // 0x5d2886
            if (p.matIndex >= 0 && p.matIndex < matCount)
                used[p.matIndex] = 1;                              // 0x5d28a7

        int dst = 0;                                               // v48
        for (int i = 0; i < matCount; ) {                         // walks the flag array
            if (used[i]) {
                ++dst;                                             // 0x5d312d
                ++i;
            } else {
                for (auto& p : m.polygons)                        // 0x5d28fc
                    if (dst < p.matIndex)
                        p.matIndex = p.matIndex - 1;               // 0x5d290e
                m.materials.erase(m.materials.begin() + i);       // MemMove 0x5d294c
                used.erase(used.begin() + i);                     // MemMove 0x5d295c
                --matCount;                                        // --v156
                // i unchanged: the slot now holds the next material.
            }
        }
    }

    // ---- 3c Reorder materials by first poly appearance (0x5d29ed..0x5d2b60) --
    // For each poly in order, the first time its (>=0) matIndex is seen, that
    // material is emitted next and every poly using it is renumbered to the new
    // sequential index. Engine uses a temp 224B*N copy (v161) + a "seen" flag
    // array (v157); we mirror with a temp material vector + remap table.
    {
        std::vector<BgfMaterial> temp = m.materials;              // v161 copy
        std::vector<char> seen(static_cast<size_t>(matCount), 0); // v157
        std::vector<BgfMaterial> reordered;
        reordered.reserve(static_cast<size_t>(matCount));
        std::vector<int> remap(static_cast<size_t>(matCount), -1);
        int next = 0;                                             // v76

        for (auto& p : m.polygons) {                             // poly order
            const int mi = p.matIndex;                            // v78
            if (mi > -1 && mi < matCount && !seen[mi]) {
                seen[mi] = 1;                                     // 0x5d2a93
                remap[mi] = next;
                reordered.push_back(temp[mi]);                   // copy temp->final
                ++next;                                          // 0x5d2b13
            }
        }
        // Renumber every poly to the new order (engine writes the model poly's
        // matIndex during the inner loop at 0x5d2abc).
        for (auto& p : m.polygons) {
            if (p.matIndex >= 0 && p.matIndex < matCount && remap[p.matIndex] >= 0)
                p.matIndex = remap[p.matIndex];
        }
        m.materials = std::move(reordered);
        matCount = static_cast<int>(m.materials.size());
    }

    m.materialCount = static_cast<u32>(matCount);
    return startCount - m.materialCount;
}

// Engine order inside VIBE_Mesh_LoadBgfFile: vertex dedup, morph bake, material
// dedup/compact/reorder. (UV-transform + texture/dummy stages are out of scope —
// see header banner.)
void PostProcessModel(BgfModel& m) {
    DeduplicateVertices(m);
    BakeMorphRotation(m);
    DeduplicateMaterials(m);
}

} // namespace guild::render
