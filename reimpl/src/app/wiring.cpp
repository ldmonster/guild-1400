// gilde.exe — app spine wiring (guild::app). Integration glue only.
// See wiring.h for the WIRED-vs-STUBBED table. No subsystem logic lives here.
#include "app/wiring.h"

#include "gui/hud.h"
#include "gui/input.h"
#include "gui/window.h"
#include "gui/tooltip.h"
#include "gui/chatconsole.h"
#include "sim/entity.h"
#include "sim/character.h"
#include "sim/object.h"
#include "sim/universe.h"   // UniverseCreateDefaultCameras / SwitchActiveSlot (real)
#include "sim/real_hooks.h"   // InstallRealSimHooks  (command-emit / entity-query / charaction)
#include "sim/real_hooks2.h"  // InstallRealSimHooks2 (interaction emit / pamphlet / turn bridge)
#include "sim/real_hooks3.h"  // InstallRealSimHooks3 (NPC leaf builders / He pool / combat sink)
#include "sim/real_hooks4.h"  // InstallRealSimHooks4 (command-APPLY entity mutators)
#include "sim/object_attach_wiring.h"  // InstallRealObjectAttachWiring (factory attach -> 0x5b3e30)
#include "sim/real_reaper_wiring.h"    // InstallRealReaperWiring (NpcEventHooks reaper leaves)
#include "sim/wire_npcevent.h"         // InstallRealNpcEventWiring / ...NpcEvent2Wiring (supersedes reaper)
#include "sim/wire_charaction.h"       // InstallRealCharActionWiring (CharAction step bridges)
#include "sim/wire_charaction2.h"      // InstallRealCharAction2Wiring (CharActionStep5-8 RNG)
#include "sim/wire_combat.h"           // InstallRealCombatWiring (combat-driver RNG)
#include "sim/wire_charrender.h"       // InstallRealCharRenderWiring (CharRender pool/strcmp)
#include "sim/wire_ai.h"               // InstallRealAiWiring (AiRecon5 decision leaves)
#include "world/wire_building.h"       // InstallRealBuildingWiring (building bridges)
#include "world/wire_event_office.h"   // InstallRealEventOfficeWiring (mission-req / interaction RNG)
#include "world/wire_location.h"       // InstallRealLocationWiring (contact-loop feast)
#include "render/wire_scene_fx.h"      // InstallRealSceneFxWiring (pure-logic scene/fx leaves)
#include "world/wire_event.h"          // InstallRealEventWiring (EventHooks / Event3/4/5)
#include "sim/wire_he.h"               // InstallRealHeWiring (He entity-query / handler / group-interact)
#include "sim/wire_charstate.h"        // InstallRealCharStateWiring (CharState + factory leaves)
#include "sim/wire_cutscene.h"         // InstallRealCutsceneWiring (cutscene body leaves)
#include "sim/wire_inventory.h"        // InstallRealInventoryWiring (apply-target / bauplatz / apply8)
#include "sim/wire_charrender2.h"      // InstallRealCharRender2Wiring (path normalize)
#include "sim/wire_apply_input.h"      // InstallRealApplyInputWiring (order router / target pick)
#include "world/wire_election.h"       // InstallRealElectionWiring (guild eligibility / office entry)
#include "world/wire_history_amt.h"    // InstallRealHistoryAmtWiring (amt economy / mission-req-event)
#include "sim/wire_npcaction1.h"       // InstallRealNpcAction1Wiring (NpcAction5/6/7)
#include "sim/wire_npcaction2.h"       // InstallRealNpcAction2Wiring (NpcAction8/9/10)
#include "sim/wire_npcaction3.h"       // InstallRealNpcAction3Wiring (NpcAction11/12 + NpcMarket)
#include "world/wire_production.h"     // InstallRealProductionWiring (production / personnel2)
#include "sim/wire_script.h"           // InstallRealScriptWiring (script-VM strcmp)
#include "sim/wire_object.h"           // InstallRealObjectWiring (object scene-entity / move-universe)
#include "sim/wire_cmdops.h"           // InstallRealCmdOpsWiring (combat order-driver RNG)
#include "sim/wire_recon45.h"          // InstallRealRecon45Wiring (recon4/5 sub-hooks)
#include "world/wire_meister_loc.h"    // InstallRealMeisterLocWiring (Meister3 / LocationDialog4 / MissionName)
#include "world/wire_economy2.h"       // InstallRealEconomy2Wiring (Supervision / Stock)
#include "sim/wire_spawnmotion.h"      // InstallRealSpawnMotionWiring (building lifecycle)
#include "sim/wire_actionops.h"        // InstallRealActionOpsWiring (misc-action find-nearby)
#include "sim/wire_selinput.h"         // InstallRealSelInputWiring (selection / check)
#include "sim/wire_objscene.h"         // InstallRealObjSceneWiring (scene-sync)
#include "world/wire_dialogtut.h"      // InstallRealDialogTutWiring (tutorial-mission / gesetz)
#include "sim/wire_combat2.h"          // InstallRealCombat2Wiring (brawl / recon2)
#include "sim/wire_misc2.h"            // InstallRealMisc2Wiring (personnel-gui)
#include "world/wire_worldnet.h"       // InstallRealWorldNetWiring (audit no-op)
#include "world/wire_privilege_panels_a.h" // InstallPrivilegePanelsA (SET-A privilege leaves)
#include "world/wire_privilege_panels_b.h" // InstallPrivilegePanelsB (SET-B privilege leaves)
#include "sim/command_apply3.h" // RegisterApplyHandlers3 + ApplyPacket3 (the apply jump table)
#include "sim/command_apply6.h" // RegisterApplyHandlers6 (incl. opcode-0x1B relation matrix)
#include "world/city.h"
#include "world/production.h"
#include "world/data_load.h"
#include "render/shapebank.h"
#include "audio/sound3d.h"

#include "app/real_boot.h"
#include "app/render_submit.h"
#include "gui/form_loader.h"
#include "io/gamestate.h"
#include "io/save_world_load.h"  // LoadWorld / WorldState (full entity-table populate)
#include "io/vfs.h"

#include "render/mesh.h"
#include "render/scene.h"
#include "render/meshlist.h"
#include "render/clip.h"
#include "render/surface.h"
#include "render/geometry_types.h"

// REAL render bridges (PLAYABLE_PLAN P6) the live frame de-inerts when enabled.
#include "play/terrain_render.h"      // Heightfield / TerrainView / RenderTerrain (real floor)
#include "play/wire_scene_bridge.h"   // SceneDrawNode / WalkSceneTree (real node dispatch)
#include "play/wire_hud_bridge.h"     // InstallRealHudBridge / BlitHudSprite (real 2D blit)
#include "play/wire_render_bridge.h"  // InstallRealRenderBridge (real Character attach/head leaves)
#include "render/colorformat.h"

#include "shim_impl/disk_filesystem.h"
#include "shim_impl/loopback_socket.h"
#include "shim_impl/mem_filesystem.h"
#include "shim_impl/memory_graphics.h"
#include "shim_impl/null_audio.h"
#include "shim_impl/null_platform.h"

#include <algorithm>

// gilde.exe 0x43dfb0 — VIBE_Character_RegisterScriptCommands. Forward-declared to
// avoid pulling the heavy script/script_vm.h into this widely-shared TU (its
// transitive include set perturbs other headers' include ordering).
namespace guild::script { int RegisterScriptCommands(); }

namespace guild::app {

using guild::config::IProfileProvider;

namespace {

// Recording ILogSink for the ErrorLog hook (the original wrote to a .log file /
// MessageBox / OutputDebugString; here a record sink so the real FormatMessage
// path runs without touching the OS).
class RecordingLogSink : public config::ILogSink {
public:
    void file(const std::string& t) override { last_ = t; }
    void msgBox(const std::string& t) override { last_ = t; }
    void debug(const std::string& t) override { last_ = t; }
    void console(const std::string& t) override { last_ = t; }
    const std::string& last() const { return last_; }
private:
    std::string last_;
};

// An empty profile provider (all defaults) used when no INI is injected.
class EmptyProfile : public IProfileProvider {
public:
    std::string getString(const std::string&, const std::string&,
                          const std::string& def) const override { return def; }
    int getInt(const std::string&, const std::string&, int def) const override { return def; }
};

EmptyProfile g_emptyProfile;
RecordingLogSink g_logSink;

// The market-ambience sample the per-frame market SFX trigger resolves (modelled
// from the Athmos\Markt loop the ambient updater plays).
constexpr char kMarketSampleName[] = "Athmos\\Markt";

// ===========================================================================
// Real per-frame draw-list pipeline backing the FrameHooks.
// ---------------------------------------------------------------------------
// FrameHooks (render/frame.h) are plain C function pointers, so the real
// scene-walk + draw-list flush they drive are backed by this file-scope context
// (the original's engine globals — the PolyLists, the live scene mesh, the
// framebuffer surface — were likewise process globals). RealSubsystems installs
// the two hooks (sceneWalk, flushDrawList) onto its FrameHooks so a non-headless
// frame actually BUILDS + PROJECTS + CULLS + SORTS + RASTERIZES a draw list
// against the real render siblings, then presents it.
//
// The scene is one front-facing textured quad (two triangles) sitting in the
// view; the walk projects its vertices (render::ProjectVerticesToScreen), the
// sort orders the list (render::RadixSortDrawList) and the flush rasterizes it
// (render::RasterizeMeshList) into a 16bpp software Surface.
struct RealFrameScene {
    static constexpr int kFbW = 64;
    static constexpr int kFbH = 48;
    static constexpr int kMaxPolys = 64;

    render::Surface* fb = nullptr;
    render::Vertex   verts[4];
    render::Polygon  polys[2];
    render::MeshGeometry geom{};

    render::DrawListEntry pool1[kMaxPolys];
    render::DrawListEntry pool2[kMaxPolys];
    render::DrawListBuffers db{};
    render::ClipScratch     scratch{};
    render::SpanDispatch    dispatch{};

    int lastAppended = 0;   // entries appended by the most recent walk
    int lastDrawn = 0;      // triangles rasterized by the most recent flush

    // ---- REAL-BRIDGE state (de-inert the live frame; OFF by default) --------
    bool                  realBridges = false;
    render::Surface*      terrainFb = nullptr;     // 8bpp floor surface (composited)
    play::Heightfield     hf{};
    play::TerrainView     view{};
    std::vector<render::Vertex>  meshVerts;        // real multi-tri mesh verts
    std::vector<render::Polygon> meshPolys;        // real multi-tri mesh polys
    render::MeshGeometry         meshGeom{};
    std::vector<render::DrawListEntry> realPool;   // project scratch sink
    int  lastTerrainTris = 0;   // tris the REAL floor leaf drew this frame
    int  lastTerrainPix  = 0;   // fb pixels the composited terrain painted
    int  lastSceneDisp   = 0;   // nodes the REAL ProcessSceneNodeAppend dispatched
    int  lastMeshTris    = 0;   // polys sourced from the REAL mesh
    int  lastHudSprites  = 0;   // HUD sprite shapes the REAL blit drew
    u8   clearR = 0, clearG = 0, clearB = 64;      // sky clear for the composited frame

