#include "test.h"

// INTEGRATION: build a small real-FORMAT scene (synthetic entity records in the
// live sim arrays) and render it through the REAL render pipeline via
// play::WorldRenderer -> render_binder (ProjectVerticesToScreen ->
// RadixSortDrawList -> RasterizeMeshList -> PresentFrame) into a headless device.
// Asserts:
//   * the build emitted the expected scene-object count,
//   * the real pipeline appended + rasterized real triangles,
//   * the framebuffer is NON-BLANK,
//   * a KNOWN terrain pixel (a deterministic point inside the terrain quad) is
//     painted (not the clear colour) — a fixed-coordinate oracle.
#include "play/world_render.h"
#include "render/surface.h"
#include "render/colorformat.h"
#include "sim/entity.h"
#include "shim_impl/memory_graphics.h"

#include <cstdint>

using namespace guild;
using namespace guild::play;
using namespace guild::sim;

namespace {

u16 Px16(const render::Surface* s, int x, int y) {
    const u16* p = reinterpret_cast<const u16*>(s->pixels);
    return p[(s->widthPx * y) + x];
}

} // namespace

TEST(WorldRenderItest, BuildRasterizeSmallSceneNonBlank) {
    ResetEntityArrays();
    // A small real-format scene: 4 alive objects + 2 scene-tree nodes.
    for (int i = 0; i < 4; ++i) {
        g_objects[i].alive = 1;
        g_objects[i].id = 200 + i;
    }
    g_sceneNodes[0].type = 7; g_sceneNodes[0].id = 900; g_sceneNodes[0].childPtr = 1;
    g_sceneNodes[1].type = 7; g_sceneNodes[1].id = 901; g_sceneNodes[1].childPtr = -1;
    g_sceneNodes[1].entityPtr = -1; g_sceneNodes[0].entityPtr = -1;
    g_sceneNodeCount = 2;

    WorldRenderer wr;
    WorldRenderer::Options opt;
    opt.fbW = 96; opt.fbH = 72;
    opt.clearR = 0; opt.clearG = 0; opt.clearB = 64;
    opt.emitTerrain = true;
    opt.scanObjects = true;
    opt.scanScene = true;
    opt.scanPersons = false;

    shim::MemoryGraphicsDevice dev;
    CHECK(dev.init(opt.fbW, opt.fbH, 16, /*fullscreen=*/false));

    RenderStats st = wr.render(opt, dev);
    const WorldDrawList& dl = wr.lastBuild();

    // The build emitted 2 scene-tree nodes + 4 objects = 6 scene objects.
    CHECK_EQ(dl.sceneQuads, 2);
    CHECK_EQ(dl.objectQuads, 4);
    CHECK_EQ(dl.sceneObjects(), 6);
    CHECK(dl.hasTerrain);

    // The REAL pipeline ran: projected/sorted entries + rasterized triangles.
    CHECK(st.appendedPolys > 0);
    CHECK(st.rasterTris > 0);
    CHECK(st.presented);
    CHECK(dev.presentCount() == 1);

    // NON-BLANK: a meaningful fraction of the frame changed from the clear colour.
    int changed = wr.binder().nonClearPixels();
    CHECK(changed > 100);

    // KNOWN PIXEL ORACLE: the terrain ground quad (the widest, always-emitted
    // object) rasterizes a deterministic upper-span footprint — the real flat-
    // triangle edge-walk paints the quad's upper region, a solid 45-px run from
    // x=2 across rows y~16..34 for this 96x72 framebuffer (verified against the
    // real rasterizer output). Pixel (10, 25) sits squarely inside that run, so it
    // must be painted (differs from the clear colour) — a fixed-coordinate oracle.
    render::Surface* fb = wr.binder().framebuffer();
    CHECK(fb != nullptr);
    if (fb) {
        u16 clear = (u16)render::PackColor(fb->fmt, opt.clearR, opt.clearG, opt.clearB);
        CHECK(Px16(fb, 10, 25) != clear);     // inside the terrain footprint
    }

    ResetEntityArrays();
}

// --- determinism across two builds+renders of the same synthetic world ---------
TEST(WorldRenderItest, DeterministicAcrossRenders) {
    ResetEntityArrays();
    for (int i = 0; i < 5; ++i) {
        g_objects[i].alive = 1;
        g_objects[i].id = 50 + i * 7;
    }

    WorldRenderer::Options opt;
    opt.fbW = 80; opt.fbH = 60;
    opt.scanScene = false;

    shim::MemoryGraphicsDevice d1, d2;
    CHECK(d1.init(opt.fbW, opt.fbH, 16, false));
    CHECK(d2.init(opt.fbW, opt.fbH, 16, false));

    WorldRenderer a, b;
    RenderStats sa = a.render(opt, d1);
    RenderStats sb = b.render(opt, d2);

    CHECK_EQ(a.lastBuild().objectQuads, b.lastBuild().objectQuads);
    CHECK_EQ(sa.appendedPolys, sb.appendedPolys);
    CHECK_EQ(sa.rasterTris, sb.rasterTris);
    CHECK(d1.lastPresented() == d2.lastPresented());

    ResetEntityArrays();
}
