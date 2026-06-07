#include "test.h"

// UNIT: mesh project + place golden over a synthetic 8-triangle mesh at a KNOWN
// WorldPlacement.
//
//   * ComposeWorldMatrix builds the engine's +72 column-major world matrix from a
//     WorldPlacement (yaw about +Y seated with the world position) — golden
//     entries for yaw=0 and yaw=90deg.
//   * TransformMeshGeometry seats a synthetic 8-tri mesh at a known placement; the
//     transformed vertices land at the EXACT world coords the matrix predicts
//     (golden coordinates), polygons rebind correctly, tri count is preserved.
//   * the world-seated mesh projects through the REAL ProjectVerticesToScreen and
//     appends the expected golden poly count for an on-screen placement.
#include "play/object_mesh_render.h"
#include "play/real_texture_source.h"
#include "render/bgf_loader.h"
#include "render/bmp.h"
#include "render/colorformat.h"
#include "render/geometry_types.h"
#include "render/mesh.h"
#include "render/surface.h"
#include "render/texture_bin.h"
#include "sim/entity.h"

#include <cmath>
#include <cstring>
#include <vector>

using namespace guild;
using namespace guild::play;

namespace {

// Build a synthetic 8-triangle mesh (an octahedron-ish fan): 6 vertices, 8 tris.
// Engine-stride render::Vertex/Polygon, model-space, centered at the origin.
struct SynthMesh {
    render::Vertex  v[6];
    render::Polygon p[8];
    render::MeshGeometry geom{};
    SynthMesh() {
        auto set = [&](int i, float x, float y, float z) {
            v[i] = render::Vertex{};
            v[i].x = x; v[i].y = y; v[i].z = z;
            v[i].lightIdx = 200; v[i].clipFlags = 0;
        };
        // +/-X, +/-Y, +/-Z unit poles.
        set(0,  1, 0, 0); set(1, -1, 0, 0);
        set(2,  0, 1, 0); set(3,  0,-1, 0);
        set(4,  0, 0, 1); set(5,  0, 0,-1);
        auto tri = [&](int i, int a, int b, int c) {
            p[i] = render::Polygon{};
            p[i].v0 = &v[a]; p[i].v1 = &v[b]; p[i].v2 = &v[c];
        };
        tri(0, 0,2,4); tri(1, 2,1,4); tri(2, 1,3,4); tri(3, 3,0,4);
        tri(4, 2,0,5); tri(5, 1,2,5); tri(6, 3,1,5); tri(7, 0,3,5);
        geom.vertices = v; geom.polygons = p;
        geom.polyCount = 8; geom.polyCap = 8; geom.vertexCount = 6;
    }
};

bool Near(float a, float b, float eps = 1e-4f) { return std::fabs(a - b) <= eps; }

} // namespace

// --- ComposeWorldMatrix golden -------------------------------------------------
TEST(ObjectMeshRenderUnit, ComposeWorldMatrixGolden) {
    // yaw=0: rotation is identity, translation in col 4.
    WorldPlacement wp0{10.0f, 5.0f, -3.0f, 0.0f, true};
    float m[16];
    ComposeWorldMatrix(wp0, m);
    CHECK(Near(m[0], 1.0f)); CHECK(Near(m[10], 1.0f)); CHECK(Near(m[5], 1.0f));
    CHECK(Near(m[8], 0.0f)); CHECK(Near(m[2], 0.0f));
    CHECK(Near(m[12], 10.0f)); CHECK(Near(m[13], 5.0f)); CHECK(Near(m[14], -3.0f));
    CHECK(Near(m[15], 1.0f));

    // yaw=90deg: M[0]=cos=0, M[8]=-sin=-1, M[2]=sin=1, M[10]=cos=0.
    WorldPlacement wp1{0, 0, 0, (float)(M_PI / 2.0), true};
    ComposeWorldMatrix(wp1, m);
    CHECK(Near(m[0], 0.0f));  CHECK(Near(m[8], -1.0f));
    CHECK(Near(m[2], 1.0f));  CHECK(Near(m[10], 0.0f));
    // WorldMatrixYaw must round-trip the encoded heading (atan2(-M[8], M[0])).
    CHECK(Near(WorldMatrixYaw(m), (float)(M_PI / 2.0)));
}

