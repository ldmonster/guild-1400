#pragma once
// =============================================================================
// guild::play — WORLD RENDER BINDER (PLAYABLE_PLAN P2 groundwork).
//
// Wires the reconstructed render pipeline end to end for a LOADED WORLD:
//
//   scene-graph build  -> render::ProjectVerticesToScreen   (per-object project)
//                      -> render::RadixSortDrawList          (back-to-front sort)
//   software rasterize -> render::RasterizeMeshList          (sorted draw-list flush)
//   HUD overlay        -> render::Surface* 2D blits          (rect + fill)
//   frame driver       -> render::RenderMainViewFrame        (the real frame entry)
//   present            -> render::PresentFrame -> IGraphicsDevice  (headless capture)
//
// The original engine kept the per-frame draw lists, the live world mesh and the
// framebuffer surface in process globals; the render::FrameHooks vtable
// (render/frame.h) ships INERT by default (null function pointers == the engine's
// "subsystem not present this frame" no-op). This binder DE-INERTS the two render
// leaves where the real reconstructed code exists — frameHooks.clearRect (clear),
// frameHooks.sceneWalk (project + sort) and frameHooks.flushDrawList (rasterize) —
// installs them onto a FrameState/FrameHooks pair, runs the real
// RenderMainViewFrame, draws a HUD overlay through the real 2D surface ops, and
// then PRESENTS the framebuffer through the real present dispatch into a headless
// shim::IGraphicsDevice so the output is inspectable.
//
// Everything here is additive (new file); it does not touch wiring.cpp /
// real_hooks*.cpp. The render leaves it drives are the same ones the in-game
// frame tick calls — proving the live render path actually runs.
// =============================================================================
#include "guild/common/types.h"
#include "render/frame.h"
#include "render/scene.h"
#include "render/meshlist.h"
#include "render/mesh.h"
#include "render/clip.h"
#include "render/geometry_types.h"
#include "render/surface.h"
#include "render/present.h"

#include "play/terrain_render.h"        // Heightfield / TerrainView (real floor leaf)
#include "play/wire_terrain_bridge.h"   // TerrainBridgeContext / InstallRealTerrainBridge
#include "play/wire_scene_bridge.h"     // SceneDrawNode / WalkSceneTree (real node dispatch)

#include <vector>

namespace guild::shim { class IGraphicsDevice; }
namespace guild::play { class RealMeshSource; }

namespace guild::play {

// One renderable object in the loaded world: a textured quad (two triangles) with
// a model-space placement, a light/shade seed and a translucency flag. This is the
// minimal payload ProjectVerticesToScreen + RasterizeMeshList consume — the same
// data the engine carried per mesh node (object +460 geometry block).
struct WorldObject {
    float x0 = 0, z0 = 0;    // model-space top-left  (x->screenX, z->screenY)
    float x1 = 0, z1 = 0;    // model-space bottom-right
    u8    light = 200;       // per-vertex light/shade index (seeds raster shade)
    bool  translucent = false;
};

// The loaded world the binder renders: a clear colour, a terrain object, a few
// scene objects/sprites and a HUD overlay rectangle. Deliberately tiny + fully
// deterministic so the framebuffer can be read back and asserted.
struct LoadedWorld {
    // -- framebuffer geometry ------------------------------------------------
    int fbW = 96;
    int fbH = 72;

    // -- clear colour (RGB; written into every pixel before the scene draw) --
    u8 clearR = 0, clearG = 0, clearB = 64;   // dark-blue sky clear

    // -- terrain (one big ground quad) + objects/sprites ---------------------
    WorldObject terrain{};
    bool hasTerrain = false;
    WorldObject objects[8]{};
    int objectCount = 0;

    // -- HUD overlay (a filled bar + outline near the top of the frame) ------
    bool hudEnabled = true;
    int  hudX = 4, hudY = 4, hudW = 40, hudH = 10;
    u8   hudR = 255, hudG = 255, hudB = 0;     // bright yellow HUD