    void init() {
        if (fb)
            return;
        fb = render::SurfaceCreate(kFbW, kFbH, 16);

        // A quad in the view (model-space). The projection maps model x->screenX
        // and model z->screenY, so distinct z values give the quad a non-degenerate
        // screen area; CW screen winding -> negative signed area -> front-facing
        // (backface bit set => appended). +76 clipFlags = 0 => direct (no-clip) path.
        auto V = [&](int i, float x, float z, float u, float v) {
            verts[i] = render::Vertex{};
            verts[i].x = x; verts[i].y = 0.0f; verts[i].z = z;
            verts[i].u = u; verts[i].v = v;
            verts[i].clipFlags = 0;   // +76: no frustum bits => direct (no-clip) path
        };
        V(0,  4.0f, 1.0f, 0.0f, 0.0f);
        V(1, 40.0f, 1.0f, 1.0f, 0.0f);
        V(2,  4.0f, 5.0f, 0.0f, 1.0f);
        V(3, 40.0f, 5.0f, 1.0f, 1.0f);

        polys[0] = render::Polygon{};
        polys[0].v0 = &verts[0]; polys[0].v1 = &verts[1]; polys[0].v2 = &verts[2];
        polys[1] = render::Polygon{};
        polys[1].v0 = &verts[1]; polys[1].v1 = &verts[3]; polys[1].v2 = &verts[2];

        geom.vertices = verts;
        geom.polygons = polys;
        geom.polyCount = 2;
        geom.polyCap = 2;
        geom.vertexCount = 4;

        db.base1 = pool1;
        db.base2 = pool2;
        db.count = 0;
        db.capacity = kMaxPolys;
    }

    // VIBE_Render_BeginUniverseFrame's scene-graph project/cull/append walk: build
    // the draw list from the scene, then radix-sort it back-to-front. Returns the
    // appended entry count (snapshotted into FrameState::appendedPolys).
    int sceneWalk() {
        if (!fb)
            init();
        db.count = 0;

        render::ProjectParams pp{};
        // Camera at origin; X/Z projection scales place the quad on-screen.
        pp.eye[0] = 0.0f; pp.eye[1] = 0.0f; pp.eye[2] = 0.0f;
        pp.invDepth[0] = pp.invDepth[1] = pp.invDepth[2] = 1.0f;
        pp.biasX  = 0.875f;     // flt_628B94
        pp.scaleY = 8.0f;       // light/shade term scale
        pp.scaleX = 8.0f;       // Z->screenY scale (vertex z=1 -> y ~ 8.875)
        pp.lightCap = 254.0f;   // flt_628B98
        pp.screenW  = (float)kFbW; // on-screen [0,screenW) clamp

        render::DrawList sink = db.AppendSink();
        // objFlags530: 0x40 (double-sided) so degenerate-normal quads still append;
        // viewCull42 0 (no backface gate). The screen winding decides front/back.
        render::ProjectVerticesToScreen(&geom, pp, /*objFlags530=*/0x40,
                                        /*viewCull42=*/0, &sink);
        db.count = sink.count;

        // Radix-sort the appended entries (ends back in base1 = PolyList1).
        render::RadixSortDrawList(db, (u32)db.count, /*twoPassOnly=*/false);
        lastAppended = db.count;
        return lastAppended;
    }

    // VIBE_Render_RasterizeMeshList: flush the sorted list into the framebuffer.
    void flushDrawList() {
        if (!fb)
            return;
        render::MeshList list{db.base1, db.count};
        render::ClipContext ctx{0, nullptr};  // no extra clip planes (direct path)
        render::ProjectScalars proj{1.0f, 0.0f, 1.0f, 0.0f};
        lastDrawn = render::RasterizeMeshList(list, fb, dispatch, ctx, proj, scratch);
    }

    // ---- REAL-BRIDGE de-inert: build the real floor + real multi-tri mesh ----
    // Wires the live frame's terrain (play::RenderTerrain) + scene-walk dispatch
    // (render::ProcessSceneNodeAppend) + HUD sprite (render::ShapeShowFromBank).
    void enableRealBridges() {
        if (!fb)
            init();
        realBridges = true;

        // A synthetic ground-shaped heightfield the REAL floor leaf rasterizes.
        hf = play::Heightfield::MakeSynthetic(/*edge=*/16, /*seed=*/0xB7u);
        view = play::TerrainView::MakeTopDown(hf.size, hf.tileSpan, kFbW, kFbH);
        if (!terrainFb)
            terrainFb = render::SurfaceCreate(kFbW, kFbH, 8);

        // A real multi-tri mesh (a hex fan: 7 verts, 6 tris) seated on the floor.
        meshVerts.clear(); meshPolys.clear();
        const float cx = kFbW * 0.30f, cz = kFbH * 0.60f, r = kFbW * 0.18f;
        auto mkV = [&](float x, float z, float u, float vv) {
            render::Vertex v{}; v.x = x; v.y = 0.0f; v.z = z; v.u = u; v.v = vv;
            v.lightIdx = 210; v.clipFlags = 0; meshVerts.push_back(v);
        };
        mkV(cx, cz, 0.5f, 0.5f);
        static const float cs[6] = { 1.0f, 0.5f, -0.5f, -1.0f, -0.5f, 0.5f };
        static const float sn[6] = { 0.0f, 0.8660254f, 0.8660254f, 0.0f,
                                     -0.8660254f, -0.8660254f };
        for (int i = 0; i < 6; ++i)
            mkV(cx + r * cs[i], cz + r * sn[i], (cs[i] + 1) * 0.5f, (sn[i] + 1) * 0.5f);
        for (int i = 0; i < 6; ++i) {
            render::Polygon p{};
            p.v0 = &meshVerts[0];
            p.v1 = &meshVerts[1 + i];
            p.v2 = &meshVerts[1 + ((i + 1) % 6)];
            meshPolys.push_back(p);
        }
        meshGeom = render::MeshGeometry{};
        meshGeom.vertices = meshVerts.data();
        meshGeom.polygons = meshPolys.data();
        meshGeom.polyCount = (i32)meshPolys.size();
        meshGeom.polyCap   = (i32)meshPolys.size();
        meshGeom.vertexCount = (i32)meshVerts.size();
        realPool.assign((size_t)kMaxPolys, render::DrawListEntry{});
    }

    // The REAL floor leaf into the 8bpp companion, composited into fb as grey
    // (run as the renderTerrain hook — terrain first, then the scene walk).
    int drawRealTerrain() {
        if (!terrainFb || !hf.valid())
            return 0;
        render::SurfaceColorFill(terrainFb, 0, 0, 0);
        play::TerrainRenderStats ts = play::RenderTerrain(terrainFb, hf, view, 0);
        lastTerrainTris = ts.trisDrawn;
        int painted = 0;
        int w = terrainFb->width < fb->width ? terrainFb->width : fb->width;
        int h = terrainFb->height < fb->height ? terrainFb->height : fb->height;
        for (int y = 0; y < h; ++y) {
            const u8* row = terrainFb->pixels + (size_t)y * terrainFb->widthPx;
            for (int x = 0; x < w; ++x) {
                u8 shade = row[x];
                if (!shade) continue;
                if (render::SurfaceSetPixelRgb(fb, x, y, shade, shade, shade))
                    ++painted;
            }
        }
        lastTerrainPix = painted;
        return painted;
    }

    // The REAL scene-walk: project the real mesh, dispatch its polys through
    // render::ProcessSceneNodeAppend (play::WalkSceneTree), append into db.base1.
    int realSceneWalk() {
        db.count = 0;
        lastMeshTris = (int)meshPolys.size();
        if (meshVerts.empty() || meshPolys.empty())
            return 0;
        render::ProjectParams pp{};
        pp.eye[0] = pp.eye[1] = pp.eye[2] = 0.0f;
        pp.invDepth[0] = pp.invDepth[1] = pp.invDepth[2] = 1.0f;
        pp.biasX = 0.0f; pp.scaleY = 1.0f; pp.scaleX = 1.0f;
        pp.lightCap = 254.0f; pp.screenW = (float)kFbW;
        render::DrawList scratchSink{realPool.data(), 0, (i32)realPool.size()};
        render::ProjectVerticesToScreen(&meshGeom, pp, 0x40, 0, &scratchSink);

        play::SceneDrawNode node{};
        node.nodeType = 3; node.cullByte = 0;
        node.polys = meshGeom.polygons; node.polyCount = meshGeom.polyCount;
        node.texSortId = nullptr;
        std::vector<play::SceneDrawNode> nodes{node};

        play::SceneBridgeContext sc{};
        sc.out = render::DrawList{db.base1, 0, db.capacity};
        sc.appendCtx.mode = render::NodeAppendMode::Software;
        sc.appendCtx.baseKey = 1;
        play::WalkSceneTree(nodes.data(), /*walkMask=*/0x1FF, sc);
        lastSceneDisp = sc.nodesDispatched;
        db.count = sc.out.count;
        render::RadixSortDrawList(db, (u32)db.count, /*twoPassOnly=*/false);
        lastAppended = db.count;
        return lastAppended;
    }

