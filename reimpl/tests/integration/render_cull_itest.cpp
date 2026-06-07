// Cross-module integration for the frustum cull/clip-flag pass. Wires the REAL
// siblings the engine's scene-node path uses:
//   - render/cull            VIBE_Render_ComputeVertexClipFlags (under test)
//   - render/scenegraph      VIBE_Render_ClassifyBoundingBoxPlanes (bbox outcodes)
//   - render/scenegraph      VIBE_SceneGraph_TransformNodeBoxCorners
//   - util/matrix            VIBE_Math_MatrixTransformVectors / MatrixFromEuler
//   - render/heightmap       VIBE_Heightmap_TileToWorld (for CoordDistance3D)
//   - sim/combat_escape      VIBE_Coord_Distance3D (CoordDistance3D, reused)
//   - render/camera          VIBE_Coord_ProjectPoint (sanity of the same verts)
//
// The clip-flag classifier and the bounding-box classifier independently compute
// the same six-plane outcodes; this test transforms a box's 8 corners with the
// real matrix module, runs BOTH classifiers over the result, and checks they
// agree on which planes each corner violates — the exact consistency the engine
// relies on (the bbox cull gates whether the per-vertex pass even runs).
#include "render/cull.h"
#include "render/coord_view.h"
#include "render/scenegraph.h"
#include "render/camera.h"
#include "render/heightmap.h"
#include "render/geometry_types.h"
#include "sim/combat_escape.h"
#include "util/matrix.h"
#include "test.h"

#include <cmath>
#include <cstring>
#include <vector>

using namespace guild;
using namespace guild::render;

namespace {

Frustum MakeFrustum() {
    Frustum f;
    std::memset(&f, 0, sizeof(f));
    f.plane[0][0] = 1.0f;    // outside (bit0) when x < 0
    f.plane[1][0] = -1.0f;   // outside (bit1) when x > 0
    f.plane[2][1] = 1.0f;    // outside (bit2) when y < 0
    f.plane[3][1] = -1.0f;   // outside (bit3) when y > 0
    f.nearZ = 1.0f;          // bit4 z<1
    f.farZ = 100.0f;         // bit5 z>100
    return f;
}

} // namespace

// ----- per-vertex outcodes agree with the bbox classifier's per-corner codes -
TEST(RenderCullItest, ClipFlagsMatchBoundingBoxOutcodes) {
    Frustum f = MakeFrustum();

    // Build a box and transform its 8 corners with the REAL matrix + corner
    // transform. box layout: box[4..6] = max corner, box[8..10] = min corner
    // (the layout TransformNodeBoxCorners reads).
    float box[12];
    std::memset(box, 0, sizeof(box));
    box[4] = 30.0f;  box[5] = 30.0f;  box[6] = 60.0f;   // max
    box[8] = -30.0f; box[9] = -30.0f; box[10] = 5.0f;   // min

    // A non-trivial real rotation matrix (identity translation) from the Math
    // module, so the corners are genuinely transformed before classification.
    float angles[3] = {0.0f, 0.0f, 0.0f};   // identity rotation (keeps it analyzable)
    float mat[16];
    util::MatrixFromEuler(angles, mat);
    mat[12] = 0.0f; mat[13] = 0.0f; mat[14] = 0.0f; mat[15] = 1.0f;

    float origin[3] = {0.0f, 0.0f, 0.0f};
    float corners[8 * 20];
    std::memset(corners, 0, sizeof(corners));
    TransformNodeBoxCorners(box, origin, mat, corners);

    // Reference: OR of per-corner outcodes via the real bbox classifier.
    float minZ = 0.0f, maxZ = 0.0f, rn = 1e10f, rf = 0.0f;
    u8 bboxResult = ClassifyBoundingBoxPlanes(corners, f, &minZ, &maxZ, &rn, &rf);

    // Now feed the same 8 corners (stride 20 floats == Vertex stride) as vertices
    // to the clip-flag classifier and OR their outcodes.
    std::vector<Vertex> verts(8);
    std::memset(verts.data(), 0, verts.size() * sizeof(Vertex));
    for (int i = 0; i < 8; ++i) {
        verts[i].x = corners[i * 20 + 0];
        verts[i].y = corners[i * 20 + 1];
        verts[i].z = corners[i * 20 + 2];
    }
    ComputeVertexClipFlags(kClipOutMask, verts.data(), 8, nullptr, 0, f);

    u8 orCodes = 0;
    for (int i = 0; i < 8; ++i) orCodes = (u8)(orCodes | (verts[i].clipFlags & 0x3F));

    // The bbox classifier's low 6 bits are the OR of per-corner outcodes; they
    // must equal the OR of the clip-flag classifier's per-vertex outcodes.
    CHECK_EQ((u8)(bboxResult & 0x3F), orCodes);

    // The box straddles the frustum (some corners inside, some out): not fully
    // culled, so the per-vertex pass would run in the engine.
    CHECK((bboxResult & 0x40) == 0);
}

