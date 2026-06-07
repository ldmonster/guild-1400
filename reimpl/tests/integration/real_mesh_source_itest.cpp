// tests/integration/real_mesh_source_itest.cpp — AGF -> BuildGeometry -> source.
//
// Builds a small AGF buffer with a couple verts/polys/materials/dummies, runs it
// through LoadAgfModel + BuildGeometry, and through RealMeshSource::DecodeBuffer,
// asserting geometry vertex/poly counts and a known vertex value. No assets.
#include "test.h"

#include "render/agf_loader.h"
#include "render/bgf_loader.h"
#include "play/real_mesh_source.h"

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
    void magic() { b.push_back('B'); b.push_back('G'); b.push_back('F'); b.push_back(0); }
};

// 4 verts, 2 polys, 2 materials, 1 dummy (frame node, TOP tok 0x37).
std::vector<u8> BuildSmall() {
    Builder w;
    w.magic();
    w.byte(0x2e); w.u32v(1);   // version

    // materials
    w.byte(0x03);
      w.byte(0x04); w.u32v(2);
      w.byte(0x05); w.byte(0x07); w.str("diffuse.tga"); w.byte(0x28);
      w.byte(0x05); w.byte(0x07); w.str("spec.tga");    w.byte(0x28);
    w.byte(0x27);

    // geometry
    w.byte(0x14);
      w.byte(0x17);
        w.byte(0x18); w.u32v(4);
        w.byte(0x19); w.u32v(4);   // alloc points
        w.byte(0x1a); w.u32v(2);   // alloc polys
        w.byte(0x1b);
          w.f32v(0); w.f32v(0); w.f32v(0);
          w.f32v(1); w.f32v(0); w.f32v(0);
          w.f32v(1); w.f32v(1); w.f32v(0);
          w.f32v(0); w.f32v(1); w.f32v(0);
        w.byte(0x1c);
          w.byte(0x1d); w.u32v(0); w.u32v(1); w.u32v(2);
          w.byte(0x1e); w.f32v(0); w.f32v(0); w.f32v(0); w.f32v(1); w.f32v(1); w.f32v(0); w.u32v(0); w.u32v(0); w.u32v(0);
          w.byte(0x20); w.byte(0);
          w.byte(0x1d); w.u32v(0); w.u32v(2); w.u32v(3);
          w.byte(0x1e); w.f32v(0); w.f32v(0); w.f32v(0); w.f32v(1); w.f32v(1); w.f32v(0); w.u32v(0); w.u32v(0); w.u32v(0);
          w.byte(0x20); w.byte(1);
        w.byte(0x27);
      w.byte(0x27);
    w.byte(0x27);

    // frame nodes (dummies): TOP tok 0x37; count then per-dummy byte/string/byte/vec3/byte/vec3
    w.byte(0x37); w.u32v(1);
      w.byte(0); w.str("root"); w.byte(0); w.f32v(5); w.f32v(6); w.f32v(7); w.byte(0); w.f32v(0); w.f32v(90); w.f32v(0);

    w.byte(0x2b);   // '+'
    return w.b;
}

} // namespace

TEST(RealMeshSourceItest, LoadAndBuildGeometry) {
    auto buf = BuildSmall();
    BgfModel m;
    CHECK(LoadAgfModel(buf.data(), buf.size(), m));
    CHECK_EQ((int)m.vertexCount, 4);
    CHECK_EQ((int)m.polyCount, 2);
    CHECK_EQ((int)m.materialCount, 2);
    CHECK_EQ((int)m.dummyCount, 1);
    if (m.dummies.size() >= 1) {
        CHECK(std::strcmp(m.dummies[0].name, "root") == 0);
        CHECK(m.dummies[0].pos[0] == 5.0f);
        CHECK(m.dummies[0].pos[2] == 7.0f);
        CHECK(m.dummies[0].rot[1] == 90.0f);
    }

    BgfGeometry g;
    CHECK(BuildGeometry(m, g));
    MeshGeometry* mg = g.View();
    CHECK(mg != nullptr);
    if (mg) {
        CHECK_EQ(mg->polyCount, 2);
        CHECK_EQ(mg->vertexCount, 4);
        // vertex 2 = (1,1,0) per the builder.
        CHECK(g.vertices[2].x == 1.0f);
        CHECK(g.vertices[2].y == 1.0f);
        CHECK(g.vertices[2].z == 0.0f);
        // polygon 0 binds verts 0,1,2.
        CHECK(g.polygons[0].v0 == &g.vertices[0]);
        CHECK(g.polygons[0].v2 == &g.vertices[2]);
    }
}

TEST(RealMeshSourceItest, DecodeBufferThroughSource) {
    auto buf = BuildSmall();
    play::RealMeshSource src;
    MeshGeometry* mg = src.DecodeBuffer("small", buf.data(), buf.size());
    CHECK(mg != nullptr);
    if (mg) {
        CHECK_EQ(mg->polyCount, 2);
        CHECK_EQ(mg->vertexCount, 4);
    }
    const BgfModel* m = src.ModelFor("small");
    CHECK(m != nullptr);
    if (m) CHECK_EQ((int)m->materialCount, 2);
    // Re-resolve from cache yields the same model.
    CHECK(src.ModelFor("small") == m);
}

TEST(RealMeshSourceItest, AdapterInertByDefault) {
    // No source / inert name resolver -> the adapter returns null (quad fallback).
    play::EntityRef e{play::EntityKind::Object, 1, 0, 1};
    CHECK(play::RealMeshResolver(e) == nullptr);
    CHECK(play::DefaultMeshNameResolver(e).empty());
    CHECK(play::ActiveRealMeshSource() == nullptr);
}

TEST(RealMeshSourceItest, AdapterResolvesWhenInstalled) {
    auto buf = BuildSmall();
    play::RealMeshSource src;
    CHECK(src.DecodeBuffer("Some/Mesh.bgf", buf.data(), buf.size()) != nullptr);

    // Install the source + a name resolver that maps every object to our key.
    play::InstallRealMeshSource(&src);
    play::InstallMeshNameResolver([](const play::EntityRef&) -> std::string {
        return "Some/Mesh.bgf";
    });

    play::EntityRef e{play::EntityKind::Object, 7, 0, 1};
    const MeshGeometry* mg = play::RealMeshResolver(e);
    CHECK(mg != nullptr);
    if (mg) CHECK_EQ(mg->polyCount, 2);

    // Restore inert defaults so other tests in the binary stay isolated.
    play::InstallRealMeshSource(nullptr);
    play::InstallMeshNameResolver(nullptr);
    CHECK(play::RealMeshResolver(e) == nullptr);
}
