#include "render/mesh_load.h"
#include "render/mesh_postprocess.h"
#include "render/bgf_loader.h"
#include "render/raster.h"
#include "render/surface.h"
#include "tests/framework/test.h"

#include <cmath>
#include <cstring>
#include <vector>

using namespace guild;
using namespace guild::render;

// The mock texture loader is defined once in the unit-test TU; force a constant
// slot id (7) for this suite via the shared g_texFixed global (set per-test).
namespace guild::render { extern int g_texFixed; }

namespace {

void putU32(std::vector<u8>& b, u32 v) {
    b.push_back((u8)(v & 0xFF));
    b.push_back((u8)((v >> 8) & 0xFF));
    b.push_back((u8)((v >> 16) & 0xFF));
    b.push_back((u8)((v >> 24) & 0xFF));
}
void putF32(std::vector<u8>& b, float f) {
    u32 v;
    std::memcpy(&v, &f, 4);
    putU32(b, v);
}
void putStr(std::vector<u8>& b, const char* s) {
    for (const char* p = s; *p; ++p)
        b.push_back((u8)*p);
    b.push_back(0);
}

// Build a synthetic .BGF fast-chunk byte buffer for a cube:
//   8 unique corner vertices, 12 triangles, 1 material, 0 dummies.
std::vector<u8> BuildCubeBgf() {
    const float c[8][3] = {
        {-1,-1,-1},{1,-1,-1},{1,1,-1},{-1,1,-1},
        {-1,-1,1},{1,-1,1},{1,1,1},{-1,1,1}};
    // 12 triangles (CCW-ish, indices into the 8 corners).
    const int tris[12][3] = {
        {0,1,2},{0,2,3},  // -Z
        {4,6,5},{4,7,6},  // +Z
        {0,4,5},{0,5,1},  // -Y
        {3,2,6},{3,6,7},  // +Y
        {1,5,6},{1,6,2},  // +X
        {0,3,7},{0,7,4},  // -X
    };
    const u32 matCount = 1, vtxCount = 8, polyCount = 12;

    std::vector<u8> chunk;
    // Header.
    putU32(chunk, matCount);
    putU32(chunk, vtxCount);
    putU32(chunk, polyCount);
    // Vertex block: (vtxCount + 8) records of (pos[3], normal[3]).
    for (u32 i = 0; i < vtxCount + 8; ++i) {
        if (i < vtxCount) {
            putF32(chunk, c[i][0]); putF32(chunk, c[i][1]); putF32(chunk, c[i][2]);
        } else {
            putF32(chunk, 0); putF32(chunk, 0); putF32(chunk, 0);
        }
        putF32(chunk, 0); putF32(chunk, 0); putF32(chunk, 0);  // normal
    }
    // Object flags.
    putU32(chunk, 0);
    // Poly block: vtx[3] (u32), uv0(vec3), uv1(vec3), uv2(vec3), matIndex(byte).
    for (u32 i = 0; i < polyCount; ++i) {
        putU32(chunk, (u32)tris[i][0]);
        putU32(chunk, (u32)tris[i][1]);
        putU32(chunk, (u32)tris[i][2]);
        putF32(chunk, 0); putF32(chunk, 0); putF32(chunk, 0);  // uv0
        putF32(chunk, 0); putF32(chunk, 0); putF32(chunk, 0);  // uv1
        putF32(chunk, 0); putF32(chunk, 0); putF32(chunk, 0);  // uv2
        chunk.push_back(0);  // matIndex byte (materialCount<=254)
    }
    // Material block: name0, name1, name2 (strings) + 6 flag bytes.
    putStr(chunk, "CUBETEX");
    putStr(chunk, "");
    putStr(chunk, "");
    for (int k = 0; k < 6; ++k) chunk.push_back(0);
    // Dummy count.
    putU32(chunk, 0);

    // Wrap as a script stream: 4-byte leading tag, '-' chunk token, u32 chunkSize,
    // u32 magic, then the chunk body.
    std::vector<u8> file;
    putU32(file, 0x12345678);            // leading tag (skipped)
    file.push_back(0x2D);                // '-' chunk header follows
    putU32(file, (u32)(chunk.size() + 4)); // chunkSize (includes the magic)
    putU32(file, kBgfFastChunkMagic);    // magic
    file.insert(file.end(), chunk.begin(), chunk.end());
    return file;
}

bool feq(float a, float b, float e = 1e-4f) { return std::fabs(a - b) <= e; }

} // namespace

