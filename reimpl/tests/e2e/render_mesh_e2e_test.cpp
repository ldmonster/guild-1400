#include "tests/framework/test.h"

#include "render/model_io.h"
#include "render/mesh.h"
#include "render/scenegraph.h"

#include <cmath>
#include <vector>

using namespace guild::render;

namespace {
bool feq(float a, float b, float eps = 1e-5f) { return std::fabs(a - b) <= eps; }

// Synthetic GMDL buffer: one back-facing triangle spanning z=4..12, produced by
// the python reference. Verts: (-1,2,4),(3,2,4),(1,6,12).
const guild::u8 kModel[] = {
    71,77,68,76, 3,0,0,0, 1,0,0,0,
    0,0,128,191, 0,0,0,64, 0,0,128,64, 0,0,0,0, 0,0,0,0, 0,
    0,0,64,64,   0,0,0,64, 0,0,128,64, 0,0,0,0, 0,0,0,0, 0,
    0,0,128,63,  0,0,192,64, 0,0,64,65, 0,0,0,0, 0,0,0,0, 0,
    0,0,0,0, 1,0,0,0, 2,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0, 0, 0};
} // namespace

// Full geometry flow: load model -> place node in scene -> cull -> project.
TEST(RenderE2E, LoadCullProjectTriangle) {
    // 1. Load the synthetic model buffer.
    Model model;
    CHECK(LoadSyntheticModel(kModel, sizeof(kModel), model));
    MeshGeometry* geom = model.View();
    CHECK_EQ(geom->vertexCount, 3);
    CHECK_EQ(geom->polyCount, 1);

    // 2. Build a frustum and place the model's bounding box as a scene node.
    //    The triangle spans z=4..12; a node box max=(3,6,12) min=(-1,2,4).
    Frustum f{};
    for (int i = 0; i < 4; ++i) f.plane[i][3] = -1e9f;
    f.nearZ = 0.0f; f.farZ = 100.0f;

    float box[11] = {0,0,0,0, 3,6,12, 0,-1,2,4};
    float origin[3] = {0,0,0};
    float ident[16] = {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
    float corners[8 * 20] = {0};
    TransformNodeBoxCorners(box, origin, ident, corners);

    // 3. Cull against the frustum: node is inside (0..100) => visible (no 0x40).
    guild::u8 code = ClassifyBoundingBoxPlanes(corners, f, nullptr, nullptr, nullptr, nullptr);
    CHECK_EQ((int)(code & 0x40), 0);  // visible

    // 4. Project the visible geometry through a known camera.
    ProjectParams pp{};
    pp.eye[0]=0; pp.eye[1]=0; pp.eye[2]=0;
    pp.invDepth[0]=0; pp.invDepth[1]=0.5f; pp.invDepth[2]=0;
    pp.biasX=0.875f; pp.scaleX=0.25f; pp.scaleY=10.0f;
    pp.lightCap=254.0f; pp.screenW=1000.0f;

    DrawListEntry dl[8];
    DrawList out{dl, 0, 8};
    // Double-sided (0x40) so the single tri is appended regardless of winding.
    ProjectVerticesToScreen(geom, pp, /*objFlags530*/0x40, /*viewCull42*/0, &out);

    // 5. Verify the resulting projected vertex list against the python reference.
    struct { float sx, sy; int light; } g[3] = {
        {0.375f, 1.875f, 20}, {2.375f, 1.875f, 20}, {1.375f, 3.875f, 60}};
    for (int i = 0; i < 3; ++i) {
        CHECK(feq(model.vertices[i].screenX, g[i].sx));
        CHECK(feq(model.vertices[i].screenY, g[i].sy));
        CHECK_EQ((int)model.vertices[i].lightIdx, g[i].light);
    }

    // The one visible (back-facing) triangle made it into the draw list with
    // sortKey = 768 * max light index = 768 * 60 = 46080.
    CHECK_EQ(out.count, 1);
    CHECK_EQ((int)out.entries[0].sortKey, 768 * 60);
    CHECK(out.entries[0].poly == &model.polygons[0]);
}
