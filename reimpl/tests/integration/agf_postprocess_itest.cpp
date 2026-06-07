// tests/integration/agf_postprocess_itest.cpp — parse a synthetic AGF buffer via
// the real LoadAgfModel, then run PostProcessModel and assert counts shrink as
// expected and geometry stays consistent (no out-of-range indices, finite bbox).
//
// The synthetic buffer (same grammar the agf_loader unit test uses) is crafted
// with two coincident vertices and two byte-identical materials so the dedup
// stages have real work to do.
#include "test.h"

#include "render/agf_loader.h"
#include "render/agf_postprocess.h"

#include <cmath>
#include <cstring>
#include <vector>

using namespace guild;
using namespace guild::render;

namespace {

struct Builder {
    std::vector<u8> b;
    void byte(u8 v) { b.push_back(v); }
    void u32v(u32 v) { b.push_back(v & 0xff); b.push_back((v >> 8) & 0xff);
                       b.push_back((v >> 16) & 0xff); b.push_back((v >> 24) & 0xff); }
    void f32v(float f) { u32 bits; std::memcpy(&bits, &f, 4); u32v(bits); }
    void str(const char* s) { while (*s) b.push_back((u8)*s++); b.push_back(0); }
    void vtx(float x, float y, float z) { f32v(x); f32v(y); f32v(z); }
    void magic() { b.push_back('B'); b.push_back('G'); b.push_back('F'); b.push_back(0); }
};

// 4 vertices (v0 and v2 coincident), 2 polys, 3 materials (m0 and m2 identical;
// m1 distinct but referenced so it survives dedup). Both polys deliberately use
// distinct material indices so the reorder pass has ordering to do.
std::vector<u8> BuildDupedModel() {
    Builder w;
    w.magic();
    w.byte(0x2e);            // '.' version marker
    w.u32v(0x000100u);

    // ---- materials ----
    w.byte(0x03);            // enter T3
      w.byte(0x04); w.u32v(3u);              // alloc 3 materials
      // m0
      w.byte(0x05); w.byte(0x07); w.str("brick.bmp"); w.byte(0x0a); w.byte(1); w.byte(0x28);
      // m1
      w.byte(0x05); w.byte(0x07); w.str("wood.bmp");  w.byte(0x0a); w.byte(2); w.byte(0x28);
      // m2 (identical to m0)
      w.byte(0x05); w.byte(0x07); w.str("brick.bmp"); w.byte(0x0a); w.byte(1); w.byte(0x28);
    w.byte(0x27);            // end T3

    // ---- geometry ----
    w.byte(0x14);
      w.byte(0x17);
        w.byte(0x18); w.u32v(4u);            // count
        w.byte(0x19); w.u32v(4u);            // alloc points
        w.byte(0x1a); w.u32v(2u);            // alloc polys
        w.byte(0x1b);                        // read 4 points
          w.vtx(0.0f, 0.0f, 0.0f);           // v0
          w.vtx(1.0f, 0.0f, 0.0f);           // v1
          w.vtx(0.0f, 0.0f, 0.0f);           // v2 == v0 (duplicate)
          w.vtx(0.0f, 1.0f, 0.0f);           // v3
        w.byte(0x1c);                        // poly records
          // poly0 uses verts (2,1,3) and material index 2 (-> identical to m0)
          w.byte(0x1d); w.u32v(2); w.u32v(1); w.u32v(3);
          w.byte(0x1e);
            w.f32v(0.1f); w.f32v(0.2f); w.f32v(0.3f); w.f32v(0.4f); w.f32v(0.5f); w.f32v(0.6f);
            w.u32v(0); w.u32v(0); w.u32v(0);
          w.byte(0x20); w.byte(2);           // mat index 2
          // poly1 uses verts (0,1,3) and material index 1 (wood)
          w.byte(0x1d); w.u32v(0); w.u32v(1); w.u32v(3);
          w.byte(0x1e);
            w.f32v(0.1f); w.f32v(0.2f); w.f32v(0.3f); w.f32v(0.4f); w.f32v(0.5f); w.f32v(0.6f);
            w.u32v(0); w.u32v(0); w.u32v(0);
          w.byte(0x20); w.byte(1);           // mat index 1
        w.byte(0x27);
      w.byte(0x27);
    w.byte(0x27);
    w.byte(0x2b);            // '+' end stream
    return w.b;
}

bool Finite(float f) { return f == f && std::fabs(f) < 1e30f; }

} // namespace

TEST(AgfPostProcessItest, CountsShrinkAndConsistent) {
    std::vector<u8> buf = BuildDupedModel();
    BgfModel m;
    CHECK(LoadAgfModel(buf.data(), buf.size(), m));

    const u32 rawV = m.vertexCount;
    const u32 rawM = m.materialCount;
    const u32 rawP = m.polyCount;
    std::printf("  raw: %u verts / %u polys / %u materials\n", rawV, rawP, rawM);
    CHECK_EQ((int)rawV, 4);
    CHECK_EQ((int)rawM, 3);
    CHECK_EQ((int)rawP, 2);

    PostProcessModel(m);

    std::printf("  post: %u verts / %u polys / %u materials\n",
                m.vertexCount, m.polyCount, m.materialCount);

    // v2 collapses into v0 -> 3 verts.
    CHECK(m.vertexCount < rawV);
    CHECK_EQ((int)m.vertexCount, 3);
    // m2 collapses into m0 -> 2 materials (brick, wood).
    CHECK(m.materialCount < rawM);
    CHECK_EQ((int)m.materialCount, 2);
    // Poly count is unchanged by post-process.
    CHECK_EQ((int)m.polyCount, (int)rawP);

    // No out-of-range vertex indices.
    int vOob = 0;
    for (const auto& p : m.polygons)
        for (int k = 0; k < 3; ++k)
            if (p.vtx[k] >= m.vertices.size()) ++vOob;
    CHECK_EQ(vOob, 0);

    // Every poly material index in range (or -1 for "none").
    int mOob = 0;
    for (const auto& p : m.polygons)
        if (p.matIndex >= (i32)m.materials.size()) ++mOob;
    CHECK_EQ(mOob, 0);

    // Bbox finite after the morph bake.
    BgfBounds bb = ComputeBoundingExtents(m);
    for (int k = 0; k < 3; ++k) { CHECK(Finite(bb.min[k])); CHECK(Finite(bb.max[k])); }
    CHECK(Finite(bb.radius));
}

TEST(AgfPostProcessItest, IdempotentOnSecondRun) {
    std::vector<u8> buf = BuildDupedModel();
    BgfModel m;
    CHECK(LoadAgfModel(buf.data(), buf.size(), m));
    PostProcessModel(m);
    const u32 v1 = m.vertexCount, m1 = m.materialCount;

    // Running dedup again must not collapse anything further (no dup remains).
    u32 vr = DeduplicateVertices(m);
    u32 mr = DeduplicateMaterials(m);
    CHECK_EQ((int)vr, 0);
    CHECK_EQ((int)mr, 0);
    CHECK_EQ((int)m.vertexCount, (int)v1);
    CHECK_EQ((int)m.materialCount, (int)m1);
}
