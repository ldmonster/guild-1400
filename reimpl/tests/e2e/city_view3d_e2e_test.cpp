#include "test.h"

// GUARDED real-asset e2e: the WHOLE REAL AUGSBURG city in real 3D through the
// reconstructed universe render chain, with the real engine camera (eye + rot).
//
//   1. mount the real game dir + bind the VFS,
//   2. io::LoadWorld Resources/gamedata/Cities/AUGSBURG.cty -> live sim::g_objects,
//   3. CityView3D: parse scenes/Staedte/stadt_AUGSBURG.ed3 (the engine city
//      scene, VIBE_Scene_LoadStadtScene @0x500218), bind the live objects to
//      their owner-id scene nodes (VIBE_Object_RebuildModelByOwner @0x5a8140),
//   4. render frames: many instances at DISTINCT real positions, a non-trivial
//      deterministic frame, camera ROTATION changes the frame, eye MOVE changes
//      the frame, and the present-layer blit runs (MemoryGraphicsDevice).
//
// Clean skip when the real game dir is absent (GUILD_GAME_DIR).
#include "app/wiring.h"
#include "io/save_world_load.h"
#include "play/city_view3d.h"
#include "render/surface.h"
#include "shim_impl/disk_filesystem.h"
#include "shim_impl/memory_graphics.h"
#include "sim/entity.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <set>
#include <string>
#include <vector>

using namespace guild;

namespace {

std::string GameDir() {
    if (const char* env = std::getenv("GUILD_GAME_DIR"))
        return env;
    return "/home/cnupt/work/reverse/guild-1400/reimpl/europe_guild_1400_original";
}

bool RealAssetsPresent() {
    shim::DiskFileSystem fs(GameDir());
    return fs.exists("Gilde.INI") &&
           fs.exists("Resources/gamedata/Cities/AUGSBURG.cty") &&
           fs.exists("Resources/scenes.BIN") &&
           fs.exists("Resources/Objects.BIN");
}

// Snapshot the rendered surface for determinism / difference comparisons.
std::vector<u8> Snapshot(render::Surface* s, int w, int h) {
    std::vector<u8> out;
    out.reserve((std::size_t)w * h * 3);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            u8 px[3];
            render::SurfaceGetPixelRgb(s, x, y, px);
            out.push_back(px[0]); out.push_back(px[1]); out.push_back(px[2]);
        }
    return out;
}

int DiffPixels(const std::vector<u8>& a, const std::vector<u8>& b) {
    int diff = 0;
    const std::size_t n = std::min(a.size(), b.size()) / 3;
    for (std::size_t i = 0; i < n; ++i)
        if (a[3 * i] != b[3 * i] || a[3 * i + 1] != b[3 * i + 1] ||
            a[3 * i + 2] != b[3 * i + 2])
            ++diff;
    return diff;
}

// Dump a 16bpp surface as a 24bpp BMP (visual artifact of the run).
bool DumpBmp(const char* path, render::Surface* s, int w, int h) {
    const int rowBytes = (w * 3 + 3) & ~3;
    const int imgBytes = rowBytes * h;
    const int fileBytes = 54 + imgBytes;
    std::vector<u8> buf((std::size_t)fileBytes, 0);
    auto put16 = [&](int o, u16 v) { buf[o] = (u8)(v & 0xFF); buf[o + 1] = (u8)(v >> 8); };
    auto put32 = [&](int o, u32 v) {
        buf[o] = (u8)v; buf[o + 1] = (u8)(v >> 8);
        buf[o + 2] = (u8)(v >> 16); buf[o + 3] = (u8)(v >> 24);
    };
    buf[0] = 'B'; buf[1] = 'M';
    put32(2, (u32)fileBytes); put32(10, 54);
    put32(14, 40); put32(18, (u32)w); put32(22, (u32)h);
    put16(26, 1); put16(28, 24); put32(34, (u32)imgBytes);
    for (int y = 0; y < h; ++y) {
        u8* dst = buf.data() + 54 + (std::size_t)y * rowBytes;
        for (int x = 0; x < w; ++x) {
            u8 px[3];
            render::SurfaceGetPixelRgb(s, x, h - 1 - y, px);
            dst[x * 3 + 0] = px[2]; dst[x * 3 + 1] = px[1]; dst[x * 3 + 2] = px[0];
        }
    }
    std::FILE* f = std::fopen(path, "wb");
    if (!f) return false;
    std::fwrite(buf.data(), 1, buf.size(), f);
    std::fclose(f);
    return true;
}

} // namespace

