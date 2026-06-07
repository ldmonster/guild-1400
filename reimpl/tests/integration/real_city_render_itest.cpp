#include "test.h"

// INTEGRATION: small real-format world; render with the REAL resolver vs the
// DEFAULT (quad) resolver -> assert the frames DIFFER and the real one has MORE
// geometry; then pick a screen point and assert it selects the expected object.
//
//   * seed a handful of live g_objects (real-format records),
//   * decode a real multi-tri AGF mesh into a RealMeshSource and install it through
//     the SAME public hooks RealCityRenderer uses,
//   * render the live world twice through ObjectMeshRenderer: once with the REAL
//     RealMeshResolver (real geometry) and once with the inert DefaultMeshResolver
//     (quad fallback) -> the real render appends/rasters MORE tris and the
//     framebuffers differ,
//   * RealCityRenderer::Pick over the SAME deterministic placements selects the
//     object at a projected screen point.
#include "play/real_city_render.h"
#include "play/object_mesh_render.h"
#include "play/object_transform.h"
#include "play/real_mesh_source.h"
#include "play/scene_pick.h"
#include "render/agf_loader.h"
#include "render/bgf_loader.h"
#include "render/colorformat.h"
#include "render/geometry_types.h"
#include "render/surface.h"
#include "sim/entity.h"

#include <cmath>
#include <cstring>
#include <vector>

using namespace guild;
using namespace guild::play;
using namespace guild::sim;

