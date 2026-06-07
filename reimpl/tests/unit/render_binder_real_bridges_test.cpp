#include "test.h"

// UNIT: the WORLD RENDER BINDER's REAL-BRIDGE mode (PLAYABLE_PLAN P6 — wire the
// real render bridges into the LIVE frame). Asserts that SetRealBridges(true)
// de-inerts the live frame leaves on the binder's OWN hooks_/frame_ and that the
// rendered synthetic frame gains REAL content the default frame lacks:
//   * the REAL terrain leaf (play::RenderTerrain) draws tiles/tris into the frame
//     (frame_.hasTerrain flipped on, renderTerrain hook de-inerted),
//   * the REAL scene-walk dispatch (render::ProcessSceneNodeAppend) visits +
//     dispatches a node (sceneNodes/sceneDispatched > 0),
//   * the REAL mesh (multi-tri, NOT a 2-tri quad) feeds the dispatch (meshTris>2),
//   * the REAL HUD sprite blit (render::ShapeShowFromBank) draws a sprite shape.
// The DEFAULT frame (bridges off) has NONE of these (legacy behaviour preserved).
#include "play/render_binder.h"
#include "play/wire_scene_bridge.h"
#include "play/wire_hud_bridge.h"
#include "render/surface.h"
#include "shim_impl/memory_graphics.h"

using namespace guild;
using namespace guild::play;

// --- default frame carries NO real-bridge content -----------------------------
TEST(RenderBinderRealBridges, DefaultFrameHasNoRealContent) {
    LoadedWorld w = LoadedWorld::MakeDefault();
    shim::MemoryGraphicsDevice dev;
    CHECK(dev.init(w.fbW, w.fbH, 16, false));

    RenderBinder rb;                       // default: bridges OFF
    CHECK(!rb.realBridges());
    CHECK(rb.load(w));
    RenderStats s = rb.renderFrame(dev);

    CHECK(!s.realBridges);
    CHECK_EQ(s.terrainTiles, 0);
    CHECK_EQ(s.terrainTris, 0);
    CHECK_EQ(s.terrainPixels, 0);
    CHECK_EQ(s.sceneNodes, 0);
    CHECK_EQ(s.sceneDispatched, 0);
    CHECK_EQ(s.meshTris, 0);
    CHECK_EQ(s.hudSprites, 0);
    // The legacy fake-quad scene still drew (so default behaviour is intact).
    CHECK(s.appendedPolys > 0);
    CHECK(s.rasterTris > 0);
}

// --- real-bridge frame gains terrain + scene-dispatch + mesh + HUD sprite ------
TEST(RenderBinderRealBridges, RealFrameGainsRealContent) {
    LoadedWorld w = LoadedWorld::MakeDefault();
    shim::MemoryGraphicsDevice dev;
    CHECK(dev.init(w.fbW, w.fbH, 16, false));

    RenderBinder rb;
    rb.SetRealBridges(true);
    CHECK(rb.realBridges());
    CHECK(rb.load(w));
    RenderStats s = rb.renderFrame(dev);

    // The real-bridge frame path ran.
    CHECK(s.realBridges);

    // REAL terrain leaf drew the full 8x8 tile grid into the frame.
    CHECK(s.terrainTiles > 0);
    CHECK_EQ(s.terrainTiles, 64);          // 8x8 tile grid
    CHECK(s.terrainTris > 0);
    CHECK(s.terrainPixels > 0);

    // REAL scene-walk dispatch visited + dispatched the mesh node through
    // render::ProcessSceneNodeAppend.
    CHECK(s.sceneNodes > 0);
    CHECK(s.sceneDispatched > 0);

    // REAL multi-tri mesh (NOT a 2-tri quad) fed the dispatch.
    CHECK(s.meshTris > 2);
    CHECK(s.meshObjects > 0);

    // REAL HUD sprite-bank blit drew a shape.
    CHECK_EQ(s.hudSprites, 1);
    // The scene-bridge dispatch is the installed (real) one for the frame.
    CHECK(RealSceneBridgeInstalled());
    CHECK(RealHudBridgeInstalled());

    CHECK(s.presented);
}

// --- the real-bridge frame is MORE non-blank than the default frame ------------
TEST(RenderBinderRealBridges, RealFrameMoreContentThanDefault) {
    LoadedWorld w = LoadedWorld::MakeDefault();

    shim::MemoryGraphicsDevice dDef, dReal;
    CHECK(dDef.init(w.fbW, w.fbH, 16, false));
    CHECK(dReal.init(w.fbW, w.fbH, 16, false));

    RenderBinder def;
    CHECK(def.load(w));
    def.renderFrame(dDef);
    int defNonClear = def.nonClearPixels();

    RenderBinder real;
    real.SetRealBridges(true);
    CHECK(real.load(w));
    real.renderFrame(dReal);
    int realNonClear = real.nonClearPixels();

    // The real terrain + mesh + HUD sprite paint substantially MORE of the frame
    // than the default fake-quad scene (terrain alone fills most of the view).
    CHECK(realNonClear > defNonClear);
    CHECK(realNonClear > 100);
}

// --- the real-bridge frame is DETERMINISTIC across two fresh binders -----------
TEST(RenderBinderRealBridges, RealFrameDeterministic) {
    LoadedWorld w = LoadedWorld::MakeDefault();

    shim::MemoryGraphicsDevice d1, d2;
    CHECK(d1.init(w.fbW, w.fbH, 16, false));
    CHECK(d2.init(w.fbW, w.fbH, 16, false));

    RenderBinder a, b;
    a.SetRealBridges(true);
    b.SetRealBridges(true);
    CHECK(a.load(w));
    CHECK(b.load(w));
    RenderStats sa = a.renderFrame(d1);
    RenderStats sb = b.renderFrame(d2);

    CHECK_EQ(sa.terrainTris, sb.terrainTris);
    CHECK_EQ(sa.terrainPixels, sb.terrainPixels);
    CHECK_EQ(sa.sceneDispatched, sb.sceneDispatched);
    CHECK_EQ(sa.meshTris, sb.meshTris);
    CHECK_EQ(sa.hudSprites, sb.hudSprites);
    CHECK_EQ(a.nonClearPixels(), b.nonClearPixels());

    // Byte-identical presented frames.
    CHECK(d1.lastPresented() == d2.lastPresented());
    CHECK(!d1.lastPresented().empty());
}
