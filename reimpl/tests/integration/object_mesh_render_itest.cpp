#include "test.h"

// INTEGRATION: render a small REAL-FORMAT mesh through the REAL pipeline at a
// DECODED engine transform.
//
//   * a real-format engine render-node byte buffer is seated with a world position
//     (+76/+80/+84), an identity frame matrix (+396) and the mesh-object type byte
//     (+533 == 1), exactly the layout SceneNodeWorldPlacement reads;
//   * a NodeResolver feeds that node and a MeshResolver feeds an engine-stride
//     (Vertex[80]/Polygon[40]) 8-triangle mesh — the real geometry the engine's
//     +460 block carries;
//   * ObjectMeshRenderer decodes the placement, seats the mesh in the world, and
//     drives the REAL render::ProjectVerticesToScreen -> RadixSortDrawList ->
//     RasterizeMeshList into a software surface;
//   * asserts the decoded transform, the mesh/append/raster TRI COUNTS, and a
//     KNOWN painted pixel (a fixed interior point of the rasterized mesh) — a
//     fixed-coordinate oracle verified against the real rasterizer output.
#include "play/object_mesh_render.h"
#include "play/real_texture_source.h"
#include "render/bgf_loader.h"
#include "render/bmp.h"
#include "render/geometry_types.h"
#include "render/mesh.h"
#include "render/surface.h"
#include "render/colorformat.h"
#include "render/texture_bin.h"
#include "sim/entity.h"

#include <cstring>
#include <vector>

using namespace guild;
using namespace guild::play;
using namespace guild::sim;

