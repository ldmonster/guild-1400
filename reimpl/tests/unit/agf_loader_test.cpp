// tests/unit/agf_loader_test.cpp — AGF token-script mesh loader (unit).
//
// Feeds hand-built minimal AGF byte buffers crafted to the recovered grammar and
// asserts parsed counts/fields, plus ReadToken / parser edge cases. No assets.
#include "test.h"

#include "render/agf_loader.h"

#include <cstring>
#include <vector>

using namespace guild;
using namespace guild::render;

namespace {

// Little-endian byte-buffer builder mirroring the on-disk AGF field encoding.
struct Builder {
    std::vector<u8> b;
    void byte(u8 v) { b.push_back(v); }
    void u32v(u32 v) { b.push_back(v & 0xff); b.push_back((v >> 8) & 0xff);
                       b.push_back((v >> 16) & 0xff); b.push_back((v >> 24) & 0xff); }
    void f32v(float f) { u32 bits; std::memcpy(&bits, &f, 4); u32v(bits); }
    void str(const char* s) { while (*s) b.push_back((u8)*s++); b.push_back(0); }
    void magic() { b.push_back('B'); b.push_back('G'); b.push_back('F'); b.push_back(0); }
};

// Build a minimal AGF buffer: header + a geometry sub-tree (tok 0x14 -> 0x17) with
// one point block (nv verts), one poly block (np polys), plus a material block.
// Token map (from the recovered tables):
//   TOP: 0x14 -> geometry sub (T14); 0x03 -> T3 (materials)
//   T14: 0x17 -> T17 (point/poly groups)
//   T17: 0x18 count, 0x19 alloc points (count), 0x1a alloc polys (count),
//        0x1b read points, 0x1c -> T1c, '(' reset
//   T1c: 0x1d vtx idx triple, 0x1e uv rows (6 floats + 3 discard), 0x1f 3-discard,
//        0x20 mat index byte, then poly advances
//   T3:  0x04 alloc materials (count), 0x05 -> T5mat; T5mat '(' next material
std::vector<u8> BuildMinimal(int nv, int np, int nmat) {
    Builder w;
    w.magic();
    w.byte(0x2e);          // '.' version marker
    w.u32v(0x000100u);     // version dword

    // ---- materials (TOP tok 0x03 -> T3) ----
    w.byte(0x03);          // enter T3
      w.byte(0x04); w.u32v((u32)nmat);  // alloc materials
      for (int mi = 0; mi < nmat; ++mi) {
        w.byte(0x05);                    // enter T5mat (one material)
          w.byte(0x07); w.str(mi == 0 ? "stone.bmp" : "wood.bmp");  // name (ext stripped)
          w.byte(0x0a); w.byte((u8)(mi + 1));                       // byteA flag
        w.byte(0x28);                    // '(' -> next material, end T5mat block
      }
    w.byte(0x27);          // '\'' end T3 block

    // ---- geometry (TOP tok 0x14 -> T14 -> T17) ----
    w.byte(0x14);          // enter T14
      w.byte(0x17);        // enter T17 (one point/poly group)
        w.byte(0x18); w.u32v((u32)nv);   // count thunk (+4)
        w.byte(0x19); w.u32v((u32)nv);   // alloc points block (vtxBlock)
        w.byte(0x1a); w.u32v((u32)np);   // alloc polys block (polyBlock)
        w.byte(0x1b);                    // read nv points (3 floats each)
          for (int i = 0; i < nv; ++i) { w.f32v((float)i); w.f32v((float)(i*2)); w.f32v((float)(i*3)); }
        w.byte(0x1c);                    // enter T1c (poly records)
          for (int p = 0; p < np; ++p) {
            w.byte(0x1d); w.u32v(0); w.u32v(1 % (nv > 1 ? nv : 1)); w.u32v(2 % (nv > 2 ? nv : 1)); // vtx idx
            w.byte(0x1e);                // uv rows: 6 floats + 3 discard dwords
              w.f32v(0.1f); w.f32v(0.2f); w.f32v(0.3f); w.f32v(0.4f); w.f32v(0.5f); w.f32v(0.6f);
              w.u32v(0); w.u32v(0); w.u32v(0);
            w.byte(0x20); w.byte((u8)(p % (nmat > 0 ? nmat : 1)));  // mat index byte -> advances poly
          }
        w.byte(0x27);      // end T1c block
      w.byte(0x27);        // end T17 block
    w.byte(0x27);          // end T14 block
    w.byte(0x2b);          // '+' end stream
    return w.b;
}

} // namespace

TEST(AgfLoader, ReadTokenEdgeCases) {
    // EOF -> '+'; byte > 0x3A -> '\''; small byte -> itself.
    const u8 buf[] = {0x05, 0x7e, 0x2e};
    const u8* p = buf;
    const u8* end = buf + 3;
    CHECK_EQ((int)AgfReadToken(&p, end), 0x05);   // raw small byte
    CHECK_EQ((int)AgfReadToken(&p, end), 0x27);   // 0x7e > 0x3A -> '\''
    CHECK_EQ((int)AgfReadToken(&p, end), 0x2e);   // raw '.'
    CHECK_EQ((int)AgfReadToken(&p, end), 0x2b);   // EOF -> '+'
    CHECK(p == end);                              // no over-read
}

