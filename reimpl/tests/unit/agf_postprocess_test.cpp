// tests/unit/agf_postprocess_test.cpp — AGF post-process stages on hand-built models.
//
// Asserts: (1) vertex dedup collapses near-duplicates and remaps poly indices,
// (2) the morph bake transforms a known vertex to the expected (-x,z,-y),
// (3) material dedup collapses byte-identical materials, drops unused ones, and
// reorders by first poly use — keeping poly matIndex consistent.
#include "test.h"

#include "render/agf_postprocess.h"
#include "render/bgf_loader.h"

#include <cmath>

using namespace guild;

namespace {

render::BgfVertex MkV(float x, float y, float z) {
    render::BgfVertex v{};
    v.pos[0] = x; v.pos[1] = y; v.pos[2] = z;
    return v;
}

render::BgfPolygon MkP(u32 a, u32 b, u32 c, i32 mat) {
    render::BgfPolygon p{};
    p.vtx[0] = a; p.vtx[1] = b; p.vtx[2] = c;
    p.matIndex = mat;
    p.texId = -1;
    return p;
}

render::BgfMaterial MkM(const char* n0, u8 flag) {
    render::BgfMaterial m{};
    m.name0 = n0;
    m.flag = flag;
    return m;
}

bool Near(float a, float b) { return std::fabs(a - b) < 1e-4f; }

} // namespace

// ---------------------------------------------------------------------------
TEST(AgfPostProcess, VectorTolerance) {
    float a[3] = {1.0f, 2.0f, 3.0f};
    float b[3] = {1.0005f, 2.0f, 3.0f};   // within 0.001
    float c[3] = {1.01f, 2.0f, 3.0f};     // outside
    CHECK(render::VectorWithinTolerance(a, b, render::kAgfVertexMergeTolerance));
    CHECK(!render::VectorWithinTolerance(a, c, render::kAgfVertexMergeTolerance));
}

// ---------------------------------------------------------------------------
TEST(AgfPostProcess, DedupCollapsesAndRemaps) {
    render::BgfModel m;
    // v0 and v2 are near-duplicates; v3 is unique. Expected result: 3 verts.
    m.vertices = {
        MkV(0.0f, 0.0f, 0.0f),       // 0  keep
        MkV(1.0f, 0.0f, 0.0f),       // 1
        MkV(0.0002f, 0.0f, 0.0f),    // 2  duplicate of 0 (within 0.001)
        MkV(2.0f, 0.0f, 0.0f),       // 3
    };
    m.vertexCount = 4;
    // Poly references the duplicate (2) and a higher index (3).
    m.polygons = { MkP(2, 1, 3, 0) };
    m.polyCount = 1;

    u32 removed = render::DeduplicateVertices(m);
    CHECK_EQ((int)removed, 1);
    CHECK_EQ((int)m.vertexCount, 3);
    CHECK_EQ((int)m.vertices.size(), 3);

    // Index 2 -> 0 (merged); index 3 -> 2 (shifted down past removed slot 2);
    // index 1 unchanged.
    if (!m.polygons.empty()) {
        CHECK_EQ((int)m.polygons[0].vtx[0], 0);
        CHECK_EQ((int)m.polygons[0].vtx[1], 1);
        CHECK_EQ((int)m.polygons[0].vtx[2], 2);
    }
    // Surviving positions remain in range.
    for (const auto& p : m.polygons)
        for (int k = 0; k < 3; ++k)
            CHECK(p.vtx[k] < m.vertices.size());
}

// ---------------------------------------------------------------------------
TEST(AgfPostProcess, MorphBakeKnownVertex) {
    render::BgfModel m;
    m.vertices = { MkV(2.0f, 3.0f, 5.0f) };
    m.vertexCount = 1;

    render::BakeMorphRotation(m);

    // With dbl_6290CC=-PI/2, dbl_6290D4=PI, dbl_6290DC=-1.0 the algebra reduces to
    // (x,y,z) -> (-x, z, -y).
    if (!m.vertices.empty()) {
        CHECK(Near(m.vertices[0].pos[0], -2.0f));
        CHECK(Near(m.vertices[0].pos[1],  5.0f));
        CHECK(Near(m.vertices[0].pos[2], -3.0f));
    }
}

