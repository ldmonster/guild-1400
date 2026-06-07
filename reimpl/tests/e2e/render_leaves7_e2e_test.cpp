#include "test.h"

#include "render/render_leaves7.h"

#include <cmath>
#include <vector>

using namespace guild;
using guild::render::ShadowClipRect;
using guild::render::GroundVertex;

namespace {
bool approx(float a, float b, float eps = 1e-4f) {
    return std::fabs(a - b) <= eps * (1.0f + std::fabs(b));
}
} // namespace

// ---------------------------------------------------------------------------
// E2E: a full ground-shadow rasterisation pass.
//   1. ComputeShadowClipRect derives the clamped sub-rect + interp params.
//   2. For each cell in the rect, RasterizeHeightVertexY emits the vertex Y
//      (using the high-detail bias selector), and ProjectGroundVertex maps the
//      cell to a world-space position.
//   3. HeightEdgeDiscontinuous flags the silhouette edges over the grid.
// This walks the same control flow the original BuildGroundShadow ->
// RasterizeHeightField path drives, asserting the cross-function arithmetic.
// ---------------------------------------------------------------------------
TEST(RenderLeaves7_E2E, GroundShadowRasterPass) {
    const int dim = 32;
    ShadowClipRect r;
    int ok = render::ComputeShadowClipRect(dim, /*minX*/-2, /*maxX*/9,
                                           /*minY*/3, /*maxY*/12, &r);
    CHECK_EQ(ok, 1);
    if (!ok) return;

    CHECK_EQ(r.x0, 0);     // -2 clamped
    CHECK_EQ(r.x1, 9);
    CHECK_EQ(r.y0, 3);
    CHECK_EQ(r.y1, 12);

    // A synthetic height field (dim x dim) with a ridge so an edge fires.
    std::vector<unsigned char> height(static_cast<size_t>(dim) * dim, 10);
    for (int y = 0; y < dim; ++y)
        height[static_cast<size_t>(y) * dim + 6] = 90;   // tall ridge at col 6

    const bool highDetail = true;
    const float bias  = render::HeightFieldBias(highDetail);
    CHECK(approx(bias, 1.5f));
    const float yStep = 0.5f, baseY = 4.0f;
    const float xBase = 0.0f, xStep = 1.0f, zBase = 0.0f, zStep = 1.0f, yScale = 0.5f;

    int vertCount = 0;
    int edgeCount = 0;
    float maxY = -1e30f, minY = 1e30f;

    for (int row = r.y0; row <= r.y1; ++row) {
        for (int col = r.x0; col <= r.x1; ++col) {
            unsigned char h = height[static_cast<size_t>(row) * dim + col];
            float vy = render::RasterizeHeightVertexY(h, bias, yStep, baseY);
            GroundVertex pos = render::ProjectGroundVertex(
                col, row, h, xBase, xStep, zBase, zStep, yScale,
                /*groundY*/2.0f);
            // World X/Z follow the cell grid exactly.
            CHECK(approx(pos.x, static_cast<float>(col)));
            CHECK(approx(pos.z, static_cast<float>(row)));
            if (vy > maxY) maxY = vy;
            if (vy < minY) minY = vy;

            // Edge between this cell and its right neighbour (where present).
            if (col < r.x1) {
                unsigned char hr = height[static_cast<size_t>(row) * dim + col + 1];
                unsigned char hd = height[static_cast<size_t>(row) * dim + col]; // self
                if (render::HeightEdgeDiscontinuous(static_cast<float>(h),
                                                    static_cast<float>(hr),
                                                    static_cast<float>(hd)))
                    ++edgeCount;
            }
            ++vertCount;
        }
    }

    // 10 cols (0..9) x 10 rows (3..12) = 100 emitted vertices.
    CHECK_EQ(vertCount, 100);
    // The ridge (height 90 vs 10) differs by 80 > 25 on BOTH the col 5->6 and
    // col 6->7 boundaries, for each of the 10 rows -> 20 edge flags.
    CHECK_EQ(edgeCount, 20);
    // Vertex Y spans flat (10) and ridge (90) cells.
    CHECK(approx(minY, render::RasterizeHeightVertexY(10, bias, yStep, baseY)));
    CHECK(approx(maxY, render::RasterizeHeightVertexY(90, bias, yStep, baseY)));
}

// ---------------------------------------------------------------------------
// E2E: a sky-flare position pass driving the grid + per-vertex alpha.
//   ComputeFlareGrid builds the 6x8 normalised grid; FlareSunAngle derives the
//   sun rotation; FlareVertexAlpha classifies each grid vertex's depth band.
// ---------------------------------------------------------------------------
TEST(RenderLeaves7_E2E, SkyFlarePositionPass) {
    float c30[3] = {1.0f, 0.0f, 0.0f};
    float c31[3] = {0.0f, 1.0f, 0.0f};
    float c32[3] = {-1.0f, 0.0f, 0.0f};
    float c33[3] = {0.0f, -1.0f, 0.0f};
    GroundVertex grid[48];
    render::ComputeFlareGrid(c30, c31, c32, c33, grid);

    // Sun direction roughly forward -> angle wrapped into [0, 2pi).
    float up[3]  = {0.0f, 0.0f, 1.0f};
    float dir[3] = {1.0f, 0.0f, 0.0f};
    double sun = render::FlareSunAngle(up, dir);
    CHECK(sun >= -render::kTwoPi && sun <= render::kTwoPi);

    // Walk the grid; classify a synthetic per-vertex depth band and alpha.
    int litCount = 0, cappedCount = 0;
    for (int k = 0; k < 48; ++k) {
        float len = std::sqrt(grid[k].x * grid[k].x + grid[k].y * grid[k].y
                              + grid[k].z * grid[k].z);
        CHECK(approx(len, 1.0f, 1e-3f));

        // Fake depth band: vertices in the back half exceed the near ceiling.
        float band = (k < 24) ? 100.0f : 300.0f;
        float raw  = 0.5f * static_cast<float>(k);     // some computed alpha
        float a = render::FlareVertexAlpha(band, raw);
        if (band > 255.0f) {
            CHECK(approx(a, 255.0f));
            ++cappedCount;
        } else {
            CHECK(approx(a, raw >= 0.0f ? raw : 0.0f));
            ++litCount;
        }
    }
    CHECK_EQ(litCount, 24);
    CHECK_EQ(cappedCount, 24);
}
