// =============================================================================
// guild::play — WORLD RENDER BINDER implementation. See render_binder.h.
//
// The render::FrameHooks vtable (render/frame.h) is plain C function pointers with
// no user-context argument — exactly the engine's process-global subsystem
// dispatch. The original threaded the live PolyLists / world mesh / framebuffer
// through file-scope globals; we mirror that by routing the C hooks through a
// file-scope "current binder" pointer set for the duration of one renderFrame()
// call. Render is single-threaded per frame, so this is faithful.
// =============================================================================
#include "play/render_binder.h"

#include "shim/IGraphicsDevice.h"

#include "play/wire_hud_bridge.h"        // InstallRealHudBridge / BlitHudSprite (real 2D blit)
#include "play/real_mesh_source.h"       // RealMeshSource / RealMeshResolver
#include "render/colorformat.h"

#include <cstring>

namespace guild::play {

// ---------------------------------------------------------------------------
// The current binder driving the C-style FrameHooks (the engine's global
// PolyList/world/framebuffer context). Set across a single renderFrame() call.
// ---------------------------------------------------------------------------
namespace {
RenderBinder* g_current = nullptr;

// FrameHooks trampolines -> the active binder's de-inerted render leaves.
void HookClearRect() {
    if (g_current)
        g_current->clearFrame();
}
int HookSceneWalk(char /*a2*/) {
    return g_current ? g_current->sceneWalk() : 0;
}
void HookFlushDrawList() {
    if (g_current)
        g_current->flushDrawList();
}
// Real-bridge terrain trampoline: run the REAL floor leaf into the 8bpp companion
// surface (via the wire_terrain_bridge real hook) THEN composite it into the 16bpp
// fb_ — so the per-tile-lit ground lands UNDER the scene, matching the engine's
// "terrain first, then scene walk" order inside BeginUniverseFrame.
void HookRenderTerrain(void* terrainCtx, char a2);
} // namespace

// ---------------------------------------------------------------------------
LoadedWorld LoadedWorld::MakeDefault() {
    LoadedWorld w;
    w.fbW = 96;
    w.fbH = 72;
    w.clearR = 0; w.clearG = 0; w.clearB = 64;

    // Terrain: a wide ground quad across the lower half of the view.
    w.hasTerrain = true;
    w.terrain.x0 = 6.0f;  w.terrain.z0 = 30.0f;
    w.terrain.x1 = 88.0f; w.terrain.z1 = 64.0f;
    w.terrain.light = 120;        // mid ground shade
    w.terrain.translucent = false;

    // Three scene objects/sprites sitting on the terrain at distinct depths.
    w.objectCount = 3;
    w.objects[0] = WorldObject{12.0f, 8.0f,  28.0f, 26.0f, 220, false}; // bright object
    w.objects[1] = WorldObject{40.0f, 12.0f, 60.0f, 30.0f, 180, false}; // mid object
    w.objects[2] = WorldObject{66.0f, 6.0f,  84.0f, 24.0f, 150, true};  // translucent sprite

    // HUD overlay: a yellow status bar near the top-left.
    w.hudEnabled = true;
    w.hudX = 4; w.hudY = 4; w.hudW = 40; w.hudH = 10;
    w.hudR = 255; w.hudG = 255; w.hudB = 0;
    return w;
}

// ---------------------------------------------------------------------------
RenderBinder::~RenderBinder() {
    if (fb_) {
        render::SurfaceDestroy(fb_);
        fb_ = nullptr;
    }
    if (terrainFb_) {
        render::SurfaceDestroy(terrainFb_);
        terrainFb_ = nullptr;
    }
}

void RenderBinder::SetMeshSource(RealMeshSource* src, const char* memberName) {
    meshSource_ = src;
    meshMember_ = memberName;
}

bool RenderBinder::load(const LoadedWorld& world) {
    world_ = world;

    if (fb_) {
        render::SurfaceDestroy(fb_);
        fb_ = nullptr;
    }
    // 16bpp software framebuffer — the engine's main-view surface bpp. The
    // rasterizer writes per-pixel shade bytes into it (widthPx == width here).
    fb_ = render::SurfaceCreate(world_.fbW, world_.fbH, 16);
    if (!fb_)
        return false;

    db_.base1 = pool1_;
    db_.base2 = pool2_;
    db_.count = 0;
    db_.capacity = kMaxPolys;

    // Install the REAL render leaves onto the frame hooks (these ship inert/null).
    hooks_ = render::FrameHooks{};
    hooks_.clearRect     = &HookClearRect;     // INERT -> REAL clear
    hooks_.sceneWalk     = &HookSceneWalk;     // INERT -> REAL project + sort
    hooks_.flushDrawList = &HookFlushDrawList; // INERT -> REAL rasterize
    // The remaining leaves (terrain/particles/sky/mirrors/anim) have no
    // reconstructed software-raster target wired here yet, so they stay inert
    // (null == the engine's "subsystem not present" no-op). Noted in the report.

    frame_ = render::FrameState{};
    frame_.engineOn = true;
    frame_.hasWorld = true;          // a live world this frame -> BeginUniverseFrame walks
    frame_.hasTerrain = false;       // terrain rendered as a scene object (see buildScene)
    frame_.useViewportClear = false; // -> clearRect path

    buildScene();

    // ---- REAL-BRIDGE wiring: de-inert the live frame leaves on hooks_/frame_ --
    if (terrainFb_) { render::SurfaceDestroy(terrainFb_); terrainFb_ = nullptr; }
    if (useRealBridges_)
        buildRealBridgeScene();

    loaded_ = true;
    return true;
}

// ---------------------------------------------------------------------------
// REAL-BRIDGE scene build: install the REAL terrain leaf onto hooks_.renderTerrain
// (InstallRealTerrainBridge flips frame_.hasTerrain=true), the REAL scene-walk
// dispatch (InstallRealSceneBridge), the REAL mesh geometry (RealMeshSource ->
// RealMeshResolver, else a built-in multi-tri mesh), and prepares the scene tree
// of SceneDrawNodes that ProcessSceneNodeAppend dispatches.
// ---------------------------------------------------------------------------
namespace {
// Build a deterministic real-format multi-tri mesh (a hex pyramid: 7 verts, 6
// side tris + 6 base tris = 12 polys) seated into the lower-left of the frame.
// This is the "real mesh" the scene-walk draws when no RealMeshSource is supplied
// — proper multi-poly geometry (NOT a 2-tri quad), exercising the real append leaf.
void BuildBuiltinMesh(float cx, float cz, float r, u8 light,
                      std::vector<render::Vertex>& vs,
                      std::vector<render::Polygon>& ps) {
    vs.clear(); ps.clear();
    const int kRim = 6;
    // apex
    auto mkV = [&](float x, float z, float u, float vv) {
        render::Vertex v{};
        v.x = x; v.y = 0.0f; v.z = z; v.u = u; v.v = vv;
        v.lightIdx = light; v.clipFlags = 0;
        vs.push_back(v);
    };
    mkV(cx, cz, 0.5f, 0.5f);                       // 0 = apex/center
    // cos/sin via a small fixed table to stay header-free + deterministic.
    static const float cs[6] = { 1.0f, 0.5f, -0.5f, -1.0f, -0.5f, 0.5f };
    static const float sn[6] = { 0.0f, 0.8660254f, 0.8660254f, 0.0f,
                                 -0.8660254f, -0.8660254f };
    for (int i = 0; i < kRim; ++i)
        mkV(cx + r * cs[i], cz + r * sn[i], (cs[i] + 1) * 0.5f, (sn[i] + 1) * 0.5f);
    auto mkP = [&](int a, int b, int c) {
        render::Polygon p{};
        p.v0 = &vs[a]; p.v1 = &vs[b]; p.v2 = &vs[c];
        ps.push_back(p);
    };
    for (int i = 0; i < kRim; ++i)
        mkP(0, 1 + i, 1 + ((i + 1) % kRim));   // 6 fan triangles around the apex
}
} // namespace

void RenderBinder::buildRealBridgeScene() {
    // (1) REAL TERRAIN LEAF -------------------------------------------------
    // The real floor leaf (play::RenderTerrain) needs an 8bpp surface; the binder
    // fb_ is 16bpp, so the floor draws into an 8bpp companion and is composited
    // into fb_ (compositeTerrain) before the scene draw — the engine drew terrain
    // first into the same surface; the bpp split is the reimpl's raster constraint.
    Heightfield hf = userTerrain_ ? terrainHf_
                                  : Heightfield::MakeSynthetic((world_.fbW / 8) * 8 < 16
                                                                   ? 16
                                                                   : (world_.fbW / 8) * 8,
                                                               0xA17u);
    TerrainView view = userTerrain_
                           ? terrainView_
                           : TerrainView::MakeTopDown(hf.size, hf.tileSpan,
                                                      world_.fbW, world_.fbH);
    terrainHf_ = hf;
    terrainView_ = view;

    terrainFb_ = render::SurfaceCreate(world_.fbW, world_.fbH, 8);
    terrainCtx_ = TerrainBridgeContext{};
    terrainCtx_.fb = terrainFb_;
    terrainCtx_.hf = terrainHf_;
    terrainCtx_.view = terrainView_;
    terrainCtx_.background = 0;

    // De-inert hooks_.renderTerrain onto the REAL floor leaf + flip hasTerrain.
    // (InstallRealTerrainBridge flips frame_.hasTerrain=true + sets the wire bridge
    // hook; we then point the hook at the binder trampoline so the 8bpp floor is
    // composited into the 16bpp fb_ in the same call.)
    InstallRealTerrainBridge(frame_, hooks_, &terrainCtx_);
    hooks_.renderTerrain = &HookRenderTerrain;

    // (2) REAL SCENE-WALK DISPATCH -----------------------------------------
    // Route the scene-graph per-node dispatch to render::ProcessSceneNodeAppend.
    InstallRealSceneBridge();

    // (3) REAL MESH GEOMETRY the scene nodes carry --------------------------
    meshVerts_.clear(); meshPolys_.clear();
    const render::MeshGeometry* resolved = nullptr;
    if (meshSource_ && meshMember_) {
        // REAL AGF mesh from the mounted source (the e2e path). Install it as the
        // live MeshResolver so the resolve goes through RealMeshResolver, then copy
        // the resolved geometry into owned storage we can world-seat + project.
        InstallRealMeshSource(meshSource_);
        resolved = meshSource_->Resolve(meshMember_);
    }
    if (resolved && resolved->vertexCount > 0 && resolved->polyCount > 0) {
        // Copy verts; rebind polys into the owned copy (centered into the frame).
        meshVerts_.assign(resolved->vertices,
                          resolved->vertices + resolved->vertexCount);
        // Re-center + scale the model into the lower view so it lands on-screen.
        float minx = 1e30f, maxx = -1e30f, minz = 1e30f, maxz = -1e30f;
        for (auto& v : meshVerts_) {
            if (v.x < minx) minx = v.x;
            if (v.x > maxx) maxx = v.x;
            if (v.z < minz) minz = v.z;
            if (v.z > maxz) maxz = v.z;
        }
        float ex = (maxx - minx) > 1e-3f ? (maxx - minx) : 1.0f;
        float ez = (maxz - minz) > 1e-3f ? (maxz - minz) : 1.0f;
        float scale = ((float)world_.fbW * 0.4f) / (ex > ez ? ex : ez);
        float ox = world_.fbW * 0.30f, oz = world_.fbH * 0.55f;
        for (auto& v : meshVerts_) {
            v.x = ox + (v.x - (minx + maxx) * 0.5f) * scale;
            v.z = oz + (v.z - (minz + maxz) * 0.5f) * scale;
            v.y = 0.0f;
            if (v.lightIdx == 0) v.lightIdx = 200;
            v.clipFlags = 0;
        }
        meshPolys_.assign(resolved->polygons,
                          resolved->polygons + resolved->polyCount);
        for (auto& p : meshPolys_) {
            // Rebind the vertex pointers from the source array into the owned copy.
            std::ptrdiff_t i0 = p.v0 - resolved->vertices;
            std::ptrdiff_t i1 = p.v1 - resolved->vertices;
            std::ptrdiff_t i2 = p.v2 - resolved->vertices;
            p.v0 = (i0 >= 0 && i0 < resolved->vertexCount) ? &meshVerts_[i0] : &meshVerts_[0];
            p.v1 = (i1 >= 0 && i1 < resolved->vertexCount) ? &meshVerts_[i1] : &meshVerts_[0];
            p.v2 = (i2 >= 0 && i2 < resolved->vertexCount) ? &meshVerts_[i2] : &meshVerts_[0];
        }
    } else {
        // Built-in real-format multi-tri mesh (no source bound / decode failed).
        BuildBuiltinMesh(world_.fbW * 0.30f, world_.fbH * 0.55f,
                         (float)world_.fbW * 0.18f, 210, meshVerts_, meshPolys_);
    }
    meshGeom_ = render::MeshGeometry{};
    meshGeom_.vertices = meshVerts_.data();
    meshGeom_.polygons = meshPolys_.data();
    meshGeom_.polyCount = (i32)meshPolys_.size();
    meshGeom_.polyCap   = (i32)meshPolys_.size();
    meshGeom_.vertexCount = (i32)meshVerts_.size();

    // One scene node carrying the real mesh polys (visible: cull byte 0, type 3).
    sceneNodes_.clear();
    SceneDrawNode node{};
    node.nodeType  = 3;          // passes the 0x1FF walk mask (TestNodeFlag)
    node.flags528  = 0;
    node.cullByte  = 0;          // visible (not 0x40 culled)
    node.polys     = meshGeom_.polygons;
    node.polyCount = meshGeom_.polyCount;
    node.texSortId = nullptr;    // untextured: software sort key 0 (real append path)
    sceneNodes_.push_back(node);

    realPool_.assign((size_t)kMaxPolys * 4, render::DrawListEntry{});
}

// Build the scene geometry into the shared vertex/poly pools: one front-facing
// textured quad (two triangles) per world object (terrain first, then objects).
void RenderBinder::buildScene() {
    geomCount_ = 0;

    auto addQuad = [&](const WorldObject& o) {
        if (geomCount_ >= kMaxObjects)
            return;
        int gi = geomCount_;
        render::Vertex* v = &verts_[gi * 4];
        render::Polygon* p = &polys_[gi * 2];

        auto V = [&](int i, float x, float z, float u, float vv) {
            v[i] = render::Vertex{};
            v[i].x = x; v[i].y = 0.0f; v[i].z = z;
            v[i].u = u; v[i].v = vv;
            v[i].lightIdx = o.light;  // seed the per-vertex shade index
            v[i].clipFlags = 0;       // +76 == 0 => direct (no-clip) raster path
        };
        // CW screen winding (x->screenX, z->screenY) -> front-facing append.
        V(0, o.x0, o.z0, 0.0f, 0.0f);
        V(1, o.x1, o.z0, 1.0f, 0.0f);
        V(2, o.x0, o.z1, 0.0f, 1.0f);
        V(3, o.x1, o.z1, 1.0f, 1.0f);

        p[0] = render::Polygon{};
        p[0].v0 = &v[0]; p[0].v1 = &v[1]; p[0].v2 = &v[2];
        p[1] = render::Polygon{};
        p[1].v0 = &v[1]; p[1].v1 = &v[3]; p[1].v2 = &v[2];
        if (o.translucent) {
            p[0].flags38 |= 0x01;   // translucency proxy (dispatch slot 3)
            p[1].flags38 |= 0x01;
        }

        render::MeshGeometry& g = geom_[gi];
        g = render::MeshGeometry{};
        g.vertices = v;
        g.polygons = p;
        g.polyCount = 2;
        g.polyCap = 2;
        g.vertexCount = 4;
        ++geomCount_;
    };

    if (world_.hasTerrain)
        addQuad(world_.terrain);
    for (int i = 0; i < world_.objectCount && i < 8; ++i)
        addQuad(world_.objects[i]);
}

// ---------------------------------------------------------------------------
// De-inerted render leaves.
// ---------------------------------------------------------------------------

void RenderBinder::clearFrame() {
    if (fb_)
        render::SurfaceColorFill(fb_, world_.clearR, world_.clearG, world_.clearB);
}

int RenderBinder::sceneWalk() {
    if (!fb_)
        return 0;
    if (useRealBridges_)
        return realSceneWalk();
    db_.count = 0;

    // Projection params: camera at origin; X->screenX, Z->screenY at unit scale so
    // the model-space quad coords land directly on pixels.
    render::ProjectParams pp{};
    pp.eye[0] = 0.0f; pp.eye[1] = 0.0f; pp.eye[2] = 0.0f;
    pp.invDepth[0] = pp.invDepth[1] = pp.invDepth[2] = 1.0f;
    pp.biasX  = 0.0f;
    pp.scaleY = 1.0f;
    pp.scaleX = 1.0f;
    pp.lightCap = 254.0f;
    pp.screenW  = (float)world_.fbW;

    // Project every object's polys into the shared draw-list sink, accumulating
    // the running count across objects (the BeginUniverseFrame per-object walk).
    for (int gi = 0; gi < geomCount_; ++gi) {
        render::DrawList sink = db_.AppendSink();
        // objFlags530 0x40 (double-sided) so the quads always append; viewCull42 0.
        render::ProjectVerticesToScreen(&geom_[gi], pp, /*objFlags530=*/0x40,
                                        /*viewCull42=*/0, &sink);
        db_.count = sink.count;
    }

    // Radix-sort the appended entries back-to-front (ends in base1 = PolyList1).
    render::RadixSortDrawList(db_, (u32)db_.count, /*twoPassOnly=*/false);
    stats_.appendedPolys = db_.count;
    return db_.count;
}

// ---------------------------------------------------------------------------
// REAL scene-walk (real-bridge mode): project the REAL mesh, then dispatch its
// polys to the draw list through render::ProcessSceneNodeAppend driven over
// play::WalkSceneTree (the real scene-graph node dispatch). Mirrors the engine's
// BeginUniverseFrame -> SceneGraph_WalkAndInvoke(ProcessSceneNode) path.
// ---------------------------------------------------------------------------
int RenderBinder::realSceneWalk() {
    db_.count = 0;
    stats_.meshTris = (int)meshPolys_.size();
    stats_.meshObjects = meshPolys_.empty() ? 0 : 1;
    if (meshVerts_.empty() || meshPolys_.empty())
        return 0;

    // PROJECT leaf: set each vertex's screenX/screenY + each poly's backface byte.
    // ProjectVerticesToScreen appends into a SCRATCH sink (we discard its append;
    // the real NODE dispatch below is what populates the live draw list, exactly
    // as the engine: the project leaf runs, then ProcessSceneNode appends).
    render::ProjectParams pp{};
    pp.eye[0] = pp.eye[1] = pp.eye[2] = 0.0f;
    pp.invDepth[0] = pp.invDepth[1] = pp.invDepth[2] = 1.0f;
    pp.biasX = 0.0f; pp.scaleY = 1.0f; pp.scaleX = 1.0f;
    pp.lightCap = 254.0f; pp.screenW = (float)world_.fbW;
    {
        render::DrawList scratchSink{realPool_.data(), 0, (i32)realPool_.size()};
        render::ProjectVerticesToScreen(&meshGeom_, pp, /*objFlags530=*/0x40,
                                        /*viewCull42=*/0, &scratchSink);
    }

    // NODE-DISPATCH leaf: walk the scene tree with the REAL dispatch
    // (ProcessSceneNodeAppend) appending the visible polys into db_.base1.
    SceneBridgeContext sc{};
    sc.out = render::DrawList{db_.base1, 0, db_.capacity};
    sc.appendCtx.mode = render::NodeAppendMode::Software;
    sc.appendCtx.baseKey = 1;
    char rc = WalkSceneTree(sceneNodes_.data(), /*walkMask=*/0x1FF, sc);
    (void)rc;
    stats_.sceneNodes      = sc.nodesVisited;
    stats_.sceneDispatched = sc.nodesDispatched;

    db_.count = sc.out.count;
    // Radix-sort the appended entries (back-to-front), ending in base1.
    render::RadixSortDrawList(db_, (u32)db_.count, /*twoPassOnly=*/false);
    stats_.appendedPolys = db_.count;
    return db_.count;
}

namespace {
void HookRenderTerrain(void* /*terrainCtx*/, char /*a2*/) {
    if (g_current)
        g_current->runRealTerrain();
}
} // namespace

// Run the REAL floor leaf (play::RenderTerrain) into the 8bpp companion surface,
// then composite it into fb_. Driven by the renderTerrain hook during the live
// BeginUniverseFrame walk (so terrain lands before the scene draw).
int RenderBinder::runRealTerrain() {
    if (!useRealBridges_ || !terrainFb_ || !terrainHf_.valid())
        return 0;
    render::SurfaceColorFill(terrainFb_, terrainCtx_.background,
                             terrainCtx_.background, terrainCtx_.background);
    terrainCtx_.lastStats =
        play::RenderTerrain(terrainFb_, terrainHf_, terrainView_, terrainCtx_.background);
    terrainCtx_.drewReal = true;
    ++terrainCtx_.drawCount;
    return compositeTerrain();
}

// Composite the 8bpp terrain shade surface into the 16bpp fb_ as grey, where the
// floor leaf painted (shade != background). Returns the fb_ pixels it changed.
int RenderBinder::compositeTerrain() {
    if (!terrainFb_ || !fb_ || !terrainFb_->pixels)
        return 0;
    int painted = 0;
    int w = terrainFb_->width < fb_->width ? terrainFb_->width : fb_->width;
    int h = terrainFb_->height < fb_->height ? terrainFb_->height : fb_->height;
    for (int y = 0; y < h; ++y) {
        const u8* row = terrainFb_->pixels + (size_t)y * terrainFb_->widthPx;
        for (int x = 0; x < w; ++x) {
            u8 shade = row[x];
            if (shade == terrainCtx_.background)
                continue;                 // floor did not paint this cell
            // grey from the shade byte (the per-tile-lit floor luma).
            if (render::SurfaceSetPixelRgb(fb_, x, y, shade, shade, shade))
                ++painted;
        }
    }
    stats_.terrainTiles  = terrainCtx_.lastStats.tilesDrawn;
    stats_.terrainTris   = terrainCtx_.lastStats.trisDrawn;
    stats_.terrainPixels = painted;
    return painted;
}

// REAL HUD sprite blit: draw a sprite-bank shape via render::ShapeShowFromBank
// (the exact leaf the HUD bridge routes to) into the 16bpp fb_, on top of the
// deterministic HUD bar. Returns the number of shapes drawn.
int RenderBinder::blitHudSprites() {
    if (!fb_ || !world_.hudEnabled)
        return 0;
    // Drive the same real leaf InstallRealHudBridge installs (BlitHudSprite ->
    // ShapeShowFromBank -> ShapeBlitColored16). Place the default 8x8 sprite at
    // the HUD bar's right edge (inside the surface).
    int sx = world_.hudX + world_.hudW + 1;
    int sy = world_.hudY;
    if (sx + 8 > fb_->widthPx) sx = fb_->widthPx - 8;
    if (sy + 8 > fb_->height)  sy = fb_->height - 8;
    if (sx < 0 || sy < 0)
        return 0;
    int rv = BlitHudSprite(sx, sy, /*shapeIndex=*/0,
                           reinterpret_cast<u16*>(fb_->pixels), fb_->widthPx, fb_->fmt);
    int drew = (rv != 0) ? 1 : 0;
    stats_.hudSprites = drew;
    return drew;
}

int RenderBinder::flushDrawList() {
    if (!fb_)
        return 0;
    render::MeshList list{db_.base1, db_.count};
    render::ClipContext ctx{0, nullptr};          // direct path (no extra planes)
    render::ProjectScalars proj{1.0f, 0.0f, 1.0f, 0.0f};
    int drawn = render::RasterizeMeshList(list, fb_, dispatch_, ctx, proj, scratch_);
    stats_.rasterTris = drawn;
    return drawn;
}

int RenderBinder::blitHud() {
    if (!fb_ || !world_.hudEnabled)
        return 0;
    int painted = 0;
    // Filled HUD bar (real per-pixel surface write).
    for (int yy = world_.hudY; yy < world_.hudY + world_.hudH; ++yy) {
        for (int xx = world_.hudX; xx < world_.hudX + world_.hudW; ++xx) {
            if (render::SurfaceSetPixelRgb(fb_, xx, yy, world_.hudR, world_.hudG,
                                           world_.hudB))
                ++painted;
        }
    }
    // Outline border around it (real Bresenham rect outline leaf).
    render::SurfaceDrawRectOutline(fb_, world_.hudX - 1, world_.hudY - 1,
                                   world_.hudW + 2, world_.hudH + 2,
                                   world_.hudR, world_.hudG, world_.hudB);
    stats_.hudPixels = painted;
    return painted;
}

// ---------------------------------------------------------------------------
RenderStats RenderBinder::renderFrame(shim::IGraphicsDevice& dev) {
    stats_ = RenderStats{};
    if (!loaded_ || !fb_)
        return stats_;

    // Bind this binder as the active FrameHooks context for the frame.
    g_current = this;

    // REAL-BRIDGE: install the real HUD/sprite blit + real scene-walk dispatch for
    // the duration of the frame (idempotent process-static installers).
    if (useRealBridges_) {
        InstallRealHudBridge();    // play::HudRenderHooks::drawSprite -> ShapeShowFromBank
        InstallRealSceneBridge();  // WalkVTable.invoke -> ProcessSceneNodeAppend
        stats_.realBridges = true;
    }

    // REAL: VIBE_Render_RenderMainViewFrame — gate + clear + RenderUniverseFrame
    // (BeginUniverseFrame drives clearRect, then renderTerrain — the REAL floor leaf
    // composited into fb_ when real-bridge mode is on — then sceneWalk: project+sort
    // through the REAL node dispatch; the flush runs from the present bracket below).
    render::RenderMainViewFrame(frame_, hooks_);

    // VIBE_Render_RasterizeMeshList — flush the sorted draw list into the
    // framebuffer (the engine ran this inside the present lock bracket).
    flushDrawList();

    // HUD overlay on top of the rendered world.
    blitHud();
    if (useRealBridges_)
        blitHudSprites();   // REAL sprite-bank blit (ShapeShowFromBank) on the HUD

    g_current = nullptr;

    // PRESENT: copy the software framebuffer into the device's backbuffer, then
    // run the REAL present dispatch (render::PresentFrame, mode 2 = Lock+Blt, the
    // mode that copies ppvBits into the locked surface).
    if (shim::Surface* bb = dev.backbuffer()) {
        if (bb->pixels && bb->bpp == (int)fb_->bpp &&
            bb->width == fb_->width && bb->height == fb_->height) {
            // Stage the rendered pixels into the device backbuffer so PresentFrame's
            // copy + present has live data (PresentState::framebuffer points at it).
            std::size_t bytes = (std::size_t)fb_->pitch * (std::size_t)fb_->height;
            std::memcpy(bb->pixels, fb_->pixels, bytes);
        }
    }
    render::PresentState ps{};
    ps.framebuffer = fb_->pixels;
    ps.copyBytes = fb_->pitch * fb_->height;
    ps.width = fb_->width;
    ps.height = fb_->height;
    stats_.presented = render::PresentFrame(dev, render::PresentMode::DDrawLockBlt, ps);
    stats_.presentCount = 0; // filled by caller via device if desired

    return stats_;
}

int RenderBinder::nonClearPixels() const {
    if (!fb_ || !fb_->pixels)
        return 0;
    // 16bpp: compare each pixel against the packed clear colour.
    u16 clear = (u16)render::PackColor(fb_->fmt, world_.clearR, world_.clearG,
                                       world_.clearB);
    const u16* px = reinterpret_cast<const u16*>(fb_->pixels);
    int n = 0;
    int total = fb_->widthPx * fb_->height;
    for (int i = 0; i < total; ++i)
        if (px[i] != clear)
            ++n;
    return n;
}

} // namespace guild::play
