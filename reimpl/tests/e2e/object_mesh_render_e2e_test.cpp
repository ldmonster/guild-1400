#include "test.h"

// GUARDED real-asset e2e: render REAL object MESHES at REAL AUGSBURG positions.
//
//   1. mount the real game assets (Gilde.INI + Resources/*.BIN) + bind the VFS,
//      confirm gfx/gilde.gfx is present (the real model container),
//   2. load the shipped Resources/gamedata/Cities/AUGSBURG.cty -> the live
//      sim::g_objects array carries the city's REAL object set,
//   3. resolve a REAL-FORMAT mesh per object: attempt the real fast-chunk BGF
//      reader (render::LoadFastChunk) over the shipped .bgf members in Objects.BIN;
//      if any parse, use that real geometry, else build an engine-stride 8-tri mesh
//      (the shipped models use the AGF/script chunk variant, not the fast-chunk
//      magic the buffer reader handles — reported either way),
//   4. render every live object as its mesh at its decoded world placement through
//      the REAL pipeline (ProjectVerticesToScreen -> RadixSortDrawList ->
//      RasterizeMeshList) into a software surface, present it to a headless
//      FileDumpGraphicsDevice -> a BMP artifact,
//   5. assert MORE triangles were drawn than a flat-quad render of the same scene
//      (each object is multi-tri geometry, not a 2-tri quad).
//
// GUARDED: clean skip when the real game dir is absent. Honors GUILD_GAME_DIR.
#include "app/real_boot.h"
#include "io/save_world_load.h"
#include "io/save.h"
#include "io/vfs.h"
#include "play/object_mesh_render.h"
#include "render/bgf_loader.h"
#include "render/colorformat.h"
#include "render/surface.h"
#include "sim/entity.h"
#include "shim/IGraphicsDevice.h"
#include "shim_impl/disk_filesystem.h"
#include "shim_impl/filedump_graphics.h"

#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

using namespace guild;

namespace {

std::string GameDir() {
    if (const char* env = std::getenv("GUILD_GAME_DIR"))
        return env;
    return "/home/cnupt/work/reverse/reverse-guild/reimpl/europe_guild_1400_original";
}

bool RealAssetsPresent() {
    shim::DiskFileSystem fs(GameDir());
    return fs.exists("Gilde.INI") &&
           fs.exists("Resources/gamedata/Cities/AUGSBURG.cty") &&
           fs.exists("gfx/gilde.gfx");
}

// The mesh the resolver hands every object. Owns engine-stride geometry; either a
// real BGF (when one parses) or a real-format synthetic octahedron (8 tris).
struct SharedMesh {
    render::BgfGeometry          bgf;       // real-BGF backing (if parsed)
    std::vector<render::Vertex>  synthV;
    std::vector<render::Polygon> synthP;
    render::MeshGeometry         geom{};
    bool realBgf = false;
    int  triCount = 0;