namespace {

// A real-format (engine-stride) 8-triangle octahedron mesh, model space, +/- S poles.
struct RealMesh {
    render::Vertex  v[6];
    render::Polygon p[8];
    render::MeshGeometry geom{};
    explicit RealMesh(float S = 4.0f) {
        auto set = [&](int i, float x, float y, float z) {
            v[i] = render::Vertex{};
            v[i].x = x; v[i].y = y; v[i].z = z;
            v[i].lightIdx = 220; v[i].clipFlags = 0;
        };
        set(0, S,0,0); set(1,-S,0,0); set(2,0,S,0);
        set(3,0,-S,0); set(4,0,0,S); set(5,0,0,-S);
        auto tri = [&](int i, int a, int b, int c) {
            p[i] = render::Polygon{};
            p[i].v0=&v[a]; p[i].v1=&v[b]; p[i].v2=&v[c];
        };
        tri(0,0,2,4); tri(1,2,1,4); tri(2,1,3,4); tri(3,3,0,4);
        tri(4,2,0,5); tri(5,1,2,5); tri(6,3,1,5); tri(7,0,3,5);
        geom.vertices=v; geom.polygons=p;
        geom.polyCount=8; geom.polyCap=8; geom.vertexCount=6;
    }
};

// A real-format engine render-node byte buffer (>= 600 bytes covers +533).
unsigned char g_nodeBuf[640];
const render::MeshGeometry* g_meshPtr = nullptr;

const void* ResolveNode(const EntityRef&) { return g_nodeBuf; }
const render::MeshGeometry* ResolveMesh(const EntityRef&) { return g_meshPtr; }

// Seat the node: world pos at +76/+80/+84, identity 4x4 frame matrix at +396
// (yaw 0), mesh-object type 1 at +533.
void SeatNode(float x, float y, float z) {
    std::memset(g_nodeBuf, 0, sizeof g_nodeBuf);
    std::memcpy(g_nodeBuf + 76, &x, 4);
    std::memcpy(g_nodeBuf + 80, &y, 4);
    std::memcpy(g_nodeBuf + 84, &z, 4);
    const float m[16] = {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
    std::memcpy(g_nodeBuf + 396, m, sizeof m);
    g_nodeBuf[533] = 1;   // kNodeTypeMeshA -> drawable
}

u16 Px16(const render::Surface* s, int x, int y) {
    return reinterpret_cast<const u16*>(s->pixels)[s->widthPx * y + x];
}

} // namespace

TEST(ObjectMeshRenderItest, RealFormatMeshThroughRealPipeline) {
    ResetEntityArrays();

    // One live object; its (synthetic engine) node is supplied by the resolver.
    g_objects[0].alive = 1;
    g_objects[0].id = 42;

    RealMesh mesh(4.0f);
    g_meshPtr = &mesh.geom;
    SeatNode(0.0f, 0.0f, 0.0f);   // world origin -> projects to the frame center

    // The decoded placement matches what we seated.
    WorldPlacement wp = SceneNodeWorldPlacement(g_nodeBuf);
    CHECK(wp.visible);
    CHECK(wp.x == 0.0f && wp.y == 0.0f && wp.z == 0.0f);

    const int fbW = 96, fbH = 72;
    render::Surface* fb = render::SurfaceCreate(fbW, fbH, 16);
    CHECK(fb != nullptr);
    render::SurfaceColorFill(fb, 0, 0, 64);

    ObjectMeshRenderer r;
    ObjectMeshRenderer::Options opt;
    opt.nodeResolver = &ResolveNode;
    opt.meshResolver = &ResolveMesh;
    opt.scanScene = false;
    opt.scanObjects = true;
    opt.pixelsPerUnit = 4.0f;
    opt.eyeX = 0.0f; opt.eyeZ = 0.0f;

    MeshRenderStats st = r.render(opt, fb);

    // Drawn as a MESH (not a quad fallback).
    CHECK_EQ(st.meshObjects, 1);
    CHECK_EQ(st.quadFallbacks, 0);
    // The mesh contributed its 8 model tris; the ground projection survives 4 of
    // them (top/bottom collapse, half backface-culled), all 4 rasterized.
    CHECK_EQ(st.meshTris, 8);
    CHECK_EQ(st.appendedPolys, 4);
    CHECK_EQ(st.rasterTris, 4);

    // NON-BLANK + KNOWN-PIXEL ORACLE: since the wave-3 surface-format routing
    // (render/meshlist.cpp RasterTri: a 16bpp target renders untextured polys
    // through the 1x1 WHITE default binding — BindActive @0x5db564 slot==0 —
    // instead of the 8bpp shade span), the seated mesh paints its TRUE footprint:
    // a white diamond centred on the frame, rows ~21..51, widest x 32..63 at
    // row 36 (verified against the real rasterizer). (48,36) is its solid
    // interior centre. (The pre-wave-3 oracle (24,18) was calibrated against
    // the old byte-pair artifact footprint.)
    u16 clear = (u16)render::PackColor(fb->fmt, 0, 0, 64);
    int changed = 0;
    for (int y = 0; y < fbH; ++y)
        for (int x = 0; x < fbW; ++x)
            if (Px16(fb, x, y) != clear) ++changed;
    CHECK(changed > 100);
    CHECK(Px16(fb, 48, 36) != clear);   // fixed-coordinate oracle

    render::SurfaceDestroy(fb);
    ResetEntityArrays();
}

// --- quad fallback when no mesh is resolvable ----------------------------------
TEST(ObjectMeshRenderItest, FallsBackToQuadWhenMeshAbsent) {
    ResetEntityArrays();
    g_objects[0].alive = 1;
    g_objects[0].id = 7;

    g_meshPtr = nullptr;          // resolver yields no mesh -> quad fallback
    SeatNode(0.0f, 0.0f, 0.0f);

    render::Surface* fb = render::SurfaceCreate(96, 72, 16);
    CHECK(fb != nullptr);
    render::SurfaceColorFill(fb, 0, 0, 64);

    ObjectMeshRenderer r;
    ObjectMeshRenderer::Options opt;
    opt.nodeResolver = &ResolveNode;
    opt.meshResolver = &ResolveMesh;   // returns null
    opt.scanScene = false; opt.scanObjects = true;
    opt.pixelsPerUnit = 4.0f;

    MeshRenderStats st = r.render(opt, fb);
    CHECK_EQ(st.meshObjects, 0);
    CHECK_EQ(st.quadFallbacks, 1);     // drew the fallback quad
    CHECK(st.rasterTris > 0);          // the quad rasterized (>=1 tri)

    render::SurfaceDestroy(fb);
    ResetEntityArrays();
}

// =============================================================================
// TEXTURED render vs untextured (Wave 30): same mesh, the textured frame differs
// from the flat frame, samples the real texture, and is deterministic.
// =============================================================================
namespace {

// 8x8 24-bit texture with a strong colour gradient (R ramps with x, B with y).
std::vector<u8> MakeGradTex() {
    const int w = 8, h = 8;
    std::vector<u8> rgb((std::size_t)w * h * 3, 0);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            std::size_t i = ((std::size_t)y * w + x) * 3;
            rgb[i + 0] = (u8)(20 + 30 * x);   // R
            rgb[i + 1] = 40;                   // G
            rgb[i + 2] = (u8)(20 + 30 * y);   // B
        }
    std::vector<u8> file = render::BmpSave24Bit(w, h, rgb.data());
    // Engine Save24 (gilde.exe 0x5f18f4) writes pixel data at 58; loaders seek
    // the standard 0x36 (0x5f0ce4) like real tool-authored assets. Convert the
    // synthetic fixture to standard layout.
    file.erase(file.begin() + 54, file.begin() + 58);
    file[10] = 54;
    for (int k = 0; k < 4; ++k) file[2 + k] = (u8)(file.size() >> (8 * k));
    return file;
}

// A textured 2-tri quad mesh (engine stride) seated at origin, full-tex UVs.
struct TexQuad {
    render::Vertex  v[4];
    render::Polygon p[2];
    render::MeshGeometry geom{};
    explicit TexQuad(float S = 6.0f) {
        auto set = [&](int i, float x, float z, float u, float vv) {
            v[i] = render::Vertex{}; v[i].x = x; v[i].y = 0; v[i].z = z;
            v[i].u = u; v[i].v = vv; v[i].lightIdx = 210;
        };
        set(0, -S, -S, 0.02f, 0.02f); set(1, S, -S, 0.98f, 0.02f);
        set(2, -S,  S, 0.02f, 0.98f); set(3, S,  S, 0.98f, 0.98f);
        p[0] = render::Polygon{}; p[0].v0=&v[0]; p[0].v1=&v[1]; p[0].v2=&v[2];
        p[1] = render::Polygon{}; p[1].v0=&v[1]; p[1].v1=&v[3]; p[1].v2=&v[2];
        geom.vertices=v; geom.polygons=p; geom.polyCount=2; geom.polyCap=2;
        geom.vertexCount=4;
    }
};

TexQuad* g_tq = nullptr;
const play::MaterialTextureTable* g_itTable = nullptr;
const render::MeshGeometry* ResolveTexMesh(const EntityRef&) { return &g_tq->geom; }
const play::MaterialTextureTable* ResolveTexTable(const EntityRef&) { return g_itTable; }

int CountNonClear(render::Surface* s, u16 clear) {
    const u16* px = reinterpret_cast<const u16*>(s->pixels);
    int total = s->widthPx * s->height, n = 0;
    for (int i = 0; i < total; ++i) if (px[i] != clear) ++n;
    return n;
}

} // namespace