TEST(CityView3DE2E, RealAugsburgWholeCityRealPositions) {
    if (!RealAssetsPresent()) {
        std::printf("  [skip] CityView3DE2E.RealAugsburgWholeCityRealPositions: "
                    "real game dir absent (%s)\n", GameDir().c_str());
        CHECK(true);
        return;
    }

    // 1. Real assets + VFS.
    shim::DiskFileSystem fs(GameDir());
    app::RealGameAssets assets = app::MountRealGameAssets(&fs, GameDir(), "Gilde.INI");
    CHECK(assets.vfsBound);

    // 2. The live world + the EMBEDDED city scene stream (the blob the original's
    //    PostLoadInitScene @0x5a7ef8 hands to Scene_LoadFromStream @0x5e7e38).
    sim::ResetEntityArrays();
    io::WorldState world{};
    std::vector<u8> sceneBlob;
    CHECK(io::LoadWorldEx("Resources/gamedata/Cities/AUGSBURG.cty", world, &sceneBlob));
    int liveObjects = 0;
    for (int i = 0; i < sim::kObjectCapacity; ++i)
        if (sim::g_objects[i].alive) ++liveObjects;
    std::printf("[cv3d-e2e] live g_objects=%d embedded scene=%zu bytes\n",
                liveObjects, sceneBlob.size());
    CHECK(liveObjects > 0);
    CHECK(sceneBlob.size() > 1000);   // the .cty really embeds the scene

    // 3. The city scene + the live-object bind.
    play::CityView3D view;
    CHECK(view.Init(&fs));
    CHECK(view.mounted());
    CHECK(view.LoadCityFromWorld(sceneBlob));

    // The persisted scene carries the +512 owner-object ids (the shipped
    // stadt_AUGSBURG.ed3 has none) — most live object ids resolve to a node.
    int ownerNodes = 0;
    for (const auto& o : view.scene())
        if (o.ownerId != 0) ++ownerNodes;
    std::printf("[cv3d-e2e] owner-linked nodes=%d\n", ownerNodes);
    CHECK(ownerNodes > 0);
    std::printf("[cv3d-e2e] scene nodes=%zu static instances=%zu fogFar=%.1f tex=%d\n",
                view.scene().size(), view.instances().size(), view.sceneFogFar(),
                (int)view.texturesMounted());
    CHECK(view.scene().size() > 10);
    CHECK(!view.instances().empty());

    const int placedObjects = view.BindWorldObjects();
    std::printf("[cv3d-e2e] bound objects=%d (unplaced=%d modelUnresolved=%d)\n",
                placedObjects, view.unplacedObjects(), view.modelUnresolved());
    // The REAL owner-id match (RebuildModelByOwner @0x5a8140) seats live objects
    // on their city scene nodes.
    CHECK(placedObjects > 0);

    // Bound objects sit at DISTINCT real world positions (not a synthetic grid:
    // positions come straight from the stadt_AUGSBURG.ed3 node transforms).
    std::set<std::pair<long, long>> distinctPos;
    for (const auto& b : view.boundObjects())
        distinctPos.insert({(long)std::lround(b.place.pos[0] * 16.0f),
                            (long)std::lround(b.place.pos[2] * 16.0f)});
    std::printf("[cv3d-e2e] distinct bound positions=%zu\n", distinctPos.size());
    CHECK((int)distinctPos.size() >= 2);
    CHECK((int)distinctPos.size() >= placedObjects / 2);

    // 4. Render the whole city through the universe chain with the REAL camera.
    play::CityView3D::Options opt;
    opt.fbW = 160; opt.fbH = 120;
    play::CityCamera3D cam = view.OverviewCamera();
    play::CityView3D::Result r = view.RenderFrame(cam, opt);
    std::printf("[cv3d-e2e] instances=%d objectInstances=%d polysIn=%d appended=%d "
                "dispatched=%d rasterTris=%d nonClear=%d colors=%d textured=%d "
                "texPolys=%d boundMats=%d\n",
                r.instancesDrawn, r.objectInstances, r.meshPolysIn, r.appendedPolys,
                r.nodesDispatched, r.rasterTris, r.nonClearPixels, r.distinctColors,
                (int)r.textured, r.texturedPolys, r.boundMaterials);

    CHECK(r.instancesDrawn > 10);                 // many city nodes drew
    CHECK(r.objectInstances > 0);                 // live objects among them
    CHECK(r.meshPolysIn > 1000);                  // real multi-tri geometry
    CHECK(r.appendedPolys > 100);                 // real dispatch appended
    CHECK(r.nodesDispatched >= r.instancesDrawn); // every node hit the dispatch
    CHECK(r.rasterTris > 100);                    // the raster flushed the city
    CHECK(r.nonClearPixels > (opt.fbW * opt.fbH) / 50);
    CHECK(r.distinctColors > 3);                  // non-trivial frame content

    DumpBmp("/tmp/guild_cityview3d_augsburg.bmp", view.surface(), opt.fbW, opt.fbH);
    std::printf("[cv3d-e2e] bmp=/tmp/guild_cityview3d_augsburg.bmp\n");

    // Deterministic: the same camera renders the byte-identical frame.
    std::vector<u8> f1 = Snapshot(view.surface(), opt.fbW, opt.fbH);
    play::CityView3D::Result r2 = view.RenderFrame(cam, opt);
    std::vector<u8> f2 = Snapshot(view.surface(), opt.fbW, opt.fbH);
    CHECK_EQ(r.rasterTris, r2.rasterTris);
    CHECK_EQ(r.nonClearPixels, r2.nonClearPixels);
    CHECK_EQ(0, DiffPixels(f1, f2));

    sim::ResetEntityArrays();
}