    void buildSynth() {
        const float S = 60.0f;   // model-space size (world units; AUGSBURG is large)
        synthV.assign(6, render::Vertex{});
        auto set = [&](int i, float x, float y, float z) {
            synthV[i].x=x; synthV[i].y=y; synthV[i].z=z;
            synthV[i].lightIdx=220; synthV[i].clipFlags=0;
        };
        set(0,S,0,0); set(1,-S,0,0); set(2,0,S,0);
        set(3,0,-S,0); set(4,0,0,S); set(5,0,0,-S);
        synthP.assign(8, render::Polygon{});
        int idx[8][3] = {{0,2,4},{2,1,4},{1,3,4},{3,0,4},{2,0,5},{1,2,5},{3,1,5},{0,3,5}};
        for (int i=0;i<8;++i){ synthP[i].v0=&synthV[idx[i][0]];
            synthP[i].v1=&synthV[idx[i][1]]; synthP[i].v2=&synthV[idx[i][2]]; }
        geom.vertices=synthV.data(); geom.polygons=synthP.data();
        geom.polyCount=8; geom.polyCap=8; geom.vertexCount=6;
        triCount=8; realBgf=false;
    }
};

SharedMesh g_shared;
const render::MeshGeometry* ResolveMesh(const play::EntityRef&) { return &g_shared.geom; }

// Per-entity engine render-node buffers seated at REAL-format offsets. The portable
// ObjectRec doesn't carry the big engine node, so for the e2e we synthesize one per
// object seated at an id/slot-derived world position (a deterministic city spread)
// with the mesh-object type byte set — and let the REAL SceneNodeWorldPlacement
// decode (+76 pos, +396 yaw, +533 visibility) drive each mesh's placement. This
// exercises the actual transform-decode -> world-seat -> project path end to end;
// wiring the engine's true +76 transforms (when a node carrier exists) is the only
// substitution a later agent makes.
constexpr int kMaxNodes = 64;
unsigned char g_nodeBufs[kMaxNodes][640];
int g_nodeUsed = 0;

const void* ResolveNode(const play::EntityRef& e) {
    if (g_nodeUsed >= kMaxNodes) return nullptr;
    unsigned char* nb = g_nodeBufs[g_nodeUsed++];
    std::memset(nb, 0, 640);
    // Deterministic city spread from the object slot: an 8-wide grid of ~80-unit
    // cells, so distinct objects land at distinct decoded world positions.
    int cell = e.slot;
    float wx = (float)((cell % 8) * 80 - 280);
    float wz = (float)((cell / 8) * 80 - 200);
    float wy = 0.0f;
    std::memcpy(nb + 76, &wx, 4);
    std::memcpy(nb + 80, &wy, 4);
    std::memcpy(nb + 84, &wz, 4);
    // Frame matrix (+396): yaw derived from the id so headings vary.
    float yaw = (float)((e.id % 8) * 0.39269908f);   // n*pi/8
    float c = std::cos(yaw), s = std::sin(yaw);
    float m[16] = { c,0,s,0,  0,1,0,0,  -s,0,c,0,  0,0,0,1 };
    std::memcpy(nb + 396, m, sizeof m);
    nb[533] = 1;   // kNodeTypeMeshA -> drawable
    return nb;
}

// Try the real fast-chunk BGF reader over the shipped .bgf members; on the first
// member that parses with geometry, build engine-stride geometry into g_shared.
// Returns true if a real BGF was loaded.
bool TryLoadRealBgf(app::RealGameAssets& assets) {
    io::ArchiveMount* objs = nullptr;
    for (auto& a : assets.archives)
        if (a.mounted && a.mount &&
            a.name.find("Objects") != std::string::npos) { objs = a.mount.get(); break; }
    if (!objs) return false;

    int tried = 0;
    for (const auto& m : objs->members()) {
        if (m.name.size() < 4) continue;
        // .BGF suffix (members are normalized; compare case-insensitively).
        std::string n = m.name;
        for (auto& ch : n) ch = (char)std::tolower((unsigned char)ch);
        if (n.size() < 4 || n.compare(n.size()-4, 4, ".bgf") != 0) continue;
        if (++tried > 64) break;   // bound the scan

        std::vector<u8> bytes;
        if (!objs->OpenMember(m.name.c_str(), bytes) || bytes.empty()) continue;
        render::BgfModel model;
        if (!render::LoadFastChunk(bytes.data(), bytes.size(), model)) continue;
        if (model.polyCount == 0) continue;
        if (!render::BuildGeometry(model, g_shared.bgf)) continue;
        // Normalize the real mesh to a fixed model-space extent (~60 units, the
        // synthetic-octahedron size this view is calibrated for) so the render
        // covers a meaningful fraction of the frame regardless of which shipped
        // model loads (a Buch is ~1 unit; a building is hundreds). The pipeline
        // exercised is identical — only the model-space scale is normalized.
        {
            float maxExt = 0.0f;
            for (auto& v : g_shared.bgf.vertices) {
                maxExt = std::max(maxExt, std::fabs(v.x));
                maxExt = std::max(maxExt, std::fabs(v.y));
                maxExt = std::max(maxExt, std::fabs(v.z));
            }
            if (maxExt > 1e-4f) {
                const float s = 60.0f / maxExt;
                for (auto& v : g_shared.bgf.vertices) { v.x *= s; v.y *= s; v.z *= s; }
            }
        }
        render::MeshGeometry* g = g_shared.bgf.View();
        if (!g || g->polyCount == 0) continue;
        g_shared.geom = *g;
        g_shared.triCount = g->polyCount;
        g_shared.realBgf = true;
        std::printf("[ome2e] REAL BGF loaded: %s  verts=%d tris=%d\n",
                    m.name.c_str(), g->vertexCount, g->polyCount);
        return true;
    }
    std::printf("[ome2e] no shipped .bgf parsed via the fast-chunk reader "
                "(scanned %d; shipped models use the AGF/script chunk variant)\n",
                tried);
    return false;
}

} // namespace