TEST(ObjectMeshRenderItest, TexturedFrameDiffersAndIsDeterministic) {
    ResetEntityArrays();
    g_objects[0].alive = 1; g_objects[0].id = 99;

    TexQuad quad(6.0f);
    g_tq = &quad;
    SeatNode(0.0f, 0.0f, 0.0f);

    // Texture source + per-poly table (both polys -> the gradient texture).
    play::RealTextureSource src;
    std::vector<u8> bmp = MakeGradTex();
    const render::DecodedBmp* dec = src.bin().DecodeBuffer("GradTile.BMP", bmp);
    CHECK(dec != nullptr);
    render::BgfModel model;
    model.materialCount = 1; model.materials.resize(1);
    model.materials[0].name0 = "GradTile";
    model.vertexCount = 4; model.vertices.resize(4);
    model.polyCount = 2; model.polygons.resize(2);
    model.polygons[0].matIndex = 0; model.polygons[1].matIndex = 0;
    g_itTable = src.BuildTableFor("GradMesh", model);
    CHECK(g_itTable != nullptr);

    const int fbW = 96, fbH = 72;
    const u16 clear = (u16)render::PackColor(render::Format565(), 0, 0, 64);

    ObjectMeshRenderer::Options opt;
    opt.nodeResolver = &ResolveNode;
    opt.meshResolver = &ResolveTexMesh;
    opt.scanScene = false; opt.scanObjects = true;
    opt.pixelsPerUnit = 4.0f; opt.eyeX = 0.0f; opt.eyeZ = 0.0f;

    // --- UNTEXTURED (flat) ---
    render::Surface* flat = render::SurfaceCreate(fbW, fbH, 16);
    render::SurfaceColorFill(flat, 0, 0, 64);
    ObjectMeshRenderer rf;
    MeshRenderStats sf = rf.render(opt, flat);
    CHECK_EQ(sf.texturedPolys, 0);
    int nFlat = CountNonClear(flat, clear);
    CHECK(nFlat > 50);

    // --- TEXTURED ---
    opt.textured = true;
    opt.texTableResolver = &ResolveTexTable;
    render::Surface* texd = render::SurfaceCreate(fbW, fbH, 16);
    render::SurfaceColorFill(texd, 0, 0, 64);
    ObjectMeshRenderer rt;
    MeshRenderStats st = rt.render(opt, texd);
    CHECK(st.texturedPolys > 0);
    int nTex = CountNonClear(texd, clear);
    CHECK(nTex > 50);

    // The frames DIFFER (real texels vs flat shade).
    const u16* a = reinterpret_cast<const u16*>(flat->pixels);
    const u16* b = reinterpret_cast<const u16*>(texd->pixels);
    int total = flat->widthPx * flat->height, diff = 0;
    for (int i = 0; i < total; ++i) if (a[i] != b[i]) ++diff;
    CHECK(diff > 20);

    // Textured pixels MATCH the texture: the painted colours come from the
    // gradient (G==40 across the whole texture), so a non-clear textured pixel's
    // unpacked green must round-trip to ~40 (565 quantises 40 -> 40).
    bool foundTexel = false;
    for (int y = 0; y < fbH && !foundTexel; ++y)
        for (int x = 0; x < fbW && !foundTexel; ++x) {
            if (Px16(texd, x, y) == clear) continue;
            u8 rgb[3]; render::SurfaceGetPixelRgb(texd, x, y, rgb);
            if (rgb[1] >= 36 && rgb[1] <= 44) foundTexel = true;  // G ~ 40
        }
    CHECK(foundTexel);

    // DETERMINISM: a second textured render is byte-identical.
    render::Surface* texd2 = render::SurfaceCreate(fbW, fbH, 16);
    render::SurfaceColorFill(texd2, 0, 0, 64);
    ObjectMeshRenderer rt2;
    rt2.render(opt, texd2);
    const u16* c = reinterpret_cast<const u16*>(texd2->pixels);
    int mism = 0;
    for (int i = 0; i < total; ++i) if (b[i] != c[i]) ++mism;
    CHECK_EQ(mism, 0);

    render::SurfaceDestroy(flat);
    render::SurfaceDestroy(texd);
    render::SurfaceDestroy(texd2);
    g_tq = nullptr; g_itTable = nullptr;
    ResetEntityArrays();
}