// ---------------------------------------------------------------------------
TEST(AgfPostProcess, MorphConstantsRecovered) {
    // Spot-check the recovered constants decode to the expected values.
    CHECK(Near((float)std::sin(render::kAgfMorphAngleA), -1.0f)); // sin(-PI/2)
    CHECK(Near((float)std::cos(render::kAgfMorphAngleA),  0.0f)); // cos(-PI/2)
    CHECK(Near((float)std::sin(render::kAgfMorphAngleB),  0.0f)); // sin(PI)
    CHECK(Near((float)std::cos(render::kAgfMorphAngleB), -1.0f)); // cos(PI)
    CHECK(Near((float)render::kAgfMorphZScale, -1.0f));
}

// ---------------------------------------------------------------------------
TEST(AgfPostProcess, MaterialDedupIdentical) {
    render::BgfModel m;
    // mat0 and mat2 are byte-identical; mat1 distinct. All three referenced.
    m.materials = { MkM("brick", 1), MkM("wood", 2), MkM("brick", 1) };
    m.materialCount = 3;
    m.polygons = { MkP(0,1,2, 0), MkP(0,1,2, 1), MkP(0,1,2, 2) };
    m.polyCount = 3;

    render::DeduplicateMaterials(m);

    // mat2 collapses into mat0; mat1 distinct -> 2 materials survive.
    CHECK_EQ((int)m.materialCount, 2);
    CHECK_EQ((int)m.materials.size(), 2);
    // Every poly matIndex stays in range and points at a valid material.
    for (const auto& p : m.polygons) {
        CHECK(p.matIndex >= 0);
        CHECK(p.matIndex < (i32)m.materials.size());
    }
    // The two surviving names are exactly {brick, wood} in first-use order:
    // poly0 uses brick first, poly1 uses wood -> [brick, wood].
    if (m.materials.size() == 2) {
        CHECK(m.materials[0].name0 == "brick");
        CHECK(m.materials[1].name0 == "wood");
    }
}

// ---------------------------------------------------------------------------
TEST(AgfPostProcess, MaterialDropUnusedAndReorder) {
    render::BgfModel m;
    // 3 distinct materials, but only mat2 and mat0 are referenced (mat1 unused).
    // First poly uses mat2, second uses mat0 -> reorder to [stone, grass].
    m.materials = { MkM("grass", 1), MkM("UNUSED", 9), MkM("stone", 3) };
    m.materialCount = 3;
    m.polygons = { MkP(0,1,2, 2), MkP(0,1,2, 0) };
    m.polyCount = 2;

    u32 removed = render::DeduplicateMaterials(m);
    CHECK_EQ((int)removed, 1);                 // UNUSED dropped
    CHECK_EQ((int)m.materialCount, 2);
    if (m.materials.size() == 2) {
        CHECK(m.materials[0].name0 == "stone"); // first poly used stone
        CHECK(m.materials[1].name0 == "grass");
    }
    // poly0 -> 0 (stone), poly1 -> 1 (grass).
    if (m.polygons.size() == 2) {
        CHECK_EQ((int)m.polygons[0].matIndex, 0);
        CHECK_EQ((int)m.polygons[1].matIndex, 1);
    }
}

// ---------------------------------------------------------------------------
TEST(AgfPostProcess, FullPipelineConsistent) {
    render::BgfModel m;
    m.vertices = { MkV(0,0,0), MkV(1,0,0), MkV(0.0001f,0,0), MkV(0,1,0) };
    m.vertexCount = 4;
    m.materials = { MkM("a",1), MkM("a",1), MkM("b",2) };
    m.materialCount = 3;
    m.polygons = { MkP(2,1,3, 1), MkP(0,1,3, 2) };
    m.polyCount = 2;

    render::PostProcessModel(m);

    // vertex 2 merged into 0 -> 3 verts.
    CHECK_EQ((int)m.vertexCount, 3);
    // mat1 collapses into mat0 (identical "a") -> 2 materials.
    CHECK_EQ((int)m.materialCount, 2);
    // No out-of-range indices anywhere.
    for (const auto& p : m.polygons) {
        for (int k = 0; k < 3; ++k)
            CHECK(p.vtx[k] < m.vertices.size());
        CHECK(p.matIndex >= 0);
        CHECK(p.matIndex < (i32)m.materials.size());
    }
}