TEST(AgfLoader, RejectsBadMagic) {
    BgfModel m;
    const u8 bad[] = {'X', 'G', 'F', 0, 0x2e, 0, 0, 0, 0};
    CHECK(!LoadAgfModel(bad, sizeof(bad), m));
    CHECK(!LoadAgfModel(nullptr, 0, m));
    const u8 tiny[] = {'B', 'G', 'F'};
    CHECK(!LoadAgfModel(tiny, sizeof(tiny), m));
}

TEST(AgfLoader, RejectsMissingVersionMarker) {
    BgfModel m;
    // magic ok but next token is not '.' (0x2e): use 0x05.
    const u8 buf[] = {'B', 'G', 'F', 0, 0x05, 0, 0, 0, 0};
    CHECK(!LoadAgfModel(buf, sizeof(buf), m));
}

TEST(AgfLoader, ParsesMinimalCounts) {
    auto buf = BuildMinimal(/*nv=*/4, /*np=*/3, /*nmat=*/2);
    BgfModel m;
    CHECK(LoadAgfModel(buf.data(), buf.size(), m));
    CHECK_EQ((int)m.vertexCount, 4);
    CHECK_EQ((int)m.polyCount, 3);
    CHECK_EQ((int)m.materialCount, 2);
    CHECK_EQ((int)m.vertices.size(), 4);
    CHECK_EQ((int)m.polygons.size(), 3);
    CHECK_EQ((int)m.materials.size(), 2);
}

TEST(AgfLoader, ParsesVertexPositions) {
    auto buf = BuildMinimal(3, 1, 1);
    BgfModel m;
    CHECK(LoadAgfModel(buf.data(), buf.size(), m));
    if (m.vertices.size() >= 3) {
        // vertex i = (i, 2i, 3i)
        CHECK(m.vertices[0].pos[0] == 0.0f);
        CHECK(m.vertices[2].pos[0] == 2.0f);
        CHECK(m.vertices[2].pos[1] == 4.0f);
        CHECK(m.vertices[2].pos[2] == 6.0f);
    }
}

TEST(AgfLoader, ParsesMaterialNamesStripExt) {
    auto buf = BuildMinimal(3, 1, 2);
    BgfModel m;
    CHECK(LoadAgfModel(buf.data(), buf.size(), m));
    if (m.materials.size() >= 2) {
        CHECK(m.materials[0].name0 == "stone");  // ".bmp" stripped
        CHECK(m.materials[1].name0 == "wood");
        CHECK_EQ((int)m.materials[0].flag, 1);
        CHECK_EQ((int)m.materials[1].flag, 2);
    }
}

TEST(AgfLoader, ParsesPolyIndicesAndMatIndex) {
    auto buf = BuildMinimal(4, 2, 2);
    BgfModel m;
    CHECK(LoadAgfModel(buf.data(), buf.size(), m));
    if (m.polygons.size() >= 2) {
        CHECK_EQ((int)m.polygons[0].vtx[0], 0);
        CHECK_EQ((int)m.polygons[0].vtx[1], 1);
        CHECK_EQ((int)m.polygons[0].vtx[2], 2);
        CHECK_EQ((int)m.polygons[0].texId, -1);   // resolved later
        CHECK_EQ((int)m.polygons[0].matIndex, 0);
        CHECK_EQ((int)m.polygons[1].matIndex, 1);
        // uv0 row from the builder.
        CHECK(m.polygons[0].uv0[0] == 0.1f);
        CHECK(m.polygons[0].uv1[0] == 0.4f);
    }
}

TEST(AgfLoader, BoundingExtents) {
    auto buf = BuildMinimal(4, 1, 1);
    BgfModel m;
    CHECK(LoadAgfModel(buf.data(), buf.size(), m));
    BgfBounds b = ComputeBoundingExtents(m);
    // vertex 3 = (3,6,9) -> max corner; radius >= |(3,6,9)|.
    CHECK(b.max[0] == 3.0f);
    CHECK(b.max[1] == 6.0f);
    CHECK(b.max[2] == 9.0f);
    CHECK(b.radius > 11.0f);   // sqrt(9+36+81)=~11.22
}

TEST(AgfLoader, VertexNormalsFinite) {
    auto buf = BuildMinimal(4, 2, 1);
    BgfModel m;
    CHECK(LoadAgfModel(buf.data(), buf.size(), m));
    ComputeVertexNormals(m);
    // Each touched vertex normal must be unit-length or zero (degenerate), finite.
    for (auto& v : m.vertices) {
        float len = v.normal[0]*v.normal[0] + v.normal[1]*v.normal[1] + v.normal[2]*v.normal[2];
        CHECK(len == len);                 // not NaN
        CHECK(len < 1.001f);               // <= ~1 (unit or zero)
    }
}
