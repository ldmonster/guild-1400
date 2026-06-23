#include "render/mesh_load.h"
#include "render/mesh_postprocess.h"
#include "tests/framework/test.h"

#include <cmath>
#include <cstring>

using namespace guild;
using namespace guild::render;

// Mock texture loader (the orchestrator forward-declares this). Records the
// names/flags it was asked to load and returns a deterministic slot id. This is
// the single shared definition for the whole test binary (the e2e file uses the
// same globals via extern); set g_texFixed >= 0 to force a constant slot.
#include "render/texture_loader.h"  // shared TextureLoadByName + g_texLoadRequests

static bool feq(float a, float b, float eps = 1e-4f) { return std::fabs(a - b) <= eps; }

// ---- ComputeVertexNormals: golden triangle (normal +Z) --------------------
TEST(MeshLoadUnit, VertexNormalsTriangle) {
    Mesh m;
    m.vertexCount = 3;
    m.vertices.assign(3 + 8, MeshVertex{});
    m.vertices[0].pos[0] = 0; m.vertices[0].pos[1] = 0; m.vertices[0].pos[2] = 0;
    m.vertices[1].pos[0] = 1; m.vertices[1].pos[1] = 0; m.vertices[1].pos[2] = 0;
    m.vertices[2].pos[0] = 0; m.vertices[2].pos[1] = 1; m.vertices[2].pos[2] = 0;
    m.polygons.assign(1, MeshPolygon{});
    m.polygons[0].vtx[0] = 0; m.polygons[0].vtx[1] = 1; m.polygons[0].vtx[2] = 2;
    m.polyCount = 1;

    ComputeVertexNormals(m);

    // Face normal +Z (python golden: [0,0,1]).
    CHECK(feq(m.polygons[0].normal[0], 0.0f));
    CHECK(feq(m.polygons[0].normal[1], 0.0f));
    CHECK(feq(m.polygons[0].normal[2], 1.0f));
    // Each referenced vertex gets the same averaged normal.
    for (int i = 0; i < 3; ++i) {
        CHECK(feq(m.vertices[i].normal[0], 0.0f));
        CHECK(feq(m.vertices[i].normal[1], 0.0f));
        CHECK(feq(m.vertices[i].normal[2], 1.0f));
    }
}

// ---- ComputeBoundingExtents: unit cube ------------------------------------
TEST(MeshLoadUnit, BoundingExtentsCube) {
    // 8 cube corners at +/-1 (already in mesh space, no morph here).
    const float c[8][3] = {
        {-1,-1,-1},{1,-1,-1},{1,1,-1},{-1,1,-1},
        {-1,-1,1},{1,-1,1},{1,1,1},{-1,1,1}};
    Mesh m;
    m.vertexCount = 8;
    m.vertices.assign(8 + 8, MeshVertex{});
    for (int i = 0; i < 8; ++i) {
        m.vertices[i].pos[0] = c[i][0];
        m.vertices[i].pos[1] = c[i][1];
        m.vertices[i].pos[2] = c[i][2];
    }

    ComputeBoundingExtents(m);

    // HARDEN (gilde.exe 0x5d1ff5 / 0x5d1bbe): +472 (m.radius) ends as the AABB
    // diagonal length; +468 (m.radius2) holds the max-|vertex| value. The prior
    // assertions had these two fields swapped vs the binary.
    CHECK(feq(m.radius, 2.0f * std::sqrt(3.0f)));  // +472 = AABB diagonal
    CHECK(feq(m.radius2, std::sqrt(3.0f)));        // +468 = max |vertex|
    CHECK(feq(m.centroid[0], 0.0f));
    CHECK(feq(m.centroid[1], 0.0f));
    CHECK(feq(m.centroid[2], 0.0f));
    // 8 corner verts materialised into the slack slots: corner 7 = (max,max,max).
    CHECK(feq(m.vertices[8 + 7].pos[0], 1.0f));
    CHECK(feq(m.vertices[8 + 7].pos[1], 1.0f));
    CHECK(feq(m.vertices[8 + 7].pos[2], 1.0f));
    CHECK(feq(m.vertices[8 + 0].pos[0], -1.0f));  // corner 0 = (min,min,min)
}

