// =============================================================================
// mesh_demo.cpp — end-to-end software-render demo of the guild::render pipeline.
//
// Builds a synthetic textured cube in memory using the real engine geometry
// records (Vertex/Polygon/MeshGeometry), places it in front of a camera, then
// drives the REAL frame pipeline:
//
//   ProjectVerticesToScreen  (project + backface cull + draw-list append)
//        -> RadixSortDrawList (stable LSB radix sort by the 768*lightIdx key)
//        -> RasterizeMeshList (clip-vs-direct decision + triangle-fan flush)
//        -> RasterizeTexturedTriangle (affine-shaded span fill into the surface)
//
// into a FileDumpGraphicsDevice 8-bit framebuffer, presenting each frame to
// demo/mesh_demo_frameNNNN.bmp. The camera (really the cube, via a per-frame
// Y-axis rotation of the model into world space) sweeps a short sequence.
//
// This is an INTEGRATION demo: it reuses the already-translated render modules
// (src/render/*.cpp). It does NOT re-translate anything. Two leaf behaviours are
// supplied as trivial local helpers and noted in the report:
//   - the mesh BUILD (we synthesise geometry directly rather than load a .BGF),
//   - the model->world Y rotation per frame (the engine's node transform; here a
//     tiny local rotate so the projected screen positions animate).
// =============================================================================
#include "shim_impl/filedump_graphics.h"
#include "render/geometry_types.h"
#include "render/mesh.h"
#include "render/scene.h"
#include "render/meshlist.h"
#include "render/clip.h"
#include "render/surface.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

using namespace guild;

namespace {

constexpr int   kW = 128, kH = 128;
constexpr int   kFrames = 6;
constexpr float kCenter = 64.0f;   // screen center (matches biasX below)

// ---------------------------------------------------------------------------
// Synthetic cube geometry. 8 corner vertices, 12 triangles (2 per face). Each
// face gets a distinct light/shade index so the affine span fill paints a known
// palette index per face (the rasterizer writes the interpolated lightIdx byte
// straight to the 8-bit framebuffer).
// ---------------------------------------------------------------------------
struct CubeModel {
    render::Vertex  verts[8];
    render::Polygon polys[12];
    render::MeshGeometry geom{};
};

// model-space corner positions (unit cube centred on origin, half-extent h).
void BuildCube(CubeModel& m, float h) {
    const float P[8][3] = {
        {-h,-h,-h}, { h,-h,-h}, { h, h,-h}, {-h, h,-h},
        {-h,-h, h}, { h,-h, h}, { h, h, h}, {-h, h, h},
    };
    std::memset(m.verts, 0, sizeof(m.verts));
    for (int i = 0; i < 8; ++i) {
        m.verts[i].x = P[i][0];
        m.verts[i].y = P[i][1];
        m.verts[i].z = P[i][2];
    }
    // 6 faces, 2 tris each. CCW winding (as seen from outside). The per-vertex
    // light/shade index is computed by ProjectVerticesToScreen from the depth
    // term, so a face's screen colour is its world-Y depth shade (top brighter).
    struct Face { int a,b,c,d; };
    const Face F[6] = {
        {0,1,2,3},  // -Z (front)
        {5,4,7,6},  // +Z (back)
        {4,0,3,7},  // -X (left)
        {1,5,6,2},  // +X (right)
        {3,2,6,7},  // +Y (top)
        {4,5,1,0},  // -Y (bottom)
    };
    int t = 0;
    for (int f = 0; f < 6; ++f) {
        int idx[4] = {F[f].a, F[f].b, F[f].c, F[f].d};
        const int tris[2][3] = {{0,1,2},{0,2,3}};
        for (int k = 0; k < 2; ++k, ++t) {
            std::memset(&m.polys[t], 0, sizeof(render::Polygon));
            m.polys[t].v0 = &m.verts[idx[tris[k][0]]];
            m.polys[t].v1 = &m.verts[idx[tris[k][1]]];
            m.polys[t].v2 = &m.verts[idx[tris[k][2]]];
        }
    }
    m.geom.vertices    = m.verts;
    m.geom.polygons    = m.polys;
    m.geom.polyCount   = 12;
    m.geom.polyCap     = 12;
    m.geom.vertexCount = 8;
}

// Rotate the model into world space about the Y (up/depth) axis by `ang`, writing
// the rotated positions back into the vertex x/y/z used by projection. (This is
// the per-frame node transform stand-in.) We rotate in the X-Z plane (the screen
// plane) so the cube visibly spins on screen.
void RotateModelY(render::Vertex* dst, const float src[8][3], float ang) {
    float c = std::cos(ang), s = std::sin(ang);
    for (int i = 0; i < 8; ++i) {
        float x = src[i][0], y = src[i][1], z = src[i][2];
        dst[i].x = x * c - z * s;
        dst[i].z = x * s + z * c;
        dst[i].y = y;                 // unchanged: drives the light/depth term
        dst[i].screenX = dst[i].screenY = 0.0f;
        dst[i].lightIdx = 0;
        // clear the +76 clip flag byte the flush reads (keep polys on the direct
        // path; no frustum straddle for this fully-on-screen cube).
        ((u8*)&dst[i])[76] = 0;
    }
}

} // namespace

