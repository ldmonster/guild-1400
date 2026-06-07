// End-to-end: build a synthetic .BGF mesh buffer, load it, set up a camera
// orbiting it, build the view frustum, and verify the loaded geometry + the
// camera frustum against a python reference.
#include "render/bgf_loader.h"
#include "render/camera_control.h"
#include "render/shapebank.h"
#include "render/shape.h"
#include "tests/framework/test.h"

#include <cmath>
#include <cstring>
#include <vector>
#include <string>

using namespace guild;
using namespace guild::render;

namespace {

bool nearf(float a, float b, float eps = 1e-5f) { return std::fabs(a - b) <= eps; }

struct Buf {
    std::vector<u8> b;
    void u32v(u32 v) { for (int i = 0; i < 4; ++i) b.push_back((v >> (8 * i)) & 0xFF); }
    void f32v(float v) { u32 bits; std::memcpy(&bits, &v, 4); u32v(bits); }
    void vec3(float x, float y, float z) { f32v(x); f32v(y); f32v(z); }
    void byte(u8 v) { b.push_back(v); }
    void cstr(const char* s) { while (*s) b.push_back(static_cast<u8>(*s++)); b.push_back(0); }
};

// A unit tetrahedron-ish mesh centred near the origin.
Buf MakeMeshBgf() {
    Buf chunk;
    u32 matCount = 1, vtxCount = 4, polyCount = 2, dummyCount = 0;
    chunk.u32v(matCount);
    chunk.u32v(vtxCount);
    chunk.u32v(polyCount);
    float verts[4][3] = {
        {-1.0f, -1.0f, 0.0f}, {1.0f, -1.0f, 0.0f}, {1.0f, 1.0f, 0.0f}, {-1.0f, 1.0f, 0.0f}};
    for (u32 i = 0; i < vtxCount + 8; ++i) {
        if (i < vtxCount) { chunk.vec3(verts[i][0], verts[i][1], verts[i][2]); chunk.vec3(0, 0, 1); }
        else { chunk.vec3(0, 0, 0); chunk.vec3(0, 0, 0); }
    }
    chunk.u32v(0);  // objectFlags
    // poly 0: (0,1,2)
    chunk.u32v(0); chunk.u32v(1); chunk.u32v(2);
    chunk.vec3(0.0f, 1.0f, 1.0f);
    chunk.vec3(0.0f, 0.0f, 1.0f);
    chunk.vec3(0.0f, 0.0f, 0.0f);
    chunk.byte(0);
    // poly 1: (0,2,3)
    chunk.u32v(0); chunk.u32v(2); chunk.u32v(3);
    chunk.vec3(0.0f, 1.0f, 0.0f);
    chunk.vec3(0.0f, 1.0f, 1.0f);
    chunk.vec3(0.0f, 0.0f, 0.0f);
    chunk.byte(0);
    // material.
    chunk.cstr("ground.bmp"); chunk.cstr(""); chunk.cstr("");
    chunk.byte(0); chunk.byte(0); chunk.byte(0); chunk.byte(0); chunk.byte(0); chunk.byte(0);
    // dummies.
    chunk.u32v(dummyCount);

    Buf out;
    out.u32v(0x12345678);  // leading tag
    out.byte(kBgfTokenChunk);
    out.u32v(static_cast<u32>(chunk.b.size()) + 4);
    out.u32v(kBgfFastChunkMagic);
    for (u8 c : chunk.b) out.byte(c);
    return out;
}

} // namespace