// --- TransformMeshGeometry golden coords + tri count ---------------------------
TEST(ObjectMeshRenderUnit, SeatSynthMeshGoldenCoords) {
    SynthMesh sm;
    // Known placement: translate by (100, 0, 50), yaw=90deg.
    WorldPlacement wp{100.0f, 0.0f, 50.0f, (float)(M_PI / 2.0), true};

    WorldMesh out;
    bool ok = TransformMeshGeometry(sm.geom, wp, out);
    CHECK(ok);
    CHECK_EQ((int)out.vertices.size(), 6);
    CHECK_EQ((int)out.polygons.size(), 8);          // 8 tris preserved
    CHECK_EQ(out.View()->polyCount, 8);

    // Golden world coords: with yaw=90, model (x,0,z) -> world
    //   wx = x*cos + z*(-sin) + 100 = -z + 100  (cos=0, -sin=-1)
    //   wz = x*sin + z*cos    + 50  =  x + 50   (sin=1, cos=0)
    // vertex 0 = (+1,0,0): wx=100, wy=0, wz=51.
    CHECK(Near(out.vertices[0].x, 100.0f));
    CHECK(Near(out.vertices[0].y, 0.0f));
    CHECK(Near(out.vertices[0].z, 51.0f));
    // vertex 4 = (0,0,+1): wx = -1 + 100 = 99, wz = 0 + 50 = 50.
    CHECK(Near(out.vertices[4].x, 99.0f));
    CHECK(Near(out.vertices[4].z, 50.0f));
    // vertex 2 = (0,+1,0): y carried through (col1 = identity).
    CHECK(Near(out.vertices[2].y, 1.0f));

    // Polygon 0's first vertex pointer must point at the rebound transformed v0.
    CHECK(out.polygons[0].v0 == &out.vertices[0]);
}

// --- project the seated mesh through the REAL projection (golden append) --------
TEST(ObjectMeshRenderUnit, ProjectSeatedMeshAppends) {
    SynthMesh sm;
    // Place the mesh near the framebuffer center so its projected tris are on-screen.
    WorldPlacement wp{0.0f, 0.0f, 0.0f, 0.0f, true};
    WorldMesh out;
    CHECK(TransformMeshGeometry(sm.geom, wp, out));

    // Projection params: map world (x,z) -> a 96x72 frame at 8 px/unit, eye centered.
    const int fbW = 96, fbH = 72;
    const float ppu = 8.0f;
    render::ProjectParams pp{};
    pp.eye[0] = 0.0f - ((float)fbW * 0.5f) / ppu;
    pp.eye[2] = 0.0f - ((float)fbH * 0.5f) / ppu;
    pp.eye[1] = 0.0f;
    pp.invDepth[0] = pp.invDepth[1] = pp.invDepth[2] = ppu;
    pp.biasX = 0.0f; pp.scaleX = ppu; pp.scaleY = 0.0f;
    pp.lightCap = 254.0f; pp.screenW = (float)fbW;

    render::DrawListEntry entries[64];
    render::DrawList dl{entries, 0, 64};
    // double-sided so both windings append; the on-screen clip still applies.
    render::ProjectVerticesToScreen(out.View(), pp, 0x40, 0, &dl);

    // GOLDEN: a centered unit octahedron projects to 8 model tris, but with the
    // ground projection (model x->screenX, z->screenY, y ignored) the top 4 and
    // bottom 4 tris collapse to the same 2D triangles with opposite winding, so
    // exactly HALF (4) survive the signed-area backface test and append. (Verified
    // against the real ProjectVerticesToScreen at 8px/unit, eye-centered.)
    CHECK_EQ(dl.count, 4);
    // The first appended entry's sort key is 768 * maxVertexLightIdx; lightIdx
    // clamps to >=1 (scaleY=0 -> term 0 -> clamp 1), so sortKey == 768.
    CHECK(dl.count > 0);
    if (dl.count > 0)
        CHECK_EQ(entries[0].sortKey, 768u);
}

// =============================================================================
// TEXTURED RASTER (Wave 30): RasterTexturedTriangleAffine samples the real BMP
// texel at the affine-interpolated UV per pixel.
// =============================================================================
namespace {

// 4x4 24-bit texture, each texel a distinct colour so a sampled pixel is
// uniquely identifiable: colour(x,y) = (10+40x, 20+50y, 30).
std::vector<u8> Make4x4Tex() {
    const int w = 4, h = 4;
    std::vector<u8> rgb((std::size_t)w * h * 3, 0);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            std::size_t i = ((std::size_t)y * w + x) * 3;
            rgb[i + 0] = (u8)(10 + 40 * x);   // R
            rgb[i + 1] = (u8)(20 + 50 * y);   // G
            rgb[i + 2] = 30;                   // B
        }
    return render::BmpSave24Bit(w, h, rgb.data());
}

// Build a screen-space Vertex with given screen x/y and texel UV (normalized).
render::Vertex SV(float sx, float sy, float u, float v) {
    render::Vertex r{};
    r.screenX = sx; r.screenY = sy; r.u = u; r.v = v;
    r.lightIdx = 200;
    return r;
}