// ---- Full orchestrator: parse -> dedup -> bake -> bounds -> normals --------
TEST(MeshLoadE2E, LoadCubeFull) {
    g_texFixed = 7;
    std::vector<u8> file = BuildCubeBgf();

    Mesh m;
    bool ok = LoadBgfFile(file.data(), file.size(), "CUBE.BGF", m);
    CHECK(ok);

    // Cube has 8 unique corners (no duplicates), 12 triangles.
    CHECK_EQ(m.vertexCount, 8);
    CHECK_EQ((int)m.polygons.size(), 12);
    CHECK_EQ(m.materialCount, 1);

    // The morph-bake maps (x,y,z) -> (-x, z, y); for the symmetric +/-1 cube the
    // bounds stay [-1,1]^3 (python golden).
    // HARDEN (gilde.exe 0x5d1ff5): +472 (m.radius) ends as the AABB diagonal,
    // +468 (m.radius2) holds max-|vertex| — these were swapped vs the binary.
    CHECK(feq(m.radius, 2.0f * std::sqrt(3.0f)));
    CHECK(feq(m.radius2, std::sqrt(3.0f)));
    CHECK(feq(m.centroid[0], 0.0f));
    CHECK(feq(m.centroid[1], 0.0f));
    CHECK(feq(m.centroid[2], 0.0f));

    // Every poly bound its material's resolved texture id (mock returns 7).
    for (const MeshPolygon& p : m.polygons)
        CHECK_EQ((int)p.texId, 7);

    // Every vertex normal is unit length (each cube corner touches >=1 face).
    for (int i = 0; i < m.vertexCount; ++i) {
        const float* n = m.vertices[i].normal;
        float len = std::sqrt(n[0]*n[0] + n[1]*n[1] + n[2]*n[2]);
        CHECK(feq(len, 1.0f, 1e-3f));
    }
    // Each polygon got a unit face normal.
    for (const MeshPolygon& p : m.polygons) {
        float len = std::sqrt(p.normal[0]*p.normal[0] + p.normal[1]*p.normal[1] +
                              p.normal[2]*p.normal[2]);
        CHECK(feq(len, 1.0f, 1e-3f));
    }
}

// ---- Project the loaded cube + rasterize it to confirm it renders ----------
TEST(MeshLoadE2E, ProjectAndRasterize) {
    g_texFixed = 7;
    std::vector<u8> file = BuildCubeBgf();
    Mesh m;
    CHECK(LoadBgfFile(file.data(), file.size(), "CUBE.BGF", m));

    const int W = 64, H = 64;
    Surface* fb = SurfaceCreate(W, H, 8);  // 8bpp: flat raster writes the index byte
    CHECK(fb != nullptr);
    SurfaceColorFill(fb, 0, 0, 0);

    // Simple orthographic projection: model XY (after bake) -> screen, scaled and
    // centered. Draw every front-facing-ish triangle as a flat span fill.
    auto project = [&](const MeshVertex& v, RasterVertex& rv) {
        rv.x = (float)W * 0.5f + v.pos[0] * 16.0f;
        rv.y = (float)H * 0.5f - v.pos[1] * 16.0f;
        rv.light = 200;
    };

    int drawn = 0;
    for (const MeshPolygon& p : m.polygons) {
        RasterVertex rv[3];
        project(m.vertices[p.vtx[0]], rv[0]);
        project(m.vertices[p.vtx[1]], rv[1]);
        project(m.vertices[p.vtx[2]], rv[2]);
        drawn += RasterizeFlatTriangle(fb, rv, 0xFF);
    }
    CHECK(drawn > 0);  // at least one triangle produced spans

    // Confirm the center region was painted (the cube projects over the middle).
    u8 rgb[3];
    SurfaceGetPixelRgb(fb, W / 2, H / 2, rgb);
    CHECK(rgb[0] != 0 || rgb[1] != 0 || rgb[2] != 0);

    SurfaceDestroy(fb);
}