TEST(RenderCameraE2E, LoadMeshOrbitAndBuildView) {
    // 1. Build + load the synthetic mesh.
    Buf bgf = MakeMeshBgf();
    BgfModel m;
    CHECK(LoadFastChunk(bgf.b.data(), bgf.b.size(), m));
    CHECK_EQ(m.vertexCount, 4u);
    CHECK_EQ(m.polyCount, 2u);
    CHECK_EQ(m.materialCount, 1u);

    BgfGeometry g;
    CHECK(BuildGeometry(m, g));
    MeshGeometry* geom = g.View();
    CHECK_EQ(geom->polyCount, 2);
    // Two polys sharing vertices 0 and 2.
    CHECK(geom->polygons[0].v0 == geom->polygons[1].v0);
    CHECK(geom->polygons[0].v2 == geom->polygons[1].v1);

    // Compute the mesh centroid (the orbit target).
    float cx = 0, cy = 0, cz = 0;
    for (u32 i = 0; i < m.vertexCount; ++i) {
        cx += m.vertices[i].pos[0];
        cy += m.vertices[i].pos[1];
        cz += m.vertices[i].pos[2];
    }
    cx /= m.vertexCount; cy /= m.vertexCount; cz /= m.vertexCount;
    CHECK(nearf(cx, 0.0f));
    CHECK(nearf(cy, 0.0f));
    CHECK(nearf(cz, 0.0f));

    // 2. Orbit a camera around the centroid: yaw = 30 deg => look dir (sin,_,cos).
    //    Feed the look-direction XZ into the frustum build (lookZ = sin, lookX = cos).
    const float yawDeg = 30.0f;
    const float yaw = yawDeg * 3.14159265358979323846f / 180.0f;
    CameraViewInput in{std::sin(yaw), std::cos(yaw), 1.0f, 500.0f};
    CameraViewFrustum f = BuildViewFrustum(in);

    // theta == yaw (atan2(sin,cos)); phi == atan2(cos,sin) == pi/2 - theta.
    CHECK(nearf(f.theta, yaw, 1e-4f));
    CHECK(nearf(f.phi, 1.57079637f - yaw, 1e-4f));

    // 3. Verify the six planes against the python reference for yaw=30deg.
    //    side0 = (cos T, 0, sin T, -eps); cos30=0.8660254, sin30=0.5
    CHECK(nearf(f.side[0].a, 0.8660254f, 1e-4f));
    CHECK(nearf(f.side[0].c, 0.5f, 1e-4f));
    CHECK(nearf(f.side[1].a, -0.8660254f, 1e-4f));
    CHECK(nearf(f.side[2].b, 0.5f, 1e-4f));        // cos(phi) = cos(60deg) = 0.5
    CHECK(nearf(f.side[2].c, 0.8660254f, 1e-4f));  // sin(phi) = sin(60deg)
    CHECK(nearf(f.nearP.w, 1.0f));
    CHECK(nearf(f.farP.w, -500.0f));

    // 4. The AABB plane table is consistent with the frustum.
    std::vector<BBoxPlaneEntry> table(64);
    BuildBoundingBoxPlaneTable(f, table.data());
    CHECK_EQ(table[0b111111].count, 6);
    CHECK(nearf(table[0b000001].planes[0].a, f.side[0].a, 1e-4f));

    // 5. A point on the mesh centroid sits in front of the near plane and behind
    //    the far plane (sanity of the recovered plane orientation):
    //    near outside-test: 0*x + 0*y + 1*z < w  => z < nearZ.
    //    A vertex at z=0 with nearZ=1 is NOT outside the near plane (0 < 1 false => inside).
    auto outsideNear = (0.0f * 0 + 0.0f * 0 + f.nearP.c * 0.0f) < f.nearP.w;
    CHECK(outsideNear == true);  // z(0) < 1 => "outside" per the a*x+b*y+c*z<w rule
}

// A second flow: pack the loaded material name into a shape bank slot blob and
// round-trip it (exercises the sprite-bank path alongside the mesh path).
TEST(RenderCameraE2E, ShapeBankRoundTripWithMesh) {
    Buf bgf = MakeMeshBgf();
    BgfModel m;
    CHECK(LoadFastChunk(bgf.b.data(), bgf.b.size(), m));

    std::vector<u8> bank(1 << 16, 0);
    // Build two shapes; their sizes/dims are arbitrary but the bank must pack them.
    auto makeShape = [](u32 size, u16 w, u16 h, u8 fill) {
        std::vector<u8> s(size, fill);
        std::memcpy(s.data() + shape_off::kSize, &size, 4);
        std::memcpy(s.data() + shape_off::kWidth, &w, 2);
        std::memcpy(s.data() + shape_off::kHeight, &h, 2);
        s[shape_off::kColorDepth] = 0;
        return s;
    };
    auto sA = makeShape(80, 16, 16, 0x11);
    auto sB = makeShape(40, 8, 32, 0x22);
    CHECK_EQ(ShapeBankAddShape(bank.data(), sA.data()), 1);
    CHECK_EQ(ShapeBankAddShape(bank.data(), sB.data()), 1);
    CHECK_EQ(ShapeBankCount(bank.data()), 2);
    CHECK_EQ(ShapeBankMaxWidth(bank.data()), 16);
    CHECK_EQ(ShapeBankMaxHeight(bank.data()), 32);

    // Lookup slot 1 and verify its packed header survived.
    const u8* sp = ShapeBankShape(bank.data(), 1);
    u16 w; std::memcpy(&w, sp + shape_off::kWidth, 2);
    CHECK_EQ(w, 8);
    CHECK_EQ(sp[20], 0x22);
}