    // Build a representative default world (terrain + 3 objects + HUD).
    static LoadedWorld MakeDefault();
};

// The binder's per-render result snapshot (read back for assertions).
struct RenderStats {
    int appendedPolys = 0;   // draw-list entries the scene walk projected+sorted
    int rasterTris = 0;      // triangles RasterizeMeshList flushed
    int hudPixels = 0;       // HUD overlay pixels written
    bool presented = false;  // PresentFrame succeeded into the device
    int presentCount = 0;    // device present() invocations so far

    // ---- REAL-BRIDGE content (zero unless useRealBridges enabled) -----------
    bool realBridges     = false; // the real-bridge frame path ran this frame
    int  terrainTiles    = 0;     // tiles the REAL floor leaf drew (RenderTerrain)
    int  terrainTris     = 0;     // triangles the REAL floor leaf rasterized
    int  terrainPixels   = 0;     // 16bpp fb pixels the composited terrain painted
    int  sceneNodes      = 0;     // nodes the REAL scene-walk dispatch visited
    int  sceneDispatched = 0;     // nodes the REAL ProcessSceneNodeAppend dispatched
    int  meshTris        = 0;     // polys sourced from REAL mesh geometry (pre-cull)
    int  meshObjects     = 0;     // objects drawn as a resolved REAL mesh
    int  hudSprites      = 0;     // HUD sprite shapes the REAL blit leaf drew
};

// The WORLD RENDER BINDER. Owns the software framebuffer + the per-frame draw
// lists; installs the real render leaves onto its FrameHooks; renders one frame of
// the loaded world through the REAL RenderMainViewFrame/present path.
class RenderBinder {
public:
    RenderBinder() = default;
    ~RenderBinder();

    RenderBinder(const RenderBinder&) = delete;
    RenderBinder& operator=(const RenderBinder&) = delete;

    // ---- REAL-BRIDGE mode (PLAYABLE_PLAN P6 — wire the live frame) -----------
    // OFF by default: the binder keeps faking the world as flat scene-object quads
    // (hasTerrain=false, inert renderTerrain, no scene-walk dispatch, no real
    // meshes, no real HUD sprite). Legacy tests rely on that default.
    //
    // When enabled (BEFORE load()), load()/renderFrame() install the REAL Wave-28
    // bridges onto THIS binder's own hooks_/frame_ and draw a real live frame:
    //   * the REAL terrain leaf   (hasTerrain=true + play::RenderTerrain via
    //                              InstallRealTerrainBridge on hooks_.renderTerrain),
    //   * the REAL scene-walk      dispatch (render::ProcessSceneNodeAppend through
    //                              play::WalkSceneTree / InstallRealSceneBridge),
    //   * the REAL mesh resolver   (play::RealMeshSource -> RealMeshResolver) when a
    //                              source is supplied, else a built-in multi-tri mesh,
    //   * the REAL HUD sprite blit (render::ShapeShowFromBank via the HUD bridge).
    // Returns *this for chaining. Re-call with `false` to revert to the default.
    RenderBinder& SetRealBridges(bool on) { useRealBridges_ = on; return *this; }
    bool realBridges() const { return useRealBridges_; }

    // Supply the floor heightfield + view the REAL terrain leaf draws (real-bridge
    // mode only). When unset, load() builds a deterministic synthetic ground via
    // play::Heightfield::MakeSynthetic / TerrainView::MakeTopDown sized to the fb.
    void SetTerrain(const Heightfield& hf, const TerrainView& view) {
        userTerrain_ = true; terrainHf_ = hf; terrainView_ = view;
    }

    // Supply a mounted REAL mesh source + member name so the real MeshResolver
    // (play::RealMeshResolver) feeds the scene-walk real multi-tri AGF geometry
    // (real-bridge mode only; used by the AUGSBURG e2e). When unset, the scene-walk
    // dispatch draws a built-in real-format multi-tri mesh so the path still runs.
    void SetMeshSource(RealMeshSource* src, const char* memberName);

    // Load a world: allocate the 16bpp software framebuffer and build the scene
    // geometry (terrain + objects) into the draw-list source buffers. Idempotent
    // re-load rebuilds the scene. Returns false on allocation failure.
    bool load(const LoadedWorld& world);

