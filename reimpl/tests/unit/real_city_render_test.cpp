#include "test.h"

// UNIT: prove the REAL mesh-resolver path is taken (not the inert quad fallback)
// and that a resolved mesh's vertices are seated at the object's world placement.
//
//   * build a known multi-tri AGF mesh (6 verts / 8 tris) into a RealMeshSource via
//     DecodeBuffer (a stub "mesh source returning a known multi-tri mesh"),
//   * install it as the live object_mesh_render MeshResolver through the SAME public
//     hooks RealCityRenderer uses (InstallRealMeshSource + InstallMeshNameResolver
//     -> RealMeshResolver),
//   * render one synthetic live object through ObjectMeshRenderer with a node seated
//     at a known world position -> assert it drew as a MESH (meshObjects==1,
//     quadFallbacks==0, >4 verts of geometry), not a quad,
//   * independently assert ComposeWorldMatrix/TransformMeshGeometry seat the mesh's
//     vertices at the object's world placement (the real world-seat the renderer runs).
#include "play/real_city_render.h"
#include "play/object_mesh_render.h"
#include "play/object_transform.h"
#include "play/real_mesh_source.h"
#include "render/agf_loader.h"
#include "render/bgf_loader.h"
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

// A minimal AGF builder (same grammar the real_mesh_source itest uses).
struct AgfBuilder {
    std::vector<u8> b;
    void byte(u8 v) { b.push_back(v); }
    void u32v(u32 v) { b.push_back(v & 0xff); b.push_back((v >> 8) & 0xff);
                       b.push_back((v >> 16) & 0xff); b.push_back((v >> 24) & 0xff); }
    void f32v(float f) { u32 bits; std::memcpy(&bits, &f, 4); u32v(bits); }
    void str(const char* s) { while (*s) b.push_back((u8)*s++); b.push_back(0); }
    void magic() { byte('B'); byte('G'); byte('F'); byte(0); }
};

// 6-vert octahedron (8 tris). >4 verts == real geometry, not a 2-tri quad.
std::vector<u8> BuildOcta(float S) {
    AgfBuilder w;
    w.magic();
    w.byte(0x2e); w.u32v(1);   // version
    // one material
    w.byte(0x03);
      w.byte(0x04); w.u32v(1);
      w.byte(0x05); w.byte(0x07); w.str("diffuse.tga"); w.byte(0x28);
    w.byte(0x27);
    // geometry
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
    w.byte(0x2b);   // '+'
    return w.b;
}

// A real-format engine node buffer seated at a world position (the layout
// SceneNodeWorldPlacement decodes: +76 pos, +396 identity yaw, +533 type 1).
unsigned char g_node[640];
void SeatNode(float x, float y, float z) {
    std::memset(g_node, 0, sizeof g_node);
    std::memcpy(g_node + kNodePosX, &x, 4);
    std::memcpy(g_node + kNodePosY, &y, 4);
    std::memcpy(g_node + kNodePosZ, &z, 4);
    const float m[16] = {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
    std::memcpy(g_node + kNodeFrameMatrix, m, sizeof m);
    g_node[kNodeTypeByte] = kNodeTypeMeshA;
}
const void* ResolveNode(const EntityRef&) { return g_node; }

} // namespace