// ---- Vertex dedup collapses duplicates ------------------------------------
TEST(MeshLoadUnit, VertexDedupCollapses) {
    ParsedModel pm;
    // 4 vertices, #2 is a duplicate of #0 (within tolerance).
    pm.vertices.assign(4, MeshVertex{});
    pm.vertices[0].pos[0] = 0; pm.vertices[0].pos[1] = 0; pm.vertices[0].pos[2] = 0;
    pm.vertices[1].pos[0] = 1; pm.vertices[1].pos[1] = 0; pm.vertices[1].pos[2] = 0;
    pm.vertices[2].pos[0] = 0.0005f; pm.vertices[2].pos[1] = 0; pm.vertices[2].pos[2] = 0; // dup of 0
    pm.vertices[3].pos[0] = 0; pm.vertices[3].pos[1] = 1; pm.vertices[3].pos[2] = 0;
    // One triangle referencing the duplicate index 2.
    pm.polygons.assign(1, MeshPolygon{});
    pm.polygons[0].vtx[0] = 1; pm.polygons[0].vtx[1] = 2; pm.polygons[0].vtx[2] = 3;
    pm.polygons[0].matIndex = -1;

    Mesh m;
    g_texLoadRequests.clear(); g_texFixed = -1;
    bool ok = LoadBgfPostProcess(pm, "DUP", 0, false, m);
    CHECK(ok);
    // 4 verts -> 3 after dedup.
    CHECK_EQ(m.vertexCount, 3);
    // The poly's middle index, formerly 2 (dup of 0), is remapped to 0; index 3
    // shifts down to 2 (removal of slot 2).
    CHECK_EQ((int)m.polygons[0].vtx[0], 1);
    CHECK_EQ((int)m.polygons[0].vtx[1], 0);
    CHECK_EQ((int)m.polygons[0].vtx[2], 2);
}

// ---- Material dedup collapses identical records ---------------------------
TEST(MeshLoadUnit, MaterialDedupAndResolve) {
    ParsedModel pm;
    pm.skipVertexDedup = true;  // keep geometry untouched
    pm.vertices.assign(3, MeshVertex{});
    pm.vertices[1].pos[0] = 1;
    pm.vertices[2].pos[1] = 1;
    // Two polys; two materials that are byte-identical -> collapse to one.
    pm.polygons.assign(2, MeshPolygon{});
    pm.polygons[0].vtx[0]=0; pm.polygons[0].vtx[1]=1; pm.polygons[0].vtx[2]=2;
    pm.polygons[1].vtx[0]=0; pm.polygons[1].vtx[1]=1; pm.polygons[1].vtx[2]=2;
    pm.polygons[0].matIndex = 0;
    pm.polygons[1].matIndex = 1;
    pm.materials.assign(2, MeshMaterial{});
    std::strcpy(pm.materials[0].name0, "STONE");
    pm.materials[0].uScale = 1; pm.materials[0].vScale = 1;
    pm.materials[1] = pm.materials[0];  // identical bytes

    Mesh m;
    g_texLoadRequests.clear(); g_texFixed = -1;
    g_texNextSlot = 100;
    bool ok = LoadBgfPostProcess(pm, "MAT", 0, false, m);
    CHECK(ok);
    CHECK_EQ(m.materialCount, 1);            // collapsed to a single material
    CHECK_EQ((int)g_texLoadRequests.size(), 1);     // resolved exactly once
    CHECK(g_texLoadRequests[0].name == "STONE");
    // Both polys resolve to the same texture slot.
    CHECK_EQ((int)m.polygons[0].texId, 100);
    CHECK_EQ((int)m.polygons[1].texId, 100);
    CHECK_EQ((int)m.polygons[0].matIndex, 0);
    CHECK_EQ((int)m.polygons[1].matIndex, 0);
}

// ---- Morph-bake axis swap (python golden) ---------------------------------
TEST(MeshLoadUnit, MorphBakeAxisSwap) {
    ParsedModel pm;
    pm.skipVertexDedup = true;
    pm.vertices.assign(3, MeshVertex{});
    pm.vertices[0].pos[0]=1; pm.vertices[0].pos[1]=0; pm.vertices[0].pos[2]=0;
    pm.vertices[1].pos[0]=0; pm.vertices[1].pos[1]=1; pm.vertices[1].pos[2]=0;
    pm.vertices[2].pos[0]=0; pm.vertices[2].pos[1]=0; pm.vertices[2].pos[2]=1;
    pm.polygons.assign(1, MeshPolygon{});
    pm.polygons[0].vtx[0]=0; pm.polygons[0].vtx[1]=1; pm.polygons[0].vtx[2]=2;
    pm.polygons[0].matIndex = -1;

    Mesh m;
    g_texLoadRequests.clear(); g_texFixed = -1;
    bool ok = LoadBgfPostProcess(pm, "MORPH", 0, false, m);
    CHECK(ok);
    // python golden: morph(1,0,0)=(-1,0,0); morph(0,1,0)=(0,~0,-1); morph(0,0,1)=(0,1,~0)
    CHECK(feq(m.vertices[0].pos[0], -1.0f)); CHECK(feq(m.vertices[0].pos[1], 0.0f));  CHECK(feq(m.vertices[0].pos[2], 0.0f));
    CHECK(feq(m.vertices[1].pos[0], 0.0f));  CHECK(feq(m.vertices[1].pos[1], 0.0f));  CHECK(feq(m.vertices[1].pos[2], -1.0f));
    CHECK(feq(m.vertices[2].pos[0], 0.0f));  CHECK(feq(m.vertices[2].pos[1], 1.0f));  CHECK(feq(m.vertices[2].pos[2], 0.0f));
}