// Read back a 16bpp pixel and unpack to RGB (565 round-trip tolerant compare).
void PxRGB(const render::Surface* s, int x, int y, u8 out[3]) {
    render::SurfaceGetPixelRgb(s, x, y, out);
}

} // namespace

// --- affine textured triangle paints the sampled texel ------------------------
TEST(ObjectMeshRenderUnit, TexturedTriangleSamplesTexel) {
    play::RealTextureSource src;
    std::vector<u8> bmp = Make4x4Tex();
    const render::DecodedBmp* tex = src.bin().DecodeBuffer("t4.BMP", bmp);
    CHECK(tex != nullptr);
    if (!tex) return;
    CHECK_EQ(tex->width, 4);

    render::Surface* fb = render::SurfaceCreate(40, 40, 16);
    CHECK(fb != nullptr);
    render::SurfaceColorFill(fb, 0, 0, 0);

    // A big triangle whose THREE corner UVs all point at texel (2,1) center
    // (u=2.5/4, v=1.5/4) -> every covered pixel samples that one texel, so the
    // expected colour is exactly colour(2,1) = (90, 70, 30).
    const float U = 2.5f / 4.0f, V = 1.5f / 4.0f;
    render::Vertex a = SV( 5.0f,  5.0f, U, V);
    render::Vertex b = SV(35.0f,  8.0f, U, V);
    render::Vertex c = SV(10.0f, 34.0f, U, V);

    int painted = play::RasterTexturedTriangleAffine(fb, &a, &b, &c, *tex);
    CHECK(painted > 50);   // a sizeable filled triangle

    // An interior pixel must carry the sampled texel colour (565 round-trip:
    // compare against the packed-then-unpacked expected texel).
    u8 exp[3]; { render::Surface* tmp = render::SurfaceCreate(1, 1, 16);
        render::SurfaceSetPixelRgb(tmp, 0, 0, 90, 70, 30);
        render::SurfaceGetPixelRgb(tmp, 0, 0, exp);
        render::SurfaceDestroy(tmp); }
    u8 got[3]; PxRGB(fb, 15, 15, got);
    CHECK_EQ((int)got[0], (int)exp[0]);
    CHECK_EQ((int)got[1], (int)exp[1]);
    CHECK_EQ((int)got[2], (int)exp[2]);

    // A different texel's UV produces a DIFFERENT colour (texel (0,3) center).
    render::SurfaceColorFill(fb, 0, 0, 0);
    const float U2 = 0.5f / 4.0f, V2 = 3.5f / 4.0f;   // colour(0,3) = (10,170,30)
    a.u = b.u = c.u = U2; a.v = b.v = c.v = V2;
    play::RasterTexturedTriangleAffine(fb, &a, &b, &c, *tex);
    u8 got2[3]; PxRGB(fb, 15, 15, got2);
    bool differ = got2[0] != got[0] || got2[1] != got[1] || got2[2] != got[2];
    CHECK(differ);

    render::SurfaceDestroy(fb);
}

// --- affine UV interpolation across the triangle (two corners -> two texels) ---
TEST(ObjectMeshRenderUnit, TexturedTriangleInterpolatesUV) {
    play::RealTextureSource src;
    std::vector<u8> bmp = Make4x4Tex();
    const render::DecodedBmp* tex = src.bin().DecodeBuffer("t4b.BMP", bmp);
    CHECK(tex != nullptr);
    if (!tex) return;

    render::Surface* fb = render::SurfaceCreate(64, 16, 16);
    render::SurfaceColorFill(fb, 0, 0, 0);

    // A wide flat triangle spanning x in [2,60] with U ramping 0 -> ~1 across it,
    // V fixed at row 0 (v=0.5/4). Left pixels sample texel x=0, right x=3, so the
    // left and right halves carry different texel colours -> the U gradient shows.
    const float Vrow = 0.5f / 4.0f;
    render::Vertex a = SV( 2.0f,  2.0f, 0.05f, Vrow);
    render::Vertex b = SV(60.0f,  2.0f, 0.95f, Vrow);
    render::Vertex c = SV( 2.0f, 14.0f, 0.05f, Vrow);
    int painted = play::RasterTexturedTriangleAffine(fb, &a, &b, &c, *tex);
    CHECK(painted > 50);

    // Sample near the TOP row where the triangle is widest (the hypotenuse runs
    // b(60,2)->c(2,14), so at y=3 the right edge reaches x~55).
    u8 left[3], right[3];
    PxRGB(fb,  6, 3, left);
    PxRGB(fb, 52, 3, right);
    // Left samples a low-x texel (small R), right a high-x texel (large R).
    CHECK(right[0] > left[0]);

    render::SurfaceDestroy(fb);
}