// --- the REAL resolver path is taken (not the quad fallback) -------------------
TEST(RealCityRenderUnit, RealResolverPathTakenNotQuad) {
    ResetEntityArrays();
    g_objects[0].alive = 1;
    g_objects[0].id = 11;

    // Decode a real multi-tri AGF mesh into a source, install it via the public
    // hooks (exactly what RealCityRenderer::Render does), and confirm RealMeshResolver
    // now hands back the real geometry (>4 verts).
    auto buf = BuildOcta(4.0f);
    RealMeshSource src;
    render::MeshGeometry* mg = src.DecodeBuffer("octa.bgf", buf.data(), buf.size());
    CHECK(mg != nullptr);
    if (mg) CHECK(mg->vertexCount > 4);   // real geometry, not a 2-tri quad

    InstallRealMeshSource(&src);
    InstallMeshNameResolver([](const EntityRef&) -> std::string { return "octa.bgf"; });

    EntityRef probe{EntityKind::Object, 11, 0, 1};
    const render::MeshGeometry* resolved = RealMeshResolver(probe);
    CHECK(resolved != nullptr);
    if (resolved) CHECK(resolved->vertexCount > 4);

    SeatNode(0.0f, 0.0f, 0.0f);
    render::Surface* fb = render::SurfaceCreate(96, 72, 16);
    CHECK(fb != nullptr);

    ObjectMeshRenderer r;
    ObjectMeshRenderer::Options opt;
    opt.nodeResolver = &ResolveNode;
    opt.meshResolver = &RealMeshResolver;   // the REAL source adapter (not inert default)
    opt.scanScene = false; opt.scanObjects = true;
    opt.pixelsPerUnit = 4.0f;

    MeshRenderStats st = r.render(opt, fb);
    CHECK_EQ(st.meshObjects, 1);     // drew as a RESOLVED mesh
    CHECK_EQ(st.quadFallbacks, 0);   // NOT the quad fallback
    CHECK(st.meshTris >= 8);         // the octahedron's 8 model tris

    render::SurfaceDestroy(fb);
    InstallRealMeshSource(nullptr);
    InstallMeshNameResolver(nullptr);
    ResetEntityArrays();
}

// --- vertices are seated at the object's world placement ------------------------
TEST(RealCityRenderUnit, MeshVerticesSeatedAtPlacement) {
    auto buf = BuildOcta(4.0f);
    RealMeshSource src;
    render::MeshGeometry* mg = src.DecodeBuffer("octa.bgf", buf.data(), buf.size());
    CHECK(mg != nullptr);
    if (!mg) return;

    // Seat at a known world translation with zero yaw: every vertex shifts by +wp.
    WorldPlacement wp; wp.x = 100.0f; wp.y = 0.0f; wp.z = -50.0f; wp.yaw = 0.0f; wp.visible = true;
    WorldMesh wm;
    CHECK(TransformMeshGeometry(*mg, wp, wm));
    CHECK((int)wm.vertices.size() > 4);

    // model vertex 0 = (+4,0,0); after the +wp seat with identity yaw it lands at
    // (104, 0, -50). The world matrix is the engine's record+72 transform.
    if (wm.vertices.size() > 0) {
        CHECK(std::fabs(wm.vertices[0].x - 104.0f) < 1e-3f);
        CHECK(std::fabs(wm.vertices[0].y - 0.0f)   < 1e-3f);
        CHECK(std::fabs(wm.vertices[0].z - (-50.0f)) < 1e-3f);
    }

    // Cross-check ComposeWorldMatrix puts the translation in the 4th column.
    float m[16];
    ComposeWorldMatrix(wp, m);
    CHECK(m[12] == 100.0f);
    CHECK(m[14] == -50.0f);
}

// --- the renderer falls back to a quad when no source is installed -------------
TEST(RealCityRenderUnit, FallsBackToQuadWhenNoSource) {
    ResetEntityArrays();
    g_objects[0].alive = 1;
    g_objects[0].id = 3;
    SeatNode(0.0f, 0.0f, 0.0f);

    // inert: no source installed -> RealMeshResolver returns null -> quad fallback.
    InstallRealMeshSource(nullptr);
    InstallMeshNameResolver(nullptr);
    CHECK(RealMeshResolver(EntityRef{EntityKind::Object, 3, 0, 1}) == nullptr);

    render::Surface* fb = render::SurfaceCreate(96, 72, 16);
    CHECK(fb != nullptr);
    ObjectMeshRenderer r;
    ObjectMeshRenderer::Options opt;
    opt.nodeResolver = &ResolveNode;
    opt.meshResolver = &RealMeshResolver;
    opt.scanScene = false; opt.scanObjects = true;
    opt.pixelsPerUnit = 4.0f;

    MeshRenderStats st = r.render(opt, fb);
    CHECK_EQ(st.meshObjects, 0);
    CHECK_EQ(st.quadFallbacks, 1);

    render::SurfaceDestroy(fb);
    ResetEntityArrays();
}