    // Render ONE frame of the loaded world into `dev` (which must already be
    // init()'d to the world's fbW x fbH x bpp). Drives the full real path:
    //   RenderMainViewFrame -> (clearRect) -> (sceneWalk: project+sort)
    //   -> flushDrawList: rasterize -> HUD overlay blit -> PresentFrame(dev).
    // Returns the per-render stats (also retrievable via stats()).
    RenderStats renderFrame(shim::IGraphicsDevice& dev);

    // The software framebuffer surface (null until load()). Inspectable directly.
    render::Surface* framebuffer() { return fb_; }
    const render::Surface* framebuffer() const { return fb_; }

    const RenderStats& stats() const { return stats_; }

    // Count framebuffer pixels that differ from the clear colour (a NON-BLANK
    // measure: how much of the frame the scene+HUD actually painted).
    int nonClearPixels() const;

    // ---- the de-inerted render leaves (exposed for unit testing the math) ----
    // The scene-graph project + sort step the BeginUniverseFrame walk drives.
    // Returns the appended (sorted) draw-list entry count.
    int sceneWalk();
    // The sorted draw-list flush (RasterizeMeshList) into the framebuffer.
    int flushDrawList();
    // The clear: fill the framebuffer with the loaded world's clear colour.
    void clearFrame();
    // The HUD overlay blit (filled bar + outline) through the real 2D surface ops.
    int blitHud();

    // (real-bridge) run the REAL floor leaf into the 8bpp companion + composite it
    // into fb_ (driven from the renderTerrain hook). Public so the C trampoline
    // can reach it; a no-op outside real-bridge mode.
    int runRealTerrain();

private:
    LoadedWorld world_{};
    bool loaded_ = false;

    render::Surface* fb_ = nullptr;

    // Scene geometry source (one MeshGeometry per object, sharing vertex/poly pools).
    static constexpr int kMaxObjects = 9;     // terrain + 8 objects
    static constexpr int kMaxPolys   = 64;
    render::Vertex   verts_[kMaxObjects * 4];
    render::Polygon  polys_[kMaxObjects * 2];
    render::MeshGeometry geom_[kMaxObjects];
    int geomCount_ = 0;

    // Per-frame draw-list ping-pong buffers + clip/dispatch scratch.
    render::DrawListEntry pool1_[kMaxPolys];
    render::DrawListEntry pool2_[kMaxPolys];
    render::DrawListBuffers db_{};
    render::ClipScratch     scratch_{};
    render::SpanDispatch    dispatch_{};

    render::FrameState frame_{};
    render::FrameHooks hooks_{};

    RenderStats stats_{};

    void buildScene();

    // ======================= REAL-BRIDGE state (P6) ==========================
    bool useRealBridges_ = false;

    // -- real terrain leaf: an 8bpp companion surface the floor draws into,
    //    composited into the 16bpp fb_ before the scene draw. ------------------
    bool                 userTerrain_ = false;
    Heightfield          terrainHf_{};
    TerrainView          terrainView_{};
    render::Surface*     terrainFb_ = nullptr;     // 8bpp floor surface
    TerrainBridgeContext terrainCtx_{};

    // -- real scene-walk: a SceneDrawNode tree carrying projected real mesh
    //    polys, dispatched through render::ProcessSceneNodeAppend. -------------
    std::vector<SceneDrawNode> sceneNodes_;        // one node per real mesh object
    std::vector<render::DrawListEntry> realPool_;  // append sink for the dispatch

    // -- real mesh geometry the scene nodes carry (built-in or from a source) --
    RealMeshSource* meshSource_ = nullptr;
    const char*     meshMember_ = nullptr;
    std::vector<render::Vertex>  meshVerts_;        // built-in / copied mesh verts
    std::vector<render::Polygon> meshPolys_;        // built-in / copied mesh polys
    render::MeshGeometry         meshGeom_{};

    // Build the real terrain companion surface + the projected real mesh scene
    // tree (called from load() in real-bridge mode).
    void buildRealBridgeScene();
    // The REAL scene-walk: project the real mesh, dispatch its polys through
    // ProcessSceneNodeAppend into db_. Returns appended polys.
    int realSceneWalk();
    // Composite the 8bpp terrain shade surface into the 16bpp fb_ as grey.
    int compositeTerrain();
    // The REAL HUD sprite blit (render::ShapeShowFromBank) into fb_.
    int blitHudSprites();
};

} // namespace guild::play
