#include "render/mesh_load.h"
#include "render/mesh_postprocess.h"

#include "util/math.h"  // VectorWithinTolerance, VectorNormalize

#include <cmath>     // sin, cos
#include <cstring>   // memcmp, memmove, memcpy, strncpy

namespace guild::render {

// ---------------------------------------------------------------------------
// Forward declaration of the real texture loader (a different module). The
// orchestrator resolves each material's diffuse name to a texture-slot index via
// VIBE_Texture_LoadByName @0x5DA714; the production code returned a pointer into
// the global record array (dword_1406A84, 128-byte stride) and the slot id was
//   (ptr - base) >> 7
// We model the loader as returning the slot id directly (-1 = not found), so the
// orchestrator stays platform-neutral and testable with a mock backend.
//
//   name  : the material's diffuse texture name (NUL-terminated, <=63 chars)
//   flag0 : the assembled present/format flag word (v100 = v171)
//   flag1 : the palette/group byte (v97)
//   flag2 : the secondary format byte (v98)
// Returns the resolved texture slot id, or -1 if the texture could not be loaded.
extern int TextureLoadByName(const char* name, u32 flag0, u32 flag1, u32 flag2);

namespace {

// VIBE_Util_StrNCopyPad @0x5D9360 — copy up to `n` chars then NUL-terminate.
void StrNCopyPad(char* dst, const char* src, int n) {
    int i = 0;
    for (; i < n && src[i]; ++i)
        dst[i] = src[i];
    dst[i] = 0;
}

} // namespace

// gilde.exe 0x5D2348 — VIBE_Mesh_LoadBgfFile (post-parse orchestration).
bool LoadBgfPostProcess(ParsedModel& pm, const std::string& name,
                        i32 morphFrames, bool morphFlag, Mesh& out) {
    // v143 = poly count, v145 = vertex count (mutated by dedup), v141 = material count.
    int polyCount = static_cast<int>(pm.polygons.size());
    int vertCount = static_cast<int>(pm.vertices.size());
    int matCount  = static_cast<int>(pm.materials.size());

    // Original gate: bail unless both polys and verts are present (LABEL_6).
    if (polyCount <= 0 || vertCount <= 0)
        return false;

    MeshPolygon* polys = pm.polygons.data();  // v144 (56-byte stride)
    MeshVertex*  verts = pm.vertices.data();   // v146 (24-byte stride)

    // Name (StrNCopyPad of the upper-cased path; 63 + NUL).
    out.name = name;
    if (out.name.size() > 63)
        out.name.resize(63);

    // =====================================================================
    // 1) VERTEX DEDUP — collapse coincident vertices (skipped if v140[1]).
    //    For each ordered pair (v10, v11), v10 != v11, whose positions are
    //    within 0.001 per axis: remap every poly index (==v11 -> v10; >v11 ->
    //    index-1) and remove vertex v11 (MemMove the tail up), decrement count.
    // =====================================================================
    if (!pm.skipVertexDedup) {
        for (int a = 0; a < vertCount; ++a) {
            for (int b = 0; b < vertCount; ) {
                if (a == b ||
                    !util::VectorWithinTolerance(verts[a].pos, verts[b].pos,
                                                 kVertexDedupTol)) {
                    ++b;
                    continue;
                }
                // Remap poly vertex indices.
                for (int p = 0; p < polyCount; ++p) {
                    u32* idx = polys[p].vtx;  // +24/+28/+32
                    for (int k = 0; k < 3; ++k) {
                        int v15 = static_cast<int>(idx[k]);
                        if (b == v15)
                            idx[k] = static_cast<u32>(a);
                        else if (b < v15)
                            idx[k] = static_cast<u32>(v15 - 1);
                    }
                }
                // MemMove(dst=&vert[b], src=&vert[b+1], 24*(--count - b)).
                --vertCount;
                std::memmove(&verts[b], &verts[b + 1],
                             sizeof(MeshVertex) * static_cast<size_t>(vertCount - b));
                // (Do NOT advance b: a new vertex now occupies slot b.)
            }
        }
    }

    // Record logical vertex count and allocate the engine-sized array (count+8
    // slack slots for the 8 AABB corner verts written by ComputeBoundingExtents).
    out.vertexCount = vertCount;
    out.vertices.assign(static_cast<size_t>(vertCount) + 8, MeshVertex{});

    // =====================================================================
    // 2) MORPH-TARGET BAKE — rotate each (deduped) source vertex by the fixed
    //    euler triple (dbl_6290CC/D4/DC) into the mesh vertex array.
    // =====================================================================
    {
        const double sinA = std::sin(kMorphAngleA);  // v158
        const double cosA = std::cos(kMorphAngleA);   // v159
        const double sinB = std::sin(kMorphAngleB);   // v160
        const double cosB = std::cos(kMorphAngleB);   // v161
        for (int i = 0; i < vertCount; ++i) {
            const float* s = verts[i].pos;
            double vx = s[0];                       // v25 / v164
            double vy = s[1];
            double vz = s[2];
            double v24 = vy * cosA;                 // vy*cosA
            double v26 = vy * sinA + vz * cosA;     // v167
            double v27 = vz * sinA;
            double v28 = v26 * cosB + (-vx) * sinB; // v168
            double v29 = (v26 * sinB + vx * cosB) * kMorphScaleZ;
            double v31 = v24 - v27;                 // v165 / out.y
            double v32 = v28 * cosB;
            float* d = out.vertices[i].pos;
            d[0] = static_cast<float>(v29 * cosB + v28 * sinB);
            d[1] = static_cast<float>(v31);
            d[2] = static_cast<float>(v32 + (-v29 * sinB));
        }
    }

    // =====================================================================
    // 3) MATERIAL DEDUP — two passes over the 224-byte material records.
    //    Pass 1: drop byte-identical materials (memcmp), remap poly +40.
    //    Pass 2: drop materials referenced by no polygon, compact, remap.
    // =====================================================================
    MeshMaterial* mats = pm.materials.data();  // v142
    if (matCount > 0) {
        // ---- pass 1: collapse identical material records ----
        // v162 = "claimed" flag array.
        std::vector<u8> claimed(static_cast<size_t>(matCount), 0);
        for (int a = 0; a < matCount; ++a) {
            if (claimed[a])
                continue;
            claimed[a] = 1;
            for (int b = a + 1; b < matCount; ++b) {
                if (std::memcmp(&mats[a], &mats[b], 224) == 0) {
                    for (int p = 0; p < polyCount; ++p) {
                        if (static_cast<int>(polys[p].matIndex) == b)  // +40
                            polys[p].matIndex = a;
                    }
                    claimed[b] = 1;
                }
            }
        }

        // ---- pass 2: mark referenced, then compact out the unreferenced ----
        std::vector<u8> used(static_cast<size_t>(matCount), 0);  // v162 (reused)
        for (int p = 0; p < polyCount; ++p) {
            int mi = static_cast<int>(polys[p].matIndex);  // +40
            if (mi >= 0)
                used[mi] = 1;
        }
        // Compact: walk the material list; for each unused entry remove it and
        // decrement every poly material index above the removal point.
        int remaining = matCount;  // v151
        for (int a = 0; a < remaining; ) {
            if (used[a]) {
                ++a;
                continue;
            }
            for (int p = 0; p < polyCount; ++p) {
                int mi = static_cast<int>(polys[p].matIndex);
                if (a < mi)
                    polys[p].matIndex = mi - 1;
            }
            --remaining;
            // MemMove the material tail + the flag tail up by one record.
            std::memmove(&mats[a], &mats[a + 1],
                         224 * static_cast<size_t>(remaining - a));
            std::memmove(&used[a], &used[a + 1],
                         static_cast<size_t>(remaining - a));
            // (Do NOT advance a.)
        }
        matCount = remaining;
        pm.materials.resize(static_cast<size_t>(matCount));
        mats = pm.materials.data();
    }

    // =====================================================================
    // Allocate the mesh poly array (56 * polyCount) and copy the polys in.
    // =====================================================================
    out.polyCount = polyCount;  // *(v155+19)
    out.polygons.assign(static_cast<size_t>(polyCount), MeshPolygon{});
    std::memcpy(out.polygons.data(), polys, sizeof(MeshPolygon) * static_cast<size_t>(polyCount));
    MeshPolygon* mpolys = out.polygons.data();

    // =====================================================================
    // 4) MATERIAL RE-ORDER — re-index materials in first-referenced order, copy
    //    them into a temp, and remap poly material indices to the new order.
    // =====================================================================
    std::vector<MeshMaterial> reordered;
    {
        std::vector<u8> placed(static_cast<size_t>(matCount), 0);  // v152
        std::vector<MeshMaterial> tmp(pm.materials.begin(), pm.materials.end());  // v156
        reordered.reserve(static_cast<size_t>(matCount));
        int next = 0;  // v69
        for (int p = 0; p < polyCount; ++p) {
            int mi = static_cast<int>(mpolys[p].matIndex);  // +40
            if (mi > -1 && !placed[mi]) {
                placed[mi] = 1;
                for (int q = 0; q < polyCount; ++q) {
                    if (static_cast<int>(mpolys[q].matIndex) == mi)
                        mpolys[q].matIndex = next;
                }
                reordered.push_back(tmp[mi]);
                ++next;
            }
        }
    }
    matCount = static_cast<int>(reordered.size());
    out.materialCount = matCount;

    // =====================================================================
    // 5) MATERIAL NAME BLOCK — pack the diffuse names (64-byte stride) and run
    //    the morph-bake of the polygon UV/normal slots (v142[+204..+220]).
    // =====================================================================
    // Bake the per-poly UV rotation (the v182 loop): for each poly with a valid
    // material index, transform its three (u,v) pairs through the material's
    // scale/offset/rotation, then offset by (uOffset, vOffset).
    for (int p = 0; p < polyCount; ++p) {
        int mi = static_cast<int>(mpolys[p].matIndex);  // +40
        if (mi < 0)
            continue;
        const MeshMaterial& mat = reordered[mi];
        float* uv = &mpolys[p].uv0[0];  // 3 (u,v) pairs at +0..; stride 8 bytes
        const double half = kDummyHalf;  // flt_6290E4
        for (int k = 0; k < 3; ++k) {
            float* pair = uv + 2 * k;  // (u,v)
            double u = pair[0];
            double v = pair[1];
            double vv = v * mat.vScale + half - mat.vScale * half;        // v89
            double uu = u * mat.uScale + half - mat.uScale * half;        // v90
            pair[0] = static_cast<float>(std::cos(mat.rotation) * uu -
                                         std::sin(mat.rotation) * vv + mat.uOffset);
            double ang = mat.rotation + kDummyPi;                          // v91
            pair[1] = static_cast<float>(uu * std::sin(ang) + vv * std::cos(ang) +
                                         mat.vOffset);
        }
    }

    // Build / take the packed material-name block (+516). The original either
    // adopts a caller-provided block (the morph-target fast path) or allocates a
    // fresh (frames * (matCount<<6)) byte block and packs the names into it.
    if (morphFlag && morphFrames > 0) {
        out.morphFrameCount = morphFrames;  // *(v155+121)
    } else {
        out.morphFrameCount = 1;            // *(v155+121) = 1
    }
    out.materialNames.assign(
        static_cast<size_t>(out.morphFrameCount) * static_cast<size_t>(matCount) * 64u, 0);
    // Pack each material's preferred name (name2 if present, else name0, else
    // name1) into the first frame's slots (64-byte stride).
    for (int i = 0; i < matCount; ++i) {
        const MeshMaterial& mat = reordered[i];
        const char* src;
        if (mat.name2[0])      src = mat.name2;
        else if (mat.name0[0]) src = mat.name0;
        else                   src = mat.name1;
        char* dst = out.materialNames.data() + static_cast<size_t>(i) * 64u;
        std::size_t n = 0;
        for (; n < 63 && src[n]; ++n)
            dst[n] = src[n];
        dst[n] = 0;
    }

    // =====================================================================
    // 6) TEXTURE RESOLUTION — assemble the per-material flag words exactly as the
    //    original, resolve each via TextureLoadByName, and store the slot id into
    //    every referencing polygon's +36 (texId).
    // =====================================================================
    {
        std::vector<u8> done(static_cast<size_t>(polyCount), 0);  // v189
        for (int i = 0; i < matCount; ++i) {
            const MeshMaterial& mat = reordered[i];
            // Assemble the flag word (v171) byte-by-byte, matching the original.
            u32 flag0 = 255;  // v171 seeded 0x000000FF (low byte default 255)
            if (mat.presentFlag) {              // v142[+192]
                flag0 = (flag0 & 0xFFFFFF00u) | mat.paletteByte;  // LOBYTE = +193
                if (mat.presentFlag == 2)
                    flag0 |= (1u << 16);        // BYTE2 |= 1
            }
            // BYTE1 = +199
            flag0 = (flag0 & 0xFFFF00FFu) | (static_cast<u32>(mat.byte1) << 8);
            // BYTE2 = (2*(+194 & 1)) | (BYTE2 & 0xFD)
            {
                u32 b2 = (flag0 >> 16) & 0xFF;
                b2 = (2u * (mat.blendBit & 1u)) | (b2 & 0xFDu);
                flag0 = (flag0 & 0xFF00FFFFu) | (b2 << 16);
            }
            u32 flag1 = static_cast<u32>(mat.lowNibble | (16 * mat.mul16));  // v97
            u32 flag2;  // v98
            if (mat.presentFlag) {  // original tests v142[+128] (name2 present);
                                    // both branches differ only by the |1 bit.
                flag2 = (2u * mat.mul2) | 1u | (4u * mat.mul4) |
                        (static_cast<u32>(mat.shiftHi) << 6);
            } else {
                flag2 = (4u * mat.mul4) | (2u * mat.mul2) |
                        (static_cast<u32>(mat.shiftHi) << 6);
            }
            // Diffuse name: the packed name block slot for this material.
            const char* texName = out.materialNames.data() + static_cast<size_t>(i) * 64u;
            int slot = TextureLoadByName(texName, flag0, flag1, flag2);

            for (int p = 0; p < polyCount; ++p) {
                if (done[p])
                    continue;
                if (static_cast<int>(mpolys[p].matIndex) == i) {  // +40
                    mpolys[p].texId = slot;  // +36
                    done[p] = 1;
                }
            }
        }
    }

    // =====================================================================
    // 7) DUMMY/LOCATOR COPY — copy the parsed 88-byte dummy records into the
    //    mesh dummy array (88-byte stride at mesh +116).
    // =====================================================================
    int dummyCount = static_cast<int>(pm.dummies.size());  // v147
    out.dummies.assign(static_cast<size_t>(dummyCount), MeshDummy{});
    for (int i = 0; i < dummyCount; ++i) {
        const MeshDummySource& src = pm.dummies[i];
        MeshDummy& d = out.dummies[i];
        StrNCopyPad(d.name, src.name, 63);
        d.f92  = src.pos[0];                               // +92  <- src +64
        d.f96  = src.pos[1];                               // +96  <- src +68
        std::memcpy(&d.d100, &src.pos[2], 4);              // +100 <- src +72
        std::memcpy(&d.d104, &src.rot[0], 4);              // +104 <- src +76
        std::memcpy(&d.d108, &src.rot[1], 4);              // +108 <- src +80
        std::memcpy(&d.d112, &src.rot[2], 4);              // +112 <- src +84
    }

    // =====================================================================
    // 8) FINAL GEOMETRY PASSES.
    // =====================================================================
    ComputeBoundingExtents(out);
    ComputeVertexNormals(out);
    return true;
}

// Adapt a fast-chunk BgfModel into a ParsedModel.
void BuildParsedFromBgf(const BgfModel& m, ParsedModel& out) {
    out.skipVertexDedup = false;

    // Vertices: BgfVertex (pos[3], normal[3]) maps 1:1 to MeshVertex. The
    // fast-chunk reader over-allocates 8 slack slots; the orchestrator works on
    // the LOGICAL vertex count, so copy only m.vertexCount records.
    int vcount = static_cast<int>(m.vertexCount);
    out.vertices.assign(static_cast<size_t>(vcount), MeshVertex{});
    for (int i = 0; i < vcount; ++i) {
        out.vertices[i].pos[0] = m.vertices[i].pos[0];
        out.vertices[i].pos[1] = m.vertices[i].pos[1];
        out.vertices[i].pos[2] = m.vertices[i].pos[2];
        out.vertices[i].normal[0] = m.vertices[i].normal[0];
        out.vertices[i].normal[1] = m.vertices[i].normal[1];
        out.vertices[i].normal[2] = m.vertices[i].normal[2];
    }

    // Polygons.
    out.polygons.assign(m.polygons.size(), MeshPolygon{});
    for (size_t i = 0; i < m.polygons.size(); ++i) {
        const BgfPolygon& q = m.polygons[i];
        MeshPolygon& p = out.polygons[i];
        p.uv0[0] = q.uv0[0]; p.uv0[1] = q.uv0[1]; p.uv0[2] = q.uv0[2];
        p.uv1[0] = q.uv1[0]; p.uv1[1] = q.uv1[1]; p.uv1[2] = q.uv1[2];
        p.vtx[0] = q.vtx[0]; p.vtx[1] = q.vtx[1]; p.vtx[2] = q.vtx[2];
        p.texId = -1;
        p.matIndex = q.matIndex;
        // uv2 carried into the face-normal slot region until ComputeVertexNormals
        // overwrites it (the on-disk uv2 lives at +44 in the raw record).
        p.normal[0] = q.uv2[0]; p.normal[1] = q.uv2[1]; p.normal[2] = q.uv2[2];
    }

    // Materials: the fast-chunk BgfMaterial carries three names + 6 flag bytes.
    // Map them onto the 224-byte MeshMaterial (names + the flag bytes the texture
    // stage reads; identity UV transform so the bake is a no-op for fast-chunk).
    out.materials.assign(m.materials.size(), MeshMaterial{});
    for (size_t i = 0; i < m.materials.size(); ++i) {
        const BgfMaterial& bm = m.materials[i];
        MeshMaterial& mm = out.materials[i];
        StrNCopyPad(mm.name0, bm.name0.c_str(), 63);
        StrNCopyPad(mm.name1, bm.name1.c_str(), 63);
        StrNCopyPad(mm.name2, bm.name2.c_str(), 63);
        mm.presentFlag = bm.flag;
        mm.paletteByte = bm.b1;
        mm.blendBit    = bm.b2;
        mm.shiftHi     = bm.b3;
        mm.mul2        = bm.b4;
        mm.mul4        = bm.b5;
        mm.uScale = 1.0f; mm.vScale = 1.0f;  // identity UV transform
        mm.uOffset = 0.0f; mm.vOffset = 0.0f; mm.rotation = 0.0f;
    }

    // Dummies.
    out.dummies.assign(m.dummies.size(), MeshDummySource{});
    for (size_t i = 0; i < m.dummies.size(); ++i) {
        std::memcpy(out.dummies[i].name, m.dummies[i].name, 64);
        out.dummies[i].pos[0] = m.dummies[i].pos[0];
        out.dummies[i].pos[1] = m.dummies[i].pos[1];
        out.dummies[i].pos[2] = m.dummies[i].pos[2];
        out.dummies[i].rot[0] = m.dummies[i].rot[0];
        out.dummies[i].rot[1] = m.dummies[i].rot[1];
        out.dummies[i].rot[2] = m.dummies[i].rot[2];
    }
}

bool LoadBgfFile(const u8* data, size_t size, const std::string& name, Mesh& out) {
    BgfModel bm;
    if (!LoadFastChunk(data, size, bm))
        return false;
    ParsedModel pm;
    BuildParsedFromBgf(bm, pm);
    // Fast-chunk meshes are not morph-targets; frames=1, flag off.
    return LoadBgfPostProcess(pm, name, /*morphFrames=*/0, /*morphFlag=*/false, out);
}

} // namespace guild::render