// --- ObjectMeshRenderer: textured ON vs OFF over the same synthetic mesh -------
namespace {
play::RealTextureSource* g_unitTexSrc = nullptr;
const play::MaterialTextureTable* g_unitTable = nullptr;
const play::MaterialTextureTable* UnitTableResolver(const guild::play::EntityRef&) {
    return g_unitTable;
}
} // namespace

TEST(ObjectMeshRenderUnit, RendererTexturedDiffersFromFlat) {
    using namespace guild::sim;
    ResetEntityArrays();
    g_objects[0].alive = 1; g_objects[0].id = 7;

    // A 2-poly quad mesh (engine stride) with full-tex UVs, model space at origin.
    static render::Vertex mv[4];
    static render::Polygon mp[2];
    static render::MeshGeometry mg{};
    auto setv = [&](int i, float x, float z, float u, float v) {
        mv[i] = render::Vertex{}; mv[i].x = x; mv[i].y = 0; mv[i].z = z;
        mv[i].u = u; mv[i].v = v; mv[i].lightIdx = 200;
    };
    setv(0, -8, -8, 0.05f, 0.05f); setv(1, 8, -8, 0.95f, 0.05f);
    setv(2, -8,  8, 0.05f, 0.95f); setv(3, 8,  8, 0.95f, 0.95f);
    mp[0] = render::Polygon{}; mp[0].v0=&mv[0]; mp[0].v1=&mv[1]; mp[0].v2=&mv[2];
    mp[1] = render::Polygon{}; mp[1].v0=&mv[1]; mp[1].v1=&mv[3]; mp[1].v2=&mv[2];
    mg.vertices=mv; mg.polygons=mp; mg.polyCount=2; mg.polyCap=2; mg.vertexCount=4;

    // Texture source + a 2-poly table (both polys -> texId 0).
    static play::RealTextureSource src;
    g_unitTexSrc = &src;
    std::vector<u8> bmp = Make4x4Tex();
    CHECK(src.bin().DecodeBuffer("UnitTile.BMP", bmp) != nullptr);
    render::BgfModel model;
    model.materialCount = 1; model.materials.resize(1);
    model.materials[0].name0 = "UnitTile";
    model.vertexCount = 4; model.vertices.resize(4);
    model.polyCount = 2; model.polygons.resize(2);
    model.polygons[0].matIndex = 0; model.polygons[1].matIndex = 0;
    g_unitTable = src.BuildTableFor("UnitMesh", model);
    CHECK(g_unitTable != nullptr);
    if (g_unitTable) CHECK_EQ(g_unitTable->texturedPolys, 2);

    auto nodeRes = [](const guild::play::EntityRef&) -> const void* {
        static unsigned char nb[640];
        std::memset(nb, 0, sizeof nb);
        const float m[16] = {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
        std::memcpy(nb + 396, m, sizeof m);
        nb[533] = 1;   // drawable mesh
        return nb;
    };
    auto meshRes = [](const guild::play::EntityRef&) -> const render::MeshGeometry* {
        return &mg;
    };

    play::ObjectMeshRenderer::Options opt;
    opt.nodeResolver = +nodeRes;
    opt.meshResolver = +meshRes;
    opt.scanScene = false; opt.scanObjects = true;
    opt.pixelsPerUnit = 4.0f;

    // --- FLAT (textured OFF, the default) ---
    render::Surface* flat = render::SurfaceCreate(96, 72, 16);
    render::SurfaceColorFill(flat, 0, 0, 0);
    play::ObjectMeshRenderer rf;
    play::MeshRenderStats sf = rf.render(opt, flat);
    CHECK_EQ(sf.texturedPolys, 0);

    // --- TEXTURED (ON) ---
    opt.textured = true;
    opt.texTableResolver = &UnitTableResolver;
    render::Surface* texd = render::SurfaceCreate(96, 72, 16);
    render::SurfaceColorFill(texd, 0, 0, 0);
    play::ObjectMeshRenderer rt;
    play::MeshRenderStats st = rt.render(opt, texd);
    CHECK(st.texturedPolys > 0);    // polys bound + sampled from the real texture

    // The two frames must DIFFER (flat shade vs real texels).
    const u16* a = reinterpret_cast<const u16*>(flat->pixels);
    const u16* b = reinterpret_cast<const u16*>(texd->pixels);
    int total = flat->widthPx * flat->height, diff = 0;
    for (int i = 0; i < total; ++i) if (a[i] != b[i]) ++diff;
    CHECK(diff > 20);

    render::SurfaceDestroy(flat);
    render::SurfaceDestroy(texd);
    g_unitTexSrc = nullptr; g_unitTable = nullptr;
    ResetEntityArrays();
}