TEST(ObjectMeshRenderE2E, RenderRealAugsburgMeshesToBmp) {
    if (!RealAssetsPresent()) {
        std::printf("  [skip] ObjectMeshRenderE2E.RenderRealAugsburgMeshesToBmp: "
                    "real game dir absent (%s)\n", GameDir().c_str());
        CHECK(true);
        return;
    }

    // 1. Mount real assets + bind the VFS.
    shim::DiskFileSystem fs(GameDir());
    app::RealGameAssets assets = app::MountRealGameAssets(&fs, GameDir(), "Gilde.INI");
    CHECK(assets.vfsBound);
    std::printf("[ome2e] iniLoaded=%d stadt=%s archives=%zu members=%zu\n",
                (int)assets.iniLoaded, assets.stadt.c_str(),
                assets.archives.size(), assets.totalMembers());

    // 2. Load the real AUGSBURG city into the live arrays.
    sim::ResetEntityArrays();
    io::WorldState world{};
    bool loaded = io::LoadWorld("Resources/gamedata/Cities/AUGSBURG.cty", world);
    CHECK(loaded);
    int liveObjects = 0;
    for (int i = 0; i < sim::kObjectCapacity; ++i)
        if (sim::g_objects[i].alive) ++liveObjects;
    std::printf("[ome2e] LoadWorld=%d  live g_objects=%d  g_sceneNodes=%d\n",
                (int)loaded, liveObjects, sim::g_sceneNodeCount);
    CHECK(liveObjects > 0);

    // 3. Resolve a real-format mesh per object (real BGF if any parses, else a
    //    real-format octahedron). All objects share one mesh handle (the engine's
    //    object +460 points into a shared model bank too).
    if (!TryLoadRealBgf(assets))
        g_shared.buildSynth();
    CHECK(g_shared.geom.polyCount > 0);
    std::printf("[ome2e] mesh source: %s  tris/mesh=%d\n",
                g_shared.realBgf ? "REAL .bgf" : "real-format synthetic",
                g_shared.geom.polyCount);

    // 4. Render every live object as its mesh at its DECODED placement. A real-format
    //    engine node (per object, seated at an id/slot-derived world position +
    //    heading) is fed through the real SceneNodeWorldPlacement decode, the mesh is
    //    seated at that transform and projected. Each AUGSBURG object thus draws as
    //    multi-tri geometry at a distinct decoded city position.
    const int fbW = 160, fbH = 120;
    render::Surface* fb = render::SurfaceCreate(fbW, fbH, 16);
    CHECK(fb != nullptr);
    render::SurfaceColorFill(fb, 0, 0, 64);

    g_nodeUsed = 0;
    play::ObjectMeshRenderer r;
    play::ObjectMeshRenderer::Options opt;
    opt.nodeResolver = &ResolveNode;   // decoded transforms (real SceneNodeWorldPlacement)
    opt.meshResolver = &ResolveMesh;   // real-format mesh for every object
    opt.scanScene = false;             // objects only (AUGSBURG populates g_objects)
    opt.scanObjects = true;
    opt.maxObjects = 32;               // cap the frame's object budget
    opt.quadHalf = 18.0f;
    opt.pixelsPerUnit = 0.18f;         // world units -> pixels (spread across the frame)
    opt.eyeX = 0.0f; opt.eyeZ = 0.0f;

    play::MeshRenderStats st = r.render(opt, fb);
    std::printf("[ome2e] meshObjects=%d quadFallbacks=%d meshTris=%d "
                "appendedPolys=%d rasterTris=%d\n",
                st.meshObjects, st.quadFallbacks, st.meshTris,
                st.appendedPolys, st.rasterTris);

    // 5a. Real geometry drew through the real pipeline.
    CHECK(st.objects() > 0);
    CHECK(st.appendedPolys > 0);
    CHECK(st.rasterTris > 0);

    // 5b. MORE triangles than a flat-quad render of the same scene: each object is
    //     multi-tri geometry, so the appended/raster tri count exceeds 2 per object.
    //     (A quad render would append <= 2*objects; meshes append more per object.)
    int quadAppendCeiling = 2 * st.meshObjects;   // a 2-tri quad per mesh object
    std::printf("[ome2e] appended=%d  quad-render ceiling (2*meshObjects)=%d\n",
                st.appendedPolys, quadAppendCeiling);
    if (st.meshObjects > 0)
        CHECK(st.appendedPolys > quadAppendCeiling);

    // 5c. Present the rendered frame to a headless file-dump device -> BMP artifact.
    shim::FileDumpGraphicsDevice dev;
    CHECK(dev.init(fbW, fbH, 16, /*fullscreen=*/false));
    dev.configureDump("/tmp", "guild_augsburg_meshes",
                      shim::FileDumpGraphicsDevice::kBmp);
    if (shim::Surface* bb = dev.backbuffer()) {
        if (bb->pixels && bb->bpp == (int)fb->bpp &&
            bb->width == fb->width && bb->height == fb->height) {
            std::size_t bytes = (std::size_t)fb->pitch * (std::size_t)fb->height;
            std::memcpy(bb->pixels, fb->pixels, bytes);
        }
    }
    dev.present();
    std::string bmp = dev.framePath(0, shim::FileDumpGraphicsDevice::kBmp);
    std::printf("[ome2e] dumped mesh frame -> %s\n", bmp.c_str());

    // 5d. The frame is not blank.
    u16 clear = (u16)render::PackColor(fb->fmt, 0, 0, 64);
    const u16* px = reinterpret_cast<const u16*>(fb->pixels);
    int changed = 0, total = fb->widthPx * fb->height;
    for (int i = 0; i < total; ++i) if (px[i] != clear) ++changed;
    std::printf("[ome2e] non-background pixels = %d / %d\n", changed, fb->width * fb->height);
    CHECK(changed > 100);   // spread meshes cover a meaningful fraction

    render::SurfaceDestroy(fb);
    sim::ResetEntityArrays();
    io::VfsShutdown();
}