    // The REAL HUD sprite-bank blit into fb (render::ShapeShowFromBank).
    int drawRealHud() {
        if (!fb)
            return 0;
        int sx = 4, sy = 4;
        if (sx + 8 > fb->widthPx) sx = fb->widthPx - 8;
        if (sy + 8 > fb->height)  sy = fb->height - 8;
        if (sx < 0 || sy < 0) return 0;
        int rv = play::BlitHudSprite(sx, sy, 0, reinterpret_cast<u16*>(fb->pixels),
                                     fb->widthPx, fb->fmt);
        lastHudSprites = (rv != 0) ? 1 : 0;
        return lastHudSprites;
    }
};

// The single live scene the FrameHooks drive (one per process, like the engine's
// global PolyLists). A pointer is threaded to the C-style hooks below.
RealFrameScene* g_frameScene = nullptr;

int FrameSceneWalk(char /*a2*/) {
    if (!g_frameScene)
        return 0;
    return g_frameScene->realBridges ? g_frameScene->realSceneWalk()
                                     : g_frameScene->sceneWalk();
}
void FrameSceneFlush() {
    if (g_frameScene)
        g_frameScene->flushDrawList();
}
// renderTerrain hook (real-bridge mode): run the REAL floor leaf into the 8bpp
// companion + composite into the live fb before the scene walk (terrain first).
void FrameRenderTerrain(void* /*terrain*/, char /*a2*/) {
    if (g_frameScene && g_frameScene->realBridges)
        g_frameScene->drawRealTerrain();
}

} // namespace

RealSubsystems::RealSubsystems(shim::IPlatform* plat, shim::IGraphicsDevice* gfx,
                               shim::IAudioDevice* audioDev, shim::IFileSystem* fs,
                               shim::INetSocket* net, const IProfileProvider* ini)
    : plat_(plat), gfx_dev_(gfx), audio_dev_(audioDev), fs_(fs), net_(net),
      ini_(ini ? ini : &g_emptyProfile),
      tracker_(heap_),
      sound_(audioDev),
      transport_(net),
      timer_(plat) {
    scriptSlots_.resize(sim::kScriptContextCount);
    // Build the season-0 day/night keyframe thresholds the brightness curve uses.
    render::BuildTimeTable(0, dayKeyframes_);
    // Seed a representative 24-hour weather-intensity arc (dword_11BC038) so the
    // real render::Weather* core has data to integrate (mid-day peak).
    for (int h = 0; h < 24; ++h)
        weatherArc_[h] = (h >= 8 && h <= 18) ? 60 + (h - 8) * 8 : 20;
    // Bind the music director's streaming sink (recording stand-in) and seed one
    // OUTDOOR track entry so SelectOutdoorSeasonTrack/UpdateOutdoorTrackPlayback
    // run the real selection state machine.
    musicDir_.sink = &musicSink_;

    // Install the real per-frame draw-list pipeline onto the frame hooks: a world
    // frame now BUILDS + PROJECTS + CULLS + SORTS (sceneWalk) and RASTERIZES
    // (flushDrawList) a real draw list against the render siblings, then presents.
    // (These two FrameHooks were null/mock before this pass.)
    InstallRealFramePipeline();
}

void RealSubsystems::InstallRealFramePipeline() {
    static RealFrameScene s_scene;   // the process-global live scene (one per run)
    s_scene.init();
    g_frameScene = &s_scene;
    frameHooks_.sceneWalk = &FrameSceneWalk;       // mock(null) -> REAL
    frameHooks_.flushDrawList = &FrameSceneFlush;  // mock(null) -> REAL
    frameSceneInstalled_ = true;
}

void RealSubsystems::EnableRealRenderBridges() {
    // De-inert the live frame's terrain + scene-walk dispatch + HUD sprite leaves
    // (PLAYABLE_PLAN P6). OFF by default so the lifecycle/wiring tests are
    // unaffected; once enabled the next renderMainViewFrame draws REAL terrain +
    // a REAL multi-tri mesh (via render::ProcessSceneNodeAppend) + a REAL HUD
    // sprite (via render::ShapeShowFromBank) into the live framebuffer.
    if (!frameSceneInstalled_)
        InstallRealFramePipeline();
    if (!g_frameScene)
        return;
    g_frameScene->clearR = 0; g_frameScene->clearG = 0; g_frameScene->clearB = 64;
    g_frameScene->enableRealBridges();
    // De-inert the live frame hooks: terrain leaf + scene-walk dispatch + HUD blit.
    frameHooks_.terrain       = g_frameScene;
    frameHooks_.renderTerrain = &FrameRenderTerrain;   // INERT(null) -> REAL floor
    frame_.hasTerrain         = true;                  // dword_64A028 != 0
    play::InstallRealSceneBridge();   // WalkVTable.invoke -> ProcessSceneNodeAppend
    play::InstallRealHudBridge();     // HudRenderHooks::drawSprite -> ShapeShowFromBank
    // De-inert the Character attach-offset / head-variant render cohort
    // (sim::CharRenderHooks): VIBE_Character_ComputeAttachOffset @0x404860 ->
    // util::PointThroughBoneChainPivot (0x5c8d0c) + mesh-root translation
    // (mesh+132/136/140) and VIBE_Character_ApplyHeadVariant @0x57c548 ->
    // sim::ObjectSelectTextureSet (0x5b3f54). These leaves belong to the same
    // "real render bridges" feature the engine enables for the in-game frame: with
    // the cohort on, an attached actor's geometry is transformed through the real
    // bone-chain pivot and its texture set is actually resolved (rule 13).
    play::InstallRealRenderBridge();
    realRenderBridges_ = true;
}

const void* RealSubsystems::frameBufferPixels(int* w, int* h, int* pitch) const {
    if (!g_frameScene || !g_frameScene->fb)
        return nullptr;
    render::Surface* s = g_frameScene->fb;
    if (w) *w = s->width;
    if (h) *h = s->height;
    if (pitch) *pitch = s->pitch;
    return s->pixels;
}

void RealSubsystems::BindRealAssets(const RealGameAssets* assets,
                                    const std::string& gameDir) {
    assets_ = assets;
    gameDir_ = gameDir;
    realAssets_ = (assets != nullptr);
}

int RealSubsystems::shapeBankCount() const {
    return shapeBank_.empty() ? 0
                              : static_cast<int>(render::ShapeBankCount(shapeBank_.data()));
}

bool RealSubsystems::fired(const char* hook) const {
    for (const auto& e : events_)
        if (std::string(e.hook) == hook) return true;
    return false;
}

bool RealSubsystems::firedReal(const char* hook) const {
    for (const auto& e : events_)
        if (std::string(e.hook) == hook && e.kind == Kind::Real) return true;
    return false;
}

// ===========================================================================
// init
// ===========================================================================
void RealSubsystems::errorLogInit() {
    // REAL: exercise the reconstructed ErrorLog formatter/sink path.
    config::FormatMessage(g_logSink, config::kLogDebug, "main_InitSubsystems()");
    rec("errorLogInit", Kind::Real);
}

void RealSubsystems::memoryInitTracker(int bytes) {
    // REAL: VIBE_Memory_InitTracker. `bytes` is the capacity hint (32678).
    tracker_.Init(static_cast<u32>(bytes));
    trackerInited_ = true;
    rec("memoryInitTracker", Kind::Real);
}

void RealSubsystems::memPoolStartupStack(int /*size*/) {
    // REAL: prime the fixed-block pool. The original's startup-stack pool begins
    // empty; ResetBlocks establishes the reset state without committing chunks.
    mem::MemPoolResetBlocks(&pool_);
    rec("memPoolStartupStack", Kind::Real);
}

// moveahead.dll is replaced (rule-6, user-approved) by pl_mpeg behind the IVideo
// shim. "Loading the movie DLL" == creating the video decoder; success means a
// video backend is compiled in (GUILD_BACKEND). The portable build's
// CreateVideoDecoder() returns nullptr -> false -> the engine skips movies, the
// same as the prior stub.
bool RealSubsystems::loadMovieDll() {
    videoDecoder_.reset(shim::CreateVideoDecoder());
    const bool ok = (videoDecoder_ != nullptr);
    rec("loadMovieDll", ok ? Kind::Real : Kind::Stub);
    return ok;
}

void RealSubsystems::fileCreateDirectory(const std::string& path) {
    if (fs_) {
        // REAL: the original VIBE_File_CreateDirectory ensured a save/config dir
        // existed (CreateDirectoryA). The only reconstructed OS boundary for the
        // file layer is shim::IFileSystem; faithfully "ensure exists" by probing
        // the path and, when absent, materialising a directory marker through the
        // shim's create-on-write open (no real CreateDirectoryA reconstructed).
        if (!fs_->exists(path.c_str())) {
            std::string marker = path;
            if (!marker.empty() && marker.back() != '\\' && marker.back() != '/')
                marker += '\\';
            marker += ".dir";
            if (shim::IFile* f = fs_->open(marker.c_str(), "w"))
                fs_->close(f);
        }
        rec("fileCreateDirectory", Kind::Real);
    } else {
        rec("fileCreateDirectory", Kind::Stub);
    }
}

void RealSubsystems::vfsInit(const std::string& /*root*/) {
    if (fs_) {
        // REAL: VIBE_Vfs_Init — bind the host filesystem (case-insensitive off,
        // matching byte_62EB84 default for the loose-file path).
        io::VfsInit(fs_, false);
        vfsInited_ = true;
        rec("vfsInit", Kind::Real);
    } else {
        rec("vfsInit", Kind::Stub);
    }
}

void RealSubsystems::timeBaseStartTimer(int a, int b) {
    if (plat_) {
        // REAL: VIBE_TimeBase_StartTimer — begin the periodic multimedia timer.
        // `a` is the per-tick delay (ms), `b` the periodic flag; driven by the
        // shim clock (crt::TimeBase::StartTimer reseeds + records the start time).
        timer_.StartTimer(static_cast<u32>(a > 0 ? a : 1), b);
        timerRunning_ = true;
        rec("timeBaseStartTimer", Kind::Real);
    } else {
        rec("timeBaseStartTimer", Kind::Stub);
    }
}

// ===========================================================================
// render init
// ===========================================================================
void RealSubsystems::configReadGfxAndSound() {
    // REAL: VIBE_Config_ReadGfxAndSoundSettings — read [Gfx]/[Sound]/[Game].
    config::ReadGfxAndSoundSettings(*ini_, gfx_, snd_, game_);
    rec("configReadGfxAndSound", Kind::Real);
}

bool RealSubsystems::renderEnumDisplayModes() {
    rec("renderEnumDisplayModes", Kind::Stub);
    return true; // permit the spine to continue
}

bool RealSubsystems::renderInitEngineDevice(int w, int h, int /*bpp*/, bool /*fs*/) {
    // The actual device bring-up went through shim::IGraphicsDevice in the spine
    // (gfx_.init was already called by InitDisplayAndPaths). Seed the present
    // state so presentFrame() has valid dimensions.
    present_.width = w;
    present_.height = h;
    if (gfx_dev_) {
        // REAL: prime the reconstructed scene-render walk + present path with an
        // initial (empty-world) frame so the engine globals (gate / reentrancy /
        // depth bounds) reach their post-init state, then present it. This is the
        // scene-init half of the engine device bring-up reachable headless.
        frame_.engineOn = true;
        frame_.hasWorld = false;
        frame_.hasTerrain = false;
        render::RenderMainViewFrame(frame_, frameHooks_);
        render::PresentFrame(*gfx_dev_, render::PresentMode::GdiBitBlt, present_);
        ++presentCount_;
        rec("renderInitEngineDevice", Kind::Real);
        return true;
    }
    rec("renderInitEngineDevice", Kind::Stub);
    return true;
}

void RealSubsystems::universeCreateDefaultCameras() {
    // REAL: VIBE_Universe_CreateDefaultCameras @0x5b5f48 — spawn the "MegaCam"
    // perspective camera (and, when byte_649D54 is set, the three "Oben"/"Vorne"/
    // "Seite" ortho cams) and link them into the active scene slot. The object
    // spawn/position/link are render/scene leaves (VIBE_Object_Spawn / SetPosition /
    // LinkIntoScene) routed through the module's UniverseRenderHooks (inert default:
    // they return synthetic non-zero handles); the camera bootstrap bookkeeping
    // (g_megaCam et al.) is fully reconstructed, so run the real builder. byte_649D54
    // (g_extraCameras) is left at its reset default (0) on the headless path, so the
    // single MegaCam is created — exactly the cold-init shape the spine performs.
    sim::ResetUniverse();              // cold slot/global state (byte_13ECEC8 zeroed)
    sim::UniverseCreateDefaultCameras();
    universeMegaCam_ = sim::g_megaCam; // observable: the spawned camera handle
    rec("universeCreateDefaultCameras", Kind::Real);
}

void RealSubsystems::inputDirectInputInit(int) {
    // REAL: VIBE_Input_DirectInputInit established the DirectInput devices and the
    // mouse/click latch state. The device acquisition is OS-coupled (no shim
    // DirectInput backend); the reconstructed, platform-neutral half is the GUI
    // input latch-state reset (g_mouseClick/g_mouseDown/g_lastClicked* etc.).
    gui::ResetInputState();
    rec("inputDirectInputInit", Kind::Real);
}
void RealSubsystems::renderSetAssetPaths() { rec("renderSetAssetPaths", Kind::Stub); }
void RealSubsystems::renderApplyGfxSettings() { rec("renderApplyGfxSettings", Kind::Stub); }

bool RealSubsystems::guiLoadGfxFile(const std::string&) {
    if (realAssets_) {
        // REAL-ASSET: load the actual gfx/gilde.gfx through the reconstructed gui
        // loader over the now-bound VFS (VIBE_Gui_LoadGfxFile file-parse half). This
        // populates g_gfxObjects[] (1806 records) from real bytes — no synthetic
        // shape stand-in.
        gui::ResetWindows();
        bool ok = gui::Form_LoadFromFile("gfx/gilde.gfx");
        gfxObjectCount_ = gui::g_gfxObjectCount;
        rec("guiLoadGfxFile", Kind::Real);
        return ok;
    }
    // REAL: VIBE_Gui_LoadGfxFile loaded the .gfx (shape bank + form/text tables).
    // The on-disk loader is deferred (needs a real .gfx asset); the reconstructed,
    // platform-neutral half wired here is: reset the GUI form/window tables, then
    // build a shape bank by appending one synthetic shape via the real
    // ShapeBankAddShape path, and seed the text/label DB. This exercises the real
    // gui::text::TextDb + render::ShapeBank* entry points.
    gui::ResetWindows();

    // Build a minimal 16bpp shape (depth 0) and add it through the real bank path.
    shapeBank_.assign(8192, 0);
    u8 shape[64] = {0};
    shape[render::shape_off::kSize] = sizeof(shape);     // +0x00 total size
    shape[render::shape_off::kWidth] = 4;                // +0x06 width
    shape[render::shape_off::kHeight] = 4;               // +0x0A height
    shape[render::shape_off::kColorDepth] = 0;           // +0x0C depth
    render::ShapeBankAddShape(shapeBank_.data(), shape);

    // Seed a couple of label DB entries (mirrors the .gfx form-label load).
    textDb_.Add("OK", "btn_ok");
    textDb_.Add("Cancel", "btn_cancel");

    rec("guiLoadGfxFile", Kind::Real);
    return true; // permit the spine to continue
}

void RealSubsystems::widgetInitSystem() {
    // REAL: reset the reconstructed GUI table/input/HUD state (the closest
    // wireable analogue of VIBE_Widget_InitSystem's table reset).
    gui::ResetWindows();
    gui::ResetInputState();
    gui::ResetHudSlots();
    rec("widgetInitSystem", Kind::Real);
}

// ===========================================================================
// engine init
// ===========================================================================
bool RealSubsystems::textLoadDefinitionFile(const std::string&) {
    // REAL: VIBE_Text_LoadDefinitionFile populated the localized label/text DB.
    // The .dat loader (VIBE_Text_BuildTextArray) is deferred (needs the on-disk
    // resource); the reconstructed in-memory DB (gui::text::TextDb) is filled here
    // with a representative set so FindIndex/Tag/Text resolve against real code.
    textDb_.Add("Augsburg", "city_0");
    textDb_.Add("Cologne", "city_1");
    textDb_.Add("Yes", "answer_yes");
    textDb_.Add("No",  "answer_no");
    // The real lookup path resolves the name back to its index (case-insensitive).
    int idx = textDb_.FindIndex("answer_yes");
    (void)idx;
    rec("textLoadDefinitionFile", Kind::Real);
    return true;
}

void RealSubsystems::netConnectToServer(const std::string& host, int port) {
    if (net_) {
        // REAL: VIBE_Net_ConnectToServer. Empty host == local/single-player; the
        // loopback socket already reports connected, so this binds the transport.
        transport_.ConnectToServer(host.empty() ? nullptr : host.c_str(),
                                   static_cast<u16>(port));
        rec("netConnectToServer", Kind::Real);
    } else {
        rec("netConnectToServer", Kind::Stub);
    }
}

// Install EVERY reconstructed gameplay hook bridge into its global hook table
// (rule 13). Operates entirely on process globals (no per-instance state), so it is
// reusable both by the live spine (commandQueueInitAndSync) and by tests that want
// the full wired system without booting the whole RealSubsystems lifecycle. Each
// installer seeds from its module inert defaults and overrides only wireable fields.
void InstallAllRealGameplayHooks() {
    sim::InstallRealSimHooks();
    sim::InstallRealSimHooks2();
    sim::InstallRealSimHooks3();
    sim::InstallRealSimHooks4();
    sim::InstallRealObjectAttachWiring();
    sim::InstallRealNpcEventWiring();   // supersedes the reaper-only install
    sim::InstallRealNpcEvent2Wiring();
    sim::InstallRealCharActionWiring();
    sim::InstallRealCharAction2Wiring();
    sim::InstallRealCombatWiring();
    sim::InstallRealCharRenderWiring();
    sim::InstallRealAiWiring();
    world::InstallRealBuildingWiring();
    world::InstallRealEventOfficeWiring();
    world::InstallRealLocationWiring();
    render::InstallRealSceneFxWiring();
    world::InstallRealEventWiring();
    sim::InstallRealHeWiring();
    sim::InstallRealCharStateWiring();
    sim::InstallRealCutsceneWiring();
    sim::InstallRealInventoryWiring();
    sim::InstallRealCharRender2Wiring();
    sim::InstallRealApplyInputWiring();
    world::InstallRealElectionWiring();
    world::InstallRealHistoryAmtWiring();
    sim::InstallRealNpcAction1Wiring();
    sim::InstallRealNpcAction2Wiring();
    sim::InstallRealNpcAction3Wiring();
    world::InstallRealProductionWiring();
    sim::InstallRealScriptWiring();
    sim::InstallRealObjectWiring();
    sim::InstallRealCmdOpsWiring();
    sim::InstallRealRecon45Wiring();
    world::InstallRealMeisterLocWiring();
    // Fourth wiring wave (seed-from-defaults; each binds the signature-compatible
    // reconstructed leaves of a previously-inert bridge, rest stay inert stubs).
    world::InstallRealEconomy2Wiring();       // Supervision He-probe/RNG + Stock cmd tails
    sim::InstallRealSpawnMotionWiring();       // building lifecycle release/free-child
    sim::InstallRealActionOpsWiring();         // misc-action find-nearby
    sim::InstallRealSelInputWiring();          // selection parseInt + check person-query
    sim::InstallRealObjSceneWiring();          // scene-sync category/money-rate
    world::InstallRealDialogTutWiring();       // tutorial-mission clock + gesetz portrait
    sim::InstallRealCombat2Wiring();           // brawl He/person + recon2 strcmp/strlen
    sim::InstallRealMisc2Wiring();             // personnel-gui RNG
    world::InstallRealWorldNetWiring();        // audit (no-op): documents the inert net/history points

    // Wave-20 (rule 13): the SET-A guild-office PRIVILEGE panels. InvokePrivilegeLeaf
    // (live caller in sim/contextaction2.cpp) routed through the inert g_privilegeHook
    // (returned 0). InstallPrivilegePanelsA installs a SetPrivilegeLeafHook adapter
    // that, for the 11 SET-A leaf ids (GenerateHatred/ChangeProfession/ExpelWorker/
    // Blackmail/MakePeace/Convert/Interrogation/Medicus/Divorce/Apology/CharmConfirm),
    // dispatches to the reconstructed VIBE_Privilege_Panel* bodies (cost math, RNG
    // draw order, relation deltas, return codes — verbatim). Non-SET-A leaf ids
    // return 0 (identical to the prior inert default). No provider yet -> the panels
    // run against the inert PrivilegePanelHooks (compute verdict, emit nothing) —
    // the documented deferral posture (a live provider binds the command queue later).
    world::InstallPrivilegePanelsA();          // 11 SET-A privilege leaves -> live verdicts

    // Wave-21 (rule 13): the SET-B guild-office PRIVILEGE panels (law / evidence /
    // espionage + dispatch). contextaction2.cpp already invokes these leaf-ids
    // (kPrivEnactLaw/RemoveFromOffice/CounterEspionage/Embezzlement/SwapSeats/
    // Miracle/EvidenceReview(+Alt)/ShowDialog). InstallPrivilegePanelsB installs a
    // SetPrivilegeLeafHook adapter that owns those nine ids and CHAINS onto the
    // SET-A adapter just installed (single g_privilegeHook slot is shared), so both
    // batches stay reachable. No provider yet -> the panels run against the inert
    // PrivilegePanelBHooks (compute verdict, emit nothing) — the documented deferral
    // posture (a live provider binds the command queue + person/office arrays later).
    world::InstallPrivilegePanelsB();          // 9 SET-B privilege leaves -> live verdicts (chained after A)
}

void RealSubsystems::commandQueueInitAndSync() {
    // REAL: VIBE_Command_QueueInitAndSync (init half). Standalone for single-player.
    cmdQueue_.set_standalone(true);
    cmdQueue_.Init();

    // REAL (sim-leaf wiring): the original's engine/command bring-up also bound the
    // command codec + the per-opcode APPLY jump table (funcs_4941F4 @0x631298) and
    // the cross-module entity/charaction/NPC leaves the simulation dispatch invokes.
    // The four reconstructed installers do exactly that — they bind every wireable
    // sim hook to its real reconstructed target (command-emit -> command_codec on a
    // shared CommandQueue; command-APPLY entity mutators -> the real object/building/
    // person modules over the shared g_sceneNodes/g_buildingPersons/g_persons arrays;
    // entity queries -> the real entity arrays; charaction step handlers -> the real
    // character.cpp catalog). Before this pass these installers were reconstructed but
    // NEVER invoked by the spine, so the per-frame/per-turn sim dispatch ran against
    // INERT default hooks. Install them here (the faithful command-system init point)
    // and register the apply-3 jump-table entries onto the owned queue, so an applied
    // packet now mutates the REAL shared entity arrays.
    // Bind every reconstructed gameplay leaf into its previously-inert hook bridge
    // (rule 13) — the faithful command-system init point. Before this, the per-frame/
    // per-turn sim dispatch ran against INERT defaults.
    InstallAllRealGameplayHooks();
    sim::RegisterApplyHandlers3(cmdQueue_);
    // Batch 6 closes the 96-entry table on the owned queue — in particular the
    // opcode-0x1B relation-matrix handler (ExComputeObjectCoords @0x49818C) the
    // new-game commit's six QueueRequestCoord27 packets dispatch to. In the
    // binary this is one static jump table (funcs_4941F4 @0x631298); the batch
    // registries are disjoint, so composing them here is order-independent.
    sim::RegisterApplyHandlers6(cmdQueue_);
    simHooksInstalled_ = true;

    rec("commandQueueInitAndSync", Kind::Real);
}

void RealSubsystems::worldLoadBuildingAndObjectData(const std::string&) {
    if (realAssets_ && fs_) {
        // REAL-ASSET: read the real building/object type tables from the shipped
        // data/A_Geb.dat (72 records) + data/A_Obj.dat (731 records) through the
        // reconstructed loader (VIBE_World_LoadBuildingAndObjectData) over real
        // bytes, then seed the economy table + reset the entity arrays as the
        // original's Game_InitWorldAndSounds does.
        int rc = world::WorldLoadBuildingAndObjectData(fs_, "data/");
        if (rc == 0) {
            buildingTypeCount_ = world::kBuildingTypeLoadCount; // 72
            sceneTypeCount_    = world::kSceneTypeLoadCount;     // 731
        }
        world::CityInitParameterTable(100.0f);
        sim::ResetEntityArrays();

        // Load the configured start city seed (<Stadt>.cty) through the bound VFS
        // (transparent gunzip) + the reconstructed save/city header loader. This is
        // the real "load the city" the spine performs before the session begins.
        if (assets_) {
            const std::string& city = !assets_->stadt.empty() ? assets_->stadt
                                                              : assets_->game.stadt;
            std::string path = RealCityPath(city);
            if (!path.empty()) {
                io::GameState cityState{};
                cityState.relink.assign(io::kRelinkBytes, 0);
                if (io::LoadGameState(path.c_str(), cityState, /*load=*/nullptr)) {
                    cityLoaded_ = true;
                    cityName_   = cityState.header.name;
                }
                // Populate the LIVE entity tables (g_persons / g_objects) from the
                // .cty via the full VIBE_Save_LoadGameFile table-load driver, so the
                // wired per-entity sim/turn/event hooks run over a real loaded world
                // instead of empty state. (LoadWorld resets + loads the header +
                // scene/building/person tables; partial city-seed files stop there.)
                io::WorldState world{};
                if (io::LoadWorld(path.c_str(), world)) {
                    worldLoaded_ = true;
                    for (int i = 0; i < sim::kPersonCapacity; ++i)
                        if (sim::g_persons[i].marker != -1) ++livePersonCount_;
                    for (int i = 0; i < sim::kObjectCapacity; ++i)
                        if (sim::g_objects[i].alive) ++liveObjectCount_;
                }
            }
        }
        rec("worldLoadBuildingAndObjectData", Kind::Real);
        return;
    }
    // REAL: seed the economy parameter table and reset the entity record arrays
    // (the loadable world-data half of VIBE_Game_InitWorldAndSounds reachable
    // without on-disk assets).
    world::CityInitParameterTable(100.0f);
    sim::ResetEntityArrays();
    rec("worldLoadBuildingAndObjectData", Kind::Real);
}

void RealSubsystems::buildingComputeMarketPrices() {
    // REAL: the original VIBE_Building_ComputeMarketPrice recomputed each good's
    // price from its accumulated production output. The on-disk per-building
    // ledger is deferred, but the price input is the production-output integral
    // over the work window — a fully reconstructed economy core
    // (world::ProductionComputeOutputOverTime). Re-seed the 28-good parameter
    // table (real) and integrate one work day's output so a market-price recompute
    // runs against real reconstructed economy code.
    world::CityInitParameterTable(100.0f);
    world::ProdTime start{/*day=*/0, /*hour=*/6, /*minute=*/0};
    world::ProdTime end{/*day=*/0, /*hour=*/22, /*minute=*/0};
    marketWorkMinutes_ =
        world::ProductionComputeOutputOverTime(start, end, /*pauseMode=*/false);
    rec("buildingComputeMarketPrices", Kind::Real);
}

void RealSubsystems::audioStartupMilesDriver() { rec("audioStartupMilesDriver", Kind::Stub); }

bool RealSubsystems::soundLibInit(int voices, int channels, int rate) {
    if (audio_dev_) {
        // REAL: VIBE_Sound_LibInit — bring up the digital output + voice pool.
        bool ok = sound_.init(voices, channels, rate);
        soundInited_ = ok;
        // The digital/3D sound subsystem is now enabled (dword_63C900 / dword_63C904
        // become true once the sound lib + spatial layer are up); the per-frame
        // AudioTick gates the ambient/3D/voice blocks on these.
        audioEnable_.sound3dOn = ok;
        audioEnable_.weatherOn = ok;
        rec("soundLibInit", Kind::Real);
        return ok;
    }
    rec("soundLibInit", Kind::Stub);
    return false;
}

void RealSubsystems::sound3dInitPool(int n) {
    if (audio_dev_) {
        // REAL: VIBE_Sound3d_InitPool sized the 84-byte spatial-voice entry pool
        // (84-byte entries, the byte_424538 leaf). The OS thread/driver is not
        // reconstructed, but the spatial pool IS (audio::Sound3dPool over the real
        // SoundSystem voice pool). Construct the pool with the requested capacity
        // and run one find-free-slot probe so the real pool entry array exists and
        // its allocator is exercised, mirroring the cold pool init.
        int cap = n > 0 ? n : 16;
        sound3dPool_ = std::make_unique<audio::Sound3dPool>(&sound_.voices(), cap);
        (void)sound3dPool_->findFreeSlot(); // first free entry (the init probe)
        sound3dCapacity_ = sound3dPool_->capacity();
        rec("sound3dInitPool", Kind::Real);
    } else {
        rec("sound3dInitPool", Kind::Stub);
    }
}
void RealSubsystems::soundWaveInitSineTables() {
    // REAL: VIBE_SoundWave_InitSineTables (d3sndw_Init) @0x424d40 — build the three
    // N-float waveform lookup tables (table0 = sin(k*2pi/N)). This is the portable
    // d3sndw waveform-layer init the original ran during sound bring-up; it has no
    // OS dependency, so run the real reconstructed builder. N=256 matches the
    // engine's default table length (word_62D430); the resulting table feeds the
    // tone/wave synthesis the sound layer uses.
    audio::SineTables tables = audio::InitSineTables(256);
    sineTableCount_ = tables.valid() ? static_cast<int>(tables.count) : 0;
    rec("soundWaveInitSineTables", Kind::Real);
}
void RealSubsystems::soundLoadSampleBank(const std::string&) {
    if (audio_dev_) {
        // REAL: VIBE_Sound_LoadSampleBank @0x446b2c parsed an .sbf sample bank off
        // disk and indexed its named records. The on-disk .sbf parse is deferred
        // (needs a real sample asset), but the in-memory bank index IS reconstructed
        // (audio::SampleBank). Seed the market-ambience sample the per-frame market
        // SFX trigger (StartMarketLoop) resolves, through the real bank-add path, so
        // the name lookup (VIBE_SampleBank_FindSampleByName) resolves against real
        // code.
        audio::SampleRecord& s = sound_.bank().addSample(kMarketSampleName);
        s.format = 2;                       // .mp3 (Markt ambience)
        s.pcm.assign(64, 0);                // a tiny non-empty PCM payload
        s.sampleRate = 44100;
        rec("soundLoadSampleBank", Kind::Real);
    } else {
        rec("soundLoadSampleBank", Kind::Stub);
    }
}
void RealSubsystems::soundPreloadIncludeFile(const std::string&) {
    if (audio_dev_) {
        // REAL: VIBE_Sound_PreloadFromIncludeFile @0x52f154 read an include_sfx.ini
        // listing the banks to preload, then called VIBE_Sound_LoadSampleBank for
        // each. The on-disk include-file parse is deferred (needs the .ini asset);
        // the reconstructed half wired here is the per-entry bank-load it drives —
        // seed one more named SFX entry through the real SampleBank index so the
        // preload's load-each-listed-bank path runs against real code.
        audio::SampleRecord& s = sound_.bank().addSample("_KLICK");
        s.format = 1;                       // .wav (UI click)
        s.pcm.assign(32, 0);
        rec("soundPreloadIncludeFile", Kind::Real);
    } else {
        rec("soundPreloadIncludeFile", Kind::Stub);
    }
}

void RealSubsystems::soundInitMusicThread(int) {
    if (audio_dev_) {
        // REAL: the original VIBE_Sound_InitThread brought up the streaming-music
        // worker that the world-coupled track director feeds. The OS thread is not
        // reconstructed, but the director's track table IS (audio::music_world).
        // Seed the OUTDOOR track entry (id 9876) so the real selection state
        // machine has a table to act on, mirroring the cold music-table init.
        musicDir_.table.clear();
        audio::TrackEntry outdoor;
        outdoor.ids = {audio::kOutdoorTrackId};
        musicDir_.table.push_back(outdoor);
        // Streaming music is now enabled (dword_63C8F8); the AudioTick music block
        // is gated on this + the day-cycle mask bit.
        audioEnable_.musicOn = true;
        rec("soundInitMusicThread", Kind::Real);
    } else {
        rec("soundInitMusicThread", Kind::Stub);
    }
}

void RealSubsystems::audioApplyVolumeSettings() {
    if (audio_dev_) {
        // REAL: push the configured master volume through the music player
        // (VIBE_Audio_ApplyMasterVolume). master_vol is read from the INI above.
        sound_.music().applyMasterVolume(snd_.masterVol);
        rec("audioApplyVolumeSettings", Kind::Real);
    } else {
        rec("audioApplyVolumeSettings", Kind::Stub);
    }
}

void RealSubsystems::scriptRegisterCommands() {
    // REAL: the original VIBE_App_InitEngineAndScriptCommands registered the .esc
    // command table (VIBE_Script_ImportCommand x N), then the VM invokes them by
    // name with the by-pointer arg ABI. The on-disk command table import is
    // deferred (cold-init table); the reconstructed VM register/invoke ABI
    // (sim::ScriptVm::InvokeCommand) is exercised here against a host sink so the
    // command-dispatch path runs against real code.
    sim::ScriptHost host;
    host.invokeCommand = [](const std::string&, std::vector<i32>& args) -> i32 {
        // A trivial registered-command body: sum the by-pointer args.
        i32 sum = 0;
        for (auto v : args) sum += v;
        return sum;
    };
    sim::ScriptVm vm(scriptProgram_, host);
    scriptCmdResult_ = vm.InvokeCommand("init", {1, 2, 3});

    // REAL (rule 13): VIBE_App_InitEngineAndScriptCommands @0x528560 registers the
    // per-character .esc command table via VIBE_Character_RegisterScriptCommands
    // @0x43dfb0 (the EngineInitHooks.characterRegisterScriptCommands seam). Bind the
    // reconstructed registrar so the 38 character commands populate the shared
    // guild::sim::Commands() table (ImportCommand replaces by name -> idempotent).
    scriptCharCmdResult_ = script::RegisterScriptCommands();

    rec("scriptRegisterCommands", Kind::Real);
}

// ===========================================================================
// intro movie
// ===========================================================================
// VIBE_Movie_PlayIntroSequence @0x5347d4 — play "<movie>\Intro.mpg" through the
// pl_mpeg decoder via the reconstructed video backing. The decode pump delivers
// each RGB frame to a sink (here a counter; the real host blits it through the
// IGraphicsDevice). No decoder (portable build) -> silent skip (the stub path).
void RealSubsystems::moviePlayIntroSequence() {
    if (!videoDecoder_) { rec("moviePlayIntroSequence", Kind::Stub); return; }

    play::VideoMovieBacking backing;
    backing.decoder  = videoDecoder_.get();
    // The extracted assets keep movies under "<gameDir>/movie/"; probe the two
    // common casings (the in-engine VFS path is "\\project\\movie\\").
    backing.movieDir = gameDir_.empty() ? std::string("movie/")
                                        : (gameDir_ + "/movie/");
    backing.clipName = "Intro.mpg";
    movieFramesDecoded_ = 0;
    backing.frameSink = [this](const shim::VideoFrame&) { ++movieFramesDecoded_; };
    if (movieFrameCap_ > 0) {
        const int cap = movieFrameCap_;
        backing.shouldAbort = [this, cap]() { return movieFramesDecoded_ >= cap; };
    }

    play::MovieDllHooks dll = play::MakeVideoMovieHooks(backing);
    const int handle = dll.prepare ? dll.prepare(nullptr, 0) : 0;  // open Intro.mpg
    if (handle == 0) {
        // Casing fallback: lower-case name (case-sensitive filesystems).
        backing.clipName = "intro.mpg";
        play::MovieDllHooks dll2 = play::MakeVideoMovieHooks(backing);
        const int h2 = dll2.prepare ? dll2.prepare(nullptr, 0) : 0;
        if (h2) dll2.play(h2);
        if (dll2.dispose) dll2.dispose();
    } else {
        dll.play(handle);            // decode-and-present pump
        if (dll.dispose) dll.dispose();
    }
    rec("moviePlayIntroSequence", movieFramesDecoded_ > 0 ? Kind::Real : Kind::Stub);
}

void RealSubsystems::movieDllExit() {
    if (videoDecoder_) videoDecoder_->close();
    videoDecoder_.reset();
    rec("movieDllExit", Kind::Real);
}

// ===========================================================================
// per-frame
// ===========================================================================
void RealSubsystems::inputLatchAndPump() {
    if (plat_) {
        // REAL: PumpMessages + latch mouse (shim equivalents of the Win32 pump +
        // DirectInput latch the original ran at the top of the frame). A false
        // pump is WM_QUIT / window-closed: flag it and do NOT count or latch the
        // quit frame (the spine breaks the loop before rendering it, matching the
        // original's message-loop quit check at the top of the frame).
        if (!plat_->pumpMessages()) {
            quitRequested_ = true;
            rec("inputLatchAndPump", Kind::Real);
            return;
        }
        shim::MouseState m;
        plat_->getMouse(m);
        rec("inputLatchAndPump", Kind::Real);
    } else {
        rec("inputLatchAndPump", Kind::Stub);
    }
    ++frameCount_;
}

void RealSubsystems::widgetDispatchMouseClick() {
    // REAL: VIBE_Widget_DispatchMouseClick. The live hover/render-mouse state is
    // not wired headless, so drive the reconstructed hit-test + click-routing core
    // with the current GUI input globals: resolve the hovered widget's child slot,
    // then route a click for it. Both reach real gui::input code.
    int slot = gui::ResolveClickedSlot(gui::g_lastClickedWindow, gui::g_hoverObject);
    (void)slot;
    lastClickedId_ = gui::RouteClick(gui::g_lastClickedWindow, gui::g_hoverObject);
    rec("widgetDispatchMouseClick", Kind::Real);
}

void RealSubsystems::hudHandleMouseClick() {
    // REAL: VIBE_Hud_HandleMouseClick — resolve the hovered widget to its HUD
    // action slot and classify/dispatch the click. Headless there is no live hover
    // so the slot scan returns "none"; the real hit-test/classify path still runs.
    gui::Hud_DispatchClick(gui::kClickFlagStatusBanner, gui::g_hoverObject);
    rec("hudHandleMouseClick", Kind::Real);
}

void RealSubsystems::inputCommandPoll() {
    // REAL: the original input/cmd-request poll block also serviced the active
    // cutscene table each frame (VIBE_Cutscene_ProcessActive). The full per-type
    // step dispatch is deferred, but the cutscene SLOT substrate is reconstructed
    // (sim::CutsceneTable). Drive the real find-lowest-priority scan over the slot
    // table (the head of ProcessActive): on the first poll, allocate a slot so the
    // scan has a live entry; each poll picks the lowest-priority active slot.
    if (cutscenesProcessed_ == 0) {
        sim::CutsceneSlot tmpl{};
        tmpl.type = 1;
        tmpl.partCount = 1;                 // alive gate
        tmpl.stateFlags = sim::kCsFlagActive; // bit0 -> eligible for the priority pick
        cutscenes_.AllocSlot(tmpl, /*newId=*/1);
    }
    sim::CutsceneSlot* active = cutscenes_.FindLowestPriority();
    if (active) ++cutscenesProcessed_;
    rec("inputCommandPoll", Kind::Real);
}

void RealSubsystems::commandNetworkPump() {
    if (net_) {
        // REAL: drain/refill the transport (VIBE_Net_SendPacket / ReceivePacket).
        transport_.SendPacket();
        transport_.ReceivePacket();
        rec("commandNetworkPump", Kind::Real);
    } else {
        rec("commandNetworkPump", Kind::Stub);
    }
}

void RealSubsystems::scriptStepAllActive() {
    // REAL: VIBE_Script_StepAllActive — scan the 128-context table, step runnable
    // ones. The per-context step body is a no-op hook here (the lexer/scene-swap
    // edge is not wired in a headless run).
    sim::StepAllActive(scriptSlots_, [](sim::ScriptSlot&) { return 0; });
    rec("scriptStepAllActive", Kind::Real);
}

void RealSubsystems::gameObjectDispatchInteractions() {
    // REAL: VIBE_GameObject_DispatchInteractions — drain the current tile bucket's
    // pending-interaction queue and forward each event to the result handler. With
    // no live world the bucket is empty (count 0); the real drain loop still runs.
    static int s_dispatched = 0;
    sim::InteractionEvent queue[1] = {};
    sim::InteractionDispatch(
        queue, /*count=*/0,
        [](i32, i32, i32, i32, i32, i32) { ++s_dispatched; },
        /*ctxA=*/0, /*ctxB=*/0);

    // REAL (command-APPLY turn step): the original applies the turn's received
    // command packets here (VIBE_Command_ExecCommands dispatches each opcode to its
    // apply handler). With the four installers now wired by commandQueueInitAndSync,
    // an applied packet reaches the REAL reconstructed sim leaves. Drive ONE command
    // on the first turn (standalone => FlushSendQueue applies it locally) so a real
    // sim leaf is exercised across the frame loop.
    //
    // We use the object-resolve opcode 0x4A (ExDeselectObject), which routes through
    // the wired SetObjectFindHook -> the real sim::GameObjectResolveEntityById probe
    // (the object/scene/person record resolver over the SHARED g_sceneNodes/g_objects/
    // g_persons arrays). This is a pure ENTITY QUERY: it MUTATES NO state and does not
    // grow any live count, so it composes with every other suite's entity-array
    // expectations. Headless there is no live entity at the queried id, so the real
    // resolver reports "not found" (the apply rejects with status 1) — exactly the
    // cold-world outcome, but the real reconstructed resolver ran. cmdApplyReachedReal_
    // records that the wired apply path actually dispatched through the real handler.
    if (simHooksInstalled_ && !cmdTurnDriven_) {
        cmdTurnDriven_ = true;
        sim::CommandPacket query{};
        query.opcode() = sim::kOp3DeselectObject;  // 0x4A
        query.put32(0x10, 0x7FFFFFFEu);            // an id with no live record
        cmdQueue_.EnqueuePacket(query);
        cmdQueue_.FlushSendQueue(); // standalone: stage -> received
        // Apply the command; the dispatcher routes 0x4A -> ExDeselectObject ->
        // the wired real GameObjectResolveEntityById. We snapshot the apply outcome.
        cmdApplyResult_ = sim::ApplyPacket3(query, /*ack=*/nullptr);
        cmdQueue_.ExecCommands();   // also drains the queued packet through dispatch
        cmdApplyReachedReal_ = true; // the wired apply path ran (real resolver probed)
    }

    // The 0x80 block also advances the screen fade (fade/decompress step). Drive
    // the reconstructed fade alpha core off the per-frame fade tick.
    ++fadeTick_;
    float a = render::FadeAlpha(render::kFadeIn, fadeTick_, /*startTick=*/0,
                                /*lastTick=*/fadeTick_, /*duration=*/30);
    fadeDone_ = render::FadeIsDone(render::kFadeIn, a);

    rec("gameObjectDispatchInteractions", Kind::Real);
}

void RealSubsystems::renderMainViewFrame() {
    // REAL: VIBE_Render_RenderMainViewFrame. With the real frame pipeline installed
    // the walk runs its gate + clear + the scene-graph project/cull/append + sort
    // (frameHooks_.sceneWalk -> RealFrameScene::sceneWalk, building the draw list
    // from the live scene mesh through render::ProjectVerticesToScreen +
    // RadixSortDrawList). hasWorld is enabled so BeginUniverseFrame actually walks.
    frame_.engineOn = true;
    frame_.hasWorld = frameSceneInstalled_;  // a live scene/world this frame
    // REAL-BRIDGE: keep hasTerrain ON (set by EnableRealRenderBridges) so the live
    // walk reaches the REAL floor leaf; else the legacy "no terrain" frame.
    frame_.hasTerrain = realRenderBridges_;
    if (realRenderBridges_ && g_frameScene && g_frameScene->fb) {
        // The engine cleared the main-view surface inside the frame bracket; clear
        // to the sky colour so the composited terrain/mesh is the visible delta.
        render::SurfaceColorFill(g_frameScene->fb, g_frameScene->clearR,
                                 g_frameScene->clearG, g_frameScene->clearB);
    }
    render::RenderMainViewFrame(frame_, frameHooks_);

    // VIBE_Render_RasterizeMeshList — flush the sorted draw list into the software
    // framebuffer (the original ran the flush from the present/lock bracket). This
    // completes build -> project/cull -> sort -> RASTERIZE for the frame.
    if (frameSceneInstalled_ && g_frameScene) {
        g_frameScene->flushDrawList();
        lastDrawListPolys_ = g_frameScene->lastAppended;
        lastRasterTris_    = g_frameScene->lastDrawn;
        if (realRenderBridges_) {
            // REAL HUD sprite-bank blit on top of the rendered world.
            g_frameScene->drawRealHud();
            lastTerrainTris_  = g_frameScene->lastTerrainTris;
            lastTerrainPix_   = g_frameScene->lastTerrainPix;
            lastSceneDispatch_= g_frameScene->lastSceneDisp;
            lastMeshTris_     = g_frameScene->lastMeshTris;
            lastHudSprites_   = g_frameScene->lastHudSprites;
        }
    }
    rec("renderMainViewFrame", Kind::Real);
}

void RealSubsystems::weatherUpdateSky() {
    // REAL: VIBE_Weather_UpdateSky — drive the reconstructed intensity + cloud
    // scroll core off the 24-hour weather arc at the current clock hour. The
    // snow/rain particle grow-lists and the cloud-texture swaps need the live
    // sky-layer subsystem (deferred), but the intensity/category/scroll math is
    // fully reconstructed (render::Weather*).
    int h = clock_.hour % 24;
    weatherIntensity_ = render::WeatherIntensity(weatherArc_, h);
    render::WeatherCategory cat = render::CategoryFor(weatherIntensity_);
    // Exercise the scroll + grow math (results threaded to the sky-layer subsystem
    // in the original; here just computed against real code).
    (void)render::CloudScrollMagnitude(/*windX=*/-1.0f, /*windY=*/0.5f,
                                       weatherIntensity_);
    (void)render::SnowGrowAmount(-1.0f, weatherIntensity_);
    (void)render::SelectCloudLayerIndex(cat, /*current=*/-1);
    rec("weatherUpdateSky", Kind::Real);
}

void RealSubsystems::dayCycleAndOutdoorMusic() {
    // REAL: VIBE_DayCycle_UpdateBrightness — map the wall clock to a 0..600
    // brightness step. Keyframes are built from season 0 at construction-time
    // demand here; advance the clock a little each frame so brightness evolves.
    sim::GameTimeAdvance(&clock_, 0, 0, 1); // +1 minute/frame
    brightness_ = render::UpdateBrightness(dayKeyframes_, clock_.hour, clock_.minute);

    // REAL: the AUDIO half of the world-render block — the per-frame audio tick of
    // VIBE_GameLogic_RunFrameLoop (@0x4c09a0). app::AudioTick drives the real audio
    // cores in the original's order, gated by the audio-enable flags
    // (dword_63C900/63C904/63C8F8 -> audioEnable_): the 3D positional pool update
    // (VIBE_Sound3d_UpdateAll), the speech queue (VIBE_VoiceQueue_ProcessNext), the
    // outdoor-music state machine (VIBE_Music_UpdateOutdoorTrackPlayback), and the
    // mixer voice recycle (VIBE_Sound_UpdateVoices). The listener pose is the
    // camera (here a fixed forward-looking listener).
    if (!musicDir_.table.empty()) {
        int season = audio::SeasonFromDay(clock_);
        // (Re)select the season track once (VIBE_Music_SelectOutdoorSeasonTrack).
        if (musicDir_.currentTrackHandle == 0)
            audio::SelectOutdoorSeasonTrack(musicDir_, season);

        // Fire the market-ambience SFX trigger once (VIBE_Ambient_StartMarketLoop):
        // a looping positional 3D sound the per-frame tick then updates. Resolves
        // the sample seeded by soundLoadSampleBank.
        if (!marketLoopStarted_ && audioEnable_.sound3dOn) {
            AudioListener listener0{};
            listener0.forward = audio::Vec3{0.0f, 0.0f, 1.0f};
            audio::Vec3 marketPos{10.0f, 0.0f, 10.0f};
            if (StartMarketLoop(sound_, marketLoop_, marketPos, listener0,
                                kMarketSampleName))
                marketLoopStarted_ = true;
        }

        // Drive the full audio tick (the kDayCycleMusic bit is set so the music
        // block runs). atStreamEnd=false / locationId=0 -> outdoor.
        AudioListener listener{};
        listener.forward = audio::Vec3{0.0f, 0.0f, 1.0f};
        lastAudioTick_ = AudioTick(sound_, musicDir_, audioEnable_,
                                   mask::kDayCycleMusic, /*tickCounter=*/frameCount_,
                                   listener, season, /*atStreamEnd=*/false,
                                   /*locationId=*/0);
        ++audioTicks_;
    }
    rec("dayCycleAndOutdoorMusic", Kind::Real);
}

void RealSubsystems::hudSelectionAndTargets() {
    // REAL: VIBE_Hud_UpdateSelectionAndTargets / DrawSelectedUnitInfo — the
    // selection/target overlay registers status-text and floating damage labels
    // for the selected units. The live unit state is not wired headless, but the
    // status-text + damage-label TABLES are reconstructed (gui::StatusText_Register
    // / DamageLabel_Register). Register one representative status + damage entry so
    // the real de-dup/LRU table code runs (the selected-unit info the original
    // would have produced).
    gui::StatusText_Register(/*key=*/1, /*tag=*/100);
    gui::DamageLabel_Register(/*source=*/1, /*amount=*/5, /*now=*/frameCount_);
    rec("hudSelectionAndTargets", Kind::Real);
}

void RealSubsystems::tooltipDispatch() {
    // REAL: VIBE_Tooltip_DispatchByType — classify the hovered subject's scene
    // reference and select its builder. Headless there is no live hover so the
    // scene-reference is null; the real classification core (id-fallback range
    // tests, sceneRef==0 path) still runs and reports "no tooltip".
    gui::TooltipTables tables{}; // no live scene tables -> the null-base fallback
    gui::TooltipSubject subj =
        gui::Tooltip_ClassifySubject(tables, /*sceneRef=*/nullptr, /*tooltipId=*/0);
    lastTooltipKind_ = static_cast<int>(gui::Tooltip_SelectBuilder(tables, subj));
    rec("tooltipDispatch", Kind::Real);
}
void RealSubsystems::hudLabelsAndCaption() {
    // REAL: the HUD on-screen labels / name-input caption block. The original
    // computes each label's x-placement via the alignment layout math
    // (VIBE_Hud_AddCenteredLabel/...). The glyph blit needs a live surface
    // (deferred), but the LAYOUT math is reconstructed (gui::Hud_LabelLayout /
    // Hud_ButtonRowLayout). Compute a centered caption layout + a 3-button row so
    // the real layout code runs and yields the label x the HUD would place.
    gui::HudLabelLayout lay =
        gui::Hud_LabelLayout(gui::HudLabelAlign::kCentered, /*anchorX=*/320, /*width=*/80);
    hudLabelX_ = lay.x;
    int widths[3] = {40, 60, 50};
    int outX[3] = {0, 0, 0};
    gui::Hud_ButtonRowLayout(widths, 3, /*windowWidth=*/640, outX);
    rec("hudLabelsAndCaption", Kind::Real);
}
void RealSubsystems::cameraCombatScroll() {
    // REAL: VIBE_Camera_UpdateCombatScroll @0x487b2c — the combat-mode edge-scroll
    // DECISION core (ResolveCombatScroll). The scroll-arrow blit + the camera pan
    // are render leaves (Animation_Basic / Coord) that need a live camera+surface
    // (deferred); the portable part — turning the two scroll-direction inputs +
    // the four edge motion vectors into per-edge scroll codes — is reconstructed
    // and run here. Headless there is no live unit motion, so the edge vectors are
    // the settled {0,0,0} block: with a "scroll right" (scrollX=-1) input the real
    // core reports the right edge SETTLED (code 1), exercising the real decision +
    // util::VectorWithinTolerance path the original runs.
    CombatScrollDecision d = ResolveCombatScroll(
        /*scrollX=*/-1, /*scrollY=*/1,
        /*topVec=*/combatEdgeVecs_[0], /*bottomVec=*/combatEdgeVecs_[1],
        /*leftVec=*/combatEdgeVecs_[2], /*rightVec=*/combatEdgeVecs_[3]);
    combatScrollRight_  = static_cast<int>(d.right);
    combatScrollBottom_ = static_cast<int>(d.bottom);
    rec("cameraCombatScroll", Kind::Real);
}

void RealSubsystems::presentFrame() {
    if (gfx_dev_) {
        // REAL: VIBE_Render_PresentFrame — present the software framebuffer.
        // Mode 0 (GDI BitBlt) needs no lock, so a null framebuffer is fine.
        render::PresentFrame(*gfx_dev_, render::PresentMode::GdiBitBlt, present_);
        ++presentCount_;
        rec("presentFrame", Kind::Real);
    } else {
        rec("presentFrame", Kind::Stub);
    }
}

void RealSubsystems::optionsChatHotkeyPanels() {
    // REAL: the options/chat/hotkey panel block. The options + hotkey panels need
    // live UI surface state (deferred), but the CHAT console IS reconstructed
    // (gui::ChatConsole). Drive its real per-frame substrate: when a recipient
    // channel toggle is pending it sets the channel state, then any queued input
    // line is submitted through the real colour-prefix assembler (the same path
    // the chat hotkey would run). Headless there is no live input, so this seeds
    // and submits one representative line so the real ChatConsole code runs.
    if (chatLines_ == 0) {
        chat_.SetChannel(0, 1);
        chat_.SetInput("hello");
        std::string line = chat_.Submit(/*colourIndex=*/0);
        (void)line;
    }
    chatLines_ = chat_.LineCount();
    rec("optionsChatHotkeyPanels", Kind::Real);
}
// In-process datagram bus + lobby hook for the net-wait wiring (file-scope so the
// header stays free of the discovery datagram type). The bus is a tiny loopback so
// the host-advertise -> client-discover round-trip runs through the REAL discovery
// siblings with no OS socket — the same shape the multiplayer browser uses.
namespace {
struct LobbyDgBus { std::vector<std::pair<u16, net::Datagram>> queues; };

class LobbyDatagram : public net::INetDatagram {
public:
    LobbyDatagram(LobbyDgBus* bus, std::string ip) : bus_(bus), ip_(std::move(ip)) {}
    bool open(u16 bindPort, bool) override { bindPort_ = bindPort; return true; }
    void close() override {}
    int sendTo(u16 dstPort, const void* data, std::size_t n) override {
        net::Datagram dg; dg.fromIp = ip_;
        const u8* p = static_cast<const u8*>(data);
        dg.data.assign(p, p + n);
        bus_->queues.emplace_back(dstPort, std::move(dg));
        return static_cast<int>(n);
    }
    bool recvFrom(net::Datagram& out) override {
        for (auto it = bus_->queues.begin(); it != bus_->queues.end(); ++it) {
            if (it->first == bindPort_) {
                out = std::move(it->second);
                bus_->queues.erase(it);
                return true;
            }
        }
        return false;
    }
private:
    LobbyDgBus* bus_;
    std::string ip_;
    u16 bindPort_ = 0;
};

// Drives the owned CommandQueue (flush+exec applies the ready cmd locally in
// standalone) and walks byte_63CC28: pump 1 = host designated, pump 2 = all ready.
class WiringLobbyHook : public net::ILobbyHook {
public:
    explicit WiringLobbyHook(sim::CommandQueue* q) : q_(q) {}
    bool pumpAndRefresh(u8& readyMask) override {
        ++pumps_;
        if (pumps_ == 1) {
            readyMask |= net::kReadyHost;
        } else {
            q_->FlushSendQueue();   // standalone => apply the cmd6 locally
            q_->ExecCommands();     // ack it
            readyMask |= net::kReadyAllPeers;
        }
        return true;
    }
private:
    sim::CommandQueue* q_;
    int pumps_ = 0;
};

// A monotonic step clock for the discovery timeout (no wall clock).
struct LobbyClock { u32 t = 0; u32 step = 0; };
u32 LobbyNow(void* user) {
    auto* c = static_cast<LobbyClock*>(user);
    u32 v = c->t; c->t += c->step; return v;
}
} // namespace

void RealSubsystems::autosaveAndNetWait() {
    // REAL: the original ran the autosave + NET-WAIT block here. The autosave half
    // needs live world state (deferred); the NET-WAIT half is the multiplayer-lobby
    // ready/sync handshake, which is fully reconstructed. Drive it ONCE (the
    // original fires the wait on entering a networked round) over the owned, real
    // CommandQueue + the real discovery siblings.
    if (net_ && !lobbyDriven_) {
        lobbyDriven_ = true;
        // The lobby advert this host would broadcast (kind 1, host new game). The
        // city name + player count come from the loaded config/world (here the
        // parsed [Game]/Stadt and a 2-player default).
        const std::string city = !cityName_.empty() ? cityName_
                                 : (!game_.stadt.empty() ? game_.stadt : "AUGSBURG");
        net::LobbyAdvert advert =
            net::BuildHostAdvert(city, /*players=*/2, /*scenario=*/0, nullptr);

        // 1. Host-advertise -> client-discover round-trip through the REAL discovery
        //    siblings (BroadcastAdvertiser + DiscoverServers) over a loopback bus.
        LobbyDgBus bus;
        LobbyDatagram hostDg(&bus, "127.0.0.1");
        LobbyDatagram clientDg(&bus, "0.0.0.0");
        LobbyClock clk{0, 1};
        std::vector<net::DiscoveredServer> found;
        lobbyDiscovered_ = net::AdvertiseAndDiscover(
            &hostDg, &clientDg, advert, net::kDiscoveryPort, net::kDiscoveryWindow,
            found, &LobbyNow, &clk, /*tickMs=*/1);

        // 2. The ready/ship/barrier handshake over the owned CommandQueue. The queue
        //    is standalone (single-player/host) so the cmd6 ready packet applies
        //    locally and the all-ready barrier clears — the single-machine lobby.
        cmdQueue_.set_standalone(true);
        cmdQueue_.set_disconnected(false);
        WiringLobbyHook hook(&cmdQueue_);
        // A small representative savegame the host ships (the real world-save bytes
        // are produced by the save spine; here a non-empty stand-in payload).
        std::vector<u8> save(64, 0);
        lobbyResult_ = net::RunNetworkLobby(advert, cmdQueue_, hook, save,
                                            /*maxPumps=*/64);
        rec("autosaveAndNetWait", Kind::Real);
        return;
    }
    rec("autosaveAndNetWait", Kind::Stub);
}
void RealSubsystems::characterCollectByOwner() {
    // REAL: the original ran this block (unless the kCombatSelect suppressor was
    // set) to drive the live scene actors and collect units by owner. The
    // ownership collect needs live world state (deferred), but the live-actor
    // per-frame driver IS reconstructed (sim::CharacterUpdate). Run it: it walks
    // the 512-slot dword_66F0D0 live array and steps each active actor's action
    // coroutine. With no live actors the count gate is 0 and it returns 0; the
    // real driver loop still runs.
    sim::CharacterUpdate();
    ++characterUpdates_;
    rec("characterCollectByOwner", Kind::Real);
}
void RealSubsystems::quickJumpContact() {
    // REAL: the quick-jump-to-contact block resolved a contact entity id to its
    // record before snapping the camera to it. The camera snap needs live camera
    // state (deferred), but the entity RESOLVE is reconstructed
    // (sim::GameObjectResolveEntityById over the shared g_sceneNodes/g_persons/
    // g_objects arrays). Run the real resolver for the current contact id; with no
    // live world it resolves to nothing (the headless "no contact" outcome), but
    // the real object/scene/person probe still runs.
    sim::ObjectRec* obj = nullptr;
    sim::SceneNode* scene = nullptr;
    sim::Person* person = nullptr;
    quickJumpResolved_ =
        sim::GameObjectResolveEntityById(&obj, &scene, quickJumpContactId_, &person);
    rec("quickJumpContact", Kind::Real);
}

// ===========================================================================
// shutdown teardown (13 steps)
// ===========================================================================
void RealSubsystems::tdGameShutdownSubsystems() {
    if (audio_dev_) {
        // REAL: VIBE_Game_ShutdownSubsystems tore the engine subsystems down; the
        // reconstructed, wireable part is the sound system teardown
        // (VIBE_Sound_Shutdown: stop 3D voices, flush the queue, free the pool).
        // First stop the market-ambience loop (VIBE_Ambient_StopMarketLoop) so its
        // 3D entry is detached before the pool is torn down.
        StopMarketLoop(sound_, marketLoop_);
        sound_.shutdown();
        soundInited_ = false;
        rec("tdGameShutdownSubsystems", Kind::Real);
    } else {
        rec("tdGameShutdownSubsystems", Kind::Stub);
    }
}

void RealSubsystems::tdWidgetShutdownSystem() {
    // REAL: tear down the GUI table/input/HUD state.
    gui::ResetWindows();
    gui::ResetInputState();
    gui::ResetHudSlots();
    rec("tdWidgetShutdownSystem", Kind::Real);
}

void RealSubsystems::tdConfigWriteGfxSettings() {
    // REAL: VIBE_Config_WriteGfxSettings — serialize the effective [Gfx]/[Sound]/
    // [Game] settings back out. The OS WritePrivateProfileStringA is the genuine
    // leaf (no portable INI writer in the config layer); the reconstructed half —
    // the exact key order + int/float value formatting — runs against a record
    // sink that stands in for that leaf. The settings written are the ones the
    // spine read at startup (configReadGfxAndSound), so the serializer round-trips
    // the live config exactly as the original's shutdown step 3 would persist it.
    settingsKeysWritten_ = 0;
    ConfigWriteGfxSettings(
        gfx_, snd_, game_,
        [this](const std::string& /*section*/, const std::string& key,
               const std::string& value) {
            ++settingsKeysWritten_;
            if (key == "stadt")
                writtenStadt_ = value;
        });
    rec("tdConfigWriteGfxSettings", Kind::Real);
}

void RealSubsystems::tdGameStateFreeAllResources() {
    // REAL: VIBE_GameState_FreeAllResources — release the loaded session's entity
    // records. The reconstructed, platform-neutral part is the entity-array reset
    // (sim::ResetEntityArrays), mirroring the original's array-base teardown.
    sim::ResetEntityArrays();
    rec("tdGameStateFreeAllResources", Kind::Real);
}

void RealSubsystems::tdUniverseSwitchActiveSlot0() {
    // REAL: VIBE_Universe_SwitchActiveSlot(0, 1) @0x5b4a24 — the shutdown step that
    // makes scene slot 0 the active universe (quiet=1, so only the slot swap runs,
    // not the present-frame DDraw tail). The render/scene leaves (heightmap rebuild,
    // fog config, the present flip) are routed through the inert UniverseRenderHooks;
    // the byte-exact per-slot save/restore + the active-slot id (dword_649D60 ->
    // g_activeUniverseId) is fully reconstructed, so run the real swap. Returns 1
    // (true) — the result the original's shutdown step relied on.
    universeSwitchedToSlot0_ = sim::UniverseSwitchActiveSlot(/*slot=*/0, /*quiet=*/true);
    rec("tdUniverseSwitchActiveSlot0", Kind::Real);
}
void RealSubsystems::tdTableResetLightmaps() { rec("tdTableResetLightmaps", Kind::Stub); }

void RealSubsystems::tdRenderShutdownEngine() {
    if (gfx_dev_) {
        gfx_dev_->shutdown(); // shim equivalent of VIBE_Render_ShutdownEngine
        rec("tdRenderShutdownEngine", Kind::Real);
    } else {
        rec("tdRenderShutdownEngine", Kind::Stub);
    }
}

void RealSubsystems::tdInputDirectInputShutdown() { rec("tdInputDirectInputShutdown", Kind::Stub); }

void RealSubsystems::tdTimeBaseStopTimer() {
    if (timerRunning_) {
        timer_.StopTimer(); // REAL: VIBE_TimeBase_StopTimer.
        timerRunning_ = false;
        rec("tdTimeBaseStopTimer", Kind::Real);
    } else {
        rec("tdTimeBaseStopTimer", Kind::Stub);
    }
}

void RealSubsystems::tdVfsShutdown() {
    if (vfsInited_) {
        io::VfsShutdown(); // REAL: VIBE_Vfs_Shutdown — clears the bound filesystem.
        vfsInited_ = false;
        rec("tdVfsShutdown", Kind::Real);
    } else {
        rec("tdVfsShutdown", Kind::Stub);
    }
}

void RealSubsystems::tdMemPoolShutdownStack() {
    // REAL: free every chunk of the startup-stack pool.
    mem::MemPoolFreeAll(&pool_, tracker_);
    rec("tdMemPoolShutdownStack", Kind::Real);
}

void RealSubsystems::tdMemoryShutdownTracker() {
    if (trackerInited_) {
        tracker_.Shutdown(); // REAL: VIBE_Memory_ShutdownTracker.
        trackerInited_ = false;
        rec("tdMemoryShutdownTracker", Kind::Real);
    } else {
        rec("tdMemoryShutdownTracker", Kind::Stub);
    }
}

void RealSubsystems::tdErrorLogShutdown() {
    // REAL: final ErrorLog formatter/sink path (mirrors the shutdown log line).
    config::FormatMessage(g_logSink, config::kLogDebug, "main_Shutdown()");
    rec("tdErrorLogShutdown", Kind::Real);
}

// ===========================================================================
// RunHeadless
// ===========================================================================
HeadlessResult RunHeadless(int displayMode, bool showIntro, bool networkClient,
                           int frames) {
    shim::NullPlatform plat;
    shim::MemoryGraphicsDevice gfx;
    shim::NullAudioDevice audioDev;
    shim::MemFileSystem fs;

    // A connected loopback pair: the transport talks to itself (single-player).
    auto pair = shim::LoopbackSocket::makePair();
    shim::INetSocket* sock = pair.first.get();

    config::IniFile ini; // empty -> all defaults

    RealSubsystems sub(&plat, &gfx, &audioDev, &fs, sock, &ini);
    GameApp app(plat, gfx, audioDev, sub);

    HeadlessResult r;
    r.exitCode = app.Run("\\project\\", displayMode, showIntro, networkClient, frames);
    r.presentCount = sub.presentCount();
    r.frameCount = sub.frameCount();
    r.lastFeatureMask = app.lastFeatureMask();
    r.memoryTrackerInited = sub.memoryTrackerInited();
    r.vfsInited = sub.vfsInited();
    r.soundInited = sub.soundInited();
    r.settingsKeysWritten = sub.settingsKeysWritten();
    r.writtenStadt = sub.writtenStadt();
    r.sineTableCount = sub.sineTableCount();
    return r;
}

// ===========================================================================
// RunHeadlessRealAssets — the playable real-asset boot path.
// ===========================================================================
RealHeadlessResult RunHeadlessRealAssets(const std::string& gameDir, int frames,
                                         int displayMode) {
    RealHeadlessResult out;
    if (gameDir.empty())
        return out; // assetsPresent stays false

    // 1. Disk filesystem rooted at the real install + key-asset presence probe.
    shim::DiskFileSystem fs(gameDir);
    const char* kProbe[] = {
        "Gilde.INI", "gfx/gilde.gfx", "Resources/forms.BIN",
        "Resources/gamedata/Cities/AUGSBURG.cty",
    };
    for (const char* p : kProbe) {
        if (!fs.exists(p))
            return out; // assetsPresent stays false — caller should skip
    }
    out.assetsPresent = true;

    // 2. Mount the real assets: parse Gilde.INI, bind the VFS to the disk fs, and
    //    mount the Resources/*.BIN archives (all through the reconstructed loaders).
    RealGameAssets assets =
        MountRealGameAssets(&fs, gameDir, "Gilde.INI", {}, /*caseInsensitive=*/false);
    out.iniLoaded = assets.iniLoaded;
    out.stadt     = assets.stadt;
    for (const auto& a : assets.archives)
        if (a.mounted) ++out.archivesMounted;
    out.totalMembers = assets.totalMembers();

    // 3. Wire the spine to RealSubsystems fed by the REAL parsed INI + the real
    //    disk fs, and flip on real-asset mode so the file-layer hooks consume the
    //    real loaders (gilde.gfx / A_Geb+A_Obj / <Stadt>.cty).
    shim::NullPlatform plat;
    shim::MemoryGraphicsDevice gfx;
    shim::NullAudioDevice audioDev;
    auto pairSock = shim::LoopbackSocket::makePair();
    shim::INetSocket* sock = pairSock.first.get();

    RealSubsystems sub(&plat, &gfx, &audioDev, &fs, sock, &assets.ini);
    sub.BindRealAssets(&assets, gameDir);

    GameApp app(plat, gfx, audioDev, sub);

    // 4. Run the full lifecycle ENTIRELY through the spine.
    out.base.exitCode =
        app.Run(gameDir, displayMode, /*showIntro=*/false, /*networkClient=*/false,
                frames);
    out.base.presentCount = sub.presentCount();
    out.base.frameCount = sub.frameCount();
    out.base.lastFeatureMask = app.lastFeatureMask();
    out.base.memoryTrackerInited = sub.memoryTrackerInited();
    out.base.vfsInited = sub.vfsInited();
    out.base.soundInited = sub.soundInited();
    out.base.settingsKeysWritten = sub.settingsKeysWritten();
    out.base.writtenStadt = sub.writtenStadt();
    out.base.sineTableCount = sub.sineTableCount();

    out.gfxObjectCount   = sub.gfxObjectCount();
    out.buildingTypeCount = sub.buildingTypeCount();
    out.sceneTypeCount   = sub.sceneTypeCount();
    out.cityLoaded       = sub.cityLoaded();
    out.cityName         = sub.cityName();
    out.worldLoaded      = sub.worldLoaded();
    out.livePersonCount  = sub.livePersonCount();
    out.liveObjectCount  = sub.liveObjectCount();
    return out;
}

} // namespace guild::app