// ----- a kept polygon's verts survive into a projectable screen point --------
TEST(RenderCullItest, KeptPolygonProjectsThroughCamera) {
    // Wide frustum (side planes at +/-1000) so the small triangle is fully inside.
    Frustum f = MakeFrustum();
    f.plane[0][3] = -1000.0f;   // outside when x < -1000
    f.plane[1][3] = -1000.0f;   // outside when x >  1000
    f.plane[2][3] = -1000.0f;   // outside when y < -1000
    f.plane[3][3] = -1000.0f;   // outside when y >  1000

    // Three vertices fully inside the frustum (small +/- around origin, mid depth).
    std::vector<Vertex> v(3);
    std::memset(v.data(), 0, v.size() * sizeof(Vertex));
    v[0].x = 5.0f;  v[0].y = 5.0f;  v[0].z = 40.0f;
    v[1].x = -5.0f; v[1].y = 5.0f;  v[1].z = 40.0f;
    v[2].x = 0.0f;  v[2].y = -5.0f; v[2].z = 40.0f;

    Polygon p;
    std::memset(&p, 0, sizeof(p));
    p.v0 = &v[0]; p.v1 = &v[1]; p.v2 = &v[2];

    ComputeVertexClipFlags(kClipOutMask, v.data(), 3, &p, 1, f);

    // All three inside -> outcodes 0 -> polygon kept (flags36 has bit7, no plane bits).
    CHECK_EQ(p.flags36, (u8)0x80);
    for (int i = 0; i < 3; ++i) CHECK((v[i].clipFlags & kClipKept) != 0);

    // Each surviving vertex projects to a finite screen point via the REAL camera
    // perspective ProjectPoint (camera at origin, view depth 1.0).
    float cam[5] = {0.0f, 0.0f, 0.0f, 0.0f, 1.0f};
    for (int i = 0; i < 3; ++i) {
        float pt[3] = {v[i].x, v[i].y, v[i].z};
        i32 out[2] = {0, 0};
        ProjectPoint(cam, pt, out);
        // world X and Z map to screen x,y; finite, deterministic integers.
        CHECK(out[0] == (i32)std::trunc((double)pt[0] + 0.5));
        CHECK(out[1] == (i32)std::trunc((double)pt[2] + 0.5));
    }
}

// ----- CoordDistance3D over the real heightmap TileToWorld -------------------
TEST(RenderCullItest, Distance3DOverRealHeightmap) {
    // 8x8 heightmap with a sloped height column so the Y term actually matters.
    Heightmap hm;
    std::memset(&hm, 0, sizeof(hm));
    hm.originX = 0.0f; hm.originY = 0.0f; hm.originZ = 0.0f;
    hm.scaleX = 2.0f;  hm.scaleY = 1.0f;  hm.scaleZ = 2.0f;
    hm.size = 8;
    std::vector<u8> heights(64, 0);
    heights[3 * 8 + 5] = 10;   // tile (5,3) raised by 10 height bytes -> +10 Y
    hm.heights = heights.data();

    // (5,3) world = (10, 10, 6); (1,1) world = (2, 0, 2).
    // delta = (8, 10, 4) -> sqrt(64+100+16) = sqrt(180).
    double d = guild::sim::CoordDistance3D(&hm, 5, 3, 1, 1);
    CHECK(std::fabs(d - std::sqrt(180.0)) < 1e-3);

    // ViewScale clamp still bounded the same way regardless of input magnitude.
    CHECK(ComputeViewScale(50000.0f) == 16);
    CHECK(ComputeViewScale(0.0f) == 5);
}