namespace {

struct AgfBuilder {
    std::vector<u8> b;
    void byte(u8 v) { b.push_back(v); }
    void u32v(u32 v) { b.push_back(v & 0xff); b.push_back((v >> 8) & 0xff);
                       b.push_back((v >> 16) & 0xff); b.push_back((v >> 24) & 0xff); }
    void f32v(float f) { u32 bits; std::memcpy(&bits, &f, 4); u32v(bits); }
    void str(const char* s) { while (*s) b.push_back((u8)*s++); b.push_back(0); }
    void magic() { byte('B'); byte('G'); byte('F'); byte(0); }
};

std::vector<u8> BuildOcta(float S) {
    AgfBuilder w;
    w.magic();
    w.byte(0x2e); w.u32v(1);
    w.byte(0x03);
      w.byte(0x04); w.u32v(1);
      w.byte(0x05); w.byte(0x07); w.str("diffuse.tga"); w.byte(0x28);
    w.byte(0x27);
    w.byte(0x14);
      w.byte(0x17);
        w.byte(0x18); w.u32v(6);
        w.byte(0x19); w.u32v(6);
        w.byte(0x1a); w.u32v(8);
        w.byte(0x1b);
          w.f32v(S);  w.f32v(0);  w.f32v(0);
          w.f32v(-S); w.f32v(0);  w.f32v(0);
          w.f32v(0);  w.f32v(S);  w.f32v(0);
          w.f32v(0);  w.f32v(-S); w.f32v(0);
          w.f32v(0);  w.f32v(0);  w.f32v(S);
          w.f32v(0);  w.f32v(0);  w.f32v(-S);
        w.byte(0x1c);
          int tri[8][3] = {{0,2,4},{2,1,4},{1,3,4},{3,0,4},{2,0,5},{1,2,5},{3,1,5},{0,3,5}};
          for (int i = 0; i < 8; ++i) {
            w.byte(0x1d); w.u32v(tri[i][0]); w.u32v(tri[i][1]); w.u32v(tri[i][2]);
            w.byte(0x1e); w.f32v(0); w.f32v(0); w.f32v(0); w.f32v(1); w.f32v(1); w.f32v(0);
                          w.u32v(0); w.u32v(0); w.u32v(0);
            w.byte(0x20); w.byte(0);
          }
        w.byte(0x27);
      w.byte(0x27);
    w.byte(0x27);
    w.byte(0x2b);
    return w.b;
}

// A node resolver matching RealCityRenderer's grid layout, so the render placements
// and the pick roster line up.
RealCityRenderer::Options g_opt;
unsigned char g_nodes[64][640];
int g_nodeUsed = 0;

const void* GridNode(const EntityRef& e) {
    if (g_nodeUsed >= 64) return nullptr;
    unsigned char* nb = g_nodes[g_nodeUsed++];
    std::memset(nb, 0, 640);
    int cols = g_opt.gridCols;
    float wx = g_opt.originX + (float)(e.slot % cols) * g_opt.cellSize;
    float wz = g_opt.originZ + (float)(e.slot / cols) * g_opt.cellSize;
    float wy = 0.0f;
    std::memcpy(nb + kNodePosX, &wx, 4);
    std::memcpy(nb + kNodePosY, &wy, 4);
    std::memcpy(nb + kNodePosZ, &wz, 4);
    const float m[16] = {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
    std::memcpy(nb + kNodeFrameMatrix, m, sizeof m);
    nb[kNodeTypeByte] = kNodeTypeMeshA;
    return nb;
}

int CountNonClear(render::Surface* fb, u16 clear) {
    const u16* px = reinterpret_cast<const u16*>(fb->pixels);
    int total = fb->widthPx * fb->height, n = 0;
    for (int i = 0; i < total; ++i) if (px[i] != clear) ++n;
    return n;
}

void SeedWorld(int n) {
    ResetEntityArrays();
    for (int i = 0; i < n; ++i) {
        g_objects[i].alive = 1;
        g_objects[i].id = 100 + i;
    }
}

} // namespace

TEST(RealCityRenderItest, RealResolverHasMoreGeometryThanQuad) {
    SeedWorld(6);

    auto buf = BuildOcta(20.0f);
    RealMeshSource src;
    CHECK(src.DecodeBuffer("octa.bgf", buf.data(), buf.size()) != nullptr);
    InstallRealMeshSource(&src);
    InstallMeshNameResolver([](const EntityRef&) -> std::string { return "octa.bgf"; });

    g_opt = RealCityRenderer::Options{};
    g_opt.fbW = 160; g_opt.fbH = 120;
    // Center the eye on the 6-object grid spread and use a scale that fits it in the
    // frame (the same kind of fit the e2e uses for AUGSBURG's wide spread).
    g_opt.pixelsPerUnit = 0.6f;
    g_opt.eyeX = g_opt.originX + 2.5f * g_opt.cellSize;   // center of a 6-wide row
    g_opt.eyeZ = g_opt.originZ;

    // --- REAL resolver render ---
    render::Surface* fbReal = render::SurfaceCreate(g_opt.fbW, g_opt.fbH, 16);
    render::SurfaceColorFill(fbReal, 0, 0, 64);
    const u16 clear = (u16)render::PackColor(fbReal->fmt, 0, 0, 64);
    g_nodeUsed = 0;
    ObjectMeshRenderer rReal;
    ObjectMeshRenderer::Options opR;
    opR.nodeResolver = &GridNode;
    opR.meshResolver = &RealMeshResolver;
    opR.scanScene = false; opR.scanObjects = true;
    opR.maxObjects = 32; opR.pixelsPerUnit = g_opt.pixelsPerUnit;
    opR.eyeX = g_opt.eyeX; opR.eyeZ = g_opt.eyeZ;
    MeshRenderStats stReal = rReal.render(opR, fbReal);

    // --- DEFAULT (quad) resolver render ---
    render::Surface* fbQuad = render::SurfaceCreate(g_opt.fbW, g_opt.fbH, 16);
    render::SurfaceColorFill(fbQuad, 0, 0, 64);
    g_nodeUsed = 0;
    ObjectMeshRenderer rQuad;
    ObjectMeshRenderer::Options opQ = opR;
    opQ.meshResolver = &DefaultMeshResolver;   // inert -> quad fallback
    MeshRenderStats stQuad = rQuad.render(opQ, fbQuad);

    // Real geometry: drew as meshes; quad: drew as fallback quads.
    CHECK(stReal.meshObjects > 0);
    CHECK_EQ(stReal.quadFallbacks, 0);
    CHECK_EQ(stQuad.meshObjects, 0);
    CHECK(stQuad.quadFallbacks > 0);

    // More geometry in the real render than a 2-tri-per-object quad render.
    CHECK(stReal.meshTris > stQuad.appendedPolys);
    CHECK(stReal.appendedPolys > stQuad.appendedPolys);

    // The framebuffers differ.
    int nReal = CountNonClear(fbReal, clear);
    int nQuad = CountNonClear(fbQuad, clear);
    CHECK(nReal > 0);
    bool differ = (nReal != nQuad);
    if (!differ) {
        const u16* a = reinterpret_cast<const u16*>(fbReal->pixels);
        const u16* b = reinterpret_cast<const u16*>(fbQuad->pixels);
        int total = fbReal->widthPx * fbReal->height;
        for (int i = 0; i < total && !differ; ++i) if (a[i] != b[i]) differ = true;
    }
    CHECK(differ);

    render::SurfaceDestroy(fbReal);
    render::SurfaceDestroy(fbQuad);
    InstallRealMeshSource(nullptr);
    InstallMeshNameResolver(nullptr);
    ResetEntityArrays();
}

TEST(RealCityRenderItest, PickSelectsExpectedObject) {
    SeedWorld(6);

    RealCityRenderer rc;   // not mounted; Pick regenerates the deterministic roster
    RealCityRenderer::Options opt;
    opt.fbW = 160; opt.fbH = 120;
    opt.pixelsPerUnit = 0.6f;
    opt.maxObjects = 32;
    opt.eyeX = opt.originX + 2.5f * opt.cellSize;   // center the spread (as the render does)
    opt.eyeZ = opt.originZ;

    // Compute where object slot 2 projects (the SAME camera Pick uses), then pick
    // there and assert it selects that object.
    float ppu = opt.pixelsPerUnit;
    int cols = opt.gridCols;
    int slot = 2;
    float world[3] = {
        opt.originX + (float)(slot % cols) * opt.cellSize, 0.0f,
        opt.originZ + (float)(slot / cols) * opt.cellSize
    };
    float eye[3] = {
        opt.eyeX - ((float)opt.fbW * 0.5f) / ppu, 0.0f,
        opt.eyeZ - ((float)opt.fbH * 0.5f) / ppu
    };
    CityViewCamera cam = MakeCityViewCamera(eye, ppu, opt.fbW, opt.fbH);
    float sx = 0.0f, sy = 0.0f;
    bool on = ProjectWorldToScreen(cam, world, &sx, &sy);
    CHECK(on);

    ScenePickResult res = rc.Pick(opt, sx, sy, /*pickRadius=*/24.0f);
    CHECK(res.index >= 0);
    CHECK_EQ(res.id, g_objects[slot].id);   // selected the expected object (id 102)

    // A click far outside the city spread selects nothing.
    ScenePickResult miss = rc.Pick(opt, 0.0f, 0.0f, /*pickRadius=*/2.0f);
    CHECK(miss.index == -1 || miss.id != g_objects[slot].id);

    ResetEntityArrays();
}