int main() {
    // --- output device: 8-bit paletted file-dump framebuffer -----------------
    shim::FileDumpGraphicsDevice gfx;
    gfx.configureDump("demo", "mesh_demo_frame", shim::FileDumpGraphicsDevice::kBmp);
    gfx.init(kW, kH, 8, false);

    // Palette: a grayscale ramp so the affine depth-shaded faces are legible, with
    // index 0 reserved as a dark-blue background.
    std::uint32_t pal[256];
    for (int i = 0; i < 256; ++i)
        pal[i] = (std::uint32_t)((i << 16) | (i << 8) | i); // grayscale ramp
    pal[0] = 0x101830;  // background (dark blue)
    gfx.setPalette(pal);

    // --- build the cube once; keep the un-rotated model for per-frame spin ----
    CubeModel cube;
    BuildCube(cube, 22.0f);
    float model0[8][3];
    for (int i = 0; i < 8; ++i) {
        model0[i][0] = cube.verts[i].x;
        model0[i][1] = cube.verts[i].y;
        model0[i][2] = cube.verts[i].z;
    }
    // Projection parameters. The camera sits below the cube on the world-Y (depth)
    // axis; world X/Z map to screen x/y, world Y drives the light/depth term, so
    // lightIdx == (y - eye[1]) clamped to [1,254] (top of the cube shades brighter).
    render::ProjectParams pp{};
    pp.eye[0] = 0.0f; pp.eye[1] = -160.0f; pp.eye[2] = 0.0f;
    pp.invDepth[0] = pp.invDepth[1] = pp.invDepth[2] = 1.0f; // X scale = 1px/unit
    pp.biasX  = kCenter;     // screen-centre additive bias (so model 0 -> centre)
    pp.scaleX = 1.0f;        // Z->screenY scale
    pp.scaleY = 1.0f;        // depth/light scale: lightIdx == (y - eye[1])
    pp.lightCap = 254.0f;
    pp.screenW  = (float)kW; // on-screen clamp == framebuffer width

    // Draw-list ping-pong buffers (PolyList1/2) + radix histogram.
    std::vector<render::DrawListEntry> list1(64), list2(64);
    render::DrawListBuffers db{};
    db.base1 = list1.data();
    db.base2 = list2.data();
    db.capacity = (i32)list1.size();

    // Flush plumbing: dispatch table, empty clip context (direct path), scratch.
    render::SpanDispatch disp;            // slot[4]=opaque, slot[3]=blend
    render::ClipContext  clipCtx{0, nullptr}; // no clip planes -> direct raster
    render::ProjectScalars proj{1, 0, 1, 0};
    render::ClipScratch  scratch{};

    for (int frame = 0; frame < kFrames; ++frame) {
        // 1) animate: rotate the model into world space for this frame.
        float ang = (float)frame * 0.42f;
        RotateModelY(cube.verts, model0, ang);

        // 2) reset poly flags so re-projection re-evaluates cull each frame.
        for (int t = 0; t < 12; ++t) {
            cube.polys[t].flags36 = 0;
            cube.polys[t].flags38 = 0;
        }

        // 3) clear the backbuffer to the background index.
        shim::Surface* sh = gfx.backbuffer();
        std::memset(sh->pixels, 0, (size_t)sh->height * sh->pitch);

        // 4) bridge shim::Surface -> render::Surface over the same pixel buffer.
        render::Surface fb{};
        fb.width = sh->width; fb.height = sh->height; fb.pitch = sh->pitch;
        fb.widthPx = sh->pitch; fb.bpp = 8; fb.pixels = (u8*)sh->pixels;
        fb.clipX0 = 0; fb.clipY0 = 0; fb.clipX1 = sh->width; fb.clipY1 = sh->height;

        // 5) PROJECT + CULL + APPEND. objFlags530 = 0x40 (double-sided) so every
        //    face is considered regardless of its model-space normal.y.
        db.count = 0;
        render::DrawList sink = db.AppendSink();
        render::ProjectVerticesToScreen(&cube.geom, pp, /*objFlags530=*/0x40,
                                        /*viewCull42=*/0, &sink);
        db.count = sink.count;

        // 6) SORT the appended draw list (LSB radix, 4 passes -> result in base1).
        render::RadixSortDrawList(db, (u32)db.count, /*twoPassOnly=*/false);

        // 7) FLUSH: clip-vs-direct + triangle-fan rasterize into the surface.
        render::MeshList ml{db.base1, db.count};
        int drawn = render::RasterizeMeshList(ml, &fb, disp, clipCtx, proj, scratch);

        // 8) present -> demo/mesh_demo_frameNNNN.bmp
        gfx.present();
        std::printf("frame %d: appended=%d drawn=%d -> %s\n",
                    frame, db.count, drawn,
                    gfx.framePath(frame, shim::FileDumpGraphicsDevice::kBmp).c_str());
    }

    std::printf("done: %d frames\n", kFrames);
    return 0;
}
