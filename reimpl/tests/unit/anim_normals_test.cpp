// gilde.exe 0x5d0020 — VIBE_Anim_CalculateAnimNormals golden tests. Deterministic meshes:
// a flat quad (face normals +Z, all vertex normals +Z), translated/scaled across frames,
// and the 3-frame neighbour-union bbox smoothing. Plus the normal->byte quantizer.
#include "tests/framework/test.h"
#include "render/anim_normals.h"
#include <array>
#include <cmath>
#include <vector>
using namespace guild;
using namespace guild::render;

static bool near(float a, float b) { return std::fabs(a-b) < 1e-4f; }

TEST(AnimNormals, FlatQuadNormalsPointUp) {
    // quad verts (0,0,0)(1,0,0)(1,1,0)(0,1,0); two CCW triangles -> +Z faces.
    std::vector<std::vector<float>> frames = {
        {0,0,0,  1,0,0,  1,1,0,  0,1,0},
    };
    std::vector<std::array<int,3>> tris = {{0,1,2},{0,2,3}};
    std::vector<std::vector<float>> N; std::vector<AnimFrameBounds> B;
    CalculateAnimNormals(frames, tris, 4, N, B);
    CHECK_EQ((int)N.size(), 1);
    for (int v = 0; v < 4; ++v) {
        CHECK(near(N[0][3*v+0], 0.0f));
        CHECK(near(N[0][3*v+1], 0.0f));
        CHECK(near(N[0][3*v+2], 1.0f));
    }
    CHECK(near(B[0].bbMin[0], 0.0f)); CHECK(near(B[0].bbMax[0], 1.0f));
    CHECK(near(B[0].bbMin[2], 0.0f)); CHECK(near(B[0].bbMax[2], 0.0f));
}

TEST(AnimNormals, BboxNeighbourUnionSmoothing) {
    std::vector<std::array<int,3>> tris = {{0,1,2},{0,2,3}};
    std::vector<std::vector<float>> frames = {
        {0,0,0,  1,0,0,  1,1,0,  0,1,0},        // f0: z=0,  xy [0,1]
        {0,0,5,  1,0,5,  1,1,5,  0,1,5},        // f1: z=5
        {-1,-1,0, 2,-1,0, 2,2,0, -1,2,0},       // f2: xy [-1,2], z=0
    };
    std::vector<std::vector<float>> N; std::vector<AnimFrameBounds> B;
    CalculateAnimNormals(frames, tris, 4, N, B);
    // f0 smoothed = union(raw0, raw1): z in [0,5], xy [0,1]
    CHECK(near(B[0].bbMin[2], 0.0f)); CHECK(near(B[0].bbMax[2], 5.0f));
    CHECK(near(B[0].bbMin[0], 0.0f)); CHECK(near(B[0].bbMax[0], 1.0f));
    // f1 smoothed = union(raw0,raw1,raw2): xy [-1,2], z [0,5]
    CHECK(near(B[1].bbMin[0], -1.0f)); CHECK(near(B[1].bbMax[0], 2.0f));
    CHECK(near(B[1].bbMin[2], 0.0f));  CHECK(near(B[1].bbMax[2], 5.0f));
    // f2 smoothed = union(raw1,raw2): xy [-1,2], z [0,5]
    CHECK(near(B[2].bbMin[0], -1.0f)); CHECK(near(B[2].bbMax[0], 2.0f));
    CHECK(near(B[2].bbMax[2], 5.0f));
    // normals still +Z on every frame
    for (int f = 0; f < 3; ++f) for (int v = 0; v < 4; ++v) CHECK(near(N[f][3*v+2], 1.0f));
}

TEST(AnimNormals, NormalByteQuantization) {
    CHECK_EQ((int)QuantizeNormalByte( 1.0f), 255);   // (1+1)*0.5*255 = 255
    CHECK_EQ((int)QuantizeNormalByte(-1.0f), 0);     // (0)*... = 0
    CHECK_EQ((int)QuantizeNormalByte( 0.0f), 127);   // 0.5*255 = 127.5 -> trunc 127
}

TEST(AnimNormals, EmptyClipSafe) {
    std::vector<std::vector<float>> N; std::vector<AnimFrameBounds> B;
    CalculateAnimNormals({}, {{0,1,2}}, 4, N, B);
    CHECK_EQ((int)N.size(), 0);
    CHECK_EQ((int)B.size(), 0);
}