TEST(CityView3DE2E, CameraRotationAndEyeMoveChangeTheFrame) {
    if (!RealAssetsPresent()) {
        std::printf("  [skip] CityView3DE2E.CameraRotationAndEyeMoveChangeTheFrame: "
                    "real game dir absent\n");
        CHECK(true);
        return;
    }
    shim::DiskFileSystem fs(GameDir());
    app::RealGameAssets assets = app::MountRealGameAssets(&fs, GameDir(), "Gilde.INI");
    CHECK(assets.vfsBound);

    sim::ResetEntityArrays();
    io::WorldState world{};
    std::vector<u8> sceneBlob;
    CHECK(io::LoadWorldEx("Resources/gamedata/Cities/AUGSBURG.cty", world, &sceneBlob));

    play::CityView3D view;
    CHECK(view.Init(&fs));
    CHECK(view.LoadCityFromWorld(sceneBlob));
    CHECK(view.BindWorldObjects() > 0);

    play::CityView3D::Options opt;
    opt.fbW = 160; opt.fbH = 120;

    play::CityCamera3D cam = view.OverviewCamera();
    play::CityView3D::Result r0 = view.RenderFrame(cam, opt);
    CHECK(r0.rasterTris > 0);
    std::vector<u8> base = Snapshot(view.surface(), opt.fbW, opt.fbH);

    // Camera ROTATION (the +132 euler) changes the frame — the rotation drives
    // the view basis (MatrixFromEuler(-rot)), exactly the engine camera model.
    play::CityCamera3D rot = cam;
    rot.rot[1] += 0.6f;   // yaw
    play::CityView3D::Result rr = view.RenderFrame(rot, opt);
    std::vector<u8> rotF = Snapshot(view.surface(), opt.fbW, opt.fbH);
    const int rotDiff = DiffPixels(base, rotF);
    std::printf("[cv3d-e2e] yaw+0.6: rasterTris=%d diffPixels=%d\n",
                rr.rasterTris, rotDiff);
    CHECK(rotDiff > 50);

    play::CityCamera3D pitch = cam;
    pitch.rot[0] += 0.3f;
    view.RenderFrame(pitch, opt);
    const int pitchDiff = DiffPixels(base, Snapshot(view.surface(), opt.fbW, opt.fbH));
    std::printf("[cv3d-e2e] pitch+0.3: diffPixels=%d\n", pitchDiff);
    CHECK(pitchDiff > 50);

    // Eye MOVE (node +76, the SessionCamera pan) changes the frame.
    play::CityCamera3D moved = cam;
    moved.eye[0] += 120.0f;
    moved.eye[2] += 80.0f;
    view.RenderFrame(moved, opt);
    const int moveDiff = DiffPixels(base, Snapshot(view.surface(), opt.fbW, opt.fbH));
    std::printf("[cv3d-e2e] eye move: diffPixels=%d\n", moveDiff);
    CHECK(moveDiff > 50);

    // Zoom (eye height descend toward the city) changes the frame too.
    play::CityCamera3D zoom = cam;
    zoom.eye[1] *= 0.55f;
    view.RenderFrame(zoom, opt);
    const int zoomDiff = DiffPixels(base, Snapshot(view.surface(), opt.fbW, opt.fbH));
    std::printf("[cv3d-e2e] eye descend: diffPixels=%d\n", zoomDiff);
    CHECK(zoomDiff > 50);

    sim::ResetEntityArrays();
}

// The scenes.BIN fallback path: the shipped stadt_AUGSBURG.ed3 (no owner ids —
// they only exist in the persisted .cty scene) still parses + renders the
// static city scenery through the same chain.
TEST(CityView3DE2E, StadtSceneFallbackParsesAndRenders) {
    if (!RealAssetsPresent()) {
        std::printf("  [skip] CityView3DE2E.StadtSceneFallbackParsesAndRenders: "
                    "real game dir absent\n");
        CHECK(true);
        return;
    }
    shim::DiskFileSystem fs(GameDir());
    app::RealGameAssets assets = app::MountRealGameAssets(&fs, GameDir(), "Gilde.INI");
    CHECK(assets.vfsBound);

    play::CityView3D view;
    CHECK(view.Init(&fs));
    CHECK(view.LoadCity("AUGSBURG"));      // scenes/Staedte/stadt_AUGSBURG.ed3
    CHECK(view.scene().size() > 100);
    CHECK(!view.instances().empty());

    play::CityView3D::Options opt;
    opt.fbW = 160; opt.fbH = 120;
    play::CityView3D::Result r = view.RenderFrame(view.OverviewCamera(), opt);
    std::printf("[cv3d-e2e] stadt fallback: instances=%d raster=%d nonClear=%d\n",
                r.instancesDrawn, r.rasterTris, r.nonClearPixels);
    CHECK(r.instancesDrawn > 10);
    CHECK(r.rasterTris > 100);
    CHECK(r.nonClearPixels > 0);
}

TEST(CityView3DE2E, PresentsThroughHeadlessDevice) {
    if (!RealAssetsPresent()) {
        std::printf("  [skip] CityView3DE2E.PresentsThroughHeadlessDevice: "
                    "real game dir absent\n");
        CHECK(true);
        return;
    }
    shim::DiskFileSystem fs(GameDir());
    app::RealGameAssets assets = app::MountRealGameAssets(&fs, GameDir(), "Gilde.INI");
    CHECK(assets.vfsBound);

    sim::ResetEntityArrays();
    io::WorldState world{};
    std::vector<u8> sceneBlob;
    CHECK(io::LoadWorldEx("Resources/gamedata/Cities/AUGSBURG.cty", world, &sceneBlob));

    play::CityView3D view;
    CHECK(view.Init(&fs));
    CHECK(view.LoadCityFromWorld(sceneBlob));
    view.BindWorldObjects();

    play::CityView3D::Options opt;
    opt.fbW = 160; opt.fbH = 120;
    play::CityView3D::Result r = view.RenderFrame(view.OverviewCamera(), opt);
    CHECK(r.nonClearPixels > 0);

    shim::MemoryGraphicsDevice dev;
    CHECK(dev.init(opt.fbW, opt.fbH, 16, /*fullscreen=*/false));
    CHECK(view.PresentToDevice(dev));
    CHECK(dev.presentCount() >= 1);

    // The blit really landed: the backbuffer carries non-clear pixels too.
    shim::Surface* bb = dev.backbuffer();
    CHECK(bb && bb->pixels);
    int nonClear = 0;
    const u16 clear = (u16)(((opt.clearR & 0xF8) << 8) | ((opt.clearG & 0xFC) << 3) |
                            (opt.clearB >> 3));
    for (int y = 0; y < bb->height; ++y) {
        const u16* row = reinterpret_cast<const u16*>(
            static_cast<const u8*>(bb->pixels) + (std::size_t)y * bb->pitch);
        for (int x = 0; x < bb->width; ++x)
            if (row[x] != clear) ++nonClear;
    }
    std::printf("[cv3d-e2e] backbuffer nonClear=%d\n", nonClear);
    CHECK(nonClear > 0);

    sim::ResetEntityArrays();
}
